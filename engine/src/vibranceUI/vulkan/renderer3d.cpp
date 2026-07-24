#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <vibranceUI/renderer/renderer3d.h>
#include <vibranceUI/renderer/buffer.h>
#include <vibranceUI/renderer/image.h>
#include <vibranceUI/factories/mesh_factory.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
    constexpr uint32_t kDepthPass = 0;
    constexpr uint32_t kColorPass = 1;
    constexpr uint32_t kMaxDepthLayer = 65534u;

    RasterPushConstants make_perspective_constants(const Swapchain& swapchain, const Camera& camera)
    {
        // Fullscreen 3D uses the swapchain extent directly
        const float width = static_cast<float>(std::max(swapchain.extent.width, 1u));
        const float height = static_cast<float>(std::max(swapchain.extent.height, 1u));
        const float aspect = width / height;

        glm::mat4 projection = glm::perspective(glm::radians(60.0f), aspect, 0.01f, 100.0f);
        projection[1][1] *= -1.0f;

        RasterPushConstants constants = {};
        constants.worldToClip = projection * camera.get_view_matrix();
        constants.viewportRect = { 0.0f, 0.0f, width, height };
        constants.clipRect = constants.viewportRect;
        return constants;
    }

    glm::mat4 make_model_matrix(const Renderer3DModelBatch& model)
    {
        glm::mat4 matrix { 1.0f };
        matrix = glm::translate(matrix, model.position);
        matrix = glm::rotate(matrix, model.rotationRadians.x, glm::vec3 { 1.0f, 0.0f, 0.0f });
        matrix = glm::rotate(matrix, model.rotationRadians.y, glm::vec3 { 0.0f, 1.0f, 0.0f });
        matrix = glm::rotate(matrix, model.rotationRadians.z, glm::vec3 { 0.0f, 0.0f, 1.0f });
        matrix = glm::scale(matrix, model.scale);
        return matrix;
    }

    struct HostedModelConstants
    {
        RasterPushConstants raster;
        Model3DPushConstants graphics;
    };

    glm::mat3 make_normal_matrix(const glm::mat4& modelMatrix)
    {
        const glm::mat3 model3 = glm::mat3(modelMatrix);
        const float determinant = glm::determinant(model3);
        if (std::abs(determinant) <= 0.000001f)
        {
            return glm::mat3 { 1.0f };
        }
        return glm::transpose(glm::inverse(model3));
    }

    HostedModelConstants make_hosted_model_constants(const Renderer3DModelBatch& model)
    {
        // Hosted models render into a 2D viewport but keep their own camera parameters
        const float width = std::max(model.viewportRect.z, 1.0f);
        const float height = std::max(model.viewportRect.w, 1.0f);
        const float aspect = width / height;
        const float nearPlane = std::max(model.nearPlane, 0.0001f);
        const float farPlane = std::max(model.farPlane, nearPlane + 0.0001f);
        const float fov = std::clamp(model.fieldOfViewRadians, 0.01f, 3.0f);

        glm::mat4 projection = glm::perspective(fov, aspect, nearPlane, farPlane);
        projection[1][1] *= -1.0f;

        const glm::vec3 viewDirection = model.cameraTarget - model.cameraPosition;
        const glm::vec3 safeTarget = glm::length(viewDirection) > 0.0001f
            ? model.cameraTarget
            : model.cameraPosition + glm::vec3 { 0.0f, 0.0f, -1.0f };
        const glm::mat4 view = glm::lookAt(model.cameraPosition, safeTarget, glm::vec3 { 0.0f, 1.0f, 0.0f });
        const glm::mat4 modelMatrix = make_model_matrix(model);
        const glm::mat3 normalMatrix = make_normal_matrix(modelMatrix);
        const glm::vec3 lightDirection = glm::length2(model.lightDirection) > 0.0001f
            ? glm::normalize(model.lightDirection)
            : glm::normalize(glm::vec3 { -0.35f, 0.65f, 0.68f });

        HostedModelConstants constants = {};
        constants.raster.worldToClip = projection * view * modelMatrix;
        constants.raster.viewportRect = model.viewportRect;
        constants.raster.clipRect = model.clipRect;
        constants.raster.materialColor = model.materialColor;

        constants.graphics.worldToClip = constants.raster.worldToClip;
        constants.graphics.normalToWorld0 = glm::vec4(normalMatrix[0], lightDirection.x);
        constants.graphics.normalToWorld1 = glm::vec4(normalMatrix[1], lightDirection.y);
        constants.graphics.normalToWorld2 = glm::vec4(normalMatrix[2], lightDirection.z);
        constants.graphics.materialColor = model.materialColor;
        return constants;
    }

    bool rect_empty(glm::vec4 rect)
    {
        return rect.z <= 0.0f || rect.w <= 0.0f;
    }

    void insert_compute_memory_barrier(vk::CommandBuffer commandBuffer)
    {
        vk::MemoryBarrier barrier = {};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags(),
            barrier,
            nullptr,
            nullptr
        );
    }

    bool make_scissor(glm::vec4 clipRect, vk::Extent2D extent, vk::Rect2D& scissor)
    {
        const int32_t minX = std::max(0, static_cast<int32_t>(std::floor(clipRect.x)));
        const int32_t minY = std::max(0, static_cast<int32_t>(std::floor(clipRect.y)));
        const int32_t maxX = std::min(static_cast<int32_t>(extent.width),
            static_cast<int32_t>(std::ceil(clipRect.x + clipRect.z)));
        const int32_t maxY = std::min(static_cast<int32_t>(extent.height),
            static_cast<int32_t>(std::ceil(clipRect.y + clipRect.w)));

        if (maxX <= minX || maxY <= minY)
        {
            return false;
        }

        scissor.offset = vk::Offset2D { minX, minY };
        scissor.extent = vk::Extent2D {
            static_cast<uint32_t>(maxX - minX),
            static_cast<uint32_t>(maxY - minY)
        };
        return true;
    }

    bool record_graphics_triangles(
        vk::CommandBuffer commandBuffer,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        StorageBuffer& vertexBuffer,
        StorageImage& renderTarget,
        vk::RenderPass renderPass,
        vk::Framebuffer framebuffer,
        RasterPushConstants constants,
        const Model3DPushConstants& modelConstants,
        const Model3DAsset* modelAsset,
        bool renderPassHasResolveAttachment)
    {
        // Graphics rendering is used only for hosted 3D content inside 2D panels
        if (constants.triangleCount == 0 ||
            renderTarget.extent.width == 0 ||
            renderTarget.extent.height == 0 ||
            !renderPass ||
            !framebuffer)
        {
            return false;
        }

        if (modelAsset == nullptr || !modelAsset->fallbackMaterialDescriptorSet)
        {
            return false;
        }

        const PipelineType pipelineType = PipelineType::eModel3D;
        const auto pipelineIt = pipelines.find(pipelineType);
        if (pipelineIt == pipelines.end() || !pipelineIt->second)
        {
            return false;
        }

        vk::Rect2D scissor = {};
        if (!make_scissor(constants.clipRect, renderTarget.extent, scissor))
        {
            return false;
        }

        transition_image_layout(commandBuffer, renderTarget.image,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eColorAttachmentOptimal,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
            vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eColorAttachmentOutput
        );

        std::array<vk::ClearValue, 3> clearValues = {};
        clearValues[0].color = vk::ClearColorValue(std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });
        clearValues[1].depthStencil.depth = 1.0f;
        clearValues[1].depthStencil.stencil = 0;
        clearValues[2].color = vk::ClearColorValue(std::array<float, 4> { 0.0f, 0.0f, 0.0f, 0.0f });

        vk::RenderPassBeginInfo renderPassInfo = {};
        renderPassInfo.renderPass = renderPass;
        renderPassInfo.framebuffer = framebuffer;
        renderPassInfo.renderArea.offset = scissor.offset;
        renderPassInfo.renderArea.extent = scissor.extent;
        renderPassInfo.clearValueCount = renderPassHasResolveAttachment ? 3u : 2u;
        renderPassInfo.pClearValues = clearValues.data();

        commandBuffer.beginRenderPass(renderPassInfo, vk::SubpassContents::eInline);

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eGraphics, pipelineIt->second);

        vk::Viewport viewport = {};
        viewport.x = constants.viewportRect.x;
        viewport.y = constants.viewportRect.y;
        viewport.width = constants.viewportRect.z;
        viewport.height = constants.viewportRect.w;
        viewport.minDepth = 0.0f;
        viewport.maxDepth = 1.0f;
        commandBuffer.setViewport(0, 1, &viewport);
        commandBuffer.setScissor(0, 1, &scissor);

        vk::Buffer vertexBuffers[] = { vertexBuffer.buffer };
        vk::DeviceSize vertexOffsets[] = { vertexBuffer.vertexDataOffset };
        commandBuffer.bindVertexBuffers(0, 1, vertexBuffers, vertexOffsets);

        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0, sizeof(modelConstants), &modelConstants);

        const uint32_t requestedFirst = constants.firstTriangle;
        const uint32_t requestedEnd = requestedFirst + constants.triangleCount;
        bool drewAnyRange = false;
        auto draw_range = [&](uint32_t rangeFirst, uint32_t rangeCount, vk::DescriptorSet materialSet) {
            if (rangeCount == 0)
            {
                return;
            }

            const uint32_t rangeEnd = rangeFirst + rangeCount;
            const uint32_t drawFirst = std::max(rangeFirst, requestedFirst);
            const uint32_t drawEnd = std::min(rangeEnd, requestedEnd);
            if (drawEnd <= drawFirst)
            {
                return;
            }

            if (!materialSet)
            {
                materialSet = modelAsset->fallbackMaterialDescriptorSet;
            }
            commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                pipelineLayouts[pipelineType], 0, 1, &materialSet, 0, nullptr);
            commandBuffer.draw((drawEnd - drawFirst) * 3u, 1, drawFirst * 3u, 0);
            drewAnyRange = true;
        };

        if (!modelAsset->drawRanges.empty())
        {
            for (const Model3DDrawRange& range : modelAsset->drawRanges)
            {
                draw_range(range.firstTriangle, range.triangleCount, range.materialDescriptorSet);
            }
        }
        else
        {
            draw_range(constants.firstTriangle, constants.triangleCount, modelAsset->fallbackMaterialDescriptorSet);
        }

        commandBuffer.endRenderPass();

        transition_image_layout(commandBuffer, renderTarget.image,
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::eGeneral,
            vk::AccessFlagBits::eColorAttachmentWrite,
            vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite,
            vk::PipelineStageFlagBits::eColorAttachmentOutput,
            vk::PipelineStageFlagBits::eComputeShader
        );

        return drewAnyRange;
    }

    void record_triangles(
        vk::CommandBuffer commandBuffer,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope renderTargetScope,
        RasterPushConstants constants)
    {
        if (constants.triangleCount == 0)
        {
            return;
        }

        const PipelineType pipelineType = PipelineType::eRasteriseSmall;
        const auto pipelineIt = pipelines.find(pipelineType);
        if (pipelineIt == pipelines.end() || !pipelineIt->second)
        {
            return;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineIt->second);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
            0, 1, &descriptorSets[renderTargetScope], 0, nullptr);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
            1, 1, &descriptorSets[DescriptorScope::eDrawCall], 0, nullptr);

        const uint32_t workgroupCount = (constants.triangleCount + 63u) / 64u;

        constants.pass = kDepthPass;
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        commandBuffer.dispatch(workgroupCount, 1, 1);

        insert_compute_memory_barrier(commandBuffer);

        constants.pass = kColorPass;
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        commandBuffer.dispatch(workgroupCount, 1, 1);

        insert_compute_memory_barrier(commandBuffer);
    }
}

void Renderer3D::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    DescriptorScope renderTargetScope,
    uint32_t firstTriangle,
    uint32_t triangleCount,
    const Camera& camera
) const
{
    RasterPushConstants constants = make_perspective_constants(swapchain, camera);
    constants.firstTriangle = firstTriangle;
    constants.triangleCount = triangleCount;
    constants.materialColor = { 1.0f, 1.0f, 1.0f, 1.0f };
    record_triangles(commandBuffer, pipelines, descriptorSets, pipelineLayouts, renderTargetScope, constants);
}

bool Renderer3D::record_model(
    vk::CommandBuffer commandBuffer,
    Swapchain&,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    DescriptorScope renderTargetScope,
    const Renderer3DModelBatch& model,
    uint32_t defaultFirstTriangle,
    uint32_t defaultTriangleCount,
    uint32_t depthLayer,
    StorageBuffer* vertexBuffer,
    Model3DAsset* modelAsset,
    StorageImage* renderTarget,
    vk::RenderPass renderPass,
    vk::Framebuffer framebuffer,
    bool renderPassHasResolveAttachment
) const
{
    if (rect_empty(model.viewportRect) || rect_empty(model.clipRect))
    {
        return false;
    }

    HostedModelConstants hostedConstants = make_hosted_model_constants(model);
    RasterPushConstants& constants = hostedConstants.raster;
    constants.firstTriangle = model.triangleCount > 0 ? model.firstTriangle : defaultFirstTriangle;
    constants.triangleCount = model.triangleCount > 0 ? model.triangleCount : defaultTriangleCount;
    constants.flags = std::min(depthLayer, kMaxDepthLayer);

    if (vertexBuffer != nullptr && constants.firstTriangle < vertexBuffer->triangleCount)
    {
        constants.triangleCount = std::min(constants.triangleCount,
            vertexBuffer->triangleCount - constants.firstTriangle);
    }
    else if (vertexBuffer != nullptr)
    {
        constants.triangleCount = 0;
    }

    if (constants.triangleCount == 0)
    {
        return false;
    }

    if (vertexBuffer != nullptr && renderTarget != nullptr && modelAsset != nullptr)
    {
        return record_graphics_triangles(commandBuffer, pipelines, pipelineLayouts,
            *vertexBuffer, *renderTarget, renderPass, framebuffer, constants, hostedConstants.graphics, modelAsset,
            renderPassHasResolveAttachment);
    }

    record_triangles(commandBuffer, pipelines, descriptorSets, pipelineLayouts, renderTargetScope, constants);
    return true;
}

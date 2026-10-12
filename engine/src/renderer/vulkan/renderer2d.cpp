#include <vibranceUI/renderer/renderer2d.h>
#include <vibranceUI/renderer/renderer3d.h>
#include "../common/scene_helpers.h"
#include "../common/entity_bounds.h"
#include "text_batch.h"
#include "render_op_order.h"
#include <cstring>
namespace {
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

    void dispatch_bounds(vk::CommandBuffer commandBuffer, const DispatchBounds& bounds)
    {
        if (!bounds.empty())
        {
            commandBuffer.dispatch(workgroup_count(bounds.width), workgroup_count(bounds.height), 1);
        }
    }

    bool bind_pipeline(
        vk::CommandBuffer commandBuffer,
        PipelineType pipelineType,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines)
    {
        const auto pipelineIt = pipelines.find(pipelineType);
        if (pipelineIt == pipelines.end() || !pipelineIt->second)
        {
            return false;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineIt->second);
        return true;
    }

    void bind_frame_set(
        vk::CommandBuffer commandBuffer,
        PipelineType pipelineType,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope frameScope = DescriptorScope::eFrame)
    {
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
            0, 1, &descriptorSets[frameScope], 0, nullptr);
    }

    void bind_post_set(
        vk::CommandBuffer commandBuffer,
        PipelineType pipelineType,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope postScope = DescriptorScope::ePost)
    {
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
            1, 1, &descriptorSets[postScope], 0, nullptr);
    }

    void clear_frame_surface(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope frameScope,
        glm::uvec4 clearRect = glm::uvec4(0u))
    {
        const PipelineType pipelineType = PipelineType::eClear;
        if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
        {
            return;
        }

        bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
        DispatchBounds bounds = make_full_screen_bounds(swapchain);
        if (clearRect.z > 0u && clearRect.w > 0u)
        {
            const uint32_t x = std::min(clearRect.x, swapchain.extent.width);
            const uint32_t y = std::min(clearRect.y, swapchain.extent.height);
            bounds = {
                x,
                y,
                std::min(clearRect.z, swapchain.extent.width - x),
                std::min(clearRect.w, swapchain.extent.height - y)
            };
        }
        if (bounds.empty())
        {
            return;
        }
        const glm::uvec4 clearConstants { 0u, bounds.x, bounds.y, 0u };
        commandBuffer.pushConstants(
            pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute,
            0u,
            sizeof(clearConstants),
            &clearConstants);
        dispatch_bounds(commandBuffer, bounds);
        insert_compute_memory_barrier(commandBuffer);
    }

    void record_batches(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        PipelineType pipelineType,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const std::vector<Renderer2DBatch>& batches,
        DispatchBoundsMode dispatchMode,
        DescriptorScope frameScope)
    {
        if (batches.empty() || !bind_pipeline(commandBuffer, pipelineType, pipelines))
        {
            return;
        }

        bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);

        for (const Renderer2DBatch& batch : batches)
        {
            const DispatchBounds bounds = make_dispatch_bounds(swapchain, batch, dispatchMode);
            if (bounds.empty())
            {
                continue;
            }

            Renderer2DPushConstants constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
            commandBuffer.pushConstants(pipelineLayouts[pipelineType],
                vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
            dispatch_bounds(commandBuffer, bounds);
            insert_compute_memory_barrier(commandBuffer);
        }
    }

}

void ShadowPipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    DescriptorScope frameScope) const
{
    record_batches(commandBuffer, swapchain, PipelineType::eShadow2D, pipelines, descriptorSets, pipelineLayouts, batches,
        DispatchBoundsMode::eShadow, frameScope);
}

void ShadowPipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    DescriptorScope frameScope,
    bool synchronize) const
{
    if (!bind_pipeline(commandBuffer, PipelineType::eShadow2D, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, PipelineType::eShadow2D, descriptorSets, pipelineLayouts, frameScope);
    const DispatchBounds bounds = make_dispatch_bounds(swapchain, batch, DispatchBoundsMode::eShadow);
    if (bounds.empty())
    {
        return;
    }

    Renderer2DPushConstants constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    commandBuffer.pushConstants(pipelineLayouts[PipelineType::eShadow2D],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    if (synchronize)
    {
        insert_compute_memory_barrier(commandBuffer);
    }
}

void BlurPipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    DescriptorScope frameScope,
    DescriptorScope postScope) const
{
    for (const Renderer2DBatch& batch : batches)
    {
        record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, batch, frameScope, postScope,
            frameScope == DescriptorScope::eFrame, false);
    }
}

void BlurPipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    DescriptorScope frameScope,
    DescriptorScope postScope,
    bool includeStaticBackdrop,
    bool includeExternalBackdrop) const
{
    const PipelineType pipelineType = PipelineType::eBlur2D;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    const bool liquid = (batch.flags & eRenderer2DStyleLiquidGlass) != 0u;
    const DispatchBounds bounds = liquid ?
        make_liquid_output_dispatch_bounds(swapchain, batch) :
        make_blur_dispatch_bounds(swapchain, batch);
    if (bounds.empty())
    {
        return;
    }

    Renderer2DBatch sourceBatch = batch;
    // Populate the complete sampling halo even when a scroll viewport clips
    // the control. Only the final optical output uses the original clip.
    if (liquid) sourceBatch.clipRect = { 0.0f, 0.0f,
        static_cast<float>(swapchain.extent.width),
        static_cast<float>(swapchain.extent.height) };
    const DispatchBounds sourceBounds = liquid ?
        make_blur_dispatch_bounds(swapchain, sourceBatch) : bounds;

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, postScope);

    Renderer2DPushConstants constants = {};
    constants.rect = {
        static_cast<float>(bounds.x),
        static_cast<float>(bounds.y),
        static_cast<float>(bounds.width),
        static_cast<float>(bounds.height)
    };
    constants.data = {
        static_cast<uint32_t>(Renderer2DPrimitive::eClear),
        eRenderer2DStyleClear,
        0u,
        pack_dispatch_origin(bounds.x, bounds.y)
    };
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);

    const uint32_t passCount = liquid ? 1u :
        std::max(1u, static_cast<uint32_t>(std::round(std::max(batch.effect0.y, 1.0f))));
    Renderer2DBatch passBatch = batch;
    const float targetOpacity = std::clamp(batch.effect0.w, 0.0f, 1.0f);
    passBatch.effect0.w = passCount > 1u
        ? 1.0f - std::pow(1.0f - targetOpacity, 1.0f / static_cast<float>(passCount))
        : targetOpacity;
    for (uint32_t pass = 0; pass < passCount; ++pass)
    {
        uint32_t passData = liquid ? batch.packedData : pass;
        if (includeStaticBackdrop)
        {
            passData |= kBlurUseStaticBackdrop;
        }
        if (includeExternalBackdrop)
        {
            passData |= kBlurUseExternalBackdrop;
        }

        constants = make_push_constants(passBatch, passData, sourceBounds.x, sourceBounds.y);
        constants.data.x |= batch.backdropCapColour << 8u;
        constants.data.y |= eRenderer2DStyleBlurHorizontal;
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        dispatch_bounds(commandBuffer, sourceBounds);
        insert_compute_memory_barrier(commandBuffer);

        constants = make_push_constants(passBatch, passData, bounds.x, bounds.y);
        constants.data.x |= batch.backdropCapColour << 8u;
        constants.data.y |= eRenderer2DStyleBlurVertical;
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        dispatch_bounds(commandBuffer, bounds);
        insert_compute_memory_barrier(commandBuffer);
    }

    constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    constants.data.y |= eRenderer2DStyleBlurComposite;
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void BlurPipeline::record_foreground_group(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch) const
{
    constexpr PipelineType pipelineType = PipelineType::eBlur2D;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    const DispatchBounds bounds = make_blur_dispatch_bounds(swapchain, batch);
    if (bounds.empty())
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts,
        DescriptorScope::eForegroundBlur);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts,
        DescriptorScope::ePost);

    Renderer2DPushConstants constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    constants.data.y = eRenderer2DStyleClear;
    constants.data.z = 0u;
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);

    constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    constants.data.y |= eRenderer2DStyleBlurHorizontal | eRenderer2DStyleBlurForegroundGroupSource;
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);

    constants.data.y = eRenderer2DStyleBlurVertical | eRenderer2DStyleBlurForegroundGroupSource;
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts,
        DescriptorScope::eFrame);
    constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    constants.data.y = eRenderer2DStyleBlurComposite;
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void ShapePipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    DescriptorScope frameScope) const
{
    record_batches(commandBuffer, swapchain, PipelineType::eShape2D, pipelines, descriptorSets, pipelineLayouts, batches,
        DispatchBoundsMode::eShape, frameScope);
}

void ShapePipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    DescriptorScope frameScope,
    bool synchronize) const
{
    if (!bind_pipeline(commandBuffer, PipelineType::eShape2D, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, PipelineType::eShape2D, descriptorSets, pipelineLayouts, frameScope);
    const DispatchBounds bounds = make_dispatch_bounds(swapchain, batch, DispatchBoundsMode::eShape);
    if (bounds.empty())
    {
        return;
    }

    Renderer2DPushConstants constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    commandBuffer.pushConstants(pipelineLayouts[PipelineType::eShape2D],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    if (synchronize)
    {
        insert_compute_memory_barrier(commandBuffer);
    }
}

void MediaPipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
    DescriptorScope frameScope,
    DescriptorScope postScope) const
{
    for (const Renderer2DBatch& batch : batches)
    {
        record_batch(
            commandBuffer,
            swapchain,
            pipelines,
            descriptorSets,
            pipelineLayouts,
            batch,
            mediaAssets,
            frameScope,
            postScope);
    }
}

void MediaPipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
    DescriptorScope frameScope,
    DescriptorScope postScope,
    bool synchronize) const
{
    if (mediaAssets == nullptr || batch.mediaId == 0)
    {
        return;
    }

    const auto mediaIt = mediaAssets->find(batch.mediaId);
    if (mediaIt == mediaAssets->end() || !mediaIt->second.drawable || !mediaIt->second.descriptorSet)
    {
        return;
    }
    const Media2DAsset& asset = mediaIt->second;

    const PipelineType pipelineType = PipelineType::eMedia2D;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    const DispatchBounds bounds = make_dispatch_bounds(swapchain, batch, DispatchBoundsMode::eExact);
    if (bounds.empty())
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
    vk::DescriptorSet mediaSet = asset.descriptorSet;
    if (!asset.frameDescriptorSets.empty())
    {
        const uint32_t frameIndex = std::min<uint32_t>(
            batch.frameIndex,
            static_cast<uint32_t>(asset.frameDescriptorSets.size() - 1u));
        mediaSet = asset.frameDescriptorSets[frameIndex];
    }
    if (!mediaSet)
    {
        return;
    }
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
        1, 1, &mediaSet, 0, nullptr);
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipelineLayouts[pipelineType],
        2,
        1,
        &descriptorSets[postScope],
        0,
        nullptr);

    // Media primitive values occupy four bits. Carry the exact dispatch extent
    // in the remaining bits so the shader can reject spare invocations in the
    // final 8x8 workgroup before running an expensive source filter. A zero
    // extent is the fallback for an unusually large (>16K) batch.
    constexpr uint32_t kMediaDispatchExtentMask = 0x3fffu;
    const auto make_media_constants = [&] (
        const DispatchBounds& dispatchBounds,
        uint32_t extraFlags) {
        Renderer2DPushConstants constants = make_push_constants(
            batch,
            0u,
            dispatchBounds.x,
            dispatchBounds.y);
        constants.data.y |= extraFlags;
        if (dispatchBounds.width <= kMediaDispatchExtentMask &&
            dispatchBounds.height <= kMediaDispatchExtentMask)
        {
            constants.data.x =
                (static_cast<uint32_t>(batch.primitive) & 0x0fu) |
                ((dispatchBounds.width & kMediaDispatchExtentMask) << 4u) |
                ((dispatchBounds.height & kMediaDispatchExtentMask) << 18u);
        }
        return constants;
    };

    const uint32_t blurRadius = static_cast<uint32_t>(
        std::clamp(std::lround(batch.effect0.z), 0l, 32l));
    const bool useSeparableBlur =
        blurRadius > 0u &&
        (batch.flags & eRenderer2DStyleMediaSingleBlurSample) != 0u &&
        (batch.flags & eRenderer2DStyleTransform2_5D) == 0u;
    if (useSeparableBlur)
    {
        const uint32_t horizontalTop =
            bounds.y > blurRadius ? bounds.y - blurRadius : 0u;
        const uint32_t horizontalBottom = std::min(
            bounds.y + bounds.height + blurRadius,
            swapchain.extent.height);
        const DispatchBounds horizontalBounds {
            bounds.x,
            horizontalTop,
            bounds.width,
            horizontalBottom - horizontalTop
        };
        Renderer2DPushConstants horizontalConstants = make_media_constants(
            horizontalBounds,
            eRenderer2DStyleMediaBlurHorizontal);
        commandBuffer.pushConstants(
            pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute,
            0,
            sizeof(horizontalConstants),
            &horizontalConstants);
        dispatch_bounds(commandBuffer, horizontalBounds);
        insert_compute_memory_barrier(commandBuffer);
    }

    Renderer2DPushConstants constants = make_media_constants(
        bounds,
        useSeparableBlur ? eRenderer2DStyleMediaBlurVertical : 0u);
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    if (synchronize || useSeparableBlur)
    {
        // A following media batch may reuse overlapping horizontal scratch
        // even when its final destination is disjoint. Always finish vertical
        // scratch reads before allowing the next horizontal writer to begin.
        insert_compute_memory_barrier(commandBuffer);
    }
}

void TextPipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    DescriptorScope frameScope,
    DescriptorScope postScope) const
{
    for (const Renderer2DBatch& batch : batches)
    {
        if ((batch.flags & (eRenderer2DStyleShadow | eRenderer2DStyleGlow)) != 0u)
        {
            record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, batch, frameScope,
                postScope, kTextPassUnderlay);
        }
    }

    for (const Renderer2DBatch& batch : batches)
    {
        record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, batch, frameScope, postScope,
            kTextPassForeground);
    }
}

void TextPipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    DescriptorScope frameScope,
    DescriptorScope postScope,
    uint32_t textPass,
    bool synchronize) const
{
    const PipelineType pipelineType = PipelineType::eTextMSDF;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, postScope);

    auto record_text_draw = [&](const Renderer2DBatch& drawBatch, uint32_t drawPass, DispatchBoundsMode boundsMode) {
        const DispatchBounds bounds = make_dispatch_bounds(swapchain, drawBatch, boundsMode, false);
        if (bounds.empty())
        {
            return;
        }

        Renderer2DPushConstants constants = make_push_constants(drawBatch, drawPass, bounds.x, bounds.y);
        // Text does not use data.x for a primitive type. Carry the exact
        // dispatch extent there so the shader can reject spare invocations in
        // the final 8x8 workgroup instead of drawing beyond a scrolling mask.
        constants.data.x = pack_dispatch_extent(bounds.width, bounds.height);
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        dispatch_bounds(commandBuffer, bounds);
        if (synchronize)
        {
            insert_compute_memory_barrier(commandBuffer);
        }
    };

    if (textPass == kTextPassUnderlay)
    {
        if ((batch.flags & eRenderer2DStyleShadow) != 0u)
        {
            Renderer2DBatch shadowBatch = batch;
            shadowBatch.color2 = batch.shadowColor;
            shadowBatch.effect0.z = 0.0f;
            shadowBatch.flags &= ~eRenderer2DStyleGlow;
            record_text_draw(shadowBatch, textPass, DispatchBoundsMode::eTextUnderlay);
        }

        if ((batch.flags & eRenderer2DStyleGlow) != 0u)
        {
            Renderer2DBatch glowBatch = batch;
            glowBatch.effect1.x = 0.0f;
            glowBatch.effect1.y = 0.0f;
            glowBatch.effect1.z = 0.0f;
            glowBatch.flags &= ~eRenderer2DStyleShadow;
            record_text_draw(glowBatch, textPass, DispatchBoundsMode::eTextUnderlay);
        }
        return;
    }

    const DispatchBoundsMode boundsMode =
        textPass == kTextPassForeground ? DispatchBoundsMode::eTextForeground :
        DispatchBoundsMode::eText;
    record_text_draw(batch, textPass, boundsMode);
}

void CompositePipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    bool useExternalBackdropUnderlay,
    bool writeNativeSurface,
    bool writeCompositionSurface,
    glm::uvec4 contentRect, Renderer2DViewportFade viewportFade) const
{
    const PipelineType pipelineType = PipelineType::eComposite2D;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts);
    constexpr uint32_t useExternalBackdropFlag = 1u << 0u;
    constexpr uint32_t writeCompositionSurfaceFlag = 1u << 1u;
    constexpr uint32_t writeNativeSurfaceFlag = 1u << 2u;
    const uint32_t flags =
        (useExternalBackdropUnderlay ? useExternalBackdropFlag : 0u) |
        (writeCompositionSurface ? writeCompositionSurfaceFlag : 0u) |
        (writeNativeSurface ? writeNativeSurfaceFlag : 0u);
    DispatchBounds bounds = {};
    if (writeNativeSurface || useExternalBackdropUnderlay)
    {
        bounds = make_full_screen_bounds(swapchain);
    }
    else if (contentRect.z > 0u && contentRect.w > 0u)
    {
        const uint32_t x = std::min(contentRect.x, swapchain.extent.width);
        const uint32_t y = std::min(contentRect.y, swapchain.extent.height);
        bounds = {
            x,
            y,
            std::min(contentRect.z, swapchain.extent.width - x),
            std::min(contentRect.w, swapchain.extent.height - y)
        };
    }
    if (bounds.empty())
    {
        return;
    }

    const Renderer2DCompositeConstants constants { { flags | (viewportFade.enabled ? 16u : 0u), bounds.x, bounds.y, 0u },
        viewportFade.rect, { viewportFade.topHeight, viewportFade.bottomHeight, viewportFade.minimumOpacity, 0.0f } };
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void Hosted3DCompositePipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    DescriptorScope frameScope,
    DescriptorScope postScope,
    glm::uvec4 dispatchRect) const
{
    const PipelineType pipelineType = PipelineType::eCompositeHosted3D;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, postScope);
    const DispatchBounds bounds =
        dispatchRect.z > 0u && dispatchRect.w > 0u
        ? DispatchBounds {
            std::min(dispatchRect.x, swapchain.extent.width),
            std::min(dispatchRect.y, swapchain.extent.height),
            std::min(dispatchRect.z, swapchain.extent.width - std::min(dispatchRect.x, swapchain.extent.width)),
            std::min(dispatchRect.w, swapchain.extent.height - std::min(dispatchRect.y, swapchain.extent.height))
        }
        : make_full_screen_bounds(swapchain);
    if (bounds.empty())
    {
        return;
    }

    const glm::uvec4 constants { bounds.x, bounds.y, bounds.width, bounds.height };
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void Renderer2D::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    Renderer2DScene& scene,
    double currentTimeSeconds,
    Renderer3D* renderer3D,
    std::unordered_map<uint32_t, Model3DAsset>* modelAssets,
    std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
    StorageImage* dynamicRenderTarget,
    StorageImage* staticRenderTarget,
    StorageImage* hosted3DResolveTarget,
    vk::RenderPass hosted3DRenderPass,
    vk::Framebuffer hosted3DFramebuffer,
    bool hosted3DUsesResolveAttachment,
    bool externalBackdropAvailable,
    StorageImage* foregroundBlurRenderTarget,
    Renderer2DTextBatchResources* textBatchResources) const
{
    if (textBatchResources != nullptr)
    {
        textBatchResources->writtenGlyphs = 0u;
    }
    const auto previousViewportFade = renderPlanCache.viewportFade;
    scene.build_render_plan(renderPlanCache, currentTimeSeconds, cachedLayerGeneration);
    recordDiagnostics = {};

    if (renderPlanCache.dynamicEntities != cachedDynamicEntities &&
        !renderPlanCache.rebuildCachedLayer)
    {
        // Cache modes are public components and can be changed directly. Never
        // trust a missed mark_dirty() to leave old pixels in the static image:
        // an ownership-set change always rebuilds the static layer before the
        // same entity can be emitted dynamically (or restored to static).
        // Ownership is independent of emitted paint, so scrolling through
        // clips or zero-opacity fades cannot trigger this safeguard.
        scene.build_render_plan(renderPlanCache, currentTimeSeconds, 0u);
    }
    cachedDynamicEntities.assign(
        renderPlanCache.dynamicEntities.begin(), renderPlanCache.dynamicEntities.end());

    // Every in-flight slot owns a retained dynamic image. On first use it has no
    // trustworthy contents, so clear it completely. Afterwards, clear exactly
    // the workgroup-aligned envelope written by this same slot on its previous
    // use. The envelope is accumulated from the actual dispatch bounds below,
    // including multi-pass blur, shadow, text, masks, replayed overlays and 3D.
    // This removes stale pixels without clearing an entire ultrawide surface for
    // a small island visualiser update.
    if (!dynamicSurfaceInitialized ||
        (dynamicSurfaceBounds.z > 0u && dynamicSurfaceBounds.w > 0u))
    {
        clear_frame_surface(
            commandBuffer,
            swapchain,
            pipelines,
            descriptorSets,
            pipelineLayouts,
            DescriptorScope::eFrame,
            dynamicSurfaceInitialized ? dynamicSurfaceBounds : glm::uvec4(0u));
    }
    dynamicSurfaceInitialized = true;

    DispatchBounds currentDynamicSurfaceBounds = {};
    const auto include_dynamic_surface_bounds = [&](const DispatchBounds& bounds) {
        if (bounds.empty())
        {
            return;
        }
        if (currentDynamicSurfaceBounds.empty())
        {
            currentDynamicSurfaceBounds = bounds;
            return;
        }
        const uint32_t left = std::min(currentDynamicSurfaceBounds.x, bounds.x);
        const uint32_t top = std::min(currentDynamicSurfaceBounds.y, bounds.y);
        const uint32_t right = std::max(
            currentDynamicSurfaceBounds.x + currentDynamicSurfaceBounds.width,
            bounds.x + bounds.width);
        const uint32_t bottom = std::max(
            currentDynamicSurfaceBounds.y + currentDynamicSurfaceBounds.height,
            bounds.y + bounds.height);
        currentDynamicSurfaceBounds = {
            left,
            top,
            right - left,
            bottom - top
        };
    };

    auto record_layered_ops = [&](
        const std::vector<Renderer2DBatch>& panelBlurs,
        const std::vector<Renderer2DBatch>& shadows,
        const std::vector<Renderer2DBatch>& blurs,
        const std::vector<Renderer2DBatch>& shapes,
        const std::vector<Renderer2DBatch>& media,
        const std::vector<Renderer2DBatch>& texts,
        const std::vector<Renderer3DModelBatch>& models,
        DescriptorScope frameScope,
        DescriptorScope blurPostScope,
        DescriptorScope textPostScope,
        bool includeStaticBackdrop,
        bool includeExternalBackdrop) {
        std::vector<Renderer2DRenderOp> ops;
        ops.reserve(panelBlurs.size() + shadows.size() + blurs.size() + shapes.size() + media.size() +
            texts.size() * 2u + models.size());
        add_render_ops(ops, panelBlurs, Renderer2DRenderOpType::ePanelBlur);
        add_render_ops(ops, shadows, Renderer2DRenderOpType::eShadow);
        add_render_ops(ops, blurs, Renderer2DRenderOpType::eBlur);
        add_render_ops(ops, shapes, Renderer2DRenderOpType::eShape);
        add_render_ops(ops, media, Renderer2DRenderOpType::eMedia);
        add_model_ops(ops, models);
        for (const Renderer2DBatch& batch : texts)
        {
            if ((batch.flags & (eRenderer2DStyleShadow | eRenderer2DStyleGlow)) != 0u)
            {
                ops.push_back({
                    Renderer2DRenderOpType::eTextUnderlay,
                    &batch,
                    nullptr,
                    batch.entity,
                    batch.stackLayer,
                    batch.stackOrder,
                    batch.layer,
                    batch.order,
                    batch.alwaysOnTop
                });
            }
            ops.push_back({
                Renderer2DRenderOpType::eText,
                &batch,
                nullptr,
                batch.entity,
                batch.stackLayer,
                batch.stackOrder,
                batch.layer,
                batch.order,
                batch.alwaysOnTop
            });
        }
        sort_render_ops(ops);

        // Most UI primitives write independent rectangles into the same storage image. Vulkan
        // does not require a memory dependency between disjoint writes, so defer the compute
        // barrier until a later primitive overlaps one of them or a multi-pass effect needs the
        // accumulated image. This is especially important for text, where a marquee can otherwise
        // emit one full pipeline barrier per glyph on every submitted frame.
        std::vector<DispatchBounds> pendingWrites;
        pendingWrites.reserve(ops.size());

        const auto flush_pending_writes = [&]() {
            if (pendingWrites.empty())
            {
                return;
            }
            insert_compute_memory_barrier(commandBuffer);
            ++recordDiagnostics.layeredBarriers;
            pendingWrites.clear();
        };

        const auto prepare_simple_write = [&](const DispatchBounds& bounds) {
            if (bounds.empty())
            {
                return false;
            }
            // Static surfaces live for many frames and across cache ownership
            // handoffs. Build them deterministically: an underestimated optical
            // bound must never turn two storage-image writes into an unordered
            // race whose missing pixels then remain cached. Dynamic surfaces
            // keep the cheaper overlap-aware batching on every motion frame.
            const bool overlapsPendingWrite =
                frameScope == DescriptorScope::eUICache ||
                std::any_of(
                    pendingWrites.begin(),
                    pendingWrites.end(),
                    [&](const DispatchBounds& pending) {
                        return dispatch_bounds_overlap(pending, bounds);
                    });
            if (overlapsPendingWrite)
            {
                flush_pending_writes();
            }
            pendingWrites.push_back(bounds);
            return true;
        };

        std::vector<DispatchBounds> textBounds;
        std::vector<DispatchBounds> textWriteBounds;
        std::vector<const Renderer2DBatch*> textBatches;
        vibrance::vulkan::TextBatchPlan textBatchPlan;
        const auto batchPipeline = pipelines.find(PipelineType::eTextMSDFBatch);
        const auto batchLayout = pipelineLayouts.find(PipelineType::eTextMSDFBatch);
        const bool canBatchText = frameScope == DescriptorScope::eFrame &&
            textBatchResources != nullptr && textBatchResources->descriptorSet &&
            textBatchResources->glyphConstants != nullptr &&
            batchPipeline != pipelines.end() && batchPipeline->second &&
            batchLayout != pipelineLayouts.end() && batchLayout->second;

        if (canBatchText && textBatchResources->writtenGlyphs < textBatchResources->capacity &&
            textBatchResources->capacity - textBatchResources->writtenGlyphs >= 2u)
        {
            // Multi-pass filters and models retain their position: they may
            // read neighbouring scene pixels. Ordinary primitives access only
            // their destination rectangle and stable source textures.
            const auto simple_mode = [](const Renderer2DRenderOp& op) -> std::optional<DispatchBoundsMode> {
                DispatchBoundsMode mode;
                switch (op.type)
                {
                case Renderer2DRenderOpType::eShape: mode = DispatchBoundsMode::eShape; break;
                case Renderer2DRenderOpType::eShadow: mode = DispatchBoundsMode::eShadow; break;
                case Renderer2DRenderOpType::eMedia:
                    // Filtered media can use shared horizontal scratch. Keep
                    // it outside reordering even if its output is disjoint.
                    if (op.batch && op.batch->effect0.z >= 0.5f) return std::nullopt;
                    mode = DispatchBoundsMode::eExact;
                    break;
                case Renderer2DRenderOpType::eText: mode = DispatchBoundsMode::eTextForeground; break;
                default: return std::nullopt;
                }
                return mode;
            };
            std::vector<Renderer2DRenderOp> orderedOps;
            std::vector<DispatchBounds> simpleBounds;
            std::vector<uint8_t> foregroundText;
            vibrance::vulkan::SimpleRenderOrder simpleOrder;
            orderedOps.reserve(ops.size());
            for (std::size_t first = 0u; first < ops.size();)
            {
                simpleBounds.clear();
                foregroundText.clear();
                uint32_t foregroundCount = 0u;
                std::size_t end = first;
                for (; end < ops.size() && end - first < vibrance::vulkan::kTextBatchPlanningGlyphLimit; ++end)
                {
                    if (!simple_mode(ops[end])) break;
                    foregroundText.push_back(ops[end].type == Renderer2DRenderOpType::eText ? 1u : 0u);
                    foregroundCount += foregroundText.back();
                }
                if (end == first)
                {
                    orderedOps.push_back(ops[first++]);
                    continue;
                }
                // Pure text already has its bounded dependency planner, and
                // chunks without two glyphs cannot benefit from batching.
                if (foregroundCount < 2u || foregroundCount == end - first)
                {
                    orderedOps.insert(orderedOps.end(), ops.begin() + first, ops.begin() + end);
                    first = end;
                    continue;
                }
                // Calculate geometry only for chunks that can combine text
                // across ordinary primitives. The serial path needs no copy.
                for (std::size_t index = first; index < end; ++index)
                {
                    const auto& op = ops[index];
                    simpleBounds.push_back(op.batch ? make_dispatch_bounds(swapchain, *op.batch,
                        *simple_mode(op), op.type != Renderer2DRenderOpType::eText) : DispatchBounds {});
                }
                vibrance::vulkan::build_simple_render_order(
                    std::span<const DispatchBounds>(simpleBounds),
                    std::span<const uint8_t>(foregroundText), simpleOrder);
                for (const uint32_t index : simpleOrder.indices)
                    orderedOps.push_back(ops[first + index]);
                first = end;
            }
            ops.swap(orderedOps);
        }

        uint32_t modelOrdinal = 0;
        for (std::size_t opIndex = 0u; opIndex < ops.size(); ++opIndex)
        {
            const Renderer2DRenderOp& op = ops[opIndex];
            if (canBatchText && op.type == Renderer2DRenderOpType::eText &&
                textBatchResources->writtenGlyphs < textBatchResources->capacity &&
                textBatchResources->capacity - textBatchResources->writtenGlyphs >= 2u)
            {
                textBounds.clear();
                textWriteBounds.clear();
                textBatches.clear();
                std::size_t runEnd = opIndex;
                for (; runEnd < ops.size() &&
                    runEnd - opIndex < vibrance::vulkan::kTextBatchPlanningGlyphLimit &&
                    ops[runEnd].type == Renderer2DRenderOpType::eText; ++runEnd)
                {
                    if (ops[runEnd].batch == nullptr)
                    {
                        continue;
                    }
                    const auto& batch = *ops[runEnd].batch;
                    textBounds.push_back(make_dispatch_bounds(
                        swapchain, batch, DispatchBoundsMode::eTextForeground, false));
                    textWriteBounds.push_back(make_dispatch_bounds(
                        swapchain, batch, DispatchBoundsMode::eTextForeground));
                    textBatches.push_back(&batch);
                }
                vibrance::vulkan::build_text_batch_plan(
                    std::span<const DispatchBounds>(textBounds), textBatchPlan,
                    std::span<const DispatchBounds>(pendingWrites));
                for (const auto& group : textBatchPlan.groups)
                {
                    recordDiagnostics.textGlyphs += group.count;
                    const bool hasCapacity =
                        textBatchResources->writtenGlyphs <= textBatchResources->capacity &&
                        group.count <= textBatchResources->capacity - textBatchResources->writtenGlyphs;
                    if (group.count == 1u || !hasCapacity)
                    {
                        for (uint32_t offset = 0u; offset < group.count; ++offset)
                        {
                            const uint32_t index = textBatchPlan.glyphIndices[group.first + offset];
                            if (prepare_simple_write(textBounds[index]))
                            {
                                include_dynamic_surface_bounds(textWriteBounds[index]);
                                textPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets,
                                    pipelineLayouts, *textBatches[index], frameScope, textPostScope,
                                    kTextPassForeground, false);
                                ++recordDiagnostics.textDispatches;
                            }
                        }
                        continue;
                    }

                    // All actual writes within this group are disjoint. Check
                    // its complete set against previously dispatched writes
                    // before recording any member, then retain the existing
                    // overlap-aware dependencies across waves and other ops.
                    const bool overlapsPendingWrite = std::any_of(
                        textBatchPlan.glyphIndices.begin() + group.first,
                        textBatchPlan.glyphIndices.begin() + group.first + group.count,
                        [&](uint32_t index) {
                            return std::any_of(pendingWrites.begin(), pendingWrites.end(),
                                [&](const DispatchBounds& pending) {
                                    return dispatch_bounds_overlap(pending, textBounds[index]);
                                });
                        });
                    if (overlapsPendingWrite)
                    {
                        flush_pending_writes();
                    }
                    const uint32_t firstGlyph = textBatchResources->writtenGlyphs;
                    for (uint32_t offset = 0u; offset < group.count; ++offset)
                    {
                        const uint32_t index = textBatchPlan.glyphIndices[group.first + offset];
                        const auto& bounds = textBounds[index];
                        Renderer2DPushConstants constants = make_push_constants(
                            *textBatches[index], kTextPassForeground, bounds.x, bounds.y);
                        constants.data.x = pack_dispatch_extent(bounds.width, bounds.height);
                        std::memcpy(textBatchResources->glyphConstants +
                            static_cast<std::size_t>(firstGlyph + offset) * sizeof(constants),
                            &constants, sizeof(constants));
                        pendingWrites.push_back(textBounds[index]);
                        include_dynamic_surface_bounds(textWriteBounds[index]);
                    }
                    textBatchResources->writtenGlyphs += group.count;
                    commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, batchPipeline->second);
                    bind_frame_set(commandBuffer, PipelineType::eTextMSDFBatch, descriptorSets,
                        pipelineLayouts, frameScope);
                    bind_post_set(commandBuffer, PipelineType::eTextMSDFBatch, descriptorSets,
                        pipelineLayouts, textPostScope);
                    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, batchLayout->second,
                        2u, 1u, &textBatchResources->descriptorSet, 0u, nullptr);
                    const glm::uvec4 dispatchConstants { firstGlyph, 0u, 0u, 0u };
                    commandBuffer.pushConstants(batchLayout->second, vk::ShaderStageFlagBits::eCompute,
                        0u, sizeof(dispatchConstants), &dispatchConstants);
                    commandBuffer.dispatch(workgroup_count(group.width), workgroup_count(group.height), group.count);
                    ++recordDiagnostics.textDispatches;
                    ++recordDiagnostics.textBatchDispatches;
                }
                opIndex = runEnd - 1u;
                continue;
            }
            if (op.type == Renderer2DRenderOpType::eModel3D)
            {
                flush_pending_writes();
                if (renderer3D != nullptr && op.model != nullptr)
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(make_dispatch_bounds(
                            swapchain,
                            intersect_rect(op.model->viewportRect, op.model->clipRect),
                            2.0f));
                    }
                    const uint32_t depthLayer = kHostedModelDepthMax - std::min(modelOrdinal, kHostedModelDepthMax);
                    StorageImage* modelCompositeTarget =
                        frameScope == DescriptorScope::eUICache
                        ? staticRenderTarget
                        : (frameScope == DescriptorScope::eForegroundBlur
                            ? foregroundBlurRenderTarget
                            : dynamicRenderTarget);

                    Model3DAsset* modelAsset = nullptr;
                    StorageBuffer* modelVertexBuffer = nullptr;
                    uint32_t modelDefaultFirstTriangle = 0u;
                    uint32_t modelDefaultTriangleCount = 0u;
                    if (op.model->modelId != 0)
                    {
                        modelVertexBuffer = nullptr;
                        if (modelAssets != nullptr)
                        {
                            const auto modelIt = modelAssets->find(op.model->modelId);
                            if (modelIt != modelAssets->end())
                            {
                                modelAsset = &modelIt->second;
                                modelVertexBuffer = &modelAsset->buffer;
                                modelDefaultFirstTriangle = 0u;
                                modelDefaultTriangleCount = modelVertexBuffer->triangleCount;
                            }
                        }
                    }

                    if (modelVertexBuffer == nullptr || modelCompositeTarget == nullptr)
                    {
                        ++modelOrdinal;
                        continue;
                    }
                    const bool useHostedGraphics =
                        modelAsset != nullptr &&
                        hosted3DResolveTarget != nullptr &&
                        static_cast<bool>(hosted3DFramebuffer);

                    const bool recordedHostedModel = renderer3D->record_model(
                        commandBuffer,
                        swapchain,
                        pipelines,
                        descriptorSets,
                        pipelineLayouts,
                        frameScope,
                        *op.model,
                        modelDefaultFirstTriangle,
                        modelDefaultTriangleCount,
                        depthLayer,
                        modelVertexBuffer,
                        modelAsset,
                        useHostedGraphics ? hosted3DResolveTarget : nullptr,
                        hosted3DRenderPass,
                        useHostedGraphics ? hosted3DFramebuffer : vk::Framebuffer {},
                        hosted3DUsesResolveAttachment);
                    if (recordedHostedModel && useHostedGraphics)
                    {
                        const DispatchBounds compositeBounds = make_dispatch_bounds(
                            swapchain,
                            intersect_rect(op.model->viewportRect, op.model->clipRect),
                            0.0f);
                        if (compositeBounds.empty())
                        {
                            ++modelOrdinal;
                            continue;
                        }

                        hosted3DCompositePipeline.record(
                            commandBuffer,
                            swapchain,
                            pipelines,
                            descriptorSets,
                            pipelineLayouts,
                            frameScope,
                            blurPostScope,
                            {
                                compositeBounds.x,
                                compositeBounds.y,
                                compositeBounds.width,
                                compositeBounds.height
                            });
                    }
                    ++modelOrdinal;
                }
                continue;
            }

            if (op.batch == nullptr)
            {
                continue;
            }

            switch (op.type)
            {
            case Renderer2DRenderOpType::ePanelBlur:
                flush_pending_writes();
                if (frameScope == DescriptorScope::eFrame)
                {
                    include_dynamic_surface_bounds(
                        make_blur_dispatch_bounds(swapchain, *op.batch));
                }
                blurPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, blurPostScope, includeStaticBackdrop, includeExternalBackdrop);
                break;
            case Renderer2DRenderOpType::eShadow:
            {
                const DispatchBounds bounds = make_dispatch_bounds(
                    swapchain, *op.batch, DispatchBoundsMode::eShadow);
                if (prepare_simple_write(bounds))
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(bounds);
                    }
                    shadowPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                        *op.batch, frameScope, false);
                }
                break;
            }
            case Renderer2DRenderOpType::eBlur:
                flush_pending_writes();
                if (frameScope == DescriptorScope::eFrame)
                {
                    include_dynamic_surface_bounds(
                        make_blur_dispatch_bounds(swapchain, *op.batch));
                }
                blurPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, blurPostScope, includeStaticBackdrop, includeExternalBackdrop);
                break;
            case Renderer2DRenderOpType::eShape:
            {
                const DispatchBounds bounds = make_dispatch_bounds(
                    swapchain, *op.batch, DispatchBoundsMode::eShape);
                if (prepare_simple_write(bounds))
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(bounds);
                    }
                    shapePipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                        *op.batch, frameScope, false);
                }
                break;
            }
            case Renderer2DRenderOpType::eMedia:
            {
                const DispatchBounds bounds = make_dispatch_bounds(
                    swapchain, *op.batch, DispatchBoundsMode::eExact);
                if (prepare_simple_write(bounds))
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(bounds);
                    }
                    mediaPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                        *op.batch, mediaAssets, frameScope, blurPostScope, false);
                }
                break;
            }
            case Renderer2DRenderOpType::eModel3D:
                break;
            case Renderer2DRenderOpType::eTextUnderlay:
                flush_pending_writes();
                if (frameScope == DescriptorScope::eFrame)
                {
                    include_dynamic_surface_bounds(make_dispatch_bounds(
                        swapchain, *op.batch, DispatchBoundsMode::eTextUnderlay));
                }
                textPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, textPostScope, kTextPassUnderlay);
                break;
            case Renderer2DRenderOpType::eText:
            {
                const DispatchBounds bounds = make_dispatch_bounds(
                    swapchain, *op.batch, DispatchBoundsMode::eTextForeground, false);
                if (prepare_simple_write(bounds))
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(make_dispatch_bounds(
                            swapchain, *op.batch, DispatchBoundsMode::eTextForeground));
                    }
                    textPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                        *op.batch, frameScope, textPostScope, kTextPassForeground, false);
                    ++recordDiagnostics.textGlyphs;
                    ++recordDiagnostics.textDispatches;
                }
                break;
            }
            }
        }
        flush_pending_writes();
    };

    entt::entity foregroundBlurRoot = entt::null;
    float foregroundBlurRadius = 0.0f;
    const auto foregroundBlurGroups =
        scene.registry().view<const ForegroundBlurGroup2DComponent>();
    for (const entt::entity entity : foregroundBlurGroups)
    {
        const float radius =
            foregroundBlurGroups.get<const ForegroundBlurGroup2DComponent>(entity).radius;
        if (radius > 0.01f)
        {
            foregroundBlurRoot = entity;
            foregroundBlurRadius = radius;
            break;
        }
    }

    auto split_foreground_group = [&](const std::vector<Renderer2DBatch>& source,
                                      std::vector<Renderer2DBatch>& regular,
                                      std::vector<Renderer2DBatch>& grouped) {
        regular.clear();
        grouped.clear();
        for (const Renderer2DBatch& batch : source)
        {
            (foregroundBlurRoot != entt::null &&
                batch.foregroundBlurGroup == foregroundBlurRoot ? grouped : regular)
                .push_back(batch);
        }
    };
    Renderer2DRenderPlan regularForegroundPlan {};
    Renderer2DRenderPlan groupedForegroundPlan {};
    if (foregroundBlurRoot != entt::null)
    {
        split_foreground_group(renderPlanCache.panelBlurs, regularForegroundPlan.panelBlurs, groupedForegroundPlan.panelBlurs);
        split_foreground_group(renderPlanCache.shadows, regularForegroundPlan.shadows, groupedForegroundPlan.shadows);
        split_foreground_group(renderPlanCache.blurs, regularForegroundPlan.blurs, groupedForegroundPlan.blurs);
        split_foreground_group(renderPlanCache.shapes, regularForegroundPlan.shapes, groupedForegroundPlan.shapes);
        split_foreground_group(renderPlanCache.media, regularForegroundPlan.media, groupedForegroundPlan.media);
        split_foreground_group(renderPlanCache.texts, regularForegroundPlan.texts, groupedForegroundPlan.texts);
        regularForegroundPlan.models.reserve(renderPlanCache.models.size());
        groupedForegroundPlan.models.reserve(renderPlanCache.models.size());
        for (const Renderer3DModelBatch& model : renderPlanCache.models)
        {
            (model.foregroundBlurGroup == foregroundBlurRoot ?
                groupedForegroundPlan.models : regularForegroundPlan.models).push_back(model);
        }
    }
    // Ordinary scrolling and optical frames use the retained plan directly.
    // Partitioning only needs storage while a foreground group is active.
    const Renderer2DRenderPlan& regularPlan = foregroundBlurRoot == entt::null ?
        renderPlanCache : regularForegroundPlan;

    Renderer2DBatch foregroundBlurBatch {};
    DispatchBounds foregroundBlurContentBounds {};
    if (foregroundBlurRoot != entt::null)
    {
        foregroundBlurContentBounds =
            make_dynamic_coverage_bounds(swapchain, groupedForegroundPlan);
        if (!foregroundBlurContentBounds.empty())
        {
            foregroundBlurBatch.entity = foregroundBlurRoot;
            foregroundBlurBatch.primitive = Renderer2DPrimitive::eRectangle;
            foregroundBlurBatch.rect = {
                static_cast<float>(foregroundBlurContentBounds.x),
                static_cast<float>(foregroundBlurContentBounds.y),
                static_cast<float>(foregroundBlurContentBounds.width),
                static_cast<float>(foregroundBlurContentBounds.height)
            };
            foregroundBlurBatch.clipRect = {
                0.0f, 0.0f,
                static_cast<float>(swapchain.extent.width),
                static_cast<float>(swapchain.extent.height)
            };
            foregroundBlurBatch.effect0 = {
                foregroundBlurRadius, 1.0f, 0.0f, 1.0f
            };
            const DispatchBounds sourceClearBounds = make_dispatch_bounds(
                swapchain,
                expand_and_clip_rect(
                    foregroundBlurBatch.rect,
                    foregroundBlurRadius * 2.0f + 2.0f,
                    foregroundBlurBatch.clipRect),
                0.0f);
            clear_frame_surface(
                commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                DescriptorScope::eForegroundBlur,
                { sourceClearBounds.x, sourceClearBounds.y,
                    sourceClearBounds.width, sourceClearBounds.height });
            record_layered_ops(
                groupedForegroundPlan.panelBlurs, groupedForegroundPlan.shadows,
                groupedForegroundPlan.blurs, groupedForegroundPlan.shapes,
                groupedForegroundPlan.media, groupedForegroundPlan.texts,
                groupedForegroundPlan.models,
                DescriptorScope::eForegroundBlur, DescriptorScope::ePost,
                DescriptorScope::ePost, false, false);
        }
    }

    auto include_dynamic_layer = [](
        const RenderLayer2DKey& layer,
        bool& hasLayer,
        RenderLayer2DKey& lowestLayer) {
        if (!hasLayer || render_layer_key_less(layer, lowestLayer))
        {
            lowestLayer = layer;
            hasLayer = true;
        }
    };

    bool hasDynamicLayer = false;
    RenderLayer2DKey lowestDynamicLayer {};
    auto include_dynamic_batches = [&](const std::vector<Renderer2DBatch>& batches) {
        for (const Renderer2DBatch& batch : batches)
        {
            include_dynamic_layer(render_layer_key(batch), hasDynamicLayer, lowestDynamicLayer);
        }
    };
    include_dynamic_batches(renderPlanCache.panelBlurs);
    include_dynamic_batches(renderPlanCache.shadows);
    include_dynamic_batches(renderPlanCache.blurs);
    include_dynamic_batches(renderPlanCache.shapes);
    include_dynamic_batches(renderPlanCache.media);
    include_dynamic_batches(renderPlanCache.texts);
    for (const Renderer3DModelBatch& model : renderPlanCache.models)
    {
        include_dynamic_layer(render_layer_key(model), hasDynamicLayer, lowestDynamicLayer);
    }

    DispatchBounds dynamicCoverageBounds =
        make_dynamic_coverage_bounds(swapchain, renderPlanCache);
    if (hasDynamicLayer)
    {
        const Renderer2DRenderPlan& staticPlan = renderPlanCache.rebuildCachedLayer ?
            renderPlanCache : cachedLayerPlanCache;
        dynamicCoverageBounds = expand_cached_replay_bounds(
            swapchain, staticPlan, lowestDynamicLayer, dynamicCoverageBounds);
    }

    auto append_cached_overlays = [&](
        std::vector<Renderer2DBatch>& target,
        const std::vector<Renderer2DBatch>& cached,
        Renderer2DRenderOpType type) {
        for (const auto& selected : cachedLayerReplayBatches)
        {
            if (selected.first == static_cast<uint32_t>(type))
            {
                target.push_back(cached[selected.second]);
            }
        }
    };

    auto store_cached_layer_plan = [&]() {
        cachedLayerPlanCache.cachedPanelBlurs = renderPlanCache.cachedPanelBlurs;
        cachedLayerPlanCache.cachedShadows = renderPlanCache.cachedShadows;
        cachedLayerPlanCache.cachedBlurs = renderPlanCache.cachedBlurs;
        cachedLayerPlanCache.cachedShapes = renderPlanCache.cachedShapes;
        cachedLayerPlanCache.cachedMedia = renderPlanCache.cachedMedia;
        cachedLayerPlanCache.cachedTexts = renderPlanCache.cachedTexts;
    };

    const Renderer2DRenderPlan& currentStaticPlan = renderPlanCache.rebuildCachedLayer ?
        renderPlanCache : cachedLayerPlanCache;
    collect_cached_replay_membership(swapchain, currentStaticPlan, lowestDynamicLayer,
        dynamicCoverageBounds, hasDynamicLayer, currentCachedLayerReplayBatches);
    // Moving or bouncing controls do not change the retained static pixels
    // when they promote the same cached overlays. A changed selection still
    // rebuilds the static surface so entering/leaving overlays paint once.
    const bool rebuildForDynamicCutoff =
        currentCachedLayerReplayBatches != cachedLayerReplayBatches;
    const bool rebuildCachedLayer = renderPlanCache.rebuildCachedLayer || rebuildForDynamicCutoff;
    recordDiagnostics.cacheRebuilds = rebuildCachedLayer ? 1u : 0u;
    recordDiagnostics.cachedBatchReplays =
        static_cast<uint32_t>(currentCachedLayerReplayBatches.size());
    recordDiagnostics.dynamicBatchReplays = static_cast<uint32_t>(
        renderPlanCache.panelBlurs.size() + renderPlanCache.shadows.size() +
        renderPlanCache.blurs.size() + renderPlanCache.shapes.size() +
        renderPlanCache.media.size() + renderPlanCache.texts.size() +
        renderPlanCache.models.size());

    if (rebuildCachedLayer)
    {
        // A visibility-only dynamic transition (such as a blinking caret) does
        // not invalidate static entities. Reuse the last complete static plan
        // while rebuilding its cutoff, unless the scene itself changed.
        const Renderer2DRenderPlan& staticPlan = renderPlanCache.rebuildCachedLayer ?
            renderPlanCache :
            cachedLayerPlanCache;
        std::vector<Renderer2DBatch> cachedPanelBlurs = staticPlan.cachedPanelBlurs;
        std::vector<Renderer2DBatch> cachedShadows = staticPlan.cachedShadows;
        std::vector<Renderer2DBatch> cachedBlurs = staticPlan.cachedBlurs;
        std::vector<Renderer2DBatch> cachedShapes = staticPlan.cachedShapes;
        std::vector<Renderer2DBatch> cachedMedia = staticPlan.cachedMedia;
        std::vector<Renderer2DBatch> cachedTexts = staticPlan.cachedTexts;

        if (hasDynamicLayer)
        {
            // Higher cached batches are replayed with the moving subtree to preserve
            // z-order. Remove them from the static surface first so they are blended once.
            auto remove_replayed_overlays = [&] (
                std::vector<Renderer2DBatch>& batches,
                Renderer2DRenderOpType type) {
                std::size_t index = 0u;
                batches.erase(
                    std::remove_if(
                        batches.begin(),
                        batches.end(),
                        [&](const Renderer2DBatch&) {
                            return std::binary_search(currentCachedLayerReplayBatches.begin(),
                                currentCachedLayerReplayBatches.end(),
                                std::pair<uint32_t, std::size_t> {
                                    static_cast<uint32_t>(type), index++ });
                        }),
                    batches.end());
            };
            remove_replayed_overlays(cachedPanelBlurs, Renderer2DRenderOpType::ePanelBlur);
            remove_replayed_overlays(cachedShadows, Renderer2DRenderOpType::eShadow);
            remove_replayed_overlays(cachedBlurs, Renderer2DRenderOpType::eBlur);
            remove_replayed_overlays(cachedShapes, Renderer2DRenderOpType::eShape);
            remove_replayed_overlays(cachedMedia, Renderer2DRenderOpType::eMedia);
            remove_replayed_overlays(cachedTexts, Renderer2DRenderOpType::eText);
        }

        clear_frame_surface(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, DescriptorScope::eUICache);
        record_layered_ops(
            cachedPanelBlurs,
            cachedShadows,
            cachedBlurs,
            cachedShapes,
            cachedMedia,
            cachedTexts,
            {},
            DescriptorScope::eUICache,
            DescriptorScope::eUICachePost,
            DescriptorScope::ePost,
            false,
            externalBackdropAvailable);
        if (renderPlanCache.rebuildCachedLayer)
        {
            store_cached_layer_plan();
            cachedLayerGeneration = scene.cache_generation();
        }
        cachedLayerReplayBatches.swap(currentCachedLayerReplayBatches);
    }

    if (hasDynamicLayer)
    {
        // Cached chrome above moving content is replayed so static cache does not flatten z-order
        std::vector<Renderer2DBatch> panelBlurs = regularPlan.panelBlurs;
        std::vector<Renderer2DBatch> shadows = regularPlan.shadows;
        std::vector<Renderer2DBatch> blurs = regularPlan.blurs;
        std::vector<Renderer2DBatch> shapes = regularPlan.shapes;
        std::vector<Renderer2DBatch> media = regularPlan.media;
        std::vector<Renderer2DBatch> texts = regularPlan.texts;

        append_cached_overlays(
            panelBlurs,
            cachedLayerPlanCache.cachedPanelBlurs,
            Renderer2DRenderOpType::ePanelBlur);
        append_cached_overlays(
            shadows,
            cachedLayerPlanCache.cachedShadows,
            Renderer2DRenderOpType::eShadow);
        append_cached_overlays(
            blurs,
            cachedLayerPlanCache.cachedBlurs,
            Renderer2DRenderOpType::eBlur);
        append_cached_overlays(
            shapes,
            cachedLayerPlanCache.cachedShapes,
            Renderer2DRenderOpType::eShape);
        append_cached_overlays(
            media,
            cachedLayerPlanCache.cachedMedia,
            Renderer2DRenderOpType::eMedia);
        append_cached_overlays(
            texts,
            cachedLayerPlanCache.cachedTexts,
            Renderer2DRenderOpType::eText);

        record_layered_ops(
            panelBlurs,
            shadows,
            blurs,
            shapes,
            media,
            texts,
            regularPlan.models,
            DescriptorScope::eFrame,
            DescriptorScope::ePost,
            DescriptorScope::ePost,
            true,
            externalBackdropAvailable);
    }
    else
    {
        record_layered_ops(
            regularPlan.panelBlurs,
            regularPlan.shadows,
            regularPlan.blurs,
            regularPlan.shapes,
            regularPlan.media,
            regularPlan.texts,
            regularPlan.models,
            DescriptorScope::eFrame,
            DescriptorScope::ePost,
            DescriptorScope::ePost,
            true,
            externalBackdropAvailable);
    }

    if (!foregroundBlurContentBounds.empty())
    {
        const DispatchBounds blurBounds =
            make_blur_dispatch_bounds(swapchain, foregroundBlurBatch);
        include_dynamic_surface_bounds(blurBounds);
        blurPipeline.record_foreground_group(
            commandBuffer, swapchain, pipelines, descriptorSets,
            pipelineLayouts, foregroundBlurBatch);
    }

    const DispatchBounds visibleBounds = collect_rendered_bounds(
        swapchain, renderPlanCache, cachedLayerPlanCache,
        foregroundBlurContentBounds.empty() ? nullptr : &foregroundBlurBatch,
        currentEntityBounds);
    auto include_bounds = [&](DispatchBounds& target, const DispatchBounds& bounds) {
        if (bounds.empty())
        {
            return;
        }
        if (target.empty())
        {
            target = bounds;
            return;
        }
        const uint32_t left = std::min(target.x, bounds.x);
        const uint32_t top = std::min(target.y, bounds.y);
        const uint32_t right = std::max(
            target.x + target.width,
            bounds.x + bounds.width);
        const uint32_t bottom = std::max(
            target.y + target.height,
            bounds.y + bounds.height);
        target = { left, top, right - left, bottom - top };
    };
    contentBounds = {
        visibleBounds.x,
        visibleBounds.y,
        visibleBounds.width,
        visibleBounds.height
    };
    vibrance::renderer::coalesce_entity_bounds(currentEntityBounds);

    DispatchBounds trackedDamage = {};
    // A viewport mask changes the final pixels without changing cached paint.
    // Refresh every retained composition buffer when its gradient changes.
    if (rebuildCachedLayer || scene.fullDamagePending_ || previousViewportFade != renderPlanCache.viewportFade)
    {
        trackedDamage = make_full_screen_bounds(swapchain);
    }
    else
    {
        const auto include_entity_rect = [&](const auto& bounds) {
            include_bounds(
                trackedDamage,
                DispatchBounds {
                    bounds.second.x,
                    bounds.second.y,
                    bounds.second.z,
                    bounds.second.w });
        };
        // Every Renderer2D instance owns one in-flight frame slot. Compare its
        // current entity envelope with what that same slot presented last time;
        // a scene-global previous-bounds list can be consumed by another slot
        // first and leave moved/destroyed pixels behind when buffers rotate.
        vibrance::renderer::collect_entity_damage(
            currentEntityBounds, presentedBounds, include_entity_rect,
            [&](entt::entity entity) {
                return scene.damage_pending(entity, currentTimeSeconds);
            });
    }

    // Compute shaders dispatch in 8x8 groups. Retained clears, damage copies,
    // and compositor updates must own the same complete workgroups; tracking an
    // exact pixel rect can otherwise leave a one-pixel edge from an older slot.
    const DispatchBounds alignedDamage = align_dispatch_bounds_to_workgroups(
        swapchain,
        trackedDamage);
    damageBounds = {
        alignedDamage.x,
        alignedDamage.y,
        alignedDamage.width,
        alignedDamage.height };
    const DispatchBounds alignedDynamicSurface =
        align_dispatch_bounds_to_workgroups(swapchain, currentDynamicSurfaceBounds);
    dynamicSurfaceBounds = {
        alignedDynamicSurface.x,
        alignedDynamicSurface.y,
        alignedDynamicSurface.width,
        alignedDynamicSurface.height };
    presentedBounds.swap(currentEntityBounds);
    // History is local to this frame slot. The scene only retires dirty flags;
    // a scene-global copy is unused and cannot represent all in-flight slots.
    scene.commit_presented_bounds({});
}

void Renderer2D::record_composite(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    bool useExternalBackdropUnderlay,
    bool writeNativeSurface,
    bool writeCompositionSurface,
    glm::uvec4 contentRect) const
{
    compositePipeline.record(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
        useExternalBackdropUnderlay,
        writeNativeSurface,
        writeCompositionSurface,
        contentRect, renderPlanCache.viewportFade);
}

glm::uvec4 Renderer2D::content_bounds() const
{
    return contentBounds;
}

glm::uvec4 Renderer2D::damage_bounds() const
{
    return damageBounds;
}

Renderer2D::RecordDiagnostics Renderer2D::record_diagnostics() const
{
    return recordDiagnostics;
}

void Renderer2D::invalidate_dynamic_surface()
{
    dynamicSurfaceInitialized = false;
    cachedLayerGeneration = 0u;
    cachedLayerReplayBatches.clear();
    currentCachedLayerReplayBatches.clear();
    cachedDynamicEntities.clear();
    presentedBounds.clear();
    currentEntityBounds.clear();
    recordDiagnostics = {};
    renderPlanCache.clear();
    cachedLayerPlanCache.clear();
    dynamicSurfaceBounds = glm::uvec4(0u);
    contentBounds = glm::uvec4(0u);
    damageBounds = glm::uvec4(0u);
}

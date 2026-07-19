#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include "vibranceUI/export.h"
#include <vulkan/vulkan.hpp>
#include <unordered_map>
#include <vibranceUI/renderer/render_types.h>
#include <vibranceUI/renderer/swapchain.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/core/camera.h>

class StorageBuffer;
class StorageImage;
struct Model3DAsset;

class VIBRANCE_ENGINE_API Renderer3D
{
public:
    // Records either the full model buffer or one hosted 3D model into a render target
    void record(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope renderTargetScope,
        uint32_t firstTriangle,
        uint32_t triangleCount,
        const Camera& camera
    ) const;

    bool record_model(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope renderTargetScope,
        const Renderer3DModelBatch& model,
        uint32_t defaultFirstTriangle,
        uint32_t defaultTriangleCount,
        uint32_t depthLayer,
        StorageBuffer* vertexBuffer = nullptr,
        Model3DAsset* modelAsset = nullptr,
        StorageImage* renderTarget = nullptr,
        vk::RenderPass renderPass = nullptr,
        vk::Framebuffer framebuffer = nullptr,
        bool renderPassHasResolveAttachment = false
    ) const;
};

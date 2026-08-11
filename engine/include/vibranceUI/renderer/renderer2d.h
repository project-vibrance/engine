#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include "vibranceUI/export.h"
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <unordered_map>
#include <vibranceUI/renderer/buffer.h>
#include <vibranceUI/renderer/render_types.h>
#include <vibranceUI/renderer/swapchain.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/renderer/media2d.h>

class Renderer3D;
class StorageImage;

class ShapePipeline
{
public:
    // Records shape batches through a compute raster path
    void record(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const std::vector<Renderer2DBatch>& batches,
        DescriptorScope frameScope = DescriptorScope::eFrame
    ) const;

    void record_batch(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const Renderer2DBatch& batch,
        DescriptorScope frameScope = DescriptorScope::eFrame,
        bool synchronize = true
    ) const;
};

class ShadowPipeline
{
public:
    void record(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const std::vector<Renderer2DBatch>& batches,
        DescriptorScope frameScope = DescriptorScope::eFrame
    ) const;

    void record_batch(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const Renderer2DBatch& batch,
        DescriptorScope frameScope = DescriptorScope::eFrame,
        bool synchronize = true
    ) const;
};

class BlurPipeline
{
public:
    // Records foreground and backdrop blur passes using frame or cached sources
    void record(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const std::vector<Renderer2DBatch>& batches,
        DescriptorScope frameScope = DescriptorScope::eFrame,
        DescriptorScope postScope = DescriptorScope::ePost
    ) const;

    void record_batch(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const Renderer2DBatch& batch,
        DescriptorScope frameScope = DescriptorScope::eFrame,
        DescriptorScope postScope = DescriptorScope::ePost,
        bool includeStaticBackdrop = false,
        bool includeExternalBackdrop = false
    ) const;
};

class TextPipeline
{
public:
    void record(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const std::vector<Renderer2DBatch>& batches,
        DescriptorScope frameScope = DescriptorScope::eFrame,
        DescriptorScope postScope = DescriptorScope::ePost
    ) const;

    void record_batch(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const Renderer2DBatch& batch,
        DescriptorScope frameScope = DescriptorScope::eFrame,
        DescriptorScope postScope = DescriptorScope::ePost,
        uint32_t textPass = 0,
        bool synchronize = true
    ) const;
};

class MediaPipeline
{
public:
    void record(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const std::vector<Renderer2DBatch>& batches,
        std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
        DescriptorScope frameScope = DescriptorScope::eFrame
    ) const;

    void record_batch(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const Renderer2DBatch& batch,
        std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
        DescriptorScope frameScope = DescriptorScope::eFrame,
        bool synchronize = true
    ) const;
};

class CompositePipeline
{
public:
    void record(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        bool useExternalBackdropUnderlay = false,
        bool writeNativeSurface = true,
        bool writeCompositionSurface = false,
        glm::uvec4 contentRect = glm::uvec4(0u)
    ) const;
};

class Hosted3DCompositePipeline
{
public:
    void record(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope frameScope,
        DescriptorScope postScope,
        glm::uvec4 dispatchRect = glm::uvec4(0u)
    ) const;
};

class VIBRANCE_ENGINE_API Renderer2D
{
public:
    // Records the full 2D pass order: blur, shadow, shape, media, text, hosted 3D, composite
    void record(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        Renderer2DScene& scene,
        double currentTimeSeconds,
        Renderer3D* renderer3D = nullptr,
        std::unordered_map<uint32_t, Model3DAsset>* modelAssets = nullptr,
        std::unordered_map<uint32_t, Media2DAsset>* mediaAssets = nullptr,
        StorageImage* dynamicRenderTarget = nullptr,
        StorageImage* staticRenderTarget = nullptr,
        StorageImage* hosted3DResolveTarget = nullptr,
        vk::RenderPass hosted3DRenderPass = nullptr,
        vk::Framebuffer hosted3DFramebuffer = nullptr,
        bool hosted3DUsesResolveAttachment = false,
        bool externalBackdropAvailable = false
    ) const;

    void record_composite(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        bool useExternalBackdropUnderlay = false,
        bool writeNativeSurface = true,
        bool writeCompositionSurface = false,
        glm::uvec4 contentRect = glm::uvec4(0u)
    ) const;

    // Pixel bounds containing every visible batch in the most recently built
    // render plan. Composition uses this to avoid processing transparent
    // pixels across the rest of a full-screen overlay window.
    glm::uvec4 content_bounds() const;
    glm::uvec4 damage_bounds() const;

    // Call when the frame slot receives a new dynamic storage image. The next
    // record clears the complete image once before returning to bounded clears.
    void invalidate_dynamic_surface();

private:
    ShapePipeline shapePipeline;
    ShadowPipeline shadowPipeline;
    BlurPipeline blurPipeline;
    TextPipeline textPipeline;
    MediaPipeline mediaPipeline;
    Hosted3DCompositePipeline hosted3DCompositePipeline;
    CompositePipeline compositePipeline;
    mutable Renderer2DRenderPlan renderPlanCache;
    mutable Renderer2DRenderPlan cachedLayerPlanCache;
    mutable uint64_t cachedLayerGeneration = 0;
    mutable bool cachedLayerHasDynamicCutoff = false;
    mutable int32_t cachedLayerCutoffStackLayer = 0;
    mutable uint32_t cachedLayerCutoffStackOrder = 0;
    mutable int32_t cachedLayerCutoffLayer = 0;
    mutable uint32_t cachedLayerCutoffOrder = 0;
    mutable bool cachedLayerCutoffAlwaysOnTop = false;
    mutable glm::uvec4 cachedLayerCutoffBounds { 0u };
    mutable bool dynamicSurfaceInitialized = false;
    mutable std::vector<uint32_t> cachedDynamicEntities;
    mutable std::vector<std::pair<entt::entity, glm::uvec4>> presentedBounds;
    mutable glm::uvec4 dynamicSurfaceBounds { 0u };
    mutable glm::uvec4 contentBounds { 0u };
    mutable glm::uvec4 damageBounds { 0u };
};

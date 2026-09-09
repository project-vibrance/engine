#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vector>
#include <vibranceUI/renderer/image.h>
#include <vibranceUI/renderer/swapchain.h>
#include <vibranceUI/renderer/buffer.h>
#include <vma/vk_mem_alloc.h>
#include <vibranceUI/factories/mesh_factory.h>
#include <vibranceUI/renderer/render_types.h>
#include <vibranceUI/renderer/renderer2d.h>
#include <vibranceUI/renderer/renderer3d.h>

class Frame
{
    public:
    // Per-frame resources own render targets, synchronisation and command recording
    Frame(
        Swapchain& swapchain,
        vk::Extent2D renderExtent,
        vk::Extent2D modelRenderExtent,
        vk::Device& logicalDevice,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        vk::RenderPass hosted3DRenderPass,
        vk::SampleCountFlagBits hosted3DSamples,
        vk::CommandBuffer commandBuffer,
        vk::Queue& queue,
        std::deque<std::function<void(vk::Device)>>& deletionQueue,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        VmaAllocator& allocator,
        std::unordered_map<uint32_t, Model3DAsset>* modelAssets,
        std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
        Renderer2DScene& scene2D,
        StorageImage* fontAtlasImage
    );

    void record_command_buffer(
        uint32_t imageIndex,
        double currentTimeSeconds,
        bool externalBackdropAvailable,
        bool useExternalBackdropUnderlay,
        bool presentNativeSurface,
        bool clearNativeSurface,
        vk::Image compositionImage = {},
        vk::Buffer compositionReadbackBuffer = {},
        bool compositionImageFirstUse = true,
        glm::uvec4 pendingCompositionDamageRect = glm::uvec4(0u),
        uint32_t graphicsQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED
    );

    void resize_resources(vk::Extent2D newRenderExtent, vk::Extent2D newModelRenderExtent);

    // Rebinds the font image sampled by both the live UI and retained UI cache.
    // The graphics queue must be idle before changing descriptors for frames
    // that may already have been submitted.
    void set_font_atlas_image(StorageImage* image);

    void free_resources();

    vk::Device& logicalDevice;

    vk::CommandBuffer commandBuffer;

    Swapchain& swapchain;
    vk::Extent2D renderExtent;
    vk::Extent2D modelRenderExtent;
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines;
    vk::RenderPass hosted3DRenderPass = nullptr;
    vk::SampleCountFlagBits hosted3DSamples = vk::SampleCountFlagBits::e1;

    vk::Semaphore imageAcquiredSemaphore;

    vk::Semaphore renderFinishedSemaphore;

    vk::Fence renderFinishedFence;

    // Destination-pixel bounds written into the shared Composition texture by
    // the current command buffer. The D3D11 bridge uses the same bounds for
    // its swapchain copy and dirty rectangle.
    glm::uvec4 compositionContentRect { 0u };
    glm::uvec4 compositionDamageRect { 0u };
    glm::uvec4 compositionSceneDamageRect { 0u };
    // Native backdrop state captured from the same scene/timestamp as this
    // command buffer. It must be applied only when this Vulkan frame presents.
    std::vector<SystemBackdropRegion> compositionBackdropRegions;

    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets;
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts;

    VmaAllocator& allocator;
    // These surfaces form the 2D, hosted 3D, blur, cache, and external backdrop graph
    StorageImage* depthBuffer = nullptr, *colorBuffer = nullptr, *modelDepthBuffer = nullptr, *modelColorBuffer = nullptr, *tempSurface = nullptr;
    // DXGI Composition swapchains consume premultiplied alpha. Keep that
    // output separate from tempSurface, whose straight alpha is required by
    // Vulkan surfaces that advertise post-multiplied composition.
    StorageImage* compositionSurface = nullptr;
    StorageImage* uiBlurSurface = nullptr, *mediaBlurSurface = nullptr;
    StorageImage* uiStaticSurface = nullptr, *uiStaticBlurSurface = nullptr;
    StorageImage* externalBackdropSurface = nullptr, *fontAtlasImage = nullptr;
    ColorAttachmentImage* hosted3DColorBuffer = nullptr;
    DepthImage* hosted3DDepthBuffer = nullptr;
    vk::Framebuffer hosted3DFramebuffer = nullptr;
    std::unordered_map<uint32_t, Model3DAsset>* modelAssets = nullptr;
    std::unordered_map<uint32_t, Media2DAsset>* mediaAssets = nullptr;
    Renderer2D renderer2D;
    Renderer3D renderer3D;
    Renderer2DScene& scene2D;

    std::deque<std::function<void(VmaAllocator)>> vmaDeletionQueue;
    std::deque<std::function<void(vk::Device)>> deviceDeletionQueue;

    vk::Queue queue;
    bool uiCacheImagesReady = false;
};

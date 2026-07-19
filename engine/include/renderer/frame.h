#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>
#include <unordered_map>
#include <vibranceUI/renderer/image.h>
#include <vibranceUI/renderer/swapchain.h>
#include <vibranceUI/renderer/buffer.h>
#include <vma/vk_mem_alloc.h>
#include <vibranceUI/factories/mesh_factory.h>
#include <vibranceUI/renderer/render_types.h>
#include <vibranceUI/renderer/renderer2d.h>
#include <vibranceUI/renderer/renderer3d.h>
#include <vibranceUI/core/camera.h>

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
        StorageBuffer* vertexBuffer,
        std::unordered_map<uint32_t, Model3DAsset>* modelAssets,
        std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
        Renderer2DScene& scene2D,
        StorageImage* fontAtlasImage
    );

    void record_command_buffer(
        uint32_t imageIndex,
        const Camera& camera,
        double currentTimeSeconds,
        bool externalBackdropAvailable,
        bool useExternalBackdropUnderlay
    );
    
    void resize_resources(vk::Extent2D newRenderExtent, vk::Extent2D newModelRenderExtent);

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

    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets;
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts;

    VmaAllocator& allocator;
    // These surfaces form the 2D, hosted 3D, blur, cache, and external backdrop graph
    StorageImage* depthBuffer = nullptr, *colorBuffer = nullptr, *modelDepthBuffer = nullptr, *modelColorBuffer = nullptr, *tempSurface = nullptr;
    StorageImage* uiBlurSurface = nullptr, *uiStaticSurface = nullptr, *uiStaticBlurSurface = nullptr;
    StorageImage* externalBackdropSurface = nullptr, *fontAtlasImage = nullptr;
    ColorAttachmentImage* hosted3DColorBuffer = nullptr;
    DepthImage* hosted3DDepthBuffer = nullptr;
    vk::Framebuffer hosted3DFramebuffer = nullptr;
    StorageBuffer* vertexBuffer = nullptr;
    std::unordered_map<uint32_t, Model3DAsset>* modelAssets = nullptr;
    std::unordered_map<uint32_t, Media2DAsset>* mediaAssets = nullptr;
    Renderer2D renderer2D;
    Renderer3D renderer3D;
    Renderer2DScene& scene2D;
    uint32_t triangleCount = 0;
    uint32_t triangleCount2D = 0;
    uint32_t firstTriangle3D = 0;
    uint32_t triangleCount3D = 0;

    std::deque<std::function<void(VmaAllocator)>> vmaDeletionQueue;
    std::deque<std::function<void(vk::Device)>> deviceDeletionQueue;

    vk::Queue queue;
    bool uiCacheImagesReady = false;
};

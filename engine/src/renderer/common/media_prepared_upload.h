#pragma once

#include <vibranceUI/renderer/media2d.h>

#if !defined(__APPLE__)
// Prepared uploads own every resource until their fence retires. In
// particular, an unpublished cancelled animation never enters the Engine's
// lifetime deletion queues or its visible media cache.
struct PreparedMedia2DAsyncUpload
{
    Media2DAsset asset;
    Media2DAsset* publishedAsset = nullptr;
    vk::DescriptorPool descriptorPool = nullptr;
    vk::CommandPool commandPool = nullptr;
    vk::CommandBuffer commandBuffer = nullptr;
    vk::Fence fence = nullptr;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    VmaAllocation stagingAllocation = nullptr;
    uint32_t completedFrames = 0;
    std::size_t reservedBytes = 0;
    bool submitted = false;
    bool cancelled = false;
    bool published = false;
};

struct PreparedMediaUploadBudget
{
    static constexpr std::size_t limit = 192u * 1024u * 1024u;
    static constexpr std::size_t maximumActiveUploads = 8u;
    std::size_t reservedBytes = 0;
    std::size_t activeUploads = 0;
    bool reserve(std::size_t bytes)
    {
        if (bytes > limit || reservedBytes > limit - bytes || activeUploads >= maximumActiveUploads) return false;
        reservedBytes += bytes;
        ++activeUploads;
        return true;
    }
    void release(std::size_t bytes)
    {
        reservedBytes -= std::min(bytes, reservedBytes);
        if (activeUploads > 0u) --activeUploads;
    }
};

VIBRANCE_ENGINE_API std::size_t prepared_media_2d_upload_bytes(const PreparedMedia2D& prepared);
VIBRANCE_ENGINE_API bool prepared_media_2d_upload_retired(
    vk::Device device, const PreparedMedia2DAsyncUpload& state);
VIBRANCE_ENGINE_API void release_prepared_media_2d_upload(
    VmaAllocator allocator, vk::Device device, PreparedMedia2DAsyncUpload& state);
VIBRANCE_ENGINE_API Media2DUploadResult upload_prepared_media_2d_asset_async(
    const PreparedMedia2D& prepared, uint32_t maxFrameUploads, uint32_t mediaId,
    VmaAllocator allocator, vk::Device device, vk::Queue queue,
    uint32_t queueFamilyIndex, vk::DescriptorSetLayout descriptorSetLayout,
    PreparedMedia2DAsyncUpload& state);
#endif

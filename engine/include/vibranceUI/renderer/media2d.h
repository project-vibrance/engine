// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include "vibranceUI/export.h"
#if !defined(__APPLE__)
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>
#endif
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <stop_token>
#include <string>
#include <vector>
#include <vibranceUI/renderer/image.h>
#include <vibranceUI/renderer/renderer2d_components.h>

struct Media2DLoadOptions
{

    uint32_t rasterWidth = 0;
    uint32_t rasterHeight = 0;
    bool srgb = false;
    bool generateMipmaps = true;
    bool premultiplyAlpha = false;

    uint32_t maxAnimationFrames = 0;

    uint32_t svgAnimationFrames = 0;
    double svgAnimationFrameRate = 60.0;


    uint32_t lottieAnimationFrames = 0;



    uint32_t maxVideoFrames = 0;
    uint32_t maxVideoPixels = 640u * 640u;


    bool trimTransparentBorders = false;


    std::size_t maxDecodedBytes = 0;
    bool resizeRasterImages = false;
};



class VIBRANCE_ENGINE_API PreparedMedia2D
{
public:
    struct Data;
    PreparedMedia2D() = default;
    bool valid() const;
    uint32_t frame_count() const;
    std::size_t decoded_byte_count() const;
    const std::filesystem::path& path() const;
    const Media2DLoadOptions& options() const;
    const std::string& cache_key() const;

private:
    std::shared_ptr<const Data> data_;
    friend struct Media2DPreparedAccess;
};

VIBRANCE_ENGINE_API PreparedMedia2D prepare_media_2d(
    const std::filesystem::path& path,
    const Media2DLoadOptions& options = {},
    std::stop_token cancellation = {});


VIBRANCE_ENGINE_API std::string media_2d_cache_key(
    const std::filesystem::path& path, const Media2DLoadOptions& options);

struct Media2DUploadResult
{
    Media2DHandle handle;

    bool finished = false;
};

struct Media2DAsset
{

    uint32_t id = 0;
    Media2DSourceType sourceType = Media2DSourceType::eUnknown;
    glm::uvec2 pixelSize { 0u };
    std::filesystem::path path;
    std::string name;
    double durationSeconds = 0.0;
    double frameRate = 0.0;
    uint32_t frameCount = 1;
    bool animated = false;
    bool drawable = false;
    bool hasBlackBackground = false;
    bool premultipliedAlpha = false;
    std::unique_ptr<StorageImage> image;
    std::vector<std::unique_ptr<StorageImage>> extraFrameImages;
#if defined(__APPLE__)
    std::vector<StorageImage*> frameImages;
#else
    std::vector<vk::DescriptorSet> frameDescriptorSets;
#endif
    std::vector<double> frameDurationsSeconds;
#if !defined(__APPLE__)
    vk::Sampler sampler = nullptr;
    vk::DescriptorSet descriptorSet = nullptr;
#endif

    Media2DHandle handle() const
    {
        Media2DHandle out = {};
        out.id = id;
        out.sourceType = sourceType;
        out.pixelSize = pixelSize;
        out.durationSeconds = durationSeconds;
        out.frameRate = frameRate;
        out.frameCount = frameCount;
        out.frameDurationsSeconds = frameDurationsSeconds;
        out.animated = animated;
        out.drawable = drawable;
        out.hasBlackBackground = hasBlackBackground;
        out.premultipliedAlpha = premultipliedAlpha;
        return out;
    }
};

Media2DHandle load_media_2d_asset(
    const std::filesystem::path& path,
    const Media2DLoadOptions& options,
    uint32_t mediaId,
#if defined(__APPLE__)
    const MetalTextureUpload& upload,
#else
    VmaAllocator& allocator,
    std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
    std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
    vk::CommandBuffer commandBuffer,
    vk::Queue queue,
    vk::Device logicalDevice,
    vk::DescriptorPool descriptorPool,
    vk::DescriptorSetLayout descriptorSetLayout,
#endif
    Media2DAsset& outAsset
);

Media2DUploadResult upload_prepared_media_2d_asset(
    const PreparedMedia2D& prepared,
    uint32_t maxFrameUploads,
    uint32_t mediaId,
#if defined(__APPLE__)
    const MetalTextureUpload& upload,
#else
    VmaAllocator& allocator,
    std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
    std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
    vk::CommandBuffer commandBuffer,
    vk::Queue queue,
    vk::Device logicalDevice,
    vk::DescriptorPool descriptorPool,
    vk::DescriptorSetLayout descriptorSetLayout,
#endif
    Media2DAsset& outAsset);

#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include "vibranceUI/export.h"
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <vibranceUI/renderer/image.h>
#include <vibranceUI/renderer/renderer2d_components.h>

struct Media2DLoadOptions
{
    // Raster size is mainly for SVGs and other scalable sources
    uint32_t rasterWidth = 0;
    uint32_t rasterHeight = 0;
    bool srgb = false;
    bool generateMipmaps = true;
    bool premultiplyAlpha = false;
    // Maximum decoded/uploaded GIF/APNG/SVG/Lottie frames; set to 0 to keep all generated/source frames
    uint32_t maxAnimationFrames = 0;
    // Explicit animated SVG frame count; set to 0 to derive frames from duration * svgAnimationFrameRate
    uint32_t svgAnimationFrames = 0;
    double svgAnimationFrameRate = 60.0;
    // Lottie keeps its authored composition cadence by default. A requested
    // count resamples the same duration without changing playback speed.
    uint32_t lottieAnimationFrames = 0;
    // Maximum decoded/uploaded video frame samples; set to 0 to decode every
    // source frame. Sampling preserves source duration/frame-rate metadata but
    // reduces the number of unique frames available during playback.
    uint32_t maxVideoFrames = 0;
    uint32_t maxVideoPixels = 640u * 640u;
};

struct Media2DAsset
{
    // CPU-side metadata plus GPU images and descriptor sets for one loaded media file
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
    std::vector<vk::DescriptorSet> frameDescriptorSets;
    std::vector<double> frameDurationsSeconds;
    vk::Sampler sampler = nullptr;
    vk::DescriptorSet descriptorSet = nullptr;

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
    VmaAllocator& allocator,
    std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
    std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
    vk::CommandBuffer commandBuffer,
    vk::Queue queue,
    vk::Device logicalDevice,
    vk::DescriptorPool descriptorPool,
    vk::DescriptorSetLayout descriptorSetLayout,
    Media2DAsset& outAsset
);

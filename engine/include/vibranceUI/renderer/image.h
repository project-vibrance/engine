#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>
#include <deque>
#include <functional>

class StorageImage {

public:
	// General-purpose sampled/storage image used by renderer targets and media uploads
	StorageImage(VmaAllocator& allocator,
		vk::Format format,
		vk::Extent2D extent,
		vk::CommandBuffer commandBuffer,
		vk::Queue queue, vk::Device logicalDevice,
		std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
		std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
		vk::ImageUsageFlags extraUsage = vk::ImageUsageFlags(),
		uint32_t mipLevels = 1,
		bool storageUsage = true
	);

	vk::DescriptorImageInfo descriptor;

	vk::Extent2D extent;

	vk::Image image;

	VmaAllocation memory;

	vk::ImageView view;

	vk::Format format;

	uint32_t mipLevels = 1;

private:
	void initialise(vk::CommandBuffer commandBuffer, vk::Queue queue, vk::Device logicalDevice);

	void make_view(vk::Device logicalDevice);

	void make_descriptor();
};

class DepthImage
{
public:
	// Depth attachment wrapper used by hosted 3D rendering
	DepthImage(VmaAllocator& allocator,
		vk::Format format,
		vk::Extent2D extent,
		vk::SampleCountFlagBits samples,
		vk::Device logicalDevice,
		std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
		std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue
	);

	vk::Extent2D extent;
	vk::Image image;
	VmaAllocation memory;
	vk::ImageView view;
	vk::Format format;
};

class ColorAttachmentImage
{
public:
	// Optional MSAA colour attachment for resolving hosted 3D into 2D composition
	ColorAttachmentImage(VmaAllocator& allocator,
		vk::Format format,
		vk::Extent2D extent,
		vk::SampleCountFlagBits samples,
		vk::Device logicalDevice,
		std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
		std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue
	);

	vk::Extent2D extent;
	vk::Image image;
	VmaAllocation memory;
	vk::ImageView view;
	vk::Format format;
	vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
};

struct ImageInputChunk
{
	vk::Extent2D extent {};
	vk::ImageTiling tiling = vk::ImageTiling::eOptimal;
	vk::ImageUsageFlags usage {};
	vk::MemoryPropertyFlags memoryProperties {};
	vk::Format format = vk::Format::eUndefined;
	vk::ImageCreateFlags flags {};
	uint32_t mipLevels = 1;
	vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
};

void make_image(VmaAllocator& allocator, ImageInputChunk input, vk::Image& image, VmaAllocation& memory);

vk::ImageView create_image_view(
	vk::Device logicalDevice,
	vk::Image image,
	vk::Format format,
	vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
	uint32_t mipLevels = 1
);

void transition_image_layout(
    vk::CommandBuffer commandBuffer, vk::Image image,
    vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
    vk::AccessFlags srcAccessMask, vk::AccessFlags dstAccessMask,
    vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage,
	vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor,
	uint32_t baseMipLevel = 0,
	uint32_t levelCount = 1
);

void copy_image_to_image(vk::CommandBuffer commandBuffer, vk::Image src, vk::Image dst, vk::Extent2D srcSize, vk::Extent2D dstSize);
void copy_image_region_to_image(
    vk::CommandBuffer commandBuffer,
    vk::Image src,
    vk::Image dst,
    vk::Extent2D srcSize,
    vk::Extent2D dstSize,
    vk::Rect2D srcRegion);

#include <vibranceUI/renderer/image.h>
#include <vibranceUI/core/logger.h>
#include <algorithm>

StorageImage::StorageImage(
	VmaAllocator& allocator,
	vk::Format format,
	vk::Extent2D extent,
	vk::CommandBuffer commandBuffer,
	vk::Queue queue, vk::Device logicalDevice,
	std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
	std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
	vk::ImageUsageFlags extraUsage,
	uint32_t mipLevels,
	bool storageUsage
) {
	// Storage images default to transfer support so uploads, copies and mip work can share them
	this->format = format;
	this->mipLevels = std::max(1u, mipLevels);

	ImageInputChunk imageInput = {};
	imageInput.format = format;
	imageInput.extent = extent;
	imageInput.mipLevels = this->mipLevels;
	imageInput.tiling = vk::ImageTiling::eOptimal;
	imageInput.usage = vk::ImageUsageFlagBits::eTransferSrc |
		vk::ImageUsageFlagBits::eTransferDst |
		extraUsage;
	if (storageUsage)
	{
		imageInput.usage |= vk::ImageUsageFlagBits::eStorage;
	}
	imageInput.memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
	make_image(allocator, imageInput, image, memory);

	initialise(commandBuffer, queue, logicalDevice);

	this->extent = extent;

	VkImage imageHandle = image;
	VmaAllocation imageMemory = memory;
	Logger* logger = Logger::fetch_logger();
	vmaDeletionQueue.push_back([imageHandle, imageMemory, logger](VmaAllocator allocator) {
		vmaDestroyImage(allocator, imageHandle, imageMemory);
		logger->vulkan("Destroyed storage image.");
	});

	VkImageView imageViewHandle = view;
	deviceDeletionQueue.push_back([imageViewHandle, logger](vk::Device device) {
		device.destroyImageView(imageViewHandle);
		logger->vulkan("Destroyed storage view.");
	});
}

void StorageImage::initialise(vk::CommandBuffer commandBuffer, vk::Queue queue, vk::Device logicalDevice)
{
	// Newly allocated images are moved to eGeneral because compute shaders read and write them
	Logger* logger = Logger::fetch_logger();

	vk::Result result = commandBuffer.reset();
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to reset command buffer.");
		return;
	}

	vk::CommandBufferBeginInfo beginInfo = {};

	result = commandBuffer.begin(beginInfo);
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to begin command buffer.");
		return;
	}

	transition_image_layout(commandBuffer, image,
		vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
		vk::AccessFlagBits::eNoneKHR, vk::AccessFlagBits::eNoneKHR,
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
		vk::ImageAspectFlagBits::eColor, 0, mipLevels);
	
	result = commandBuffer.end();
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to end command buffer.");
		return;
	}

	vk::SubmitInfo submitInfo;
	submitInfo.setCommandBufferCount(1);
	submitInfo.setPCommandBuffers(&commandBuffer);
	result = queue.submit(1, &submitInfo, nullptr);
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to submit buffer to queue.");
		return;
	}

	result = queue.waitIdle();
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to wait for queue to idle.");
		return;
	}

	make_view(logicalDevice);

	make_descriptor();
}

DepthImage::DepthImage(
	VmaAllocator& allocator,
	vk::Format format,
	vk::Extent2D extent,
	vk::SampleCountFlagBits samples,
	vk::Device logicalDevice,
	std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
	std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue
) {
	this->format = format;
	this->extent = extent;

	ImageInputChunk imageInput = {};
	imageInput.format = format;
	imageInput.extent = extent;
	imageInput.tiling = vk::ImageTiling::eOptimal;
	imageInput.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
	imageInput.memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
	imageInput.samples = samples;
	make_image(allocator, imageInput, image, memory);

	view = create_image_view(logicalDevice, image, format, vk::ImageAspectFlagBits::eDepth);

	VkImage imageHandle = image;
	VmaAllocation imageMemory = memory;
	Logger* logger = Logger::fetch_logger();
	vmaDeletionQueue.push_back([imageHandle, imageMemory, logger](VmaAllocator allocator) {
		vmaDestroyImage(allocator, imageHandle, imageMemory);
		logger->vulkan("Destroyed depth image.");
	});

	VkImageView imageViewHandle = view;
	deviceDeletionQueue.push_back([imageViewHandle, logger](vk::Device device) {
		device.destroyImageView(imageViewHandle);
		logger->vulkan("Destroyed depth view.");
	});
}

ColorAttachmentImage::ColorAttachmentImage(
	VmaAllocator& allocator,
	vk::Format format,
	vk::Extent2D extent,
	vk::SampleCountFlagBits samples,
	vk::Device logicalDevice,
	std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
	std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue
) {
	this->format = format;
	this->extent = extent;
	this->samples = samples;

	ImageInputChunk imageInput = {};
	imageInput.format = format;
	imageInput.extent = extent;
	imageInput.tiling = vk::ImageTiling::eOptimal;
	imageInput.usage = vk::ImageUsageFlagBits::eColorAttachment;
	imageInput.memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
	imageInput.samples = samples;
	make_image(allocator, imageInput, image, memory);

	view = create_image_view(logicalDevice, image, format);

	VkImage imageHandle = image;
	VmaAllocation imageMemory = memory;
	Logger* logger = Logger::fetch_logger();
	vmaDeletionQueue.push_back([imageHandle, imageMemory, logger](VmaAllocator allocator) {
		vmaDestroyImage(allocator, imageHandle, imageMemory);
		logger->vulkan("Destroyed color attachment image.");
	});

	VkImageView imageViewHandle = view;
	deviceDeletionQueue.push_back([imageViewHandle, logger](vk::Device device) {
		device.destroyImageView(imageViewHandle);
		logger->vulkan("Destroyed color attachment view.");
	});
}

void StorageImage::make_view(vk::Device logicalDevice)
{
	view = create_image_view(logicalDevice, image, format, vk::ImageAspectFlagBits::eColor, mipLevels);
}

void StorageImage::make_descriptor()
{
	descriptor.imageLayout = vk::ImageLayout::eGeneral;
	descriptor.imageView = view;
	descriptor.sampler = nullptr;
}

void make_image(VmaAllocator& allocator, ImageInputChunk input, vk::Image& image, VmaAllocation& memory)
{
	// VMA keeps allocation policy centralised for every renderer image
	vk::ImageCreateInfo imageInfo;
	imageInfo.flags = vk::ImageCreateFlagBits() | input.flags;
	imageInfo.imageType = vk::ImageType::e2D;
	imageInfo.extent = vk::Extent3D(input.extent, 1);
	imageInfo.mipLevels = std::max(1u, input.mipLevels);
	imageInfo.arrayLayers = 1;
	imageInfo.format = input.format;
	imageInfo.tiling = input.tiling;
	imageInfo.initialLayout = vk::ImageLayout::eUndefined;
	imageInfo.usage = input.usage;
	imageInfo.sharingMode = vk::SharingMode::eExclusive;
	imageInfo.samples = input.samples;

	VkImageCreateInfo imageCreateInfo = imageInfo;
	VkImage imageHandle;

	VmaAllocationCreateInfo allocationInfo = {};
	allocationInfo.flags = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
	if (input.memoryProperties & vk::MemoryPropertyFlagBits::eHostVisible) {
		allocationInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}
	allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

	VmaAllocationInfo imageAllocationInfo;

	auto result = vmaCreateImage(allocator, &imageCreateInfo,
		&allocationInfo, &imageHandle, &memory,
		&imageAllocationInfo);

	Logger* logger = Logger::fetch_logger();

	if (result != VK_SUCCESS) 
	{
		logger->vulkan("Failed to create image.");
		return;
	}
	logger->vulkan("Successfully created image");

	vmaSetAllocationName(allocator, memory, "Image");
	vmaGetAllocationInfo(allocator, memory, &imageAllocationInfo);

	logger->log(imageAllocationInfo);

	image = imageHandle;
}

vk::ImageView create_image_view(
	vk::Device logicalDevice,
	vk::Image image,
	vk::Format format,
	vk::ImageAspectFlags aspectMask,
	uint32_t mipLevels)
{
    vk::ImageViewCreateInfo createInfo = {};
    createInfo.image = image;
    createInfo.viewType = vk::ImageViewType::e2D;
    createInfo.format = format;
    createInfo.components.r = vk::ComponentSwizzle::eIdentity;
    createInfo.components.g = vk::ComponentSwizzle::eIdentity;
    createInfo.components.b = vk::ComponentSwizzle::eIdentity;
    createInfo.components.a = vk::ComponentSwizzle::eIdentity;
    createInfo.subresourceRange.aspectMask = aspectMask;
    createInfo.subresourceRange.baseMipLevel = 0;
    createInfo.subresourceRange.levelCount = std::max(1u, mipLevels);
    createInfo.subresourceRange.baseArrayLayer = 0;
    createInfo.subresourceRange.layerCount = 1;

    return logicalDevice.createImageView(createInfo).value;
}

void transition_image_layout(
    vk::CommandBuffer commandBuffer, vk::Image image,
    vk::ImageLayout oldLayout, vk::ImageLayout newLayout,
	vk::AccessFlags srcAccessMask, vk::AccessFlags dstAccessMask,
	vk::PipelineStageFlags srcStage, vk::PipelineStageFlags dstStage,
	vk::ImageAspectFlags aspectMask,
	uint32_t baseMipLevel,
	uint32_t levelCount
) {
	vk::ImageSubresourceRange access;
	access.aspectMask = aspectMask;
	access.baseMipLevel = baseMipLevel;
	access.levelCount = std::max(1u, levelCount);
	access.baseArrayLayer = 0;
	access.layerCount = 1;

	vk::ImageMemoryBarrier barrier;
	barrier.oldLayout = oldLayout;
	barrier.newLayout = newLayout;
	barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
	barrier.image = image;
	barrier.subresourceRange = access;

	barrier.srcAccessMask = srcAccessMask;
	barrier.dstAccessMask = dstAccessMask;

	if (srcStage == vk::PipelineStageFlags())
	{
		srcStage = vk::PipelineStageFlagBits::eTopOfPipe;
	}

	if (dstStage == vk::PipelineStageFlags())
	{
		dstStage = vk::PipelineStageFlagBits::eBottomOfPipe;
	}

	commandBuffer.pipelineBarrier(srcStage, dstStage, vk::DependencyFlags(), nullptr, nullptr, barrier);
}

void copy_image_to_image(vk::CommandBuffer commandBuffer, vk::Image src, vk::Image dst, vk::Extent2D srcSize, vk::Extent2D dstSize)
{
	vk::ImageBlit2 blitRegion = {};

	blitRegion.srcOffsets[1].x = srcSize.width;
	blitRegion.srcOffsets[1].y = srcSize.height;
	blitRegion.srcOffsets[1].z = 1;

	blitRegion.dstOffsets[1].x = dstSize.width;
	blitRegion.dstOffsets[1].y = dstSize.height;
	blitRegion.dstOffsets[1].z = 1;

	blitRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	blitRegion.srcSubresource.baseArrayLayer = 0;
	blitRegion.srcSubresource.layerCount = 1;
	blitRegion.srcSubresource.mipLevel = 0;

	blitRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	blitRegion.dstSubresource.baseArrayLayer = 0;
	blitRegion.dstSubresource.layerCount = 1;
	blitRegion.dstSubresource.mipLevel = 0;

	vk::BlitImageInfo2 blitInfo = {};
	blitInfo.dstImage = dst;
	blitInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
	blitInfo.srcImage = src;
	blitInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
	blitInfo.filter = vk::Filter::eNearest;
	blitInfo.regionCount  = 1;
	blitInfo.pRegions = &blitRegion;

	commandBuffer.blitImage2(&blitInfo);
}

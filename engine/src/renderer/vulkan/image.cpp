#include <vibranceUI/renderer/image.h>
#include <vibranceUI/core/logger.h>
#include <algorithm>
#include <stdexcept>

namespace
{
    void require_image_operation(vk::Result result, const char* operation)
    {
        if (result != vk::Result::eSuccess)
            throw std::runtime_error(std::string(operation) + ": " + vk::to_string(result));
    }

    // Commit ownership before submitting any GPU work. A later initialisation
    // failure leaves the allocation with the frame/engine cleanup queues, whose
    // owner waits for its device before releasing submitted resources.
    void retain_image(VmaAllocator allocator, vk::Device device,
        vk::Image image, VmaAllocation memory, vk::ImageView view,
        std::deque<std::function<void(VmaAllocator)>>& allocations,
        std::deque<std::function<void(vk::Device)>>& views)
    {
        const auto allocationCount = allocations.size();
        try
        {
            allocations.push_back([image, memory](VmaAllocator owner) {
                vmaDestroyImage(owner, image, memory);
            });
            views.push_back([view](vk::Device owner) { owner.destroyImageView(view); });
        }
        catch (...)
        {
            if (allocations.size() != allocationCount) allocations.pop_back();
            device.destroyImageView(view);
            vmaDestroyImage(allocator, image, memory);
            throw;
        }
    }

    vk::ImageView make_owned_image_view(VmaAllocator allocator,
        vk::Device device, vk::Image image, VmaAllocation memory,
        vk::Format format, vk::ImageAspectFlags aspect, uint32_t mipLevels = 1u)
    {
        try { return create_image_view(device, image, format, aspect, mipLevels); }
        catch (...)
        {
            vmaDestroyImage(allocator, image, memory);
            throw;
        }
    }
}

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
	view = make_owned_image_view(allocator, logicalDevice, image, memory, format,
		vk::ImageAspectFlagBits::eColor, this->mipLevels);
	retain_image(allocator, logicalDevice, image, memory, view,
		vmaDeletionQueue, deviceDeletionQueue);
	initialise(commandBuffer, queue, logicalDevice);

	this->extent = extent;
}

void StorageImage::initialise(vk::CommandBuffer commandBuffer, vk::Queue queue, vk::Device)
{
	// Newly allocated images are moved to eGeneral because compute shaders read and write them
	vk::Result result = commandBuffer.reset();
	require_image_operation(result, "Could not reset the image initialisation command buffer");

	vk::CommandBufferBeginInfo beginInfo = {};

	result = commandBuffer.begin(beginInfo);
	require_image_operation(result, "Could not begin the image initialisation command buffer");

	transition_image_layout(commandBuffer, image,
		vk::ImageLayout::eUndefined, vk::ImageLayout::eGeneral,
		vk::AccessFlagBits::eNoneKHR, vk::AccessFlagBits::eNoneKHR,
		vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eBottomOfPipe,
		vk::ImageAspectFlagBits::eColor, 0, mipLevels);
	
	result = commandBuffer.end();
	require_image_operation(result, "Could not end the image initialisation command buffer");

	vk::SubmitInfo submitInfo;
	submitInfo.setCommandBufferCount(1);
	submitInfo.setPCommandBuffers(&commandBuffer);
	result = queue.submit(1, &submitInfo, nullptr);
	require_image_operation(result, "Could not submit image initialisation");

	result = queue.waitIdle();
	require_image_operation(result, "Could not complete image initialisation");

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

	view = make_owned_image_view(allocator, logicalDevice, image, memory, format,
		vk::ImageAspectFlagBits::eDepth);
	retain_image(allocator, logicalDevice, image, memory, view,
		vmaDeletionQueue, deviceDeletionQueue);
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

	view = make_owned_image_view(allocator, logicalDevice, image, memory, format,
		vk::ImageAspectFlagBits::eColor);
	retain_image(allocator, logicalDevice, image, memory, view,
		vmaDeletionQueue, deviceDeletionQueue);
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
	image = nullptr;
	memory = nullptr;
	if (!allocator || input.extent.width == 0u || input.extent.height == 0u)
		throw std::runtime_error("Could not allocate a Vulkan image: invalid allocator or empty extent.");
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
	VkImage imageHandle = VK_NULL_HANDLE;

	VmaAllocationCreateInfo allocationInfo = {};
	allocationInfo.flags = VMA_ALLOCATION_CREATE_STRATEGY_BEST_FIT_BIT;
	if (input.memoryProperties & vk::MemoryPropertyFlagBits::eHostVisible) {
		allocationInfo.flags |= VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT;
	}
	allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

	VmaAllocationInfo imageAllocationInfo {};
	Logger* logger = Logger::fetch_logger();

	auto result = vmaCreateImage(allocator, &imageCreateInfo,
		&allocationInfo, &imageHandle, &memory,
		&imageAllocationInfo);

	if (result != VK_SUCCESS) 
	{
		throw std::runtime_error("Could not allocate Vulkan image " +
			std::to_string(input.extent.width) + "x" + std::to_string(input.extent.height) +
			": " + vk::to_string(static_cast<vk::Result>(result)));
	}
	try
	{
		logger->vulkan("Successfully created image");
		vmaSetAllocationName(allocator, memory, "Image");
		vmaGetAllocationInfo(allocator, memory, &imageAllocationInfo);
		logger->log(imageAllocationInfo);
		image = imageHandle;
	}
	catch (...)
	{
		vmaDestroyImage(allocator, imageHandle, memory);
		memory = nullptr;
		throw;
	}
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

	if (!image)
		throw std::runtime_error("Cannot create a Vulkan view for an unallocated image.");
	const auto result = logicalDevice.createImageView(createInfo);
	require_image_operation(result.result, "Could not create a Vulkan image view");
	if (!result.value)
		throw std::runtime_error("The Vulkan driver returned an empty image view.");
	return result.value;
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

void copy_image_region_to_image(
	vk::CommandBuffer commandBuffer,
	vk::Image src,
	vk::Image dst,
	vk::Extent2D srcSize,
	vk::Extent2D dstSize,
	vk::Rect2D srcRegion)
{
	const uint32_t srcX = std::min(
		static_cast<uint32_t>(std::max(srcRegion.offset.x, 0)),
		srcSize.width);
	const uint32_t srcY = std::min(
		static_cast<uint32_t>(std::max(srcRegion.offset.y, 0)),
		srcSize.height);
	const uint32_t srcRight = std::min(
		srcX + srcRegion.extent.width,
		srcSize.width);
	const uint32_t srcBottom = std::min(
		srcY + srcRegion.extent.height,
		srcSize.height);
	if (srcRight <= srcX || srcBottom <= srcY ||
		srcSize.width == 0u || srcSize.height == 0u)
	{
		return;
	}

	auto scale_floor = [](uint32_t value, uint32_t destination, uint32_t source) {
		return static_cast<uint32_t>(
			(static_cast<uint64_t>(value) * destination) / source);
	};
	auto scale_ceil = [](uint32_t value, uint32_t destination, uint32_t source) {
		return static_cast<uint32_t>(
			(static_cast<uint64_t>(value) * destination + source - 1u) / source);
	};

	vk::ImageBlit2 blitRegion = {};
	blitRegion.srcOffsets[0] = vk::Offset3D {
		static_cast<int32_t>(srcX),
		static_cast<int32_t>(srcY),
		0 };
	blitRegion.srcOffsets[1] = vk::Offset3D {
		static_cast<int32_t>(srcRight),
		static_cast<int32_t>(srcBottom),
		1 };
	blitRegion.dstOffsets[0] = vk::Offset3D {
		static_cast<int32_t>(scale_floor(srcX, dstSize.width, srcSize.width)),
		static_cast<int32_t>(scale_floor(srcY, dstSize.height, srcSize.height)),
		0 };
	blitRegion.dstOffsets[1] = vk::Offset3D {
		static_cast<int32_t>(scale_ceil(srcRight, dstSize.width, srcSize.width)),
		static_cast<int32_t>(scale_ceil(srcBottom, dstSize.height, srcSize.height)),
		1 };
	blitRegion.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	blitRegion.srcSubresource.layerCount = 1u;
	blitRegion.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
	blitRegion.dstSubresource.layerCount = 1u;

	vk::BlitImageInfo2 blitInfo = {};
	blitInfo.srcImage = src;
	blitInfo.srcImageLayout = vk::ImageLayout::eTransferSrcOptimal;
	blitInfo.dstImage = dst;
	blitInfo.dstImageLayout = vk::ImageLayout::eTransferDstOptimal;
	blitInfo.filter = vk::Filter::eNearest;
	blitInfo.regionCount = 1u;
	blitInfo.pRegions = &blitRegion;
	commandBuffer.blitImage2(&blitInfo);
}

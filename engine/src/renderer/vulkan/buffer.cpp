#include <vibranceUI/renderer/buffer.h>
#include <vibranceUI/core/logger.h>

StorageBuffer make_depth_buffer(
    VmaAllocator& allocator, 
	std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
	vk::Extent2D size
) {
	// Depth buffer is a storage buffer because the compute raster path writes custom depth
	Logger* logger = Logger::fetch_logger();

	vk::BufferCreateInfo bufferInfo = {};
	bufferInfo.flags = vk::BufferCreateFlags();
	bufferInfo.size = size.width * size.height * sizeof(uint64_t);
	bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer;
	VkBufferCreateInfo bufferInfoHandle = bufferInfo;

	VmaAllocationCreateInfo allocationInfo = {};
	allocationInfo.flags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
	allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

	StorageBuffer mesh;
	VkBuffer bufferHandle;

	VmaAllocationInfo bufferAllocationInfo;

	vmaCreateBuffer(allocator, &bufferInfoHandle, &allocationInfo, &bufferHandle, &(mesh.allocation), &bufferAllocationInfo);
	vmaSetAllocationName(allocator, mesh.allocation, "Storage Buffer");
	vmaGetAllocationInfo(allocator, mesh.allocation, &bufferAllocationInfo);

	logger->log(bufferAllocationInfo);

	mesh.buffer = bufferHandle;

	vmaDeletionQueue.push_back([mesh](VmaAllocator allocator) {
		vmaDestroyBuffer(allocator, mesh.buffer, mesh.allocation);
	});

	mesh.descriptor.buffer = mesh.buffer;
	mesh.descriptor.offset = 0;
	mesh.descriptor.range = bufferAllocationInfo.size;

	return mesh;
}

void copy(
	vk::Buffer srcBuffer,
	vk::Buffer dstBuffer,
	vk::DeviceSize size,
	vk::Queue queue, vk::CommandBuffer commandBuffer
) {
	// Upload copies are synchronous because they happen during asset setup
	Logger* logger = Logger::fetch_logger();

	vk::Result result = commandBuffer.reset();
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to reset command buffer.");
		return;
	}

	vk::CommandBufferBeginInfo beginInfo;
	beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
	result = commandBuffer.begin(beginInfo);
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to begin command buffer.");
		return;
	}

	vk::BufferCopy copyRegion;
	copyRegion.srcOffset = 0;
	copyRegion.dstOffset = 0;
	copyRegion.size = size;
	commandBuffer.copyBuffer(srcBuffer, dstBuffer, 1, &copyRegion);

	result = commandBuffer.end();
	if (result != vk::Result::eSuccess)
	{
		logger->vulkan("Failed to end command buffer.");
		return;
	}

	vk::SubmitInfo submitInfo;
	submitInfo.commandBufferCount = 1;
	submitInfo.pCommandBuffers = &commandBuffer;
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
}

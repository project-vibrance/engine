#include <vibranceUI/renderer/command.h>

vk::CommandPool make_command_pool(vk::Device logicalDevice, uint32_t queueFamilyIndex, std::deque<std::function<void(vk::Device)>>& deletionQueue)
{
	// Resettable primary command buffers are reused by frame and upload paths
	Logger* logger = Logger::fetch_logger();

	vk::CommandPoolCreateInfo poolInfo = {};
	poolInfo.setFlags(vk::CommandPoolCreateFlags() | vk::CommandPoolCreateFlagBits::eResetCommandBuffer);
	poolInfo.setQueueFamilyIndex(queueFamilyIndex);

	vk::CommandPool pool = nullptr;

	auto result = logicalDevice.createCommandPool(poolInfo);
	if (result.result == vk::Result::eSuccess)
    {
		logger->vulkan("Successfully initialised command pool.");
		pool = result.value;
		deletionQueue.push_back([logger, pool](vk::Device device) {
			device.destroyCommandPool(pool);
			logger->vulkan("Destroyed command pool.");
		});
	}
	else 
    {
		logger->vulkan("Failed to initialise command pool.");
	}
	
	return pool;
}

vk::CommandBuffer allocate_command_buffer(vk::Device logicalDevice, vk::CommandPool commandPool) 
{
	// The renderer currently records one primary command buffer per frame
	vk::CommandBufferAllocateInfo allocateInfo = {};
	allocateInfo.setCommandBufferCount(1);
	allocateInfo.setCommandPool(commandPool);
	allocateInfo.setLevel(vk::CommandBufferLevel::ePrimary);
    
	return logicalDevice.allocateCommandBuffers(allocateInfo).value[0];
}

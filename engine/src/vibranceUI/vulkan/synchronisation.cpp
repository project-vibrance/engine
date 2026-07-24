#include <vibranceUI/renderer/synchronisation.h>
#include <vibranceUI/core/logger.h>
#include <sstream>

vk::Semaphore make_semaphore(vk::Device& logicalDevice, std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue)
{
    // Semaphores connect acquire, render and present work across frames
    Logger* logger = Logger::fetch_logger();

    vk::SemaphoreCreateInfo semaphoreInfo;
    
    vk::Semaphore semaphore = logicalDevice.createSemaphore(semaphoreInfo).value;
    VkSemaphore handle = semaphore;

    deviceDeletionQueue.push_back([handle, logger](vk::Device device) {
        vkDestroySemaphore(device, handle, nullptr);
        logger->vulkan("Destroyed semaphore.");
    });

    return semaphore;
}

vk::Fence make_fence(vk::Device& logicalDevice, std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue)
{
    // Start signalled so the first frame does not wait on work that never ran
    Logger* logger = Logger::fetch_logger();

    vk::FenceCreateInfo fenceInfo;
    fenceInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);

    vk::Fence fence = logicalDevice.createFence(fenceInfo).value;

    VkFence handle = fence;

    deviceDeletionQueue.push_back([handle, logger](vk::Device device) {
        vkDestroyFence(device, handle, nullptr);
        logger->vulkan("Destroyed fence.");
    });

    return fence;
}

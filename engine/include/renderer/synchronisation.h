#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>

vk::Semaphore make_semaphore(vk::Device& logicalDevice, std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue);

vk::Fence make_fence(vk::Device& logicalDevice, std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue);
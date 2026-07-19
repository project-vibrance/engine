#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>
#include <vibranceUI/core/logger.h>

vk::CommandPool make_command_pool(vk::Device logicalDevice, uint32_t queueFamilyIndex, std::deque<std::function<void(vk::Device)>>& deletionQueue);

vk::CommandBuffer allocate_command_buffer(vk::Device logicalDevice, vk::CommandPool commandPool);

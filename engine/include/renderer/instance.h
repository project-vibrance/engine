#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>

bool supported_by_instance(const char* const* extensionNames, uint32_t extensionCount, const char* const* layerNames, uint32_t layerCount);

vk::Instance make_instance(
    const char* applicationName,
    uint32_t requestedExtensionCount,
    const char* const* requestedExtensions,
    std::deque<std::function<void(vk::Instance)>>& deletionQueue
);

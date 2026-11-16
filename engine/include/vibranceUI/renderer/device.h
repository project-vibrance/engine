// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>


bool supports(const vk::PhysicalDevice& device, const char** ppRequestedExtensions, const uint32_t requestedExtensionCount);

bool is_suitable(const vk::PhysicalDevice& device, bool requireWindowsCompositionInterop = false,
    uint32_t instanceApiVersion = vk::ApiVersion13);






bool supports_windows_composition_interop(const vk::PhysicalDevice& device);

vk::PhysicalDevice choose_physical_device(
    const vk::Instance instance,
    bool requireWindowsCompositionInterop = false,
    void* compositionWindow = nullptr,
    uint32_t instanceApiVersion = vk::ApiVersion13);

uint32_t find_queue_family_index(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, vk::QueueFlags queueType);

vk::Device create_logical_device(
    vk::PhysicalDevice physicalDevice,
    vk::SurfaceKHR surface,
    std::deque<std::function<void(vk::Device)>>& deletionQueue,
    bool enableWindowsCompositionInterop = false,
    uint32_t instanceApiVersion = vk::ApiVersion13);

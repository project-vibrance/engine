#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>

// Device selection keeps Vulkan policy away from the app-facing Engine API
bool supports(const vk::PhysicalDevice& device, const char** ppRequestedExtensions, const uint32_t requestedExtensionCount);

bool is_suitable(const vk::PhysicalDevice& device, bool requireWindowsCompositionInterop = false);

// Reports whether the selected Vulkan device can import and synchronize the
// keyed D3D11 textures used by the optional Windows Composition presenter.
// This is deliberately separate from basic renderer suitability: Vulkan
// rendering must not move to a layered translation device merely to obtain
// the presentation bridge.
bool supports_windows_composition_interop(const vk::PhysicalDevice& device);

vk::PhysicalDevice choose_physical_device(
    const vk::Instance instance,
    bool requireWindowsCompositionInterop = false);

uint32_t find_queue_family_index(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, vk::QueueFlags queueType);

vk::Device create_logical_device(
    vk::PhysicalDevice physicalDevice,
    vk::SurfaceKHR surface,
    std::deque<std::function<void(vk::Device)>>& deletionQueue,
    bool enableWindowsCompositionInterop = false);

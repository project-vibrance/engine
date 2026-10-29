#pragma once

#include <algorithm>
#include <cstdint>
#include <vulkan/vulkan_core.h>

namespace vibrance::vulkan
{
inline constexpr std::uint32_t minimum_api_version = VK_API_VERSION_1_1;
inline constexpr std::uint32_t maximum_api_version = VK_API_VERSION_1_3;

constexpr std::uint32_t major_minor_api_version(std::uint32_t version)
{
    return VK_MAKE_API_VERSION(0u, VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version), 0u);
}

constexpr std::uint32_t negotiated_instance_api_version(std::uint32_t loaderVersion)
{
    if (VK_API_VERSION_VARIANT(loaderVersion) != 0u || loaderVersion < minimum_api_version) return 0u;
    return std::min(major_minor_api_version(loaderVersion), maximum_api_version);
}

constexpr std::uint32_t negotiated_device_api_version(
    std::uint32_t instanceVersion, std::uint32_t physicalDeviceVersion)
{
    const auto instance = negotiated_instance_api_version(instanceVersion);
    if (!instance || VK_API_VERSION_VARIANT(physicalDeviceVersion) != 0u ||
        physicalDeviceVersion < minimum_api_version) return 0u;
    return std::min(instance, major_minor_api_version(physicalDeviceVersion));
}

constexpr bool api_at_least_vulkan13(std::uint32_t version)
{
    return VK_API_VERSION_VARIANT(version) == 0u && version >= VK_API_VERSION_1_3;
}
}

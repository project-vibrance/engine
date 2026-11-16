// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "../src/renderer/vulkan/vulkan_api_policy.h"

#include <iostream>

int main()
{
    using namespace vibrance::vulkan;
    bool passed = true;
    const auto check = [&](bool value, const char* message) {
        if (!value) { std::cerr << message << '\n'; passed = false; }
    };
    check(negotiated_instance_api_version(VK_API_VERSION_1_0) == 0u,
        "A Vulkan 1.0 loader must fail before unsupported renderer calls");
    check(negotiated_instance_api_version(VK_MAKE_API_VERSION(0u, 1u, 1u, 4095u)) == 0u,
        "A Vulkan 1.1 loader must not pass the Vulkan 1.2 minimum through its patch number");
    check(negotiated_instance_api_version(VK_MAKE_API_VERSION(0u, 1u, 2u, 126u)) == VK_API_VERSION_1_2,
        "A Vulkan 1.2 loader must retain its supported minor version");
    check(negotiated_instance_api_version(VK_MAKE_API_VERSION(0u, 1u, 3u, 341u)) == VK_API_VERSION_1_3,
        "A Vulkan 1.3 loader must retain the existing modern API contract");
    check(negotiated_instance_api_version(VK_API_VERSION_1_4) == VK_API_VERSION_1_4,
        "A current Vulkan 1.4 loader must expose its supported API contract");
    check(negotiated_instance_api_version(VK_MAKE_API_VERSION(0u, 1u, 5u, 0u)) == VK_API_VERSION_1_4,
        "A future loader must not silently exceed the renderer's tested API contract");
    check(negotiated_device_api_version(VK_API_VERSION_1_4, VK_MAKE_API_VERSION(0u, 1u, 2u, 126u)) == VK_API_VERSION_1_2,
        "A current loader must use the Vulkan 1.2 physical device's supported API version");
    check(negotiated_device_api_version(VK_API_VERSION_1_2, VK_API_VERSION_1_4) == VK_API_VERSION_1_2,
        "Device and allocator API versions must not exceed the instance contract");
    check(negotiated_device_api_version(VK_API_VERSION_1_3, VK_API_VERSION_1_4) == VK_API_VERSION_1_3 &&
        negotiated_device_api_version(VK_API_VERSION_1_4, VK_API_VERSION_1_3) == VK_API_VERSION_1_3,
        "Mixed Vulkan 1.3 and 1.4 instance/device versions must retain the existing modern path");
    check(negotiated_device_api_version(VK_API_VERSION_1_2, VK_API_VERSION_1_2) == VK_API_VERSION_1_2 &&
        !api_at_least_vulkan13(VK_API_VERSION_1_2),
        "Vulkan 1.2 devices must not receive Vulkan 1.3 feature structures");
    check(negotiated_device_api_version(VK_API_VERSION_1_3, VK_API_VERSION_1_3) == VK_API_VERSION_1_3 &&
        api_at_least_vulkan13(VK_API_VERSION_1_3),
        "Modern drivers must retain the Vulkan 1.3 feature and shader path");
    check(negotiated_device_api_version(VK_API_VERSION_1_4, VK_API_VERSION_1_4) == VK_API_VERSION_1_4 &&
        api_at_least_vulkan13(VK_API_VERSION_1_4),
        "Vulkan 1.4 drivers must retain the existing modern features and shader profile");
    check(negotiated_device_api_version(VK_API_VERSION_1_4, VK_API_VERSION_1_1) == 0u &&
        negotiated_device_api_version(VK_API_VERSION_1_4, VK_API_VERSION_1_0) == 0u &&
        negotiated_device_api_version(VK_API_VERSION_1_1, VK_API_VERSION_1_4) == 0u &&
        !api_at_least_vulkan13(VK_API_VERSION_1_1),
        "Loaders and devices below Vulkan 1.2 must fail before renderer creation");
    check(negotiated_instance_api_version(VK_MAKE_API_VERSION(1u, 1u, 3u, 0u)) == 0u &&
        negotiated_device_api_version(VK_API_VERSION_1_3, VK_MAKE_API_VERSION(1u, 1u, 3u, 0u)) == 0u,
        "An unsupported API variant must not be treated as desktop Vulkan");
    return passed ? 0 : 1;
}

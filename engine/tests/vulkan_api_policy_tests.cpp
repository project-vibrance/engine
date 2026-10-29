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
    check(negotiated_instance_api_version(VK_MAKE_API_VERSION(0u, 1u, 1u, 126u)) == VK_API_VERSION_1_1,
        "A Vulkan 1.1 loader must retain its supported minor version");
    check(negotiated_instance_api_version(VK_API_VERSION_1_4) == VK_API_VERSION_1_3,
        "A newer loader must not silently raise the renderer API contract");
    check(negotiated_device_api_version(VK_API_VERSION_1_3, VK_MAKE_API_VERSION(0u, 1u, 1u, 126u)) == VK_API_VERSION_1_1,
        "A current loader with a legacy Intel ICD must use that physical device's API version");
    check(negotiated_device_api_version(VK_API_VERSION_1_1, VK_API_VERSION_1_3) == VK_API_VERSION_1_1,
        "Device and allocator API versions must not exceed the instance contract");
    check(negotiated_device_api_version(VK_API_VERSION_1_3, VK_API_VERSION_1_3) == VK_API_VERSION_1_3 &&
        api_at_least_vulkan13(VK_API_VERSION_1_3),
        "Modern drivers must retain the Vulkan 1.3 feature and shader path");
    check(negotiated_device_api_version(VK_API_VERSION_1_3, VK_API_VERSION_1_0) == 0u &&
        !api_at_least_vulkan13(VK_API_VERSION_1_1),
        "Legacy devices must not receive Vulkan 1.3 feature structures");
    check(negotiated_instance_api_version(VK_MAKE_API_VERSION(1u, 1u, 3u, 0u)) == 0u &&
        negotiated_device_api_version(VK_API_VERSION_1_3, VK_MAKE_API_VERSION(1u, 1u, 3u, 0u)) == 0u,
        "An unsupported API variant must not be treated as desktop Vulkan");
    return passed ? 0 : 1;
}

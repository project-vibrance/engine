#include <vibranceUI/renderer/shader.h>
#include <vibranceUI/renderer/render_types.h>
#include <vibranceUI/core/logger.h>
#include "embedded_shaders.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
int check_embedded_profiles()
{
    constexpr std::array<std::string_view, 13> names {
        "blur_2d.comp", "clear_screen.comp", "composite_2d.comp",
        "hosted_3d_composite.comp", "media_2d.comp", "media_2d_nvidia.comp",
        "model_3d.frag", "model_3d.vert", "rasterise_small.comp", "shadow_2d.comp",
        "shape_2d.comp", "text_msdf.comp", "text_msdf_batch.comp" };
    bool passed = true;
    for (const auto name : names)
    {
        const auto extension = name.rfind('.');
        const std::string compat = std::string(name.substr(0u, extension)) +
            "_vulkan11" + std::string(name.substr(extension));
        for (const auto& [asset, version] : {
            std::pair { std::string(name), 0x00010600u }, std::pair { compat, 0x00010300u } })
        {
            const auto bytes = vibrance_embedded_shader_spirv(asset);
            std::array<std::uint32_t, 5> header {};
            if (bytes.size() < sizeof(header) || bytes.size() % 4u)
            {
                std::cerr << "Missing or truncated embedded shader: " << asset << '\n';
                passed = false;
                continue;
            }
            std::memcpy(header.data(), bytes.data(), sizeof(header));
            if (header[0] != 0x07230203u || header[1] != version)
            {
                std::cerr << "Wrong SPIR-V version for physical-device profile: " << asset << '\n';
                passed = false;
            }
            if (asset == "model_3d_vulkan11.frag")
            {
                std::vector<std::uint32_t> words(bytes.size() / 4u);
                std::memcpy(words.data(), bytes.data(), bytes.size());
                for (std::size_t word = 5u; word < words.size();)
                {
                    const auto count = words[word] >> 16u;
                    if (!count || count > words.size() - word) { passed = false; break; }
                    // OpCapability DemoteToHelperInvocation requires a newer
                    // feature which this profile must not request implicitly.
                    if ((words[word] & 0xffffu) == 17u && count > 1u && words[word + 1u] == 5379u)
                    { std::cerr << "Legacy fragment shader requires demote-to-helper.\n"; passed = false; }
                    word += count;
                }
            }
        }
    }
    const auto modern = embedded_shader_fingerprint(VulkanShaderProfile::eVulkan13);
    const auto legacy = embedded_shader_fingerprint(VulkanShaderProfile::eVulkan11);
    if (!legacy || !modern || legacy == modern || embedded_shader_fingerprint() != modern)
    { std::cerr << "Pipeline-cache identities must separate actual shader profiles.\n"; passed = false; }
    return passed ? 0 : 1;
}

int check_legacy_pipeline_creation()
{
    LoggerOutputOptions logging;
    logging.directory = std::filesystem::current_path() / "vulkan-profile-test-logs";
    Logger::configure_output(logging);
    Logger::fetch_logger()->set_minimum_level(LogLevel::eWarning);
    vk::ApplicationInfo application;
    application.apiVersion = VK_API_VERSION_1_1;
    vk::InstanceCreateInfo instanceInfo;
    instanceInfo.pApplicationInfo = &application;
    const auto instanceResult = vk::createInstance(instanceInfo);
    if (instanceResult.result != vk::Result::eSuccess) return 77;
    const auto instance = instanceResult.value;
    const auto physicals = instance.enumeratePhysicalDevices();
    if (physicals.result != vk::Result::eSuccess || physicals.value.empty())
    { instance.destroy(); return 77; }
    const auto physical = physicals.value.front();
    if (physical.getProperties().apiVersion < VK_API_VERSION_1_1)
    { instance.destroy(); return 77; }
    const auto queues = physical.getQueueFamilyProperties();
    std::uint32_t family = 0u;
    for (; family < queues.size(); ++family)
        if (queues[family].queueFlags & vk::QueueFlagBits::eCompute) break;
    if (family == queues.size()) { instance.destroy(); return 77; }
    const float priority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo;
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1u;
    queueInfo.pQueuePriorities = &priority;
    vk::DeviceCreateInfo createInfo;
    createInfo.queueCreateInfoCount = 1u;
    createInfo.pQueueCreateInfos = &queueInfo;
    const auto deviceResult = physical.createDevice(createInfo);
    if (deviceResult.result != vk::Result::eSuccess)
    { instance.destroy(); return 77; }
    const auto device = deviceResult.value;
    const std::array<vk::DescriptorSetLayoutBinding, 2> frameBindings {
        vk::DescriptorSetLayoutBinding(0u, vk::DescriptorType::eStorageImage, 1u, vk::ShaderStageFlagBits::eCompute),
        vk::DescriptorSetLayoutBinding(1u, vk::DescriptorType::eStorageImage, 1u, vk::ShaderStageFlagBits::eCompute) };
    vk::DescriptorSetLayoutCreateInfo frameInfo;
    frameInfo.bindingCount = static_cast<std::uint32_t>(frameBindings.size());
    frameInfo.pBindings = frameBindings.data();
    const auto frame = device.createDescriptorSetLayout(frameInfo);
    const vk::DescriptorSetLayoutBinding verticesBinding(0u, vk::DescriptorType::eStorageBuffer,
        1u, vk::ShaderStageFlagBits::eCompute);
    vk::DescriptorSetLayoutCreateInfo verticesInfo;
    verticesInfo.bindingCount = 1u;
    verticesInfo.pBindings = &verticesBinding;
    const auto vertices = device.createDescriptorSetLayout(verticesInfo);
    if (frame.result != vk::Result::eSuccess || vertices.result != vk::Result::eSuccess)
    {
        if (frame.value) device.destroyDescriptorSetLayout(frame.value);
        if (vertices.value) device.destroyDescriptorSetLayout(vertices.value);
        device.destroy(); instance.destroy(); return 1;
    }
    std::deque<std::function<void(vk::Device)>> deletion;
    auto builderDevice = device;
    PipelineLayoutBuilder builder(builderDevice);
    builder.add(frame.value);
    builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(glm::uvec4));
    const auto clearLayout = builder.build(deletion);
    builder.add(frame.value);
    builder.add(vertices.value);
    builder.add_push_constants(vk::ShaderStageFlagBits::eCompute, sizeof(RasterPushConstants));
    const auto rasterLayout = builder.build(deletion);
    const auto clear = clearLayout ? make_compute_pipeline(device, "clear_screen", clearLayout,
        deletion, {}, VulkanShaderProfile::eVulkan11) : vk::Pipeline {};
    const auto raster = rasterLayout ? make_compute_pipeline(device, "rasterise_small", rasterLayout,
        deletion, {}, VulkanShaderProfile::eVulkan11) : vk::Pipeline {};
    const bool passed = clear && raster;
    while (!deletion.empty()) { deletion.back()(device); deletion.pop_back(); }
    device.destroyDescriptorSetLayout(vertices.value);
    device.destroyDescriptorSetLayout(frame.value);
    device.destroy(); instance.destroy();
    if (!passed) std::cerr << "Legacy startup compute pipelines could not be created.\n";
    return passed ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    return argc == 2 && std::string_view(argv[1]) == "--pipelines" ?
        check_legacy_pipeline_creation() : check_embedded_profiles();
}

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/renderer/shader.h>
#include <vibranceUI/renderer/render_types.h>
#include <vibranceUI/renderer/viewport_fade.h>
#include <vibranceUI/core/logger.h>
#include "embedded_shaders.h"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

namespace
{
VKAPI_ATTR VkBool32 VKAPI_CALL validation_message(VkDebugUtilsMessageSeverityFlagBitsEXT,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* message, void* user)
{
    static_cast<std::atomic<unsigned>*>(user)->fetch_add(1u, std::memory_order_relaxed);
    std::cerr << message->pMessage << '\n';
    return VK_FALSE;
}

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
            "_vulkan12" + std::string(name.substr(extension));
        for (const auto& [asset, version] : {
            std::pair { std::string(name), 0x00010600u }, std::pair { compat, 0x00010500u } })
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
            if (asset == compat)
            {
                std::vector<std::uint32_t> words(bytes.size() / 4u);
                std::memcpy(words.data(), bytes.data(), bytes.size());
                for (std::size_t word = 5u; word < words.size();)
                {
                    const auto count = words[word] >> 16u;
                    if (!count || count > words.size() - word) { passed = false; break; }
                    if (((words[word] & 0xffffu) == 17u && count > 1u &&
                            words[word + 1u] != 1u && words[word + 1u] != 50u) ||
                        (words[word] & 0xffffu) == 10u)
                    { std::cerr << "Compatibility shader requires an optional capability or extension: " << asset << '\n'; passed = false; }
                    word += count;
                }
            }
        }
    }
    const auto modern = embedded_shader_fingerprint(VulkanShaderProfile::eVulkan13);
    const auto compatibility = embedded_shader_fingerprint(VulkanShaderProfile::eVulkan12);
    if (!compatibility || !modern || compatibility == modern || embedded_shader_fingerprint() != modern)
    { std::cerr << "Pipeline-cache identities must separate actual shader profiles.\n"; passed = false; }
    return passed ? 0 : 1;
}

int check_compatibility_pipeline_creation()
{
    LoggerOutputOptions logging;
    logging.directory = std::filesystem::current_path() / "vulkan-profile-test-logs";
    Logger::configure_output(logging);
    Logger::fetch_logger()->set_minimum_level(LogLevel::eWarning);
    std::atomic<unsigned> validationErrors { 0u };
    const char* validationLayer = "VK_LAYER_KHRONOS_validation";
    const auto layers = vk::enumerateInstanceLayerProperties();
    const auto extensions = vk::enumerateInstanceExtensionProperties();
    const bool validationAvailable = layers.result == vk::Result::eSuccess &&
        extensions.result == vk::Result::eSuccess &&
        std::any_of(layers.value.begin(), layers.value.end(), [&](const auto& layer) {
            return std::strcmp(layer.layerName, validationLayer) == 0;
        }) && std::any_of(extensions.value.begin(), extensions.value.end(), [](const auto& extension) {
            return std::strcmp(extension.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0;
        });
    const char* debugExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
    VkDebugUtilsMessengerCreateInfoEXT debugInfo { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
    debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
        VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    debugInfo.pfnUserCallback = validation_message;
    debugInfo.pUserData = &validationErrors;
    vk::ApplicationInfo application;
    application.apiVersion = VK_API_VERSION_1_2;
    vk::InstanceCreateInfo instanceInfo;
    instanceInfo.pApplicationInfo = &application;
    if (validationAvailable)
    {
        instanceInfo.enabledLayerCount = 1u;
        instanceInfo.ppEnabledLayerNames = &validationLayer;
        instanceInfo.enabledExtensionCount = 1u;
        instanceInfo.ppEnabledExtensionNames = &debugExtension;
        instanceInfo.pNext = &debugInfo;
    }
    const auto instanceResult = vk::createInstance(instanceInfo);
    if (instanceResult.result != vk::Result::eSuccess) return 77;
    const auto instance = instanceResult.value;
    VkDebugUtilsMessengerEXT messenger = VK_NULL_HANDLE;
    const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
        instance.getProcAddr("vkCreateDebugUtilsMessengerEXT"));
    const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
        instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
    if (validationAvailable && (!createMessenger || !destroyMessenger ||
        createMessenger(static_cast<VkInstance>(instance), &debugInfo, nullptr, &messenger) != VK_SUCCESS))
    { instance.destroy(); return 1; }
    const auto destroyInstance = [&] {
        if (messenger) destroyMessenger(static_cast<VkInstance>(instance), messenger, nullptr);
        instance.destroy();
    };
    const auto physicals = instance.enumeratePhysicalDevices();
    if (physicals.result != vk::Result::eSuccess) { destroyInstance(); return 77; }
    vk::PhysicalDevice physical {};
    std::uint32_t family = 0u;
    const auto requiredQueue = vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eGraphics;
    for (const auto candidate : physicals.value)
    {
        if (candidate.getProperties().apiVersion < VK_API_VERSION_1_2) continue;
        const auto queues = candidate.getQueueFamilyProperties();
        for (family = 0u; family < queues.size(); ++family)
            if ((queues[family].queueFlags & requiredQueue) == requiredQueue) break;
        if (family < queues.size()) { physical = candidate; break; }
    }
    if (!physical) { destroyInstance(); return 77; }
    const float priority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo;
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1u;
    queueInfo.pQueuePriorities = &priority;
    vk::DeviceCreateInfo createInfo;
    createInfo.queueCreateInfoCount = 1u;
    createInfo.pQueueCreateInfos = &queueInfo;
    const auto deviceResult = physical.createDevice(createInfo);
    if (deviceResult.result != vk::Result::eSuccess) { destroyInstance(); return 77; }
    const auto device = deviceResult.value;
    std::vector<vk::DescriptorSetLayout> descriptors;
    const auto makeDescriptors = [&](std::initializer_list<vk::DescriptorType> types,
        vk::ShaderStageFlags stages = vk::ShaderStageFlagBits::eCompute) {
        std::vector<vk::DescriptorSetLayoutBinding> bindings;
        for (const auto type : types)
            bindings.emplace_back(static_cast<std::uint32_t>(bindings.size()), type, 1u, stages);
        vk::DescriptorSetLayoutCreateInfo info;
        info.bindingCount = static_cast<std::uint32_t>(bindings.size());
        info.pBindings = bindings.data();
        const auto result = device.createDescriptorSetLayout(info);
        if (result.result != vk::Result::eSuccess) return vk::DescriptorSetLayout {};
        descriptors.push_back(result.value);
        return result.value;
    };
    constexpr auto image = vk::DescriptorType::eStorageImage;
    constexpr auto buffer = vk::DescriptorType::eStorageBuffer;
    constexpr auto sampler = vk::DescriptorType::eCombinedImageSampler;
    const auto frame = makeDescriptors({ image, image });
    const auto vertices = makeDescriptors({ buffer });
    const auto textBatch = makeDescriptors({ buffer });
    const auto post = makeDescriptors({ image, image, image, image, image, image, image, image, image });
    const auto media = makeDescriptors({ sampler });
    const auto textures = makeDescriptors({ sampler, sampler, sampler, sampler, sampler },
        vk::ShaderStageFlagBits::eFragment);
    if (!frame || !vertices || !textBatch || !post || !media || !textures)
    {
        for (const auto layout : descriptors) device.destroyDescriptorSetLayout(layout);
        device.destroy(); destroyInstance(); return 1;
    }
    std::deque<std::function<void(vk::Device)>> deletion;
    auto builderDevice = device;
    PipelineLayoutBuilder builder(builderDevice);
    const auto makeLayout = [&](std::initializer_list<vk::DescriptorSetLayout> sets, std::uint32_t size,
        vk::ShaderStageFlags stages = vk::ShaderStageFlagBits::eCompute) {
        for (const auto set : sets) builder.add(set);
        builder.add_push_constants(stages, size);
        return builder.build(deletion);
    };
    const auto clearLayout = makeLayout({ frame }, sizeof(glm::uvec4));
    const auto rasterLayout = makeLayout({ frame, vertices }, sizeof(RasterPushConstants));
    const auto primitiveLayout = makeLayout({ frame }, sizeof(Renderer2DPushConstants));
    const auto postLayout = makeLayout({ frame, post }, sizeof(Renderer2DPushConstants));
    const auto batchLayout = makeLayout({ frame, post, textBatch }, sizeof(glm::uvec4));
    const auto mediaLayout = makeLayout({ frame, media, post }, sizeof(Renderer2DPushConstants));
    const auto compositeLayout = makeLayout({ frame, post }, sizeof(Renderer2DCompositeConstants));
    const auto hostedLayout = makeLayout({ frame, post }, sizeof(glm::uvec4));
    const auto modelLayout = makeLayout({ textures }, sizeof(Model3DPushConstants),
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment);
    const std::array<std::pair<const char*, vk::PipelineLayout>, 11> compute {
        std::pair { "clear_screen", clearLayout }, { "rasterise_small", rasterLayout },
        { "shape_2d", primitiveLayout }, { "shadow_2d", primitiveLayout },
        { "blur_2d", postLayout }, { "text_msdf", postLayout },
        { "text_msdf_batch", batchLayout }, { "media_2d", mediaLayout },
        { "media_2d_nvidia", mediaLayout }, { "composite_2d", compositeLayout },
        { "hosted_3d_composite", hostedLayout } };
    bool passed = true;
    for (const auto& [name, layout] : compute)
        if (!layout || !make_compute_pipeline(device, name, layout, deletion, {}, VulkanShaderProfile::eVulkan12))
        { std::cerr << "Vulkan 1.2 compute pipeline could not be created: " << name << '\n'; passed = false; }
    std::array<vk::AttachmentDescription, 2> attachments;
    for (auto& attachment : attachments)
    {
        attachment.samples = vk::SampleCountFlagBits::e1;
        attachment.loadOp = vk::AttachmentLoadOp::eDontCare;
        attachment.storeOp = vk::AttachmentStoreOp::eDontCare;
        attachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
        attachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
    }
    attachments[0].format = vk::Format::eR8G8B8A8Unorm;
    attachments[0].finalLayout = vk::ImageLayout::eColorAttachmentOptimal;
    attachments[1].format = vk::Format::eD32Sfloat;
    attachments[1].finalLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
    const vk::AttachmentReference colour(0u, vk::ImageLayout::eColorAttachmentOptimal);
    const vk::AttachmentReference depth(1u, vk::ImageLayout::eDepthStencilAttachmentOptimal);
    vk::SubpassDescription subpass;
    subpass.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
    subpass.colorAttachmentCount = 1u;
    subpass.pColorAttachments = &colour;
    subpass.pDepthStencilAttachment = &depth;
    vk::RenderPassCreateInfo renderPassInfo;
    renderPassInfo.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    renderPassInfo.pAttachments = attachments.data();
    renderPassInfo.subpassCount = 1u;
    renderPassInfo.pSubpasses = &subpass;
    const auto renderPass = device.createRenderPass(renderPassInfo);
    if (renderPass.result != vk::Result::eSuccess || !modelLayout ||
        !make_graphics_pipeline(device, "model_3d", modelLayout, renderPass.value,
            vk::SampleCountFlagBits::e1, deletion, {}, VulkanShaderProfile::eVulkan12))
    { std::cerr << "Vulkan 1.2 model graphics pipeline could not be created.\n"; passed = false; }
    while (!deletion.empty()) { deletion.back()(device); deletion.pop_back(); }
    if (renderPass.value) device.destroyRenderPass(renderPass.value);
    for (const auto layout : descriptors) device.destroyDescriptorSetLayout(layout);
    device.destroy();
    destroyInstance();
    if (validationErrors.load(std::memory_order_relaxed)) passed = false;
    if (passed) std::cout << "Created every Vulkan 1.2 compute variant and model graphics pipeline; validation "
                         << (validationAvailable ? "enabled" : "unavailable") << ".\n";
    return passed ? 0 : 1;
}
}

int main(int argc, char** argv)
{
    return argc == 2 && std::string_view(argv[1]) == "--pipelines" ?
        check_compatibility_pipeline_creation() : check_embedded_profiles();
}

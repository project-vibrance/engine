// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>
#include <cstdint>

class PipelineLayoutBuilder
{
    public:

    PipelineLayoutBuilder(vk::Device& logicalDevice);

    vk::PipelineLayout build(std::deque<std::function<void(vk::Device)>>& deletionQueue);

    void add(vk::DescriptorSetLayout descriptorSetLayout);

    void add_push_constants(vk::ShaderStageFlags stage, uint32_t size, uint32_t offset = 0);

    private:
    vk::Device& logicalDevice;

    std::vector<vk::DescriptorSetLayout> descriptorSetLayouts;
    std::vector<vk::PushConstantRange> pushConstantRanges;

    void reset();
};


enum class VulkanShaderProfile : std::uint8_t
{
    eVulkan12,
    eVulkan13
};

uint64_t embedded_shader_fingerprint();
uint64_t embedded_shader_fingerprint(VulkanShaderProfile profile);

vk::Pipeline make_compute_pipeline(
    vk::Device logicalDevice, const char* name,
    vk::PipelineLayout pipelineLayout,
    std::deque<std::function<void(vk::Device)>>& deletionQueue,
    vk::PipelineCache pipelineCache = {}
);
vk::Pipeline make_compute_pipeline(
    vk::Device logicalDevice, const char* name,
    vk::PipelineLayout pipelineLayout,
    std::deque<std::function<void(vk::Device)>>& deletionQueue,
    vk::PipelineCache pipelineCache, VulkanShaderProfile profile
);


vk::Pipeline make_graphics_pipeline(
    vk::Device logicalDevice,
    const char* name,
    vk::PipelineLayout pipelineLayout,
    vk::RenderPass renderPass,
    vk::SampleCountFlagBits rasterizationSamples,
    std::deque<std::function<void(vk::Device)>>& deletionQueue,
    vk::PipelineCache pipelineCache = {}
);
vk::Pipeline make_graphics_pipeline(
    vk::Device logicalDevice,
    const char* name,
    vk::PipelineLayout pipelineLayout,
    vk::RenderPass renderPass,
    vk::SampleCountFlagBits rasterizationSamples,
    std::deque<std::function<void(vk::Device)>>& deletionQueue,
    vk::PipelineCache pipelineCache, VulkanShaderProfile profile
);

#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>

class PipelineLayoutBuilder
{
    public:
    // Collect descriptor layouts and push constants before creating one pipeline layout
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

// Loads embedded compute shader SPIR-V before pipeline creation
vk::Pipeline make_compute_pipeline(
    vk::Device logicalDevice, const char* name,
    vk::PipelineLayout pipelineLayout,
    std::deque<std::function<void(vk::Device)>>& deletionQueue
);

// Loads embedded graphics shader SPIR-V before pipeline creation
vk::Pipeline make_graphics_pipeline(
    vk::Device logicalDevice,
    const char* name,
    vk::PipelineLayout pipelineLayout,
    vk::RenderPass renderPass,
    vk::SampleCountFlagBits rasterizationSamples,
    std::deque<std::function<void(vk::Device)>>& deletionQueue
);

#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <vector>
#include <deque>
#include <functional>

class DescriptorSetLayoutBuilder
{
    public:
    // Adds descriptor bindings in order, then clears itself after build
    DescriptorSetLayoutBuilder(vk::Device& logicalDevice);

    vk::DescriptorSetLayout build(std::deque<std::function<void(vk::Device)>>& deletionQueue);

    void add_entry(vk::ShaderStageFlags stage, vk::DescriptorType type, uint32_t descriptorCount = 1);

    private:
    vk::Device& logicalDevice;

    std::vector<vk::DescriptorSetLayoutBinding> layoutBindings;

    void reset();
};

// Creates one pool sized for all bindings used by a descriptor set layout
vk::DescriptorPool make_descriptor_pool(
    vk::Device device, uint32_t descriptorSetCount,
    uint32_t bindingCount, vk::DescriptorType* pBindingTypes,
    std::deque<std::function<void(vk::Device)>>& deletionQueue
);

vk::DescriptorSet allocate_descriptor_set(vk::Device device, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout layout);

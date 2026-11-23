// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>
#include <glm/vec4.hpp>
#include "composition_readback.h"
#include <memory>
#include <vector>

struct GLFWwindow;

namespace vibrance::vulkan
{
    class X11WindowShape
    {
    public:
        static bool uses_native_alpha(GLFWwindow* window);
        static std::unique_ptr<X11WindowShape> create(GLFWwindow* window, VmaAllocator allocator);
        ~X11WindowShape();

        void record(vk::CommandBuffer commands, vk::Image image, vk::Extent2D extent,
            glm::uvec4 content);
        void submitted(vk::Fence fence);
        bool apply_completed(vk::Device device);

    private:
        X11WindowShape(GLFWwindow* window, VmaAllocator allocator);
        bool ensure_readback(vk::DeviceSize size);
        void apply();
        void clear_shape();

        GLFWwindow* window;
        VmaAllocator allocator;
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        const std::uint8_t* mapped = nullptr;
        vk::DeviceSize capacity = 0;
        bool disabled = false;
        bool recorded = false;
        vk::Fence pending;
        vk::Extent2D renderExtent;
        CompositionReadbackRegion region;
        glm::ivec4 appliedRect { 0 };
        std::vector<char> appliedBits;
        std::vector<char> bits;
    };
}

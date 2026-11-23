// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "x11_window_shape.h"
#include <vibranceUI/core/logger.h>
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#define GLFW_EXPOSE_NATIVE_X11
#include <GLFW/glfw3native.h>
#include <X11/extensions/shape.h>

namespace vibrance::vulkan
{
    X11WindowShape::X11WindowShape(GLFWwindow* window, VmaAllocator allocator) :
        window(window), allocator(allocator)
    {
        clear_shape();
    }

    bool X11WindowShape::uses_native_alpha(GLFWwindow* window)
    {
#ifdef GLFW_PLATFORM_X11
        if (glfwGetPlatform() != GLFW_PLATFORM_X11)
            return false;
#endif
        return window && glfwGetWindowAttrib(window, GLFW_TRANSPARENT_FRAMEBUFFER);
    }

    std::unique_ptr<X11WindowShape> X11WindowShape::create(GLFWwindow* window, VmaAllocator allocator)
    {
#ifdef GLFW_PLATFORM_X11
        if (glfwGetPlatform() != GLFW_PLATFORM_X11)
            return {};
#endif
        if (!window || glfwGetWindowAttrib(window, GLFW_DECORATED))
            return {};

        auto* display = glfwGetX11Display();
        int eventBase = 0;
        int errorBase = 0;
        if (!display || !XShapeQueryExtension(display, &eventBase, &errorBase))
            return {};
        return std::unique_ptr<X11WindowShape>(new X11WindowShape(window, allocator));
    }

    X11WindowShape::~X11WindowShape()
    {
        if (buffer) vmaDestroyBuffer(allocator, buffer, allocation);
    }

    bool X11WindowShape::ensure_readback(vk::DeviceSize size)
    {
        if (size <= capacity) return true;

        VkBufferCreateInfo bufferInfo {};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        VmaAllocationCreateInfo allocationInfo {};
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;
        VkBuffer newBuffer = VK_NULL_HANDLE;
        VmaAllocation newAllocation = nullptr;
        VmaAllocationInfo allocated {};
        if (vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &newBuffer,
                &newAllocation, &allocated) != VK_SUCCESS)
            return false;

        if (buffer) vmaDestroyBuffer(allocator, buffer, allocation);
        buffer = newBuffer;
        allocation = newAllocation;
        mapped = static_cast<const std::uint8_t*>(allocated.pMappedData);
        capacity = size;
        return true;
    }

    void X11WindowShape::record(vk::CommandBuffer commands, vk::Image image,
        vk::Extent2D extent, glm::uvec4 content)
    {
        if (disabled) return;
        renderExtent = extent;
        region = composition_readback_region(extent.width, extent.height,
            content.x, content.y, content.z, content.w, false);
        recorded = true;
        if (region.width == 0 || region.height == 0) return;

        const auto size = vk::DeviceSize(region.width) * region.height * 4u;
        if (!ensure_readback(size))
        {
            disabled = true;
            recorded = false;
            auto* display = glfwGetX11Display();
            XShapeCombineMask(display, glfwGetX11Window(window),
                ShapeBounding, 0, 0, None, ShapeSet);
            XFlush(display);
            Logger::fetch_logger()->warning("Unable to allocate the X11 window shape readback buffer.");
            return;
        }

        vk::BufferImageCopy copy {};
        copy.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        copy.imageSubresource.layerCount = 1u;
        copy.imageOffset = vk::Offset3D { static_cast<int32_t>(region.x), static_cast<int32_t>(region.y), 0 };
        copy.imageExtent = vk::Extent3D { region.width, region.height, 1u };
        commands.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, buffer, copy);

        vk::BufferMemoryBarrier barrier {};
        barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eHostRead;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.buffer = buffer;
        barrier.size = size;
        commands.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eHost, {}, nullptr, barrier, nullptr);
    }

    void X11WindowShape::submitted(vk::Fence fence)
    {
        if (recorded) pending = fence;
        recorded = false;
    }

    bool X11WindowShape::apply_completed(vk::Device device)
    {
        if (!pending) return true;
        const auto status = device.getFenceStatus(pending);
        if (status == vk::Result::eNotReady) return false;
        pending = nullptr;
        if (status == vk::Result::eSuccess) apply();
        return true;
    }

    void X11WindowShape::clear_shape()
    {
        auto* display = glfwGetX11Display();
        XShapeCombineRectangles(display, glfwGetX11Window(window), ShapeBounding,
            0, 0, nullptr, 0, ShapeSet, Unsorted);
        XFlush(display);
        appliedRect = glm::ivec4(0);
        appliedBits.clear();
    }

    void X11WindowShape::apply()
    {
        if (region.width == 0 || region.height == 0)
        {
            if (!appliedBits.empty()) clear_shape();
            return;
        }

        int width = 0;
        int height = 0;
        glfwGetWindowSize(window, &width, &height);
        if (width <= 0 || height <= 0) return;
        const auto size = vk::DeviceSize(region.width) * region.height * 4u;
        if (vmaInvalidateAllocation(allocator, allocation, 0, size) != VK_SUCCESS) return;

        const auto scaled_edge = [](std::uint32_t edge, int windowSize, std::uint32_t renderSize) {
            return static_cast<int>((2u * std::uint64_t(edge) * windowSize + renderSize - 1u) /
                (2u * std::uint64_t(renderSize)));
        };
        const auto source_pixel = [](int pixel, int windowSize, std::uint32_t renderSize) {
            return (2u * std::uint64_t(pixel) + 1u) * renderSize / (2u * std::uint64_t(windowSize));
        };
        const int left = scaled_edge(region.x, width, renderExtent.width);
        const int top = scaled_edge(region.y, height, renderExtent.height);
        const int right = scaled_edge(region.x + region.width, width, renderExtent.width);
        const int bottom = scaled_edge(region.y + region.height, height, renderExtent.height);
        if (right == left || bottom == top)
        {
            if (!appliedBits.empty()) clear_shape();
            return;
        }

        const glm::ivec4 rect { left, top, right - left, bottom - top };
        const auto stride = (std::size_t(rect.z) + 7u) / 8u;
        bits.assign(stride * rect.w, 0);
        constexpr std::uint8_t minimumVisibleAlpha = 128;
        for (int y = 0; y < rect.w; ++y)
        {
            const auto sourceY = source_pixel(top + y, height, renderExtent.height) - region.y;
            const auto* alpha = mapped + sourceY * region.width * 4u + 3u;
            auto* row = bits.data() + stride * y;
            for (int x = 0; x < rect.z; ++x)
            {
                const auto sourceX = static_cast<std::uint32_t>(width) == renderExtent.width ?
                    std::uint64_t(x) : source_pixel(left + x, width, renderExtent.width) - region.x;
                if (alpha[sourceX * 4u] >= minimumVisibleAlpha)
                    row[x / 8] |= static_cast<char>(1u << (x % 8));
            }
        }
        if (rect == appliedRect && bits == appliedBits) return;

        auto* display = glfwGetX11Display();
        const auto x11Window = glfwGetX11Window(window);
        const auto mask = XCreateBitmapFromData(display, x11Window, bits.data(), rect.z, rect.w);
        if (!mask) return;
        XShapeCombineMask(display, x11Window, ShapeBounding, rect.x, rect.y, mask, ShapeSet);
        XFreePixmap(display, mask);
        XFlush(display);
        appliedRect = rect;
        appliedBits.swap(bits);
    }
}

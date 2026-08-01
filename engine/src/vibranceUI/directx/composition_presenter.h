#pragma once

#include <vibranceUI/graphics/backdrop.h>
#include <glm/glm.hpp>

#if defined(_WIN32) && !defined(VK_USE_PLATFORM_WIN32_KHR)
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <memory>
#include <vector>

class WindowsCompositionPresenter
{
public:
    enum class PresentResult : std::uint8_t
    {
        eFailed,
        ePresented,
        eDeferred
    };

    WindowsCompositionPresenter();
    ~WindowsCompositionPresenter();

    WindowsCompositionPresenter(const WindowsCompositionPresenter&) = delete;
    WindowsCompositionPresenter& operator=(const WindowsCompositionPresenter&) = delete;

    bool initialise(
        void* nativeWindow,
        vk::PhysicalDevice physicalDevice,
        vk::Device logicalDevice,
        std::uint32_t width,
        std::uint32_t height,
        std::uint32_t bufferCount,
        std::uint32_t graphicsQueueFamilyIndex);
    void shutdown(vk::Device logicalDevice);

    bool available() const;
    std::uint32_t buffer_count() const;
    vk::Image image(std::uint32_t index) const;
    bool first_use(std::uint32_t index) const;
    void mark_used(std::uint32_t index);
    bool acquire(std::uint32_t index);
    PresentResult present(
        std::uint32_t index,
        bool synchronize = false,
        glm::uvec4 contentRect = glm::uvec4(0u),
        glm::uvec4 damageRect = glm::uvec4(0u));
    bool set_regions(const std::vector<SystemBackdropRegion>& regions);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

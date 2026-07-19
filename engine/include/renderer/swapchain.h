#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <deque>
#include <functional>
#include <vector>
#include <vibranceUI/renderer/present_mode.h>

struct SurfaceDetails
{
    // Snapshot of surface capabilities used to rebuild the swapchain safely
    bool valid = false;

    vk::SurfaceCapabilitiesKHR capabilities;

    std::vector<vk::SurfaceFormatKHR> formats;

    std::vector<vk::PresentModeKHR> presentModes;
};

class Swapchain
{
    public:
    // Owns swapchain images and rebuilds them when the window or compositor changes
    
    void destroy(vk::Device logicalDevice);
    
    void build(
        vk::Device logicalDevice,
        vk::PhysicalDevice physicalDevice,
        vk::SurfaceKHR surface,
        uint32_t width,
        uint32_t height,
        bool transparent
    );
    
    void rebuild(
        vk::Device logicalDevice,
        vk::PhysicalDevice physicalDevice,
        vk::SurfaceKHR surface,
        uint32_t width,
        uint32_t height,
        bool transparent
    );

    uint32_t imageCount;

    vk::SwapchainKHR chain;
    
    vk::SurfaceFormatKHR format;

    vk::CompositeAlphaFlagBitsKHR compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;

    RendererPresentMode presentModePreference = RendererPresentMode::eAuto;

    RendererPresentMode activePresentMode = RendererPresentMode::eAuto;

    std::vector<RendererPresentMode> supportedPresentModes;

    vk::Extent2D extent;

    std::vector<vk::Image> images;

    std::vector<vk::ImageView> imageViews;

    bool outdated = false;

    private:
    std::deque<std::function<void(vk::Device)>> deletionQueue;

    SurfaceDetails query_surface_support(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface);

    vk::Extent2D choose_extent(uint32_t width, uint32_t height, vk::SurfaceCapabilitiesKHR capabilities);

    vk::PresentModeKHR choose_present_mode(const std::vector<vk::PresentModeKHR>& presentModes) const;

    vk::SurfaceFormatKHR choose_surface_format(std::vector<vk::SurfaceFormatKHR> formats);
};

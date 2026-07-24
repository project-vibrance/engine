#include <vibranceUI/renderer/swapchain.h>
#include <vibranceUI/renderer/image.h>
#include <vibranceUI/core/logger.h>
#include <algorithm>
#include <string>

namespace
{
    RendererPresentMode renderer_present_mode_from_vk(vk::PresentModeKHR mode)
    {
        switch (mode)
        {
        case vk::PresentModeKHR::eImmediate:
            return RendererPresentMode::eImmediate;
        case vk::PresentModeKHR::eMailbox:
            return RendererPresentMode::eMailbox;
        case vk::PresentModeKHR::eFifo:
            return RendererPresentMode::eFifo;
        case vk::PresentModeKHR::eFifoRelaxed:
            return RendererPresentMode::eFifoRelaxed;
        default:
            return RendererPresentMode::eAuto;
        }
    }

    vk::PresentModeKHR vk_present_mode_from_renderer(RendererPresentMode mode)
    {
        switch (mode)
        {
        case RendererPresentMode::eImmediate:
            return vk::PresentModeKHR::eImmediate;
        case RendererPresentMode::eMailbox:
            return vk::PresentModeKHR::eMailbox;
        case RendererPresentMode::eFifo:
            return vk::PresentModeKHR::eFifo;
        case RendererPresentMode::eFifoRelaxed:
            return vk::PresentModeKHR::eFifoRelaxed;
        case RendererPresentMode::eAuto:
        default:
            return vk::PresentModeKHR::eFifo;
        }
    }

    bool contains_present_mode(const std::vector<vk::PresentModeKHR>& modes, vk::PresentModeKHR mode)
    {
        return std::find(modes.begin(), modes.end(), mode) != modes.end();
    }

    const char* composite_alpha_name(vk::CompositeAlphaFlagBitsKHR alpha)
    {
        switch (alpha)
        {
        case vk::CompositeAlphaFlagBitsKHR::eOpaque:
            return "opaque";
        case vk::CompositeAlphaFlagBitsKHR::ePreMultiplied:
            return "pre-multiplied";
        case vk::CompositeAlphaFlagBitsKHR::ePostMultiplied:
            return "post-multiplied";
        case vk::CompositeAlphaFlagBitsKHR::eInherit:
            return "inherit";
        default:
            return "unknown";
        }
    }

    std::string composite_alpha_flags_name(vk::CompositeAlphaFlagsKHR alpha)
    {
        std::string result;
        auto append = [&](vk::CompositeAlphaFlagBitsKHR bit) {
            if (!(alpha & bit))
            {
                return;
            }

            if (!result.empty())
            {
                result += ", ";
            }
            result += composite_alpha_name(bit);
        };

        append(vk::CompositeAlphaFlagBitsKHR::eOpaque);
        append(vk::CompositeAlphaFlagBitsKHR::ePreMultiplied);
        append(vk::CompositeAlphaFlagBitsKHR::ePostMultiplied);
        append(vk::CompositeAlphaFlagBitsKHR::eInherit);
        return result.empty() ? "none" : result;
    }

    vk::CompositeAlphaFlagBitsKHR choose_composite_alpha(
        vk::CompositeAlphaFlagsKHR supportedAlpha,
        bool transparent)
    {
        // Transparent windows need compositor-friendly alpha when the surface supports it
        if (transparent)
        {
            if (supportedAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
            {
                return vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
            }

            if (supportedAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
            {
                return vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
            }

            if (supportedAlpha & vk::CompositeAlphaFlagBitsKHR::eInherit)
            {
                return vk::CompositeAlphaFlagBitsKHR::eInherit;
            }
        }

        if (supportedAlpha & vk::CompositeAlphaFlagBitsKHR::eOpaque)
        {
            return vk::CompositeAlphaFlagBitsKHR::eOpaque;
        }

        if (supportedAlpha & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied)
        {
            return vk::CompositeAlphaFlagBitsKHR::ePreMultiplied;
        }

        if (supportedAlpha & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied)
        {
            return vk::CompositeAlphaFlagBitsKHR::ePostMultiplied;
        }

        return vk::CompositeAlphaFlagBitsKHR::eInherit;
    }
}

void Swapchain::rebuild(
    vk::Device logicalDevice,
    vk::PhysicalDevice physicalDevice,
    vk::SurfaceKHR surface,
    uint32_t width,
    uint32_t height,
    bool transparent
) {
    // Rebuild owns the old swapchain teardown so resize paths stay simple
    Logger* logger = Logger::fetch_logger();
    logger->vulkan("Rebuilding swapchain.");

    if (logicalDevice.waitIdle() != vk::Result::eSuccess)
    {
        logger->vulkan("Failed to wait for device idle.");
    }

    destroy(logicalDevice);

    build(logicalDevice, physicalDevice, surface, width, height, transparent);
}

void Swapchain::destroy(vk::Device logicalDevice)
{
    while (deletionQueue.size() > 0) 
    {
		deletionQueue.back()(logicalDevice);
		deletionQueue.pop_back();
	}

    images.clear();
    imageViews.clear();
}

void Swapchain::build(
    vk::Device logicalDevice,
    vk::PhysicalDevice physicalDevice,
    vk::SurfaceKHR surface,
    uint32_t width,
    uint32_t height,
    bool transparent
) {
    Logger* logger = Logger::fetch_logger();

    // Query fresh surface data because window size and compositor support can change
    SurfaceDetails support = query_surface_support(physicalDevice, surface);
    if (!support.valid || support.formats.empty() || support.presentModes.empty())
    {
        chain = nullptr;
        images.clear();
        imageViews.clear();
        outdated = true;
        logger->vulkan("Surface support is unavailable; swapchain build skipped.");
        return;
    }

    format = choose_surface_format(support.formats);

    extent = choose_extent(width, height, support.capabilities);

    imageCount = support.capabilities.minImageCount + 1;
    if (support.capabilities.maxImageCount > 0)
    {
        imageCount = std::min(support.capabilities.maxImageCount, imageCount);
    }

    vk::SwapchainCreateInfoKHR createInfo = 
    vk::SwapchainCreateInfoKHR(vk::SwapchainCreateFlagsKHR(), 
        surface, imageCount, format.format, format.colorSpace,
        extent, 1, vk::ImageUsageFlagBits::eColorAttachment | vk::ImageUsageFlagBits::eTransferDst
    );

    createInfo.preTransform = support.capabilities.currentTransform;
    createInfo.compositeAlpha = choose_composite_alpha(
        support.capabilities.supportedCompositeAlpha,
        transparent
    );
    compositeAlpha = createInfo.compositeAlpha;
    supportedPresentModes.clear();
    supportedPresentModes.reserve(support.presentModes.size());
    for (vk::PresentModeKHR mode : support.presentModes)
    {
        supportedPresentModes.push_back(renderer_present_mode_from_vk(mode));
    }

    vk::PresentModeKHR presentMode = choose_present_mode(support.presentModes);
    activePresentMode = renderer_present_mode_from_vk(presentMode);
    createInfo.presentMode = presentMode;
    createInfo.clipped = VK_TRUE;
    logger->vulkan(
        std::string("Swapchain supported composite alpha: ") +
        composite_alpha_flags_name(support.capabilities.supportedCompositeAlpha) +
        "."
    );
    logger->vulkan(
        std::string("Swapchain composite alpha: ") +
        composite_alpha_name(createInfo.compositeAlpha) +
        (transparent ? " (transparent requested)." : " (opaque requested).")
    );
    logger->vulkan("Swapchain present mode: " + vk::to_string(presentMode) + ".");

    createInfo.oldSwapchain = vk::SwapchainKHR(nullptr);

    VkSwapchainKHR rawSwapchain = VK_NULL_HANDLE;
    const VkSwapchainCreateInfoKHR rawCreateInfo = createInfo;
    const VkResult createResult = vkCreateSwapchainKHR(logicalDevice, &rawCreateInfo, nullptr, &rawSwapchain);
    if (createResult == VK_SUCCESS) 
    {
        chain = rawSwapchain;

        deletionQueue.push_back([this, logger](vk::Device device){
            logger->vulkan("Destroyed swapchain.");
            device.destroySwapchainKHR(chain);
        });
    }
    else 
    {
        chain = nullptr;
        images.clear();
        imageViews.clear();
        outdated = true;
        logger->vulkan("Failed to create swapchain. VkResult: " + std::to_string(static_cast<int>(createResult)));
        return;
    }

    uint32_t swapchainImageCount = 0;
    VkResult imageResult = vkGetSwapchainImagesKHR(logicalDevice, chain, &swapchainImageCount, nullptr);
    if (imageResult != VK_SUCCESS || swapchainImageCount == 0)
    {
        images.clear();
        imageViews.clear();
        outdated = true;
        logger->vulkan("Failed to get swapchain image count. VkResult: " + std::to_string(static_cast<int>(imageResult)));
        return;
    }

    std::vector<VkImage> rawImages(swapchainImageCount);
    imageResult = vkGetSwapchainImagesKHR(logicalDevice, chain, &swapchainImageCount, rawImages.data());
    if (imageResult != VK_SUCCESS)
    {
        images.clear();
        imageViews.clear();
        outdated = true;
        logger->vulkan("Failed to get swapchain images. VkResult: " + std::to_string(static_cast<int>(imageResult)));
        return;
    }

    images.clear();
    images.reserve(rawImages.size());
    for (VkImage rawImage : rawImages)
    {
        images.push_back(rawImage);
    }
    
    for (uint32_t i = 0; i < images.size(); ++i) 
    {
        vk::ImageView imageView = create_image_view(logicalDevice, images[i], format.format);
        imageViews.push_back(imageView);
        VkImageView imageViewHandle = imageView;
        deletionQueue.push_back([imageViewHandle](vk::Device device) {
            vkDestroyImageView(device, imageViewHandle, nullptr);
        });
    }

    outdated = false;
}

SurfaceDetails Swapchain::query_surface_support(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface)
{
    Logger* logger = Logger::fetch_logger();

    SurfaceDetails support;
    VkSurfaceCapabilitiesKHR rawCapabilities = {};
    VkResult result = vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &rawCapabilities);
    if (result != VK_SUCCESS)
    {
        logger->vulkan("Failed to query surface capabilities. VkResult: " + std::to_string(static_cast<int>(result)));
        return support;
    }
    support.capabilities = rawCapabilities;
	logger->log(support.capabilities);
	
    uint32_t formatCount = 0;
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    if (result != VK_SUCCESS || formatCount == 0)
    {
        logger->vulkan("Failed to query surface formats. VkResult: " + std::to_string(static_cast<int>(result)));
        return support;
    }

    std::vector<VkSurfaceFormatKHR> rawFormats(formatCount);
    result = vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, rawFormats.data());
    if (result != VK_SUCCESS)
    {
        logger->vulkan("Failed to read surface formats. VkResult: " + std::to_string(static_cast<int>(result)));
        return support;
    }
    support.formats.clear();
    support.formats.reserve(rawFormats.size());
    for (const VkSurfaceFormatKHR& rawFormat : rawFormats)
    {
        support.formats.push_back(rawFormat);
    }
    logger->log(support.formats);

    uint32_t presentModeCount = 0;
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, nullptr);
    if (result != VK_SUCCESS || presentModeCount == 0)
    {
        logger->vulkan("Failed to query present modes. VkResult: " + std::to_string(static_cast<int>(result)));
        return support;
    }

    std::vector<VkPresentModeKHR> rawPresentModes(presentModeCount);
    result = vkGetPhysicalDeviceSurfacePresentModesKHR(physicalDevice, surface, &presentModeCount, rawPresentModes.data());
    if (result != VK_SUCCESS)
    {
        logger->vulkan("Failed to read present modes. VkResult: " + std::to_string(static_cast<int>(result)));
        return support;
    }
    support.presentModes.clear();
    support.presentModes.reserve(rawPresentModes.size());
    for (VkPresentModeKHR rawPresentMode : rawPresentModes)
    {
        support.presentModes.push_back(static_cast<vk::PresentModeKHR>(rawPresentMode));
    }
    logger->vulkan("Supported Present Modes:");
    logger->log(support.presentModes);

    support.valid = true;
	return support;
}

vk::Extent2D Swapchain::choose_extent(uint32_t width, uint32_t height, vk::SurfaceCapabilitiesKHR capabilities)
{
    if (capabilities.currentExtent.width != UINT32_MAX) 
    {
        return capabilities.currentExtent;
    }
    else 
    {
        vk::Extent2D extent = { width, height };

        extent.width = std::min(
            capabilities.maxImageExtent.width, 
            std::max(capabilities.minImageExtent.width, extent.width)
        );

        extent.height = std::min(
            capabilities.maxImageExtent.height,
            std::max(capabilities.minImageExtent.height, extent.height)
        );

        return extent;
    }
}

vk::PresentModeKHR Swapchain::choose_present_mode(const std::vector<vk::PresentModeKHR>& presentModes) const
{
    if (presentModePreference != RendererPresentMode::eAuto)
    {
        const vk::PresentModeKHR requested = vk_present_mode_from_renderer(presentModePreference);
        if (contains_present_mode(presentModes, requested))
        {
            return requested;
        }
    }

    for (vk::PresentModeKHR presentMode : presentModes)
    {
        if (presentMode == vk::PresentModeKHR::eMailbox) return presentMode;
    }

    return vk::PresentModeKHR::eFifo;
}

vk::SurfaceFormatKHR Swapchain::choose_surface_format(std::vector<vk::SurfaceFormatKHR> formats)
{
    for (vk::SurfaceFormatKHR format : formats)
    {
        if (format.format == vk::Format::eB8G8R8A8Unorm && format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) return format;
    }

    return formats[0];
}

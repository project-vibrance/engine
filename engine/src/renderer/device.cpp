#include <vibranceUI/renderer/device.h>
#include <vibranceUI/core/logger.h>

bool supports(const vk::PhysicalDevice& device, const char** ppRequestedExtensions, const uint32_t requestedExtensionCount)
{
    // Device extensions are checked explicitly so failures are logged before selection
    Logger* logger = Logger::fetch_logger();
    logger->vulkan("Physical device requires the following extensions:");
    logger->print_list(ppRequestedExtensions, requestedExtensionCount);

    std::vector<vk::ExtensionProperties> extensions = device.enumerateDeviceExtensionProperties().value;
    logger->vulkan("Physical device supports the extensions:");
    logger->print_extensions(extensions);

    std::vector<const char*> unsupportedExtensions;

    for (uint32_t i = 0; i < requestedExtensionCount; i++)
    {
        bool supported = false;

        for (vk::ExtensionProperties& extension : extensions)
        {
            std::string name = extension.extensionName;

            if (!name.compare(ppRequestedExtensions[i]))
            {
                supported = true;
                break;
            }
        }

        if (!supported) unsupportedExtensions.push_back(ppRequestedExtensions[i]);
    }

    if (!unsupportedExtensions.empty())
    {
        logger->vulkan(LogLevel::eError, "Physical device does NOT support the following requested extensions:");
        logger->print_list(unsupportedExtensions.data(), static_cast<uint32_t>(unsupportedExtensions.size()));
        return false;
    }

    return true;
}

bool is_suitable(const vk::PhysicalDevice& device)
{
    Logger* logger = Logger::fetch_logger();
    logger->vulkan("Checking if device is suitable...");

    uint32_t requestedExtensionCount = 1;
    const char* ppRequestedExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
	if (supports(device, ppRequestedExtensions, requestedExtensionCount))
    {
        logger->vulkan("Physical device supports the requested extensions.");
	}
	else 
    {
        logger->vulkan(LogLevel::eError, "Physical device does NOT support the requested extensions.");
		return false;
	}

    return true;
}

vk::PhysicalDevice choose_physical_device(const vk::Instance instance)
{
    // Prefer a discrete GPU, while falling back to the first suitable integrated device
    Logger* logger = Logger::fetch_logger();
    logger->vulkan("Choosing a physical device...");

    std::vector<vk::PhysicalDevice> availableDevices = instance.enumeratePhysicalDevices().value;

    vk::PhysicalDevice bestDevice = nullptr;
	bool foundDiscrete = false;

    for (vk::PhysicalDevice device : availableDevices)
    {
        logger->log(device);
        
        vk::PhysicalDeviceProperties properties = device.getProperties();

		if (is_suitable(device)) 
        {
			bool discrete = properties.deviceType == vk::PhysicalDeviceType::eDiscreteGpu;
			if (!bestDevice || (discrete && !foundDiscrete))
            {
				bestDevice = device;
				foundDiscrete = discrete;
			}
		}
    }

    return bestDevice;
}

uint32_t find_queue_family_index(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, vk::QueueFlags queueType)
{
    // The selected queue family must support graphics and presentation when a surface exists
    Logger* logger = Logger::fetch_logger();

    std::vector<vk::QueueFamilyProperties> queueFamilies = physicalDevice.getQueueFamilyProperties();
    logger->log(queueFamilies);

    for (uint32_t i = 0; i < queueFamilies.size(); i++)
    {
        vk::QueueFamilyProperties queueFamily = queueFamilies[i];

        bool canPresent = true;
        if (surface)
        {
            auto support = physicalDevice.getSurfaceSupportKHR(i, surface);
            if (support.result == vk::Result::eSuccess)
            {
                canPresent = support.value;
            }
            else
            {
                canPresent = false;
            }
        }
        else
        {
            canPresent = true;
        }

        bool supported = false;
        if (queueFamily.queueFlags & queueType) supported = true;

        if (supported && canPresent) return i;
    }

    return UINT32_MAX;
}

vk::Device create_logical_device(vk::PhysicalDevice physicalDevice, vk::SurfaceKHR surface, std::deque<std::function<void(vk::Device)>>& deletionQueue)
{
    Logger* logger = Logger::fetch_logger();

    uint32_t graphicsIndex = find_queue_family_index(physicalDevice, surface, vk::QueueFlagBits::eGraphics);
    if (graphicsIndex == UINT32_MAX)
    {
        logger->vulkan(LogLevel::eError, "Failed to find a suitable graphics queue family.");
        return vk::Device();
    }
    float queuePriority = 1.0f;

    vk::DeviceQueueCreateInfo queueInfo = vk::DeviceQueueCreateInfo(
        vk::DeviceQueueCreateFlags(), graphicsIndex, 1, &queuePriority
    );

    vk::PhysicalDeviceFeatures deviceFeatures = vk::PhysicalDeviceFeatures();

    std::vector<const char*> enabledLayers;
    if (logger->is_vulkan_validation_enabled())
    {
        enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
    }

    std::vector<const char*> enabledExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    std::vector<vk::ExtensionProperties> availableExtensions = physicalDevice.enumerateDeviceExtensionProperties().value;
    for (const vk::ExtensionProperties& extension : availableExtensions)
    {
        std::string name = extension.extensionName;
        if (name == "VK_KHR_portability_subset")
        {
            enabledExtensions.push_back("VK_KHR_portability_subset");
            break;
        }
    }

    logger->vulkan("Physical device enables the following extensions:");
    logger->print_list(enabledExtensions.data(), static_cast<uint32_t>(enabledExtensions.size()));

    vk::DeviceCreateInfo deviceInfo = vk::DeviceCreateInfo(
        vk::DeviceCreateFlags(),
        1, &queueInfo,
        static_cast<uint32_t>(enabledLayers.size()), enabledLayers.data(),
        static_cast<uint32_t>(enabledExtensions.size()), enabledExtensions.data(),
        &deviceFeatures
    );

    vk::ResultValueType<vk::Device>::type logicalDevice = physicalDevice.createDevice(deviceInfo);

    vk::Device device = nullptr;

    if (logicalDevice.result == vk::Result::eSuccess)
    {
        logger->vulkan("GPU abstracted successfully.");

        deletionQueue.push_back([logger](vk::Device device) {
            device.destroy();
            logger->vulkan("Destroyed logical device.");
        });

        device = logicalDevice.value;
    }
    else
    {
        logger->vulkan(LogLevel::eError, "Unable to proceed with device creation.");
    }

	return device;
}

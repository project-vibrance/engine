#include <vibranceUI/renderer/instance.h>
#include <vibranceUI/core/logger.h>
#include <sstream>
#include <cstdint>
#include <cstring>

bool supported_by_instance(const char* const* extensionNames, uint32_t extensionCount, const char* const* layerNames, uint32_t layerCount)
{
    // Validate requested instance extensions and layers before creating Vulkan objects
    Logger* logger = Logger::fetch_logger();
    std::stringstream lineBuilder;

    std::vector<vk::ExtensionProperties> supportedExtensions = vk::enumerateInstanceExtensionProperties().value;

    logger->vulkan("Instance supports the following extensions:");
    logger->print_extensions(supportedExtensions);

    bool found;

    for (uint32_t i = 0; i < extensionCount; i++)
    {
        const char* extension = extensionNames[i];
        found = false;

        for (vk::ExtensionProperties supportedExtension : supportedExtensions)
        {
            if (strcmp(extension, supportedExtension.extensionName) == 0)
            {
                found = true;
                lineBuilder << "Extension \"" << extension << "\" is supported by the system.";
                
                logger->vulkan(lineBuilder.str());
                lineBuilder.str("");

                break;
            }
        }

        if (!found)
        {
            lineBuilder << "Extension \"" << extension << "\" is NOT supported by the system.";

            logger->vulkan(LogLevel::eError, lineBuilder.str());
            lineBuilder.str("");

            return false;
        }
    }

    std::vector<vk::LayerProperties> supportedLayers = vk::enumerateInstanceLayerProperties().value;

    logger->vulkan("Instance supports the following layers:");
    logger->print_layers(supportedLayers);

    for (uint32_t i = 0; i < layerCount; i++)
    {
        const char* layer = layerNames[i];
        found = false;

        for (vk::LayerProperties supportedLayer : supportedLayers)
        {
            if (strcmp(layer, supportedLayer.layerName) == 0)
            {
                found = true;
                lineBuilder << "Layer \"" << layer << "\" is supported by the system.";
                
                logger->vulkan(lineBuilder.str());
                lineBuilder.str("");

                break;
            }
        }

        if (!found)
        {
            lineBuilder << "Layer \"" << layer << "\" is NOT supported by the system.";

            logger->vulkan(LogLevel::eError, lineBuilder.str());
            lineBuilder.str("");

            return false;
        }
    }
    
    return true;
}

vk::Instance make_instance(
    const char* applicationName,
    uint32_t requestedExtensionCount,
    const char* const* requestedExtensions,
    std::deque<std::function<void(vk::Instance)>>& deletionQueue
)
{
    Logger* logger = Logger::fetch_logger();
    
    logger->vulkan("Creating an instance...");

    uint32_t version = vk::enumerateInstanceVersion().value;

    logger->report_version_number(version);

    version &= ~(0xFFFU);

    vk::ApplicationInfo appInfo = vk::ApplicationInfo(applicationName, version, "vibranceUI", version, version);

    std::vector<const char*> enabledExtensions;
    enabledExtensions.reserve(requestedExtensionCount + 2);
    for (uint32_t i = 0; i < requestedExtensionCount; ++i)
    {
        enabledExtensions.push_back(requestedExtensions[i]);
    }

    std::vector<vk::ExtensionProperties> supportedExtensions = vk::enumerateInstanceExtensionProperties().value;
    auto supportsExtension = [&](const char* extensionName) {
        for (const vk::ExtensionProperties& extension : supportedExtensions)
        {
            if (strcmp(extensionName, extension.extensionName) == 0)
            {
                return true;
            }
        }
        return false;
    };
    auto isEnabledExtension = [&](const char* extensionName) {
        for (const char* enabledExtension : enabledExtensions)
        {
            if (strcmp(extensionName, enabledExtension) == 0)
            {
                return true;
            }
        }
        return false;
    };

#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    bool enablePortabilityEnumeration = false;
    // macOS portability devices require this extension to be visible to Vulkan
    if (supportsExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
    {
        if (!isEnabledExtension(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME))
        {
            enabledExtensions.push_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);
        }
        enablePortabilityEnumeration = true;
    }
#endif

    if (logger->is_vulkan_validation_enabled()) enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);

    logger->vulkan("Application requests the following Vulkan instance extensions:");
    logger->print_list(enabledExtensions.data(), static_cast<uint32_t>(enabledExtensions.size()));

    // Validation layers are only requested when logger settings allow Vulkan validation
    std::vector<const char*> enabledLayers;
    if (logger->is_vulkan_validation_enabled()) enabledLayers.push_back("VK_LAYER_KHRONOS_validation");
    
    logger->vulkan("Application requests the following Vulkan layers:");
    logger->print_list(enabledLayers.data(), static_cast<uint32_t>(enabledLayers.size()));

    if (!supported_by_instance(
        enabledExtensions.data(), static_cast<uint32_t>(enabledExtensions.size()),
        enabledLayers.data(), static_cast<uint32_t>(enabledLayers.size())
    )) return nullptr;

    vk::InstanceCreateFlags createFlags;
#ifdef VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME
    if (enablePortabilityEnumeration)
    {
        createFlags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }
#endif

    vk::InstanceCreateInfo createInfo = vk::InstanceCreateInfo(
        createFlags, &appInfo,
        static_cast<uint32_t>(enabledLayers.size()), enabledLayers.data(),
        static_cast<uint32_t>(enabledExtensions.size()), enabledExtensions.data()
    );

    vk::ResultValue<vk::Instance> instanceAttempt = vk::createInstance(createInfo);
    if (instanceAttempt.result != vk::Result::eSuccess)
    {
        logger->vulkan(LogLevel::eError, "Unable to create instance.");
        return nullptr;
    }

    vk::Instance instance = instanceAttempt.value;

    deletionQueue.push_back([logger](vk::Instance instance){
        instance.destroy();
        logger->vulkan("Destroyed instance successfully.");
    });

    return instance;
}

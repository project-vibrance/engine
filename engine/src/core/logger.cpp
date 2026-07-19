#include <iostream>
#include <chrono>
#include <format>
#include <filesystem>
#include <cstdlib>
#include <utility>
#include <vulkan/vulkan.hpp>
#include <vibranceUI/core/logger.h>

Logger* Logger::logger;

#ifndef VIBRANCE_ENABLE_VULKAN_VALIDATION
#if defined(DEBUG)
#define VIBRANCE_ENABLE_VULKAN_VALIDATION 1
#else
#define VIBRANCE_ENABLE_VULKAN_VALIDATION 0
#endif
#endif

#ifndef VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING
#if defined(DEBUG)
#define VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING 1
#else
#define VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING 0
#endif
#endif

namespace 
{
    // Normal logs stay enabled in release, while Vulkan chatter is compile-time gated
    constexpr const char* level_name(LogLevel level)
    {
        switch (level)
        {
            case LogLevel::eTrace: return "TRACE";
            case LogLevel::eDebug: return "DEBUG";
            case LogLevel::eInfo: return "INFO";
            case LogLevel::eWarning: return "WARN";
            case LogLevel::eError: return "ERROR";
            case LogLevel::eFatal: return "FATAL";
            default: return "INFO";
        }
    }

    struct DualStream 
	{
        template<typename T>
        DualStream& operator<<(const T& val)
		{
            // Mirror messages to the console and the per-run file when available
            std::cout << val;
            Logger* l = Logger::fetch_logger();
            if (l && l->is_file_open()) {
                l->get_file_stream() << val;
            }
            return *this;
        }

        DualStream& operator<<(std::ostream& (*manip)(std::ostream&))
		{
            manip(std::cout);
            Logger* l = Logger::fetch_logger();
            if (l && l->is_file_open()) {
                manip(l->get_file_stream());
            }
            return *this;
        }
    };

    DualStream logOut;

    LoggerOutputOptions& pending_logger_output_options()
    {
        static LoggerOutputOptions options = {};
        return options;
    }

    std::filesystem::path default_log_directory(const LoggerOutputOptions& options)
    {
        if (!options.directory.empty())
        {
            return options.directory;
        }

#ifdef _WIN32
        const char* userProfile = std::getenv("USERPROFILE");
        if (userProfile)
        {
            return std::filesystem::path(userProfile) / "Documents" / options.directoryName;
        }
#else
        const char* home = std::getenv("HOME");
        if (home)
        {
            return std::filesystem::path(home) / "Documents" / options.directoryName;
        }
#endif
        return std::filesystem::current_path() / options.directoryName;
    }
}

Logger::Logger() 
{
    enabled = true;
    vulkanRendererLoggingEnabled = VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING != 0;
    vulkanValidationEnabled = VIBRANCE_ENABLE_VULKAN_VALIDATION != 0;
    minLevel = LogLevel::eTrace;
    startTime = std::chrono::steady_clock::now();
    outputOptions = pending_logger_output_options();
    reopen_file();
}

void Logger::reopen_file()
{
    if (logFile.is_open())
    {
        logFile.close();
    }

    const auto now = std::chrono::system_clock::now();
    const auto nowSeconds = std::chrono::time_point_cast<std::chrono::seconds>(now);
    const std::string prefix = outputOptions.filePrefix.empty() ? "vibrance_log" : outputOptions.filePrefix;
    const std::string fileName = std::format("{}_{:%Y-%m-%d_%H-%M-%S}.txt", prefix, nowSeconds);
    const std::filesystem::path docsPath = default_log_directory(outputOptions);

    try 
	{
        std::filesystem::create_directories(docsPath);
        logPath = docsPath / fileName;
        logFile.open(logPath, std::ios::out | std::ios::trunc);
    } 
	catch (const std::exception& e) 
	{
        std::cerr << "\e[0;31m[Logger Error] Failed to create log directory/file: " << e.what() << "\033[0m" << std::endl;
    }
}

Logger::~Logger() 
{
    if (logFile.is_open()) logFile.close();
}

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
) {
    // Map Vulkan validation severity onto the engine log levels
    Logger* logger = Logger::fetch_logger();
    if (!logger || !logger->is_vulkan_validation_enabled()) return vk::False;

    LogLevel level = LogLevel::eDebug;
    if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) 
	{
        level = LogLevel::eError;
    } 
	
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) 
	{
        level = LogLevel::eWarning;
    }
	
	else if (messageSeverity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_INFO_BIT_EXT) 
	{
        level = LogLevel::eInfo;
    } 

    logger->vulkan(level, std::string("Validation: ") + (pCallbackData && pCallbackData->pMessage ? pCallbackData->pMessage : "Unknown validation message."));

    return vk::False;
}

void Logger::set_mode(bool mode) { enabled = mode; }

bool Logger::is_enabled() { return enabled; }

void Logger::set_minimum_level(LogLevel level) { minLevel = level; }

LogLevel Logger::minimum_level() const { return minLevel; }

void Logger::set_vulkan_renderer_logging_mode(bool mode)
{
    vulkanRendererLoggingEnabled = (VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING != 0) && mode;
}

bool Logger::is_vulkan_renderer_logging_enabled() const
{
    return enabled && vulkanRendererLoggingEnabled && (VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING != 0);
}

void Logger::set_vulkan_validation_mode(bool mode)
{
    vulkanValidationEnabled = (VIBRANCE_ENABLE_VULKAN_VALIDATION != 0) && mode;
}

bool Logger::is_vulkan_validation_enabled() const
{
    return enabled && vulkanValidationEnabled && (VIBRANCE_ENABLE_VULKAN_VALIDATION != 0);
}

Logger* Logger::fetch_logger()
{
    if (!logger) logger = new Logger();

    return logger;
}

void Logger::configure_output(LoggerOutputOptions options)
{
    pending_logger_output_options() = std::move(options);
    if (!logger)
    {
        return;
    }

    logger->outputOptions = pending_logger_output_options();
    logger->reopen_file();
}

void Logger::log(LogLevel level, std::string message)
{
    if (!enabled || static_cast<uint8_t>(level) < static_cast<uint8_t>(minLevel)) return;

    // Prefix messages with elapsed process time for comparing multi-window events
    auto now = std::chrono::steady_clock::now();
    auto elapsed = now - startTime;

    auto hours = std::chrono::duration_cast<std::chrono::hours>(elapsed);
    elapsed -= hours;

    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(elapsed);
    elapsed -= minutes;

    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
    elapsed -= seconds;

    auto nanoseconds = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed);

    logOut << std::format("[{:02}:{:02}:{:02}.{:09}] [{:<5}] ", hours.count(), minutes.count(), seconds.count(), nanoseconds.count(), level_name(level)) << message << std::endl;
}

void Logger::trace(std::string message) { log(LogLevel::eTrace, std::move(message)); }

void Logger::debug(std::string message) { log(LogLevel::eDebug, std::move(message)); }

void Logger::info(std::string message) { log(LogLevel::eInfo, std::move(message)); }

void Logger::warning(std::string message) { log(LogLevel::eWarning, std::move(message)); }

void Logger::error(std::string message) { log(LogLevel::eError, std::move(message)); }

void Logger::fatal(std::string message) { log(LogLevel::eFatal, std::move(message)); }

void Logger::print(std::string message) { info(std::move(message)); }

void Logger::vulkan(LogLevel level, std::string message)
{
    if (!is_vulkan_renderer_logging_enabled()) return;
    log(level, std::string("Vulkan: ") + message);
}

void Logger::vulkan(std::string message) { vulkan(LogLevel::eDebug, std::move(message)); }

void Logger::report_version_number(uint32_t version)
{
    if (!is_vulkan_renderer_logging_enabled()) return;

    vulkan(std::format("System supports the following Vulkan specifications:\n\tVariant: {}\n\tMajor: {}\n\tMinor: {}\n\tPatch: {}", vk::apiVersionVariant(version), vk::apiVersionMajor(version), vk::apiVersionMinor(version), vk::apiVersionPatch(version)));
}

void Logger::print_list(const char** list, uint32_t count)
{
    if (!is_vulkan_renderer_logging_enabled()) return;

    for (uint32_t i = 0; i < count; i++) logOut << "\t\"" << list[i] << "\"" << std::endl;
}

void Logger::print_extensions(std::vector<vk::ExtensionProperties>& extensions)
{
    if (!is_vulkan_renderer_logging_enabled()) return;

    for (vk::ExtensionProperties extension : extensions) logOut << "\t\'" << extension.extensionName << "\'" << std::endl;
}

void Logger::print_layers(std::vector<vk::LayerProperties>& layers)
{
    if (!is_vulkan_renderer_logging_enabled()) return;

    for (vk::LayerProperties layer : layers) logOut << "\t\'" << layer.layerName << "\'" << std::endl;
}

vk::DebugUtilsMessengerEXT Logger::make_debug_messenger(vk::Instance& instance, vk::detail::DispatchLoaderDynamic& dldi, std::deque<std::function<void(vk::Instance)>>& deletionQueue)
{
    if (!is_vulkan_validation_enabled()) return nullptr;

    vk::DebugUtilsMessengerCreateInfoEXT createInfo = vk::DebugUtilsMessengerCreateInfoEXT(
        vk::DebugUtilsMessengerCreateFlagsEXT(),
        vk::DebugUtilsMessageSeverityFlagBitsEXT::eError | vk::DebugUtilsMessageSeverityFlagBitsEXT::eInfo | vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning,
        vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation,
        reinterpret_cast<vk::PFN_DebugUtilsMessengerCallbackEXT>(debugCallback), nullptr
    );

    vk::DebugUtilsMessengerEXT messenger = instance.createDebugUtilsMessengerEXT(createInfo, nullptr, dldi);
    VkDebugUtilsMessengerEXT handle = messenger;

    deletionQueue.push_back([this, handle, dldi](vk::Instance instance) {
        instance.destroyDebugUtilsMessengerEXT(handle, nullptr, dldi);
        vulkan("Destroyed debug messenger.");
    });

    return messenger;
}

void Logger::log(const vk::PhysicalDevice& device)
{
    if (!is_vulkan_renderer_logging_enabled()) return;

    vk::PhysicalDeviceProperties properties = device.getProperties();

    vulkan("Physical device specifications:");

    logOut << "\tDevice name: " << properties.deviceName << std::endl;
    
    logOut << "\tDevice type: ";
    switch (properties.deviceType)
    {
        case (vk::PhysicalDeviceType::eCpu):
            logOut << "CPU";
            break;
        case (vk::PhysicalDeviceType::eDiscreteGpu):
            logOut << "Discrete GPU";
            break;
        case (vk::PhysicalDeviceType::eIntegratedGpu):
            logOut << "Integrated GPU";
            break;
        case (vk::PhysicalDeviceType::eVirtualGpu):
            logOut << "Virtual GPU";
            break;

        default:
            logOut << "Other";
    }

    logOut << std::endl;
}

void Logger::log(const std::vector<vk::QueueFamilyProperties>& queueFamilies) {

	if (!is_vulkan_renderer_logging_enabled()) return;

	logOut << "There are " << queueFamilies.size()
		<< " queue families available on the system."
		<< std::endl;

	for (uint32_t i = 0; i < queueFamilies.size(); ++i) {

		const vk::QueueFamilyProperties& queueFamily = queueFamilies[i];

		logOut << "Queue Family " << i << ":" << std::endl;

		std::vector<std::string> supportedFeatures;

		if (queueFamily.queueFlags & vk::QueueFlagBits::eCompute) 
		{
			supportedFeatures.push_back("compute");
		}

		if (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics) 
		{
			supportedFeatures.push_back("graphics");
		}

		if (queueFamily.queueFlags & vk::QueueFlagBits::eTransfer) 
		{
			supportedFeatures.push_back("transfer");
		}

		if (queueFamily.queueFlags & vk::QueueFlagBits::eOpticalFlowNV) 
		{
			supportedFeatures.push_back("NVIDIA optical flow");
		}

		if (queueFamily.queueFlags & vk::QueueFlagBits::eSparseBinding) 
		{
			supportedFeatures.push_back("sparse binding");
		}

		if (queueFamily.queueFlags & vk::QueueFlagBits::eProtected) 
		{
			supportedFeatures.push_back("protected memory");
		}

		if (queueFamily.queueFlags & vk::QueueFlagBits::eVideoDecodeKHR) 
		{
			supportedFeatures.push_back("video decode");
		}

		if (queueFamily.queueFlags & vk::QueueFlagBits::eVideoEncodeKHR) 
		{
			supportedFeatures.push_back("video encode");
		}

		logOut << "\tSupports ";

		for (size_t j = 0; j < supportedFeatures.size(); ++j) 
		{
			logOut << supportedFeatures[j];

			if (j < supportedFeatures.size() - 1) 
			{
				logOut << ", ";
			}
		}

		logOut << std::endl;

		logOut << "\tFamily supports "
			<< queueFamily.queueCount << " queue(s)." << std::endl;
	}
}

void Logger::log(const vk::SurfaceCapabilitiesKHR& capabilities) {

	if (!is_vulkan_renderer_logging_enabled()) return;

	logOut << "Swapchain supports the following surface capabilities:" << std::endl;

	logOut << "\tMinimum image count: " << capabilities.minImageCount << std::endl;
	logOut << "\tMaximum image count: " << capabilities.maxImageCount << std::endl;

	logOut << "\tCurrent extent:" << std::endl;
	log(capabilities.currentExtent, "\t\t");

	logOut << "\tMinimum supported extent:" << std::endl;
	log(capabilities.minImageExtent, "\t\t");

	logOut << "\tMaximum supported extent:" << std::endl;
	log(capabilities.maxImageExtent, "\t\t");

	logOut << "\tMaximum image array layers: " << capabilities.maxImageArrayLayers << std::endl;

	logOut << "\tSupported transforms:" << std::endl;
	std::vector<std::string> stringList = parse_transform_bits(capabilities.supportedTransforms);
	log(stringList, "\t\t");

	logOut << "\tCurrent transform:" << std::endl;
	stringList = parse_transform_bits(capabilities.currentTransform);
	log(stringList, "\t\t");

	logOut << "\tSupported alpha operations:" << std::endl;
	stringList = parse_alpha_composite_bits(capabilities.supportedCompositeAlpha);
	log(stringList, "\t\t");

	logOut << "\tSupported image usage:" << std::endl;
	stringList = parse_image_usage_bits(capabilities.supportedUsageFlags);
	log(stringList, "\t\t");
}

void Logger::log(const vk::Extent2D& extent, const char* prefix) {
	
	if (!is_vulkan_renderer_logging_enabled()) return;

	logOut << prefix << "width: " << extent.width << std::endl;
	logOut << prefix << "height: " << extent.height << std::endl;
}

void Logger::log(const std::vector<std::string>& items, const char* prefix) {

	if (!is_vulkan_renderer_logging_enabled()) return;

	for (const std::string& item : items) logOut << prefix << item << std::endl;
}

void Logger::log(const std::vector<vk::SurfaceFormatKHR>& formats) {

	if (!is_vulkan_renderer_logging_enabled()) return;

	for (vk::SurfaceFormatKHR supportedFormat : formats) 
    {
		logOut << "Supported pixel format: " 
			<< vk::to_string(supportedFormat.format) 
			<< ", supported color space: " 
			<< vk::to_string(supportedFormat.colorSpace) 
			<< std::endl;
	}
}

void Logger::log(const std::vector<vk::PresentModeKHR>& modes) {

	if (!is_vulkan_renderer_logging_enabled()) return;

	for (vk::PresentModeKHR presentMode : modes) logOut << '\t' << vk::to_string(presentMode) << std::endl;
}

void Logger::log(const VmaAllocationInfo& info)
{
	if (!is_vulkan_renderer_logging_enabled()) return;

	logOut << "---- " << info.pName << " ----" << std::endl;
	logOut << "\tMemory Type: " << info.memoryType << std::endl;
	logOut << "\tMemory Object: " << info.deviceMemory << std::endl;
	logOut << "\tOffset: " << info.offset << std::endl;
	logOut << "\tSize: " << info.size << std::endl;
}

std::vector<std::string> Logger::parse_transform_bits(
	vk::SurfaceTransformFlagsKHR bits) {
	
	std::vector<std::string> result;

	if (bits & vk::SurfaceTransformFlagBitsKHR::eIdentity) 
	{
		result.push_back("identity");
	}

	if (bits & vk::SurfaceTransformFlagBitsKHR::eRotate90) 
	{
		result.push_back("90 degree rotation");
	}

	if (bits & vk::SurfaceTransformFlagBitsKHR::eRotate180) 
	{
		result.push_back("180 degree rotation");
	}

	if (bits & vk::SurfaceTransformFlagBitsKHR::eRotate270) 
	{
		result.push_back("270 degree rotation");
	}

	if (bits & vk::SurfaceTransformFlagBitsKHR::eHorizontalMirror) 
	{
		result.push_back("horizontal mirror");
	}
	
	if (bits & vk::SurfaceTransformFlagBitsKHR::eHorizontalMirrorRotate90) 
	{
		result.push_back("horizontal mirror, then 90 degree rotation");
	}

	if (bits & vk::SurfaceTransformFlagBitsKHR::eHorizontalMirrorRotate180) 
	{
		result.push_back("horizontal mirror, then 180 degree rotation");
	}

	if (bits & vk::SurfaceTransformFlagBitsKHR::eHorizontalMirrorRotate270) 
	{
		result.push_back("horizontal mirror, then 270 degree rotation");
	}

	if (bits & vk::SurfaceTransformFlagBitsKHR::eInherit) 
	{
		result.push_back("inherited");
	}

	return result;
}

std::vector<std::string> Logger::parse_alpha_composite_bits(vk::CompositeAlphaFlagsKHR bits) 
{	
	std::vector<std::string> result;

	if (bits & vk::CompositeAlphaFlagBitsKHR::eOpaque) {
		result.push_back("opaque (alpha ignored)");
	}
	if (bits & vk::CompositeAlphaFlagBitsKHR::ePreMultiplied) {
		result.push_back("pre multiplied (alpha expected to already be multiplied in image)");
	}
	if (bits & vk::CompositeAlphaFlagBitsKHR::ePostMultiplied) {
		result.push_back("post multiplied (alpha will be applied during composition)");
	}
	if (bits & vk::CompositeAlphaFlagBitsKHR::eInherit) {
		result.push_back("inherited");
	}

	return result;
}

std::vector<std::string> Logger::parse_image_usage_bits(vk::ImageUsageFlags bits) 
{
	std::vector<std::string> result;

	if (bits & vk::ImageUsageFlagBits::eTransferSrc) result.push_back("Transfer SRC: image can be used as the source of a transfer command.");
	if (bits & vk::ImageUsageFlagBits::eTransferDst) result.push_back("Transfer DST: image can be used as the destination of a transfer command.");
	if (bits & vk::ImageUsageFlagBits::eSampled) result.push_back("Sampled: image can be used to create a VkImageView suitable for occupying a \
VkDescriptorSet slot either of type VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE or \
VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, and be sampled by a shader.");
	if (bits & vk::ImageUsageFlagBits::eStorage) result.push_back("Storage: image can be used to create a VkImageView suitable for occupying a \
VkDescriptorSet slot of type VK_DESCRIPTOR_TYPE_STORAGE_IMAGE.");
	if (bits & vk::ImageUsageFlagBits::eColorAttachment) result.push_back("Colour attachment: image can be used to create a VkImageView suitable for use as \
a color or resolve attachment in a VkFramebuffer.");
	if (bits & vk::ImageUsageFlagBits::eDepthStencilAttachment) result.push_back("Depth/stencil attachment: image can be used to create a VkImageView \
suitable for use as a depth/stencil or depth/stencil resolve attachment in a VkFramebuffer.");
	if (bits & vk::ImageUsageFlagBits::eTransientAttachment) result.push_back("Transient attachment: implementations may support using memory allocations \
with the VK_MEMORY_PROPERTY_LAZILY_ALLOCATED_BIT to back an image with this usage. This \
bit can be set for any image that can be used to create a VkImageView suitable for use as \
a color, resolve, depth/stencil, or input attachment.");
	if (bits & vk::ImageUsageFlagBits::eInputAttachment) result.push_back("Input attachment: image can be used to create a VkImageView suitable for \
occupying VkDescriptorSet slot of type VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT; be read from \
a shader as an input attachment; and be used as an input attachment in a framebuffer.");
	if (bits & vk::ImageUsageFlagBits::eFragmentDensityMapEXT) result.push_back("Fragment density map: image can be used to create a VkImageView suitable \
for use as a fragment density map image.");
	if (bits & vk::ImageUsageFlagBits::eFragmentShadingRateAttachmentKHR) result.push_back("Fragment shading rate attachment: image can be used to create a VkImageView \
suitable for use as a fragment shading rate attachment or shading rate image");

	return result;
}

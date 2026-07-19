#pragma once
#include "vibranceUI/export.h"
#include <string>
#include <filesystem>
#include <deque>
#include <functional>
#include <cstdint>
#include <iostream>
#include <fstream>
#include <chrono>
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>

enum class LogLevel : uint8_t
{
    // Ordered by severity so minimum-level filtering is a simple comparison
    eTrace = 0,
    eDebug,
    eInfo,
    eWarning,
    eError,
    eFatal
};

struct LoggerOutputOptions
{
    // Keeps the timestamped filename format while letting apps brand the path
    std::filesystem::path directory {};
    std::string directoryName = "vibranceUI";
    std::string filePrefix = "vibrance_log";
};

class VIBRANCE_ENGINE_API Logger
{
    public:
    // Process-wide logger used by the renderer, app and optional Vulkan validation
    static Logger* logger;

    static Logger* fetch_logger();

    static void configure_output(LoggerOutputOptions options);

    // Master switch for app logs outside renderer validation
    void set_mode(bool mode);

    bool is_enabled();

    void set_minimum_level(LogLevel level);

    LogLevel minimum_level() const;

    // Keeps noisy Vulkan renderer detail separate from ordinary release logs
    void set_vulkan_renderer_logging_mode(bool mode);

    bool is_vulkan_renderer_logging_enabled() const;

    void set_vulkan_validation_mode(bool mode);

    bool is_vulkan_validation_enabled() const;

    bool is_file_open() const { return logFile.is_open(); }
    std::ofstream& get_file_stream() { return logFile; }
    const std::filesystem::path& output_path() const { return logPath; }

    ~Logger();

    std::chrono::steady_clock::time_point get_start_time() const { return startTime; }

    // Central path for all log levels before filtering and formatting
    void log(LogLevel level, std::string message);

    void trace(std::string message);

    void debug(std::string message);

    void info(std::string message);

    void warning(std::string message);

    void error(std::string message);

    void fatal(std::string message);

    void print(std::string message);

    void vulkan(LogLevel level, std::string message);

    // Vulkan detail helpers are gated separately from ordinary app logs
    void vulkan(std::string message);

    void report_version_number(uint32_t version);

    void print_list(const char** list, uint32_t count);

    void print_extensions(std::vector<vk::ExtensionProperties>& extensions);

    void print_layers(std::vector<vk::LayerProperties>& layers);

    vk::DebugUtilsMessengerEXT make_debug_messenger(
        vk::Instance& instance, vk::detail::DispatchLoaderDynamic& dldi, std::deque<std::function<void(vk::Instance)>>& deletionQueue
    );

    void log(const vk::PhysicalDevice& device);

    void log(const std::vector<vk::QueueFamilyProperties>& queueFamilies);

    void log(const vk::SurfaceCapabilitiesKHR& capabilities);

    void log(const vk::Extent2D& extent, const char* prefix = "\t");

    void log(const std::vector<std::string>& items, const char* prefix = "\t");

    void log(const std::vector<vk::SurfaceFormatKHR>& formats);

    void log(const std::vector<vk::PresentModeKHR>& modes);

    void log(const VmaAllocationInfo& info);

    private:
    bool enabled;
    bool vulkanRendererLoggingEnabled;
    bool vulkanValidationEnabled;
    LogLevel minLevel;
    std::ofstream logFile;
    LoggerOutputOptions outputOptions {};
    std::filesystem::path logPath {};
    std::chrono::steady_clock::time_point startTime;

    Logger();
    void reopen_file();

    std::vector<std::string> parse_transform_bits(vk::SurfaceTransformFlagsKHR bits);

    std::vector<std::string> parse_alpha_composite_bits(vk::CompositeAlphaFlagsKHR bits);

    std::vector<std::string> parse_image_usage_bits(vk::ImageUsageFlags bits);
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
    VkDebugUtilsMessageTypeFlagsEXT messageType,
    const VkDebugUtilsMessengerCallbackDataEXT* pCallbackData,
    void* pUserData
);

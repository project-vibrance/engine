// Standalone offscreen Vulkan shader comparison. Build without the engine and
// pass baseline/candidate SPIR-V paths, plus "shape" or "text" for those fixtures.
#include <vulkan/vulkan.h>
#include "../src/renderer/vulkan/timestamp_diagnostics.h"
#include "../src/renderer/common/text_foreground_bounds.h"
#include "../src/renderer/vulkan/render_op_order.h"
#include "../include/vibranceUI/renderer/dispatch_bounds.h"
#include "shape_shader_fixtures.h"
#include "text_shader_fixtures.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
    std::atomic<uint32_t> validationErrors = 0u;

    bool validation_requested()
    {
        const char* enabled = std::getenv("VIBRANCE_SHADER_HARNESS_VALIDATION");
        return enabled && std::strcmp(enabled, "1") == 0;
    }

    bool equivalence_only_requested()
    {
        const char* enabled = std::getenv("VIBRANCE_SHADER_HARNESS_EQUIVALENCE_ONLY");
        return enabled && std::strcmp(enabled, "1") == 0;
    }

    VKAPI_ATTR VkBool32 VKAPI_CALL validation_callback(
        VkDebugUtilsMessageSeverityFlagBitsEXT severity,
        VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* message,
        void*)
    {
        if ((severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) != 0u)
            ++validationErrors;
        std::cerr << "Vulkan validation: " << message->pMessage << '\n';
        return VK_FALSE;
    }

    constexpr uint32_t clearFlag = 1u << 7u;
    constexpr uint32_t compositeFlag = 1u << 9u;
    constexpr uint32_t horizontalFlag = 1u << 17u;
    constexpr uint32_t verticalFlag = 1u << 18u;
    constexpr uint32_t liquidFlag = 1u << 22u;
    constexpr uint32_t staticBackdrop = 1u << 31u;
    constexpr uint32_t externalBackdrop = 1u << 30u;
    constexpr uint32_t parentCap = 1u << 29u;

    struct PushConstants
    {
        std::array<float, 4> rect {};
        std::array<float, 4> uvRect {};
        std::array<float, 4> color0 {};
        std::array<float, 4> color1 {};
        std::array<float, 4> color2 {};
        std::array<float, 4> effect0 {};
        std::array<float, 4> effect1 {};
        std::array<uint32_t, 4> data {};
    };
    static_assert(sizeof(PushConstants) == 128u);

    void check(VkResult result, const char* operation)
    {
        if (result != VK_SUCCESS)
            throw std::runtime_error(std::string(operation) + ": VkResult " +
                std::to_string(static_cast<int>(result)));
    }

    std::vector<uint32_t> load_spirv(const char* path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) throw std::runtime_error(std::string("Cannot read SPIR-V: ") + path);
        const auto size = file.tellg();
        if (size <= 0 || size % 4 != 0) throw std::runtime_error("Invalid SPIR-V size");
        std::vector<uint32_t> words(static_cast<std::size_t>(size) / 4u);
        file.seekg(0);
        file.read(reinterpret_cast<char*>(words.data()), size);
        if (!file) throw std::runtime_error("Incomplete SPIR-V read");
        return words;
    }

    struct Image
    {
        VkImage handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };
    struct Buffer
    {
        VkBuffer handle = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void* mapped = nullptr;
    };
    struct Case
    {
        std::string name;
        PushConstants constants;
        uint32_t originX = 0u, originY = 0u, dispatchWidth = 0u, dispatchHeight = 0u;
    };
    constexpr uint32_t textGlyphCapacity = 384u;
    constexpr uint32_t maximumTextBatchSize = 64u;
    using TextWaves = std::vector<std::vector<uint32_t>>;

    bool glyphs_overlap(const Case& left, const Case& right)
    {
        return left.originX < right.originX + right.dispatchWidth &&
            right.originX < left.originX + left.dispatchWidth &&
            left.originY < right.originY + right.dispatchHeight &&
            right.originY < left.originY + left.dispatchHeight;
    }

    struct TextDispatch
    {
        uint32_t firstGlyph, count, width, height;
    };
    struct MixedBounds { uint32_t x, y, width, height; };
    struct MixedOperation { Case draw; bool foregroundText; };
    struct MixedCommand
    {
        const MixedOperation* operation = nullptr;
        TextDispatch glyphs {};
        bool barrierBefore = false;
    };
    struct Result
    {
        std::vector<uint8_t> bytes;
        double milliseconds = 0.0;
    };

    class Harness
    {
    public:
        Harness(uint32_t imageWidth, uint32_t imageHeight,
            const std::vector<uint32_t>& baseline, const std::vector<uint32_t>& candidate,
            const TextAtlasFixture* textAtlas = nullptr, bool batchText = false,
            const std::vector<uint32_t>* shapeShader = nullptr) :
            width(imageWidth), height(imageHeight), imageBytes(static_cast<VkDeviceSize>(width) * height * 4u),
            atlas(textAtlas), batchMode(batchText)
        {
            VkApplicationInfo application { VK_STRUCTURE_TYPE_APPLICATION_INFO };
            application.pApplicationName = "Vibrance offscreen shader comparison";
            application.apiVersion = VK_API_VERSION_1_3;
            VkInstanceCreateInfo instanceInfo { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
            instanceInfo.pApplicationInfo = &application;
            const char* validationLayer = "VK_LAYER_KHRONOS_validation";
            const char* debugExtension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
            VkDebugUtilsMessengerCreateInfoEXT debugInfo { VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT };
            debugInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            debugInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
            debugInfo.pfnUserCallback = validation_callback;
            if (validation_requested())
            {
                instanceInfo.enabledLayerCount = instanceInfo.enabledExtensionCount = 1u;
                instanceInfo.ppEnabledLayerNames = &validationLayer;
                instanceInfo.ppEnabledExtensionNames = &debugExtension;
                instanceInfo.pNext = &debugInfo;
            }
            check(vkCreateInstance(&instanceInfo, nullptr, &instance), "Create instance");
            if (validation_requested())
            {
                const auto createMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance, "vkCreateDebugUtilsMessengerEXT"));
                if (!createMessenger) throw std::runtime_error("Vulkan debug messenger is unavailable");
                check(createMessenger(instance, &debugInfo, nullptr, &debugMessenger), "Create validation messenger");
            }
            uint32_t count = 0u;
            check(vkEnumeratePhysicalDevices(instance, &count, nullptr), "Count devices");
            std::vector<VkPhysicalDevice> devices(count);
            check(vkEnumeratePhysicalDevices(instance, &count, devices.data()), "Enumerate devices");
            for (VkPhysicalDevice possible : devices)
            {
                VkPhysicalDeviceProperties possibleProperties {};
                vkGetPhysicalDeviceProperties(possible, &possibleProperties);
                if (possibleProperties.apiVersion < VK_API_VERSION_1_3) continue;
                uint32_t familyCount = 0u;
                vkGetPhysicalDeviceQueueFamilyProperties(possible, &familyCount, nullptr);
                std::vector<VkQueueFamilyProperties> families(familyCount);
                vkGetPhysicalDeviceQueueFamilyProperties(possible, &familyCount, families.data());
                for (uint32_t family = 0u; family < familyCount; ++family)
                {
                    if ((families[family].queueFlags & VK_QUEUE_COMPUTE_BIT) == 0u ||
                        families[family].timestampValidBits == 0u) continue;
                    physical = possible;
                    properties = possibleProperties;
                    queueFamily = family;
                    timestampBits = families[family].timestampValidBits;
                    break;
                }
                if (physical) break;
            }
            if (!physical) throw std::runtime_error("No Vulkan 1.3 compute device with timestamps");
            VkFormatProperties formatProperties {};
            vkGetPhysicalDeviceFormatProperties(physical, VK_FORMAT_R8G8B8A8_UNORM, &formatProperties);
            if ((formatProperties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) == 0u)
                throw std::runtime_error("RGBA8 storage images are unavailable");
            vkGetPhysicalDeviceMemoryProperties(physical, &memoryProperties);
            const float priority = 1.0f;
            VkDeviceQueueCreateInfo queueInfo { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queueInfo.queueFamilyIndex = queueFamily;
            queueInfo.queueCount = 1u;
            queueInfo.pQueuePriorities = &priority;
            VkDeviceCreateInfo deviceInfo { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
            deviceInfo.queueCreateInfoCount = 1u;
            deviceInfo.pQueueCreateInfos = &queueInfo;
            check(vkCreateDevice(physical, &deviceInfo, nullptr, &device), "Create device");
            vkGetDeviceQueue(device, queueFamily, 0u, &queue);

            VkCommandPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            poolInfo.queueFamilyIndex = queueFamily;
            check(vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool), "Create command pool");
            VkCommandBufferAllocateInfo allocate { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            allocate.commandPool = commandPool;
            allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            allocate.commandBufferCount = 1u;
            check(vkAllocateCommandBuffers(device, &allocate, &command), "Allocate command buffer");
            VkFenceCreateInfo fenceInfo { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
            check(vkCreateFence(device, &fenceInfo, nullptr, &fence), "Create fence");
            VkQueryPoolCreateInfo queryInfo { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
            queryInfo.queryType = VK_QUERY_TYPE_TIMESTAMP;
            queryInfo.queryCount = 2u;
            check(vkCreateQueryPool(device, &queryInfo, nullptr, &queries), "Create timestamp pool");
            for (Image& image : images) image = create_image(width, height);
            if (atlas) fontAtlasImage = create_image(atlas->side / 4u, atlas->side);
            upload = create_buffer(imageBytes * images.size() + (atlas ? atlas->bytes.size() : 0u),
                VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
            readback = create_buffer(imageBytes * 3u, VK_BUFFER_USAGE_TRANSFER_DST_BIT);
            if (batchMode)
            {
                glyphUpload = create_buffer(sizeof(PushConstants) * textGlyphCapacity, VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
                glyphRecords = create_buffer(sizeof(PushConstants) * textGlyphCapacity,
                    VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            }
            fill_inputs();
            create_descriptors();
            baselinePipeline = create_pipeline(baseline, nullptr);
            uint32_t disabled = 0u, enabled = 1u;
            candidateOrdinary = create_pipeline(candidate, &disabled);
            candidateShared = create_pipeline(candidate, &enabled);
            if (shapeShader) shapePipeline = create_pipeline(*shapeShader, nullptr);
            std::cout << "Device: " << properties.deviceName << ", " << width << 'x' << height
                << ", shared-memory limit " << properties.limits.maxComputeSharedMemorySize << " bytes\n";
        }

        ~Harness()
        {
            if (device)
            {
                vkDeviceWaitIdle(device);
                for (VkPipeline pipeline : { baselinePipeline, candidateOrdinary, candidateShared, shapePipeline })
                    if (pipeline) vkDestroyPipeline(device, pipeline, nullptr);
                if (layout) vkDestroyPipelineLayout(device, layout, nullptr);
                if (descriptorPool) vkDestroyDescriptorPool(device, descriptorPool, nullptr);
                for (VkDescriptorSetLayout setLayout : setLayouts)
                    if (setLayout) vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
                for (Image image : images)
                {
                    if (image.view) vkDestroyImageView(device, image.view, nullptr);
                    if (image.handle) vkDestroyImage(device, image.handle, nullptr);
                    if (image.memory) vkFreeMemory(device, image.memory, nullptr);
                }
                if (fontAtlasImage.view) vkDestroyImageView(device, fontAtlasImage.view, nullptr);
                if (fontAtlasImage.handle) vkDestroyImage(device, fontAtlasImage.handle, nullptr);
                if (fontAtlasImage.memory) vkFreeMemory(device, fontAtlasImage.memory, nullptr);
                for (Buffer buffer : { upload, readback, glyphUpload, glyphRecords })
                {
                    if (buffer.mapped) vkUnmapMemory(device, buffer.memory);
                    if (buffer.handle) vkDestroyBuffer(device, buffer.handle, nullptr);
                    if (buffer.memory) vkFreeMemory(device, buffer.memory, nullptr);
                }
                if (queries) vkDestroyQueryPool(device, queries, nullptr);
                if (fence) vkDestroyFence(device, fence, nullptr);
                if (commandPool) vkDestroyCommandPool(device, commandPool, nullptr);
                vkDestroyDevice(device, nullptr);
            }
            if (debugMessenger)
            {
                const auto destroyMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                    vkGetInstanceProcAddr(instance, "vkDestroyDebugUtilsMessengerEXT"));
                destroyMessenger(instance, debugMessenger, nullptr);
            }
            if (instance) vkDestroyInstance(instance, nullptr);
        }

        Result run(const Case& testCase, bool candidate, uint32_t repetitions = 1u,
            bool horizontalOnly = false, bool ordinaryHorizontal = false, bool shape = false, bool text = false,
            const std::vector<Case>* glyphs = nullptr, bool interDrawBarriers = true,
            uint32_t glyphOffset = 0u, const TextWaves* waves = nullptr,
            const std::vector<MixedOperation>* mixed = nullptr)
        {
            std::vector<TextDispatch> textDispatches;
            std::vector<MixedCommand> mixedCommands;
            uint32_t mixedRecordCount = 0u;
            if (mixed)
            {
                if (!batchMode || !shapePipeline || glyphs)
                    throw std::runtime_error("Mixed operation fixtures require shape and text pipelines");
                std::vector<MixedBounds> bounds;
                std::vector<uint8_t> foregroundText;
                for (const auto& operation : *mixed)
                {
                    const auto& draw = operation.draw;
                    bounds.push_back({ draw.originX, draw.originY, draw.dispatchWidth, draw.dispatchHeight });
                    foregroundText.push_back(operation.foregroundText ? 1u : 0u);
                }
                std::vector<uint32_t> indices;
                if (candidate)
                {
                    vibrance::vulkan::SimpleRenderOrder order;
                    for (std::size_t first = 0u; first < mixed->size();
                        first += vibrance::vulkan::kTextBatchPlanningGlyphLimit)
                    {
                        const auto count = std::min<std::size_t>(mixed->size() - first,
                            vibrance::vulkan::kTextBatchPlanningGlyphLimit);
                        const auto textCount = static_cast<std::size_t>(std::count(
                            foregroundText.begin() + first, foregroundText.begin() + first + count, uint8_t(1)));
                        if (textCount < 2u || textCount == count)
                        {
                            for (std::size_t index = first; index < first + count; ++index)
                                indices.push_back(static_cast<uint32_t>(index));
                            continue;
                        }
                        vibrance::vulkan::build_simple_render_order(
                            std::span<const MixedBounds>(bounds).subspan(first, count),
                            std::span<const uint8_t>(foregroundText).subspan(first, count), order);
                        for (const uint32_t index : order.indices)
                            indices.push_back(static_cast<uint32_t>(first) + index);
                    }
                }
                else
                {
                    for (uint32_t index = 0u; index < mixed->size(); ++index) indices.push_back(index);
                }
                std::vector<MixedBounds> pending;
                const auto overlaps_pending = [&](const MixedBounds& drawBounds) {
                    return std::any_of(pending.begin(), pending.end(), [&](const auto& previous) {
                        return vibrance::vulkan::text_batch_bounds_overlap(previous, drawBounds);
                    });
                };
                const auto ordinary = [&](uint32_t index) {
                    if (bounds[index].width == 0u || bounds[index].height == 0u) return;
                    const bool barrier = !candidate || overlaps_pending(bounds[index]);
                    if (barrier) pending.clear();
                    mixedCommands.push_back({ &(*mixed)[index], {}, barrier });
                    pending.push_back(bounds[index]);
                };
                auto* records = static_cast<PushConstants*>(glyphUpload.mapped);
                std::fill_n(records, textGlyphCapacity, PushConstants {});
                uint32_t nextRecord = 17u;
                for (std::size_t position = 0u; position < indices.size();)
                {
                    if (!candidate || !(*mixed)[indices[position]].foregroundText)
                    {
                        ordinary(indices[position++]);
                        continue;
                    }
                    std::vector<MixedBounds> textBounds;
                    std::size_t end = position;
                    for (; end < indices.size() && end - position <
                        vibrance::vulkan::kTextBatchPlanningGlyphLimit &&
                        (*mixed)[indices[end]].foregroundText; ++end)
                        textBounds.push_back(bounds[indices[end]]);
                    vibrance::vulkan::TextBatchPlan textPlan;
                    vibrance::vulkan::build_text_batch_plan(std::span<const MixedBounds>(textBounds),
                        textPlan, std::span<const MixedBounds>(pending));
                    for (const auto& group : textPlan.groups)
                    {
                        if (group.count == 1u || group.count > textGlyphCapacity - nextRecord)
                        {
                            for (uint32_t offset = 0u; offset < group.count; ++offset)
                                ordinary(indices[position + textPlan.glyphIndices[group.first + offset]]);
                            continue;
                        }
                        bool barrier = false;
                        for (uint32_t offset = 0u; offset < group.count; ++offset)
                            barrier |= overlaps_pending(textBounds[textPlan.glyphIndices[group.first + offset]]);
                        if (barrier) pending.clear();
                        mixedCommands.push_back({ nullptr,
                            { nextRecord, group.count, group.width, group.height }, barrier });
                        for (uint32_t offset = 0u; offset < group.count; ++offset)
                        {
                            const uint32_t local = textPlan.glyphIndices[group.first + offset];
                            const uint32_t index = indices[position + local];
                            const auto& draw = (*mixed)[index].draw;
                            auto& record = records[nextRecord++];
                            record = draw.constants;
                            record.data[0] = draw.dispatchWidth | (draw.dispatchHeight << 16u);
                            record.data[3] = draw.originX | (draw.originY << 16u);
                            pending.push_back(bounds[index]);
                        }
                    }
                    position = end;
                }
                if (candidate) mixedRecordCount = nextRecord;
            }
            if (glyphs)
            {
                if (!batchMode || glyphs->empty() || glyphOffset >= textGlyphCapacity ||
                    glyphs->size() > textGlyphCapacity - glyphOffset)
                    throw std::runtime_error("Invalid batched text fixture count");
                auto* records = static_cast<PushConstants*>(glyphUpload.mapped);
                std::fill_n(records, glyphOffset + glyphs->size(), PushConstants {});
                TextWaves defaultWaves(1u);
                if (!waves)
                {
                    for (uint32_t index = 0u; index < glyphs->size(); ++index)
                        defaultWaves.front().push_back(index);
                    waves = &defaultWaves;
                }
                std::vector<uint32_t> glyphWave(glyphs->size(), UINT32_MAX);
                uint32_t nextRecord = glyphOffset;
                for (uint32_t waveIndex = 0u; waveIndex < waves->size(); ++waveIndex)
                {
                    const auto& wave = (*waves)[waveIndex];
                    if (wave.empty()) throw std::runtime_error("Empty text dependency wave");
                    for (std::size_t left = 0u; left < wave.size(); ++left)
                    {
                        if (wave[left] >= glyphs->size() || glyphWave[wave[left]] != UINT32_MAX)
                            throw std::runtime_error("Invalid text dependency wave index");
                        glyphWave[wave[left]] = waveIndex;
                        for (std::size_t right = 0u; right < left; ++right)
                            if (glyphs_overlap((*glyphs)[wave[left]], (*glyphs)[wave[right]]))
                                throw std::runtime_error("Text dependency wave contains overlapping glyphs");
                    }
                    for (std::size_t start = 0u; start < wave.size(); start += maximumTextBatchSize)
                    {
                        TextDispatch dispatch { nextRecord, 0u, 0u, 0u };
                        for (std::size_t index = start;
                            index < std::min(start + maximumTextBatchSize, wave.size()); ++index)
                        {
                            const auto& glyph = (*glyphs)[wave[index]];
                            auto& record = records[nextRecord++];
                            record = glyph.constants;
                            record.data[0] = glyph.dispatchWidth | (glyph.dispatchHeight << 16u);
                            record.data[3] = glyph.originX | (glyph.originY << 16u);
                            dispatch.width = std::max(dispatch.width, glyph.dispatchWidth);
                            dispatch.height = std::max(dispatch.height, glyph.dispatchHeight);
                            ++dispatch.count;
                        }
                        textDispatches.push_back(dispatch);
                    }
                }
                if (nextRecord != glyphOffset + glyphs->size())
                    throw std::runtime_error("Text dependency waves omitted a glyph");
                for (std::size_t left = 0u; left < glyphs->size(); ++left)
                    for (std::size_t right = left + 1u; right < glyphs->size(); ++right)
                        if (glyphs_overlap((*glyphs)[left], (*glyphs)[right]) && glyphWave[left] >= glyphWave[right])
                            throw std::runtime_error("Text dependency waves changed painter order");
            }
            check(vkResetCommandBuffer(command, 0u), "Reset command buffer");
            VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
            check(vkBeginCommandBuffer(command, &begin), "Begin command buffer");
            std::array<VkImageMemoryBarrier, 6> barriers {};
            const uint32_t barrierCount = atlas ? 6u : 5u;
            for (std::size_t index = 0u; index < images.size(); ++index)
            {
                barriers[index] = image_barrier(images[index], initialised ? VK_IMAGE_LAYOUT_GENERAL :
                    VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    initialised ? VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT : 0u,
                    VK_ACCESS_TRANSFER_WRITE_BIT);
            }
            if (atlas) barriers[5] = image_barrier(fontAtlasImage, initialised ? VK_IMAGE_LAYOUT_GENERAL :
                VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                initialised ? VK_ACCESS_SHADER_READ_BIT : 0u, VK_ACCESS_TRANSFER_WRITE_BIT);
            vkCmdPipelineBarrier(command, initialised ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT :
                VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0u,
                0u, nullptr, 0u, nullptr, barrierCount, barriers.data());
            for (std::size_t index = 0u; index < images.size(); ++index)
            {
                VkBufferImageCopy copy {};
                copy.bufferOffset = imageBytes * index;
                copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
                copy.imageExtent = { width, height, 1u };
                vkCmdCopyBufferToImage(command, upload.handle, images[index].handle,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
                barriers[index] = image_barrier(images[index], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            }
            if (atlas)
            {
                VkBufferImageCopy copy {};
                copy.bufferOffset = imageBytes * images.size();
                copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
                copy.imageExtent = { atlas->side / 4u, atlas->side, 1u };
                vkCmdCopyBufferToImage(command, upload.handle, fontAtlasImage.handle,
                    VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy);
                barriers[5] = image_barrier(fontAtlasImage, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
            }
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u, 0u, nullptr, 0u, nullptr, barrierCount, barriers.data());
            if (glyphs || mixedRecordCount != 0u)
            {
                const auto recordCount = glyphs ? glyphOffset + glyphs->size() : mixedRecordCount;
                VkBufferCopy copy { 0u, 0u, sizeof(PushConstants) * recordCount };
                vkCmdCopyBuffer(command, glyphUpload.handle, glyphRecords.handle, 1u, &copy);
                VkBufferMemoryBarrier glyphBarrier { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
                glyphBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                glyphBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
                glyphBarrier.srcQueueFamilyIndex = glyphBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                glyphBarrier.buffer = glyphRecords.handle;
                glyphBarrier.size = VK_WHOLE_SIZE;
                vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    0u, 0u, nullptr, 1u, &glyphBarrier, 0u, nullptr);
            }
            // Finish input uploads before the timed submission. A TOP_OF_PIPE
            // timestamp in this upload command buffer could overlap transfers.
            submit_and_wait();
            initialised = true;
            check(vkResetCommandBuffer(command, 0u), "Reset timed command buffer");
            check(vkBeginCommandBuffer(command, &begin), "Begin timed command buffer");
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout,
                0u, batchMode ? 3u : 2u, sets.data(), 0u, nullptr);
            if (glyphs) vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE,
                candidate ? candidateOrdinary : baselinePipeline);
            vkCmdResetQueryPool(command, queries, 0u, 2u);
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, queries, 0u);
            for (uint32_t repeat = 0u; repeat < repetitions; ++repeat)
            {
                if (mixed)
                {
                    for (const auto& draw : mixedCommands)
                    {
                        if (draw.barrierBefore) compute_barrier();
                        if (draw.operation)
                            dispatch(draw.operation->foregroundText ? baselinePipeline : shapePipeline,
                                draw.operation->draw, 0u, true, false);
                        else
                        {
                            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, candidateOrdinary);
                            vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u,
                                sizeof(draw.glyphs.firstGlyph), &draw.glyphs.firstGlyph);
                            vkCmdDispatch(command, (draw.glyphs.width + 7u) / 8u,
                                (draw.glyphs.height + 7u) / 8u, draw.glyphs.count);
                        }
                    }
                    compute_barrier();
                    continue;
                }
                if (glyphs)
                {
                    if (candidate)
                    {
                        for (const auto& dispatch : textDispatches)
                        {
                            vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u,
                                sizeof(dispatch.firstGlyph), &dispatch.firstGlyph);
                            vkCmdDispatch(command, (dispatch.width + 7u) / 8u,
                                (dispatch.height + 7u) / 8u, dispatch.count);
                            compute_barrier();
                        }
                    }
                    else
                    {
                        for (const auto& glyph : *glyphs)
                            dispatch(baselinePipeline, glyph, 0u, true, interDrawBarriers, false);
                        if (!interDrawBarriers) compute_barrier();
                    }
                    continue;
                }
                if (shape || text)
                {
                    dispatch(candidate ? candidateOrdinary : baselinePipeline, testCase, 0u, true);
                    continue;
                }
                if (!horizontalOnly) dispatch(candidate ? candidateOrdinary : baselinePipeline,
                    testCase, clearFlag, false);
                dispatch(candidate ? (ordinaryHorizontal ? candidateOrdinary : candidateShared) :
                    baselinePipeline, testCase, horizontalFlag, true);
                if (!horizontalOnly)
                {
                    dispatch(candidate ? candidateOrdinary : baselinePipeline,
                        testCase, verticalFlag, true);
                    dispatch(candidate ? candidateOrdinary : baselinePipeline,
                        testCase, compositeFlag, true);
                }
            }
            vkCmdWriteTimestamp(command, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, queries, 1u);
            for (std::size_t index = 0u; index < 3u; ++index)
                barriers[index] = image_barrier(images[index], VK_IMAGE_LAYOUT_GENERAL,
                    VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0u, 0u, nullptr, 0u, nullptr, 3u, barriers.data());
            for (std::size_t index = 0u; index < 3u; ++index)
            {
                VkBufferImageCopy copy {};
                copy.bufferOffset = imageBytes * index;
                copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 0u, 1u };
                copy.imageExtent = { width, height, 1u };
                vkCmdCopyImageToBuffer(command, images[index].handle, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    readback.handle, 1u, &copy);
                barriers[index] = image_barrier(images[index], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                    VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_TRANSFER_READ_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
            }
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0u, 0u, nullptr, 0u, nullptr, 3u, barriers.data());
            VkBufferMemoryBarrier hostBarrier { VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER };
            hostBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            hostBarrier.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            hostBarrier.srcQueueFamilyIndex = hostBarrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            hostBarrier.buffer = readback.handle;
            hostBarrier.size = VK_WHOLE_SIZE;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0u, 0u, nullptr, 1u, &hostBarrier, 0u, nullptr);
            submit_and_wait();
            std::array<uint64_t, 2> timestamps {};
            check(vkGetQueryPoolResults(device, queries, 0u, 2u, sizeof(timestamps), timestamps.data(),
                sizeof(uint64_t), VK_QUERY_RESULT_64_BIT), "Read timestamps");
            Result result;
            result.milliseconds = vibrance::renderer_diagnostics::timestamp_elapsed_milliseconds(
                timestamps[0], timestamps[1], timestampBits, properties.limits.timestampPeriod) / repetitions;
            result.bytes.resize(static_cast<std::size_t>(imageBytes * 3u));
            std::memcpy(result.bytes.data(), readback.mapped, result.bytes.size());
            return result;
        }

    private:
        void submit_and_wait()
        {
            check(vkEndCommandBuffer(command), "End command buffer");
            check(vkResetFences(device, 1u, &fence), "Reset fence");
            VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit.commandBufferCount = 1u;
            submit.pCommandBuffers = &command;
            check(vkQueueSubmit(queue, 1u, &submit, fence), "Submit offscreen work");
            check(vkWaitForFences(device, 1u, &fence, VK_TRUE, UINT64_MAX), "Wait for offscreen work");
        }

        uint32_t memory_type(uint32_t permitted, VkMemoryPropertyFlags requested)
        {
            for (uint32_t index = 0u; index < memoryProperties.memoryTypeCount; ++index)
                if ((permitted & (1u << index)) != 0u &&
                    (memoryProperties.memoryTypes[index].propertyFlags & requested) == requested)
                    return index;
            throw std::runtime_error("Required Vulkan memory type is unavailable");
        }

        Image create_image(uint32_t imageWidth, uint32_t imageHeight)
        {
            Image image;
            VkImageCreateInfo info { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
            info.imageType = VK_IMAGE_TYPE_2D;
            info.format = VK_FORMAT_R8G8B8A8_UNORM;
            info.extent = { imageWidth, imageHeight, 1u };
            info.mipLevels = info.arrayLayers = 1u;
            info.samples = VK_SAMPLE_COUNT_1_BIT;
            info.tiling = VK_IMAGE_TILING_OPTIMAL;
            info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            check(vkCreateImage(device, &info, nullptr, &image.handle), "Create image");
            VkMemoryRequirements requirements {};
            vkGetImageMemoryRequirements(device, image.handle, &requirements);
            VkMemoryAllocateInfo allocate { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            check(vkAllocateMemory(device, &allocate, nullptr, &image.memory), "Allocate image memory");
            check(vkBindImageMemory(device, image.handle, image.memory, 0u), "Bind image memory");
            VkImageViewCreateInfo viewInfo { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            viewInfo.image = image.handle;
            viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
            viewInfo.format = info.format;
            viewInfo.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
            check(vkCreateImageView(device, &viewInfo, nullptr, &image.view), "Create image view");
            return image;
        }

        Buffer create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
            VkMemoryPropertyFlags requested = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
        {
            Buffer buffer;
            VkBufferCreateInfo info { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            info.size = size;
            info.usage = usage;
            check(vkCreateBuffer(device, &info, nullptr, &buffer.handle), "Create staging buffer");
            VkMemoryRequirements requirements {};
            vkGetBufferMemoryRequirements(device, buffer.handle, &requirements);
            VkMemoryAllocateInfo allocate { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            allocate.allocationSize = requirements.size;
            allocate.memoryTypeIndex = memory_type(requirements.memoryTypeBits, requested);
            check(vkAllocateMemory(device, &allocate, nullptr, &buffer.memory), "Allocate staging memory");
            check(vkBindBufferMemory(device, buffer.handle, buffer.memory, 0u), "Bind staging memory");
            if ((requested & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) != 0u)
                check(vkMapMemory(device, buffer.memory, 0u, size, 0u, &buffer.mapped), "Map staging memory");
            return buffer;
        }

        void fill_inputs()
        {
            auto* bytes = static_cast<uint8_t*>(upload.mapped);
            for (std::size_t image = 0u; image < images.size(); ++image)
                for (uint32_t y = 0u; y < height; ++y)
                    for (uint32_t x = 0u; x < width; ++x)
                    {
                        const auto offset = static_cast<std::size_t>(imageBytes * image) +
                            (static_cast<std::size_t>(y) * width + x) * 4u;
                        bytes[offset] = static_cast<uint8_t>((x * 71u + y * 19u + image * 37u) & 255u);
                        bytes[offset + 1u] = static_cast<uint8_t>((x * 11u + y * 83u + image * 61u) & 255u);
                        bytes[offset + 2u] = static_cast<uint8_t>((x * 43u + y * 7u + image * 97u) & 255u);
                        bytes[offset + 3u] = (x + y * 3u + image) % 7u == 0u ? 0u :
                            static_cast<uint8_t>((x * 17u + y * 29u + image * 31u) & 255u);
                    }
            if (atlas) std::memcpy(bytes + imageBytes * images.size(), atlas->bytes.data(), atlas->bytes.size());
        }

        void create_descriptors()
        {
            const std::array<uint32_t, 6> bindingNumbers = { 1u, 0u, 2u, 4u, 6u, 3u };
            std::array<VkDescriptorSetLayoutBinding, 6> bindings {};
            for (std::size_t index = 0u; index < bindings.size(); ++index)
                bindings[index] = { bindingNumbers[index], VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1u,
                    VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
            for (uint32_t set = 0u; set < 2u; ++set)
            {
                VkDescriptorSetLayoutCreateInfo info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
                info.bindingCount = set == 0u ? 1u : (atlas ? 5u : 4u);
                info.pBindings = bindings.data() + (set == 0u ? 0u : 1u);
                check(vkCreateDescriptorSetLayout(device, &info, nullptr, &setLayouts[set]), "Create descriptor layout");
            }
            if (batchMode)
            {
                VkDescriptorSetLayoutBinding glyphBinding { 0u, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u,
                    VK_SHADER_STAGE_COMPUTE_BIT, nullptr };
                VkDescriptorSetLayoutCreateInfo info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
                info.bindingCount = 1u;
                info.pBindings = &glyphBinding;
                check(vkCreateDescriptorSetLayout(device, &info, nullptr, &setLayouts[2]), "Create glyph descriptor layout");
            }
            VkPushConstantRange pushRange { VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(PushConstants) };
            VkPipelineLayoutCreateInfo layoutInfo { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            layoutInfo.setLayoutCount = batchMode ? 3u : 2u;
            layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.pushConstantRangeCount = 1u;
            layoutInfo.pPushConstantRanges = &pushRange;
            check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout), "Create pipeline layout");
            std::array<VkDescriptorPoolSize, 2> poolSizes {
                VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, atlas ? 6u : 5u },
                VkDescriptorPoolSize { VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1u } };
            VkDescriptorPoolCreateInfo poolInfo { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            poolInfo.maxSets = batchMode ? 3u : 2u;
            poolInfo.poolSizeCount = batchMode ? 2u : 1u;
            poolInfo.pPoolSizes = poolSizes.data();
            check(vkCreateDescriptorPool(device, &poolInfo, nullptr, &descriptorPool), "Create descriptor pool");
            VkDescriptorSetAllocateInfo allocate { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            allocate.descriptorPool = descriptorPool;
            allocate.descriptorSetCount = batchMode ? 3u : 2u;
            allocate.pSetLayouts = setLayouts.data();
            check(vkAllocateDescriptorSets(device, &allocate, sets.data()), "Allocate descriptor sets");
            std::array<VkDescriptorImageInfo, 6> imageInfos {};
            std::array<VkWriteDescriptorSet, 6> writes {};
            const uint32_t descriptorCount = atlas ? 6u : 5u;
            for (std::size_t index = 0u; index < descriptorCount; ++index)
            {
                imageInfos[index] = { VK_NULL_HANDLE, index == 5u ? fontAtlasImage.view : images[index].view,
                    VK_IMAGE_LAYOUT_GENERAL };
                writes[index] = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                writes[index].dstSet = sets[index == 0u ? 0u : 1u];
                writes[index].dstBinding = bindingNumbers[index];
                writes[index].descriptorCount = 1u;
                writes[index].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
                writes[index].pImageInfo = &imageInfos[index];
            }
            vkUpdateDescriptorSets(device, descriptorCount, writes.data(), 0u, nullptr);
            if (batchMode)
            {
                VkDescriptorBufferInfo glyphInfo { glyphRecords.handle, 0u, VK_WHOLE_SIZE };
                VkWriteDescriptorSet glyphWrite { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                glyphWrite.dstSet = sets[2];
                glyphWrite.dstBinding = 0u;
                glyphWrite.descriptorCount = 1u;
                glyphWrite.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
                glyphWrite.pBufferInfo = &glyphInfo;
                vkUpdateDescriptorSets(device, 1u, &glyphWrite, 0u, nullptr);
            }
        }

        VkPipeline create_pipeline(const std::vector<uint32_t>& spirv, const uint32_t* specialisation)
        {
            VkShaderModuleCreateInfo moduleInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
            moduleInfo.pCode = spirv.data();
            VkShaderModule module = VK_NULL_HANDLE;
            check(vkCreateShaderModule(device, &moduleInfo, nullptr, &module), "Create shader module");
            VkSpecializationMapEntry map { 0u, 0u, sizeof(uint32_t) };
            VkSpecializationInfo specialisationInfo { 1u, &map, sizeof(uint32_t), specialisation };
            VkComputePipelineCreateInfo pipelineInfo { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            pipelineInfo.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO };
            pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
            pipelineInfo.stage.module = module;
            pipelineInfo.stage.pName = "main";
            pipelineInfo.stage.pSpecializationInfo = specialisation ? &specialisationInfo : nullptr;
            pipelineInfo.layout = layout;
            VkPipeline pipeline = VK_NULL_HANDLE;
            const VkResult result = vkCreateComputePipelines(device, VK_NULL_HANDLE,
                1u, &pipelineInfo, nullptr, &pipeline);
            vkDestroyShaderModule(device, module, nullptr);
            check(result, "Create compute pipeline");
            return pipeline;
        }

        VkImageMemoryBarrier image_barrier(Image image, VkImageLayout oldLayout, VkImageLayout newLayout,
            VkAccessFlags source, VkAccessFlags destination)
        {
            VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
            barrier.srcAccessMask = source;
            barrier.dstAccessMask = destination;
            barrier.oldLayout = oldLayout;
            barrier.newLayout = newLayout;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = image.handle;
            barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0u, 1u, 0u, 1u };
            return barrier;
        }

        void dispatch(VkPipeline pipeline, const Case& testCase, uint32_t phase, bool preserveStyle,
            bool synchronise = true, bool bindPipeline = true)
        {
            PushConstants constants = testCase.constants;
            constants.data[1] = phase | (preserveStyle ? testCase.constants.data[1] : 0u);
            constants.data[3] = testCase.originX | (testCase.originY << 16u);
            if (bindPipeline) vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0u, sizeof(constants), &constants);
            vkCmdDispatch(command, (testCase.dispatchWidth + 7u) / 8u,
                (testCase.dispatchHeight + 7u) / 8u, 1u);
            if (synchronise) compute_barrier();
        }

        void compute_barrier()
        {
            VkMemoryBarrier barrier { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
            barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0u, 1u, &barrier, 0u, nullptr, 0u, nullptr);
        }

        uint32_t width, height;
        VkDeviceSize imageBytes;
        VkInstance instance = VK_NULL_HANDLE;
        VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        VkPhysicalDeviceProperties properties {};
        VkPhysicalDeviceMemoryProperties memoryProperties {};
        uint32_t queueFamily = 0u, timestampBits = 0u;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer command = VK_NULL_HANDLE;
        VkFence fence = VK_NULL_HANDLE;
        VkQueryPool queries = VK_NULL_HANDLE;
        std::array<Image, 5> images {};
        const TextAtlasFixture* atlas = nullptr;
        Image fontAtlasImage;
        Buffer upload, readback, glyphUpload, glyphRecords;
        bool batchMode = false;
        std::array<VkDescriptorSetLayout, 3> setLayouts {};
        std::array<VkDescriptorSet, 3> sets {};
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkPipelineLayout layout = VK_NULL_HANDLE;
        VkPipeline baselinePipeline = VK_NULL_HANDLE;
        VkPipeline candidateOrdinary = VK_NULL_HANDLE;
        VkPipeline candidateShared = VK_NULL_HANDLE;
        VkPipeline shapePipeline = VK_NULL_HANDLE;
        bool initialised = false;
    };

    Case make_case(const std::string& name, uint32_t width, uint32_t height,
        float radius, uint32_t variant)
    {
        Case testCase;
        testCase.name = name;
        testCase.dispatchWidth = width;
        testCase.dispatchHeight = height;
        PushConstants& constants = testCase.constants;
        constants.rect = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) };
        constants.uvRect = { 0.45f, 4.0f, 0.0f, 0.0f };
        constants.color0 = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height) };
        constants.color1 = { 0.2f, 0.75f, 0.15f, 0.85f };
        constants.color2 = { 0.0f, 0.0f, 1.0f, 1.0f };
        constants.effect0 = { radius, 4.0f, variant % 2u == 0u ? 0.0f : 1.0f, 0.73f };
        constants.effect1 = { 3.0f, 0.75f, 0.0f, 0.0f };
        constants.data[0] = 2u | (0x5ba3d1u << 8u);
        if (variant == 1u)
        {
            constants.data[2] = 1u | staticBackdrop | externalBackdrop;
            testCase.originX = std::min(3u, width - 1u);
            testCase.originY = std::min(2u, height - 1u);
            testCase.dispatchWidth = width - testCase.originX;
            testCase.dispatchHeight = height - testCase.originY;
        }
        else if (variant >= 2u)
        {
            constants.data[1] = liquidFlag;
            constants.data[2] = 23u | (37u << 7u) | (11u << 14u) | (87u << 21u);
            if (variant == 2u || variant == 4u) constants.data[2] |= parentCap | staticBackdrop;
            if (variant >= 3u) constants.data[2] |= externalBackdrop;
            if (variant == 3u)
            {
                constants.data[0] = 3u | (0x5ba3d1u << 8u);
                constants.rect = { static_cast<float>(width) * 0.17f, static_cast<float>(height) * 0.13f,
                    static_cast<float>(width) * 0.67f, static_cast<float>(height) * 0.71f };
            }
            if (variant == 4u)
            {
                testCase.originX = width > 20u ? width - 19u : 0u;
                testCase.originY = height > 12u ? height - 11u : 0u;
                testCase.dispatchWidth = width - testCase.originX;
                testCase.dispatchHeight = height - testCase.originY;
                constants.rect = { static_cast<float>(testCase.originX), static_cast<float>(testCase.originY),
                    static_cast<float>(testCase.dispatchWidth), static_cast<float>(testCase.dispatchHeight) };
            }
        }
        return testCase;
    }

    bool compare(const Case& testCase, const Result& baseline, const Result& candidate,
        uint32_t width, uint32_t height, uint32_t tolerance = 0u)
    {
        std::size_t differences = 0u, first = 0u;
        uint32_t maximumDifference = 0u;
        for (std::size_t index = 0u; index < baseline.bytes.size(); ++index)
        {
            if (static_cast<uint32_t>(std::abs(static_cast<int>(baseline.bytes[index]) -
                static_cast<int>(candidate.bytes[index]))) <= tolerance) continue;
            if (differences++ == 0u) first = index;
            maximumDifference = std::max(maximumDifference, static_cast<uint32_t>(std::abs(
                static_cast<int>(baseline.bytes[index]) - static_cast<int>(candidate.bytes[index]))));
        }
        if (differences != 0u)
        {
            const std::size_t imageBytes = static_cast<std::size_t>(width) * height * 4u;
            const char* imageName = std::array { "scene", "scratch", "blur" }[first / imageBytes];
            const std::size_t pixel = (first % imageBytes) / 4u;
            std::cerr << "FAIL " << testCase.name << ": " << differences << " differing bytes, max delta "
                << maximumDifference << ", first " << imageName << " pixel(" << pixel % width << ','
                << pixel / width << ") channel " << first % 4u << ": "
                << static_cast<uint32_t>(baseline.bytes[first]) << " -> "
                << static_cast<uint32_t>(candidate.bytes[first]) << '\n';
            return false;
        }
        return true;
    }
}

#ifndef VIBRANCE_SHADER_HARNESS_NO_MAIN
int main(int argc, char** argv)
{
    const bool shape = argc == 4 && std::string(argv[3]) == "shape";
    const bool text = argc == 4 && std::string(argv[3]) == "text";
    const bool textBatch = argc == 4 && std::string(argv[3]) == "text-batch";
    const bool textCrop = argc == 4 && std::string(argv[3]) == "text-crop";
    const bool renderOrder = argc == 5 && std::string(argv[3]) == "render-order";
    const bool liquidCycle = argc == 4 && std::string_view(argv[3]) == "liquid-cycle";
    const bool foregroundGroup = argc == 4 && std::string_view(argv[3]) == "foreground-group";
    if (argc != 3 && !shape && !text && !textBatch && !textCrop && !renderOrder && !liquidCycle && !foregroundGroup)
    {
        std::cerr << "Usage: blur_shader_performance baseline.spv candidate.spv [shape|text|text-batch|text-crop|render-order shape.spv|liquid-cycle|foreground-group]\n";
        return 2;
    }
    try
    {
        const auto baseline = load_spirv(argv[1]);
        const auto candidate = load_spirv(argv[2]);
        bool passed = true;
        uint32_t cases = 0u;
        if (renderOrder)
        {
#if !defined(VIBRANCE_TEXT_FREETYPE)
            throw std::runtime_error("Mixed rendering comparison requires real FreeType font fixtures");
#endif
            const auto shapeSpirv = load_spirv(argv[4]);
            const auto atlas = text_atlas_fixture(std::getenv("VIBRANCE_SHADER_HARNESS_FONT"), false);
            const auto textFixtures = text_fixtures(atlas);
            const auto shapeFixtures = shape_fixtures();
            const auto colourFixture = std::find_if(textFixtures.begin(), textFixtures.end(), [](const auto& fixture) {
                return (fixture.constants.data[1] & (1u << 10u)) != 0u;
            });
            if (colourFixture == textFixtures.end()) throw std::runtime_error("Missing colour bitmap fixture");
            uint64_t operationCount = 0u;
            std::cout << "Font atlas: " << atlas.source << "; current shape and ordinary/batched text shaders\n";
            for (const auto extent : { std::array { 257u, 193u }, std::array { 641u, 389u } })
            {
                const uint32_t width = extent[0], height = extent[1];
                Harness harness(width, height, baseline, candidate, &atlas, true, &shapeSpirv);
                uint32_t randomState = 0x714529cbu;
                const auto random_unit = [&]() {
                    randomState ^= randomState << 13u;
                    randomState ^= randomState >> 17u;
                    randomState ^= randomState << 5u;
                    return static_cast<float>(randomState & 65535u) / 65535.0f;
                };
                for (uint32_t fixture = 0u; fixture < 76u; ++fixture)
                {
                    const bool grid = fixture < 12u;
                    const uint32_t count = grid ?
                        std::array { 2u, 4u, 14u, 64u, 65u, 255u, 256u, 257u, 300u, 511u, 512u, 768u }[fixture] :
                        17u + (fixture * 37u) % 287u;
                    const uint32_t columns = width / 32u, cells = columns * (height / 32u);
                    std::vector<MixedOperation> operations;
                    for (uint32_t index = 0u; index < count; ++index)
                    {
                        const bool foregroundText = grid ? index % 2u != 0u : random_unit() >= 0.37f;
                        const uint32_t cell = (index / 2u) % cells;
                        const float x = grid ? (cell % columns) * 32.0f + 3.137f :
                            random_unit() * (width + 23.0f) - 17.731f;
                        const float y = grid ? (cell / columns) * 32.0f + 2.793f :
                            random_unit() * (height + 19.0f) - 13.137f;
                        MixedOperation operation;
                        operation.foregroundText = foregroundText;
                        auto& draw = operation.draw;
                        draw.name = "mixed_" + std::to_string(fixture) + "_op_" + std::to_string(index);
                        float padding = 0.0f;
                        auto dispatchRect = std::array<float, 4> {};
                        if (foregroundText)
                        {
                            const auto& region = atlas.glyphs[index % atlas.glyphs.size()];
                            std::memcpy(&draw.constants, &textFixtures.front().constants, sizeof(PushConstants));
                            auto& value = draw.constants;
                            value.rect = { x, y, grid ? 22.731f : 3.173f + random_unit() * 49.0f,
                                grid ? 24.137f : 5.731f + random_unit() * 53.0f };
                            value.uvRect = { region.x / 512.0f, region.y / 512.0f,
                                (region.x + region.width) / 512.0f, (region.y + region.height) / 512.0f };
                            const int weight = static_cast<int>(index % 7u) * 137 - 411;
                            value.data[2] = (static_cast<uint32_t>(2048 + weight) << 2u) | 2u;
                            if (index % 11u == 0u)
                            {
                                value.data[1] |= 1u << 10u;
                                value.uvRect = colourFixture->constants.uvRect;
                            }
                            if (index % 5u == 0u) value.data[1] |= 1u;
                            if (index % 5u == 1u)
                            {
                                value.data[1] |= 1u << 1u;
                                value.effect0[1] = 1.731f;
                            }
                            if (index % 5u == 2u)
                            {
                                value.data[1] |= 1u << 15u;
                                value.color2 = { 6.0f, 5.73f, 0.83f, 4.13f };
                                value.effect1 = { x + 2.173f, y + 1.137f,
                                    value.rect[2] - 3.731f, value.rect[3] - 2.137f };
                            }
                            if (index % 5u == 3u)
                            {
                                value.data[1] |= 1u << 25u;
                                value.data[2] |= (137u << 14u) | (225u << 22u) | (2u << 30u);
                            }
                            if (index % 5u == 4u)
                            {
                                value.data[1] |= 1u << 5u;
                                value.effect1[3] = 2.137f;
                            }
                            const float outline = (value.data[1] & (1u << 1u)) != 0u ? value.effect0[1] : 0.0f;
                            padding = std::max({ std::max(weight / 256.0f, 0.0f), outline,
                                (value.data[1] & (1u << 5u)) != 0u ? value.effect1[3] : 0.0f }) + 2.0f;
                            dispatchRect = value.rect;
                            if ((value.data[1] & ((1u << 10u) | (1u << 5u))) == 0u)
                                if (const auto cropped = vibrance::renderer::sdf_foreground_bounds(value.rect,
                                    { static_cast<float>(region.width), static_cast<float>(region.height) },
                                    24.0f, value.effect0[0], weight / 256.0f, outline))
                                {
                                    dispatchRect = *cropped;
                                    padding = 0.0f;
                                }
                        }
                        else
                        {
                            std::memcpy(&draw.constants, &shapeFixtures[index % shapeFixtures.size()].constants,
                                sizeof(PushConstants));
                            auto& value = draw.constants;
                            value.rect = { x - 1.173f, y - 0.731f,
                                grid ? 26.137f : 5.731f + random_unit() * 59.0f,
                                grid ? 27.173f : 7.137f + random_unit() * 57.0f };
                            value.effect0 = { 5.137f, 0.731f, 0.793f, 0.73f };
                            // Retain all scalar/gradient/mask/transform paths,
                            // including partial workgroups beyond clip edges.
                            if ((value.data[1] & (1u << 15u)) != 0u)
                                value.effect1 = { x + 1.137f, y + 2.173f, value.rect[2] * 0.83f, value.rect[3] * 0.79f };
                            if ((value.data[1] & (1u << 5u)) != 0u) value.effect0[2] = 3.137f;
                            padding = value.effect0[1] + value.effect0[2];
                            if ((value.data[1] & (1u << 12u)) != 0u)
                                padding += std::sqrt(value.rect[2] * value.rect[2] + value.rect[3] * value.rect[3]) * 0.5f;
                            if (value.data[0] == 7u) padding += 32.0f;
                            dispatchRect = value.rect;
                        }
                        auto& value = draw.constants;
                        value.color0 = { (index % 19u + 0.5f) / 19.0f, 0.381372549f,
                            index % 3u == 0u ? 1.0f : 0.173f, 0.31f + index % 7u * 0.091f };
                        if (fixture % 7u == 0u) value.effect0[3] = 0.00137f;
                        const auto clipped = [](float coordinate, uint32_t limit, bool end) {
                            return static_cast<uint32_t>(std::clamp(end ? std::ceil(coordinate) : std::floor(coordinate),
                                0.0f, static_cast<float>(limit)));
                        };
                        draw.originX = clipped(dispatchRect[0] - padding, width, false);
                        draw.originY = clipped(dispatchRect[1] - padding, height, false);
                        const auto exactWidth = clipped(dispatchRect[0] + dispatchRect[2] + padding, width, true) - draw.originX;
                        const auto exactHeight = clipped(dispatchRect[1] + dispatchRect[3] + padding, height, true) - draw.originY;
                        draw.dispatchWidth = foregroundText ? exactWidth :
                            renderer2d_dispatch_axis_coverage(draw.originX, exactWidth, width);
                        draw.dispatchHeight = foregroundText ? exactHeight :
                            renderer2d_dispatch_axis_coverage(draw.originY, exactHeight, height);
                        if (foregroundText) value.data[0] = draw.dispatchWidth | (draw.dispatchHeight << 16u);
                        operations.push_back(operation);
                    }
                    Case group;
                    group.name = "mixed_render_order_" + std::to_string(width) + "x" + std::to_string(height) +
                        "_fixture_" + std::to_string(fixture) + "_ops_" + std::to_string(count);
                    const auto original = harness.run(group, false, 1u, false, false, false, false,
                        nullptr, true, 0u, nullptr, &operations);
                    const auto changed = harness.run(group, true, 1u, false, false, false, false,
                        nullptr, true, 0u, nullptr, &operations);
                    passed &= compare(group, original, changed, width, height);
                    operationCount += operations.size();
                    ++cases;
                }
            }
            std::cout << "Mixed render order pixel equivalence: " << cases << " traces, " << operationCount
                << " shape/text operations, 256-op chunks, complete RGBA8 scene/scratch/blur images, "
                << (passed ? "PASS" : "FAIL") << '\n';
            if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
            return passed && validationErrors.load() == 0u ? 0 : 1;
        }
        if (textCrop)
        {
#if !defined(VIBRANCE_TEXT_FREETYPE)
            throw std::runtime_error("Text crop comparison requires real FreeType font fixtures");
#endif
            const auto atlas = text_atlas_fixture(std::getenv("VIBRANCE_SHADER_HARNESS_FONT"), false);
            const auto fixtures = text_crop_fixtures(atlas);
            const auto make_case = [](const TextCropFixture& fixture, uint32_t width, uint32_t height,
                bool crop, bool& trimmed) {
                Case testCase;
                testCase.name = fixture.fixture.name;
                std::memcpy(&testCase.constants, &fixture.fixture.constants, sizeof(PushConstants));
                auto& value = testCase.constants;
                const uint32_t flags = value.data[1];
                const float weight = (static_cast<int>((value.data[2] >> 2u) & 4095u) - 2048) / 256.0f;
                const float outline = (flags & (1u << 1u)) != 0u ? std::max(value.effect0[1], 0.0f) : 0.0f;
                const float blur = (flags & (1u << 5u)) != 0u && (flags & (1u << 15u)) == 0u ?
                    std::max(value.effect1[3], 0.0f) : 0.0f;
                float padding = std::max({ std::max(weight, 0.0f), outline, blur }) + 2.0f;
                if ((value.data[2] & 3u) != 2u)
                {
                    const float shadow = (flags & (1u << 15u)) == 0u ?
                        std::max(std::abs(value.effect1[0]), std::abs(value.effect1[1])) +
                            std::max(value.effect1[2], 0.0f) : 0.0f;
                    padding = std::max({ std::max(weight, 0.0f), outline, blur,
                        std::max(value.effect0[2], 0.0f), shadow }) + 2.0f;
                }
                auto rect = value.rect;
                trimmed = false;
                if (crop && fixture.trustedUv && (value.data[2] & 3u) == 2u && (flags & (1u << 8u)) != 0u &&
                    (flags & ((1u << 10u) | (1u << 5u))) == 0u)
                {
                    if (const auto bounds = vibrance::renderer::sdf_foreground_bounds(rect,
                        fixture.atlasGlyphSize, fixture.sdfPadding, value.effect0[0], weight, outline))
                    {
                        rect = *bounds;
                        padding = 0.0f;
                        trimmed = true;
                    }
                }
                const auto origin = [](float coordinate, uint32_t limit) {
                    return static_cast<uint32_t>(std::clamp(std::floor(coordinate), 0.0f,
                        static_cast<float>(limit)));
                };
                const auto end = [](float coordinate, uint32_t limit) {
                    return static_cast<uint32_t>(std::clamp(std::ceil(coordinate), 0.0f,
                        static_cast<float>(limit)));
                };
                testCase.originX = origin(rect[0] - padding, width);
                testCase.originY = origin(rect[1] - padding, height);
                testCase.dispatchWidth = end(rect[0] + rect[2] + padding, width) - testCase.originX;
                testCase.dispatchHeight = end(rect[1] + rect[3] + padding, height) - testCase.originY;
                value.data[0] = testCase.dispatchWidth | (testCase.dispatchHeight << 16u);
                return testCase;
            };
            std::cout << "Font atlas: " << atlas.source << "; current sixteen-sample foreground shader\n";
            uint32_t trimmedCases = 0u;
            uint64_t fullPixels = 0u, croppedPixels = 0u;
            for (const auto extent : { std::array { 208u, 152u }, std::array { 317u, 193u } })
            {
                Harness harness(extent[0], extent[1], baseline, candidate, &atlas);
                for (const auto& fixture : fixtures)
                {
                    bool ignored = false, trimmed = false;
                    const auto fullCase = make_case(fixture, extent[0], extent[1], false, ignored);
                    const auto croppedCase = make_case(fixture, extent[0], extent[1], true, trimmed);
                    const auto original = harness.run(fullCase, false, 1u, false, false, false, true);
                    const auto changed = harness.run(croppedCase, true, 1u, false, false, false, true);
                    passed &= compare(fullCase, original, changed, extent[0], extent[1]);
                    fullPixels += static_cast<uint64_t>(fullCase.dispatchWidth) * fullCase.dispatchHeight;
                    croppedPixels += static_cast<uint64_t>(croppedCase.dispatchWidth) * croppedCase.dispatchHeight;
                    trimmedCases += trimmed ? 1u : 0u;
                    ++cases;
                }
            }
            std::cout << "Text foreground crop pixel equivalence: " << cases << " FreeType glyph and fallback fixtures, "
                << trimmedCases << " cropped, complete RGBA8 scene/scratch/blur images, "
                << (passed ? "PASS" : "FAIL") << '\n';
            std::cout << "Foreground dispatch pixels: full=" << fullPixels << ", cropped=" << croppedPixels
                << ", reduction=" << (1.0 - static_cast<double>(croppedPixels) / fullPixels) * 100.0 << "%\n";
            if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
            return passed && validationErrors.load() == 0u ? 0 : 1;
        }
        if (foregroundGroup)
        {
            for (const auto extent : { std::array { 160u, 60u }, std::array { 67u, 39u } })
            {
                Harness harness(extent[0], extent[1], baseline, candidate);
                for (const float radius : { 0.0f, 1.0f, 7.0f, 15.0f, 30.0f, 40.0f })
                {
                    auto oldCase = make_case("foreground_group_radius_" + std::to_string(radius),
                        extent[0], extent[1], radius, 0u);
                    oldCase.constants.data[2] = 1u << 28u;
                    auto newCase = oldCase;
                    newCase.constants.data[2] = 0u;
                    newCase.constants.data[1] = 1u << 26u;
                    const auto original = harness.run(oldCase, false);
                    const auto changed = harness.run(newCase, true);
                    if (!compare(newCase, original, changed, extent[0], extent[1])) return 1;
                    ++cases;
                }
            }
            std::cout << "Foreground blur group: " << cases << " GPU fixtures, unchanged pixels, PASS\n";
            if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
            return validationErrors.load() == 0 ? 0 : 1;
        }
        if (liquidCycle)
        {
            // Compare against the established liquid shader through every
            // phase value, including the bit-28 boundary previously omitted
            // by fixtures that always used phase 87.
            for (const auto extent : { std::array { 160u, 60u }, std::array { 67u, 39u } })
            {
                Harness harness(extent[0], extent[1], baseline, candidate);
                for (uint32_t phase = 0; phase < 256; ++phase)
                {
                    for (uint32_t variant = 2; variant <= 5; ++variant)
                    {
                        auto testCase = make_case("liquid-cycle", extent[0], extent[1], 7.0f, variant);
                        testCase.name = "liquid_phase_" + std::to_string(phase) + "_variant_" + std::to_string(variant);
                        testCase.constants.data[2] &= ~(255u << 21u);
                        testCase.constants.data[2] |= phase << 21u;
                        const auto original = harness.run(testCase, false);
                        const auto changed = harness.run(testCase, true);
                        // The optimised optical path can round one RGBA8 step
                        // differently from the original arithmetic ordering.
                        if (!compare(testCase, original, changed, extent[0], extent[1], 1u)) return 1;
                        ++cases;
                    }
                }
            }
            std::cout << "Liquid animation cycle: " << cases << " GPU fixtures, all 256 phases, PASS\n";
            if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
            return validationErrors.load() == 0 ? 0 : 1;
        }
        if (textBatch)
        {
            const auto atlas = text_atlas_fixture(std::getenv("VIBRANCE_SHADER_HARNESS_FONT"));
            const auto fixtures = text_fixtures(atlas);
            const auto ensure_disjoint = [](const std::vector<Case>& glyphs) {
                for (std::size_t left = 0u; left < glyphs.size(); ++left)
                    for (std::size_t right = left + 1u; right < glyphs.size(); ++right)
                        if (glyphs_overlap(glyphs[left], glyphs[right]))
                            throw std::runtime_error("Batched text fixtures overlap");
            };
            std::cout << "Font atlas: " << atlas.source << "; device-local 128-byte glyph SSBO\n";
            constexpr uint32_t columns = 8u, cellWidth = 224u, cellHeight = 176u, blockSize = 64u;
            for (const auto extent : { std::array { 208u, 152u }, std::array { 193u, 147u } })
            {
                Harness harness(columns * cellWidth, 8u * cellHeight, baseline, candidate, &atlas, true);
                for (std::size_t start = 0u; start < fixtures.size(); start += blockSize)
                {
                    std::vector<Case> glyphs;
                    for (std::size_t index = start; index < std::min(start + blockSize, fixtures.size()); ++index)
                    {
                        const auto& fixture = fixtures[index];
                        const uint32_t local = static_cast<uint32_t>(index - start);
                        const uint32_t x = local % columns * cellWidth, y = local / columns * cellHeight;
                        Case glyph;
                        glyph.name = fixture.name;
                        std::memcpy(&glyph.constants, &fixture.constants, sizeof(PushConstants));
                        glyph.constants.rect[0] += x;
                        glyph.constants.rect[1] += y;
                        if ((glyph.constants.data[1] & (1u << 15u)) != 0u)
                        {
                            glyph.constants.effect1[0] += x;
                            glyph.constants.effect1[1] += y;
                        }
                        glyph.originX = x + fixture.originX;
                        glyph.originY = y + fixture.originY;
                        glyph.dispatchWidth = std::min(fixture.dispatchWidth ? fixture.dispatchWidth : extent[0],
                            extent[0] - fixture.originX);
                        glyph.dispatchHeight = std::min(fixture.dispatchHeight ? fixture.dispatchHeight : extent[1],
                            extent[1] - fixture.originY);
                        glyph.constants.data[0] = glyph.dispatchWidth | (glyph.dispatchHeight << 16u);
                        glyphs.push_back(glyph);
                    }
                    ensure_disjoint(glyphs);
                    Case group;
                    group.name = "batch_extent_" + std::to_string(extent[0]) + "_fixtures_" +
                        std::to_string(start) + ".." + std::to_string(start + glyphs.size() - 1u);
                    const uint32_t glyphOffset = (start / blockSize) % 2u == 0u ? 5u : 19u;
                    const auto original = harness.run(group, false, 1u, false, false, false, true,
                        &glyphs, false, glyphOffset);
                    const auto changed = harness.run(group, true, 1u, false, false, false, true,
                        &glyphs, false, glyphOffset);
                    passed &= compare(group, original, changed, columns * cellWidth, 8u * cellHeight);
                    cases += static_cast<uint32_t>(glyphs.size());
                }
            }
            std::cout << "Batched text pixel equivalence: " << cases << " translated disjoint fixtures, "
                << (passed ? "PASS" : "FAIL") << '\n';
            const auto colourFixture = std::find_if(fixtures.begin(), fixtures.end(), [](const auto& fixture) {
                return (fixture.constants.data[1] & (1u << 10u)) != 0u;
            });
            if (colourFixture == fixtures.end()) throw std::runtime_error("Missing colour bitmap fixture");
            for (uint32_t variant = 0u; variant < 4u; ++variant)
            {
                constexpr uint32_t width = 128u, height = 64u;
                Harness harness(width, height, baseline, candidate, &atlas, true);
                std::vector<Case> glyphs;
                const std::array<std::array<uint32_t, 4>, 5> bounds {{
                    { 8u, 8u, 32u, 32u }, { 21u, 9u, 34u, 32u },
                    { 64u, 9u, 32u, 32u }, { 77u, 10u, 34u, 32u },
                    { 35u, 12u, 55u, 26u }
                }};
                for (uint32_t index = 0u; index < bounds.size(); ++index)
                {
                    Case glyph;
                    glyph.name = "overlap_glyph_" + std::to_string(index);
                    const auto& fixture = (index + variant) % 2u == 0u ? fixtures.front() : *colourFixture;
                    std::memcpy(&glyph.constants, &fixture.constants, sizeof(PushConstants));
                    const auto& box = bounds[index];
                    glyph.originX = box[0];
                    glyph.originY = box[1];
                    glyph.dispatchWidth = box[2];
                    glyph.dispatchHeight = box[3];
                    glyph.constants.data[0] = glyph.dispatchWidth | (glyph.dispatchHeight << 16u);
                    glyph.constants.rect = { box[0] + 0.137f, box[1] + 0.793f,
                        box[2] - 1.317f, box[3] - 1.731f };
                    glyph.constants.color0 = { (index + 1.5f) / 7.0f, 0.381372549f,
                        (5.5f - index) / 7.0f, 0.31f + index * 0.113f };
                    glyph.constants.data[2] |= 2u;
                    if (variant == 2u)
                    {
                        glyph.constants.data[1] |= 1u << 15u;
                        glyph.constants.color2 = { 6.0f, 5.73f, 0.83f, 4.13f };
                        glyph.constants.effect1 = { box[0] + 2.173f, box[1] + 1.137f,
                            box[2] - 3.731f, box[3] - 2.137f };
                    }
                    if (variant == 3u)
                    {
                        glyph.constants.data[1] |= 1u << 25u;
                        glyph.constants.data[2] |= (128u << 14u) | (205u << 22u) | (2u << 30u);
                    }
                    glyphs.push_back(glyph);
                }
                const TextWaves waves { { 0u, 2u }, { 1u, 3u }, { 4u } };
                Case group;
                group.name = "overlap_dependency_waves_" + std::to_string(variant);
                const auto original = harness.run(group, false, 1u, false, false, false, true,
                    &glyphs, true, 23u, &waves);
                const auto changed = harness.run(group, true, 1u, false, false, false, true,
                    &glyphs, true, 23u, &waves);
                passed &= compare(group, original, changed, width, height);
            }
            std::cout << "Overlapping text: 4 mixed SDF/colour glyph chains, 3 ordered waves, "
                << (passed ? "PASS" : "FAIL") << '\n';
            if (!passed)
            {
                if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
                return passed && validationErrors.load() == 0u ? 0 : 1;
            }
            constexpr uint32_t repetitions = 32u, measurements = 15u;
            for (uint32_t glyphCount : { 2u, 4u, 8u, 14u, 65u, 100u, 129u, 300u })
            {
                const uint32_t gridColumns = glyphCount >= 65u ? (glyphCount == 300u ? 20u : 10u) : glyphCount;
                const uint32_t rows = (glyphCount + gridColumns - 1u) / gridColumns;
                const uint32_t width = gridColumns * 48u, height = rows * 48u;
                Harness harness(width, height, baseline, candidate, &atlas, true);
                std::vector<Case> glyphs;
                uint32_t ordinaryWorkgroups = 0u, maximumWidth = 0u, maximumHeight = 0u;
                for (uint32_t index = 0u; index < glyphCount; ++index)
                {
                    const auto& region = atlas.glyphs[index % atlas.glyphs.size()];
                    const float x = static_cast<float>(index % gridColumns * 48u);
                    const float y = static_cast<float>(index / gridColumns * 48u);
                    Case glyph;
                    glyph.name = "dense_glyph_" + std::to_string(index);
                    std::memcpy(&glyph.constants, &fixtures.front().constants, sizeof(PushConstants));
                    auto& value = glyph.constants;
                    value.rect = { x + 8.137f + index % 4u * 0.173f, y + 7.793f + index % 3u * 0.191f,
                        index % 5u == 0u ? 4.173f : 11.37f + index % 5u * 2.17f,
                        index % 5u == 0u ? 7.731f : 16.17f + index % 5u * 3.13f };
                    value.uvRect = { region.x / 512.0f, region.y / 512.0f,
                        (region.x + region.width) / 512.0f, (region.y + region.height) / 512.0f };
                    if (index % 7u == 0u)
                    {
                        value.uvRect = colourFixture->constants.uvRect;
                        value.data[1] |= 1u << 10u;
                    }
                    value.color0 = { (index % 19u + 0.5f) / 19.0f, 0.381372549f,
                        index % 3u == 0u ? 1.0f : 0.173f, 0.37f + index % 7u * 0.073f };
                    value.data[2] = ((1920u + index % 7u * 43u) << 2u) | 2u;
                    if (index % 6u == 1u) value.data[1] |= 1u;
                    if (index % 6u == 2u)
                    {
                        value.data[1] |= 1u << 1u;
                        value.effect0[1] = 1.137f;
                    }
                    if (index % 6u == 3u)
                    {
                        value.data[1] |= 1u << 5u;
                        value.effect1[3] = 2.137f;
                    }
                    if (index % 6u == 4u)
                    {
                        value.data[1] |= 1u << 15u;
                        value.effect1 = { x + 10.173f, y + 9.137f, 18.731f, 21.137f };
                        value.color2 = { 6.0f, 5.73f, 0.83f, 4.13f };
                    }
                    if (index % 6u == 5u)
                    {
                        value.data[1] |= 1u << 25u;
                        value.data[2] |= (128u << 14u) | (205u << 22u) | (2u << 30u);
                    }
                    glyph.originX = static_cast<uint32_t>(std::floor(value.rect[0] - 5.0f));
                    glyph.originY = static_cast<uint32_t>(std::floor(value.rect[1] - 5.0f));
                    glyph.dispatchWidth = static_cast<uint32_t>(std::ceil(value.rect[0] + value.rect[2] + 5.0f)) - glyph.originX;
                    glyph.dispatchHeight = static_cast<uint32_t>(std::ceil(value.rect[1] + value.rect[3] + 5.0f)) - glyph.originY;
                    value.data[0] = glyph.dispatchWidth | (glyph.dispatchHeight << 16u);
                    ordinaryWorkgroups += (glyph.dispatchWidth + 7u) / 8u * ((glyph.dispatchHeight + 7u) / 8u);
                    maximumWidth = std::max(maximumWidth, glyph.dispatchWidth);
                    maximumHeight = std::max(maximumHeight, glyph.dispatchHeight);
                    glyphs.push_back(glyph);
                }
                ensure_disjoint(glyphs);
                Case group;
                group.name = "dense_glyphs_" + std::to_string(glyphCount);
                const auto original = harness.run(group, false, 1u, false, false, false, true,
                    &glyphs, true, 17u);
                const auto changed = harness.run(group, true, 1u, false, false, false, true,
                    &glyphs, true, 17u);
                passed &= compare(group, original, changed, width, height);
                std::cout << "Batch split equivalence " << glyphCount << " glyphs, "
                    << (glyphCount + maximumTextBatchSize - 1u) / maximumTextBatchSize
                    << " dispatches, firstGlyph=17: " << (passed ? "PASS" : "FAIL") << '\n';
                if (equivalence_only_requested()) continue;
                uint32_t batchedWorkgroups = 0u;
                for (std::size_t start = 0u; start < glyphs.size(); start += maximumTextBatchSize)
                {
                    uint32_t chunkWidth = 0u, chunkHeight = 0u;
                    const auto end = std::min(start + maximumTextBatchSize, glyphs.size());
                    for (std::size_t index = start; index < end; ++index)
                    {
                        chunkWidth = std::max(chunkWidth, glyphs[index].dispatchWidth);
                        chunkHeight = std::max(chunkHeight, glyphs[index].dispatchHeight);
                    }
                    batchedWorkgroups += (chunkWidth + 7u) / 8u * ((chunkHeight + 7u) / 8u) *
                        static_cast<uint32_t>(end - start);
                }
                std::cout << "Batch geometry " << glyphCount << " glyphs: ordinary=" << ordinaryWorkgroups
                    << " workgroups, batched=" << batchedWorkgroups << ", padding overhead="
                    << (static_cast<double>(batchedWorkgroups) / ordinaryWorkgroups - 1.0) * 100.0 << "%\n";
                for (bool perDrawBarriers : { false, true })
                {
                    for (uint32_t warmup = 0u; warmup < 3u; ++warmup)
                    {
                        harness.run(group, false, repetitions, false, false, false, true, &glyphs, perDrawBarriers);
                        harness.run(group, true, repetitions, false, false, false, true, &glyphs, perDrawBarriers);
                    }
                    std::vector<double> originalSamples, changedSamples;
                    for (uint32_t measurement = 0u; measurement < measurements; ++measurement)
                    {
                        Result original, changed;
                        if (measurement % 2u == 0u)
                        {
                            original = harness.run(group, false, repetitions, false, false, false, true, &glyphs, perDrawBarriers);
                            changed = harness.run(group, true, repetitions, false, false, false, true, &glyphs, perDrawBarriers);
                        }
                        else
                        {
                            changed = harness.run(group, true, repetitions, false, false, false, true, &glyphs, perDrawBarriers);
                            original = harness.run(group, false, repetitions, false, false, false, true, &glyphs, perDrawBarriers);
                        }
                        passed &= compare(group, original, changed, width, height);
                        originalSamples.push_back(original.milliseconds);
                        changedSamples.push_back(changed.milliseconds);
                    }
                    std::sort(originalSamples.begin(), originalSamples.end());
                    std::sort(changedSamples.begin(), changedSamples.end());
                    std::cout << std::fixed << std::setprecision(6) << "GPU text batch " << glyphCount
                        << ", baseline barriers=" << (perDrawBarriers ? "per draw" : "per batch")
                        << ": baseline median=" << originalSamples[measurements / 2u]
                        << " ms, candidate median=" << changedSamples[measurements / 2u]
                        << " ms, median change=" << (changedSamples[measurements / 2u] /
                            originalSamples[measurements / 2u] - 1.0) * 100.0 << "% (" << measurements
                        << " interleaved batches x " << repetitions << " repetitions)\n";
                    std::cout << "  baseline range=" << originalSamples.front() << ".." << originalSamples.back()
                        << "; candidate range=" << changedSamples.front() << ".." << changedSamples.back() << '\n';
                }
            }
            if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
            return passed && validationErrors.load() == 0u ? 0 : 1;
        }
        if (text)
        {
            const auto atlas = text_atlas_fixture(std::getenv("VIBRANCE_SHADER_HARNESS_FONT"));
            const auto fixtures = text_fixtures(atlas);
            std::cout << "Font atlas: " << atlas.source << ", " << atlas.glyphs.size()
                << " glyphs, logical " << atlas.side << 'x' << atlas.side << ", packed "
                << atlas.side / 4u << 'x' << atlas.side << " RGBA8\n";
            const auto make_text_case = [](const TextFixture& fixture, uint32_t width, uint32_t height,
                bool tightDispatch = false) {
                Case testCase;
                testCase.name = fixture.name;
                std::memcpy(&testCase.constants, &fixture.constants, sizeof(PushConstants));
                testCase.originX = fixture.originX;
                testCase.originY = fixture.originY;
                testCase.dispatchWidth = fixture.dispatchWidth ? fixture.dispatchWidth : width;
                testCase.dispatchHeight = fixture.dispatchHeight ? fixture.dispatchHeight : height;
                if (tightDispatch)
                {
                    const auto& value = fixture.constants;
                    const float glyphPad = std::max({ value.effect0[1], value.effect0[2], value.effect1[3] });
                    const float shadowPad = std::max(std::abs(value.effect1[0]), std::abs(value.effect1[1])) +
                        std::max(value.effect1[2], 0.0f);
                    const float pad = std::max(glyphPad, shadowPad) + 3.0f;
                    testCase.originX = static_cast<uint32_t>(std::max(std::floor(value.rect[0] - pad), 0.0f));
                    testCase.originY = static_cast<uint32_t>(std::max(std::floor(value.rect[1] - pad), 0.0f));
                    const uint32_t endX = static_cast<uint32_t>(std::min(std::ceil(value.rect[0] + value.rect[2] + pad),
                        static_cast<float>(width)));
                    const uint32_t endY = static_cast<uint32_t>(std::min(std::ceil(value.rect[1] + value.rect[3] + pad),
                        static_cast<float>(height)));
                    testCase.dispatchWidth = endX - testCase.originX;
                    testCase.dispatchHeight = endY - testCase.originY;
                }
                testCase.constants.data[0] = testCase.dispatchWidth | (testCase.dispatchHeight << 16u);
                return testCase;
            };
            for (const auto extent : { std::array { 208u, 152u }, std::array { 193u, 147u } })
            {
                Harness harness(extent[0], extent[1], baseline, candidate, &atlas);
                for (const auto& fixture : fixtures)
                {
                    const auto testCase = make_text_case(fixture, extent[0], extent[1]);
                    const auto original = harness.run(testCase, false, 1u, false, false, false, true);
                    const auto changed = harness.run(testCase, true, 1u, false, false, false, true);
                    passed &= compare(testCase, original, changed, extent[0], extent[1]);
                    ++cases;
                }
            }
            std::cout << "Text pixel equivalence: " << cases << " fixtures, complete RGBA8 scene/scratch/blur images, "
                << (passed ? "PASS" : "FAIL") << '\n';
            if (!passed || equivalence_only_requested())
            {
                if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
                return passed && validationErrors.load() == 0u ? 0 : 1;
            }
            constexpr uint32_t width = 208u, height = 152u, repetitions = 64u, measurements = 15u;
            Harness performance(width, height, baseline, candidate, &atlas);
            for (const auto& fixture : fixtures)
            {
                if (!fixture.benchmark) continue;
                const auto testCase = make_text_case(fixture, width, height, true);
                for (uint32_t warmup = 0u; warmup < 3u; ++warmup)
                {
                    performance.run(testCase, false, repetitions, false, false, false, true);
                    performance.run(testCase, true, repetitions, false, false, false, true);
                }
                double baselineSum = 0.0, candidateSum = 0.0;
                std::vector<double> baselineSamples, candidateSamples;
                for (uint32_t measurement = 0u; measurement < measurements; ++measurement)
                {
                    Result original, changed;
                    if (measurement % 2u == 0u)
                    {
                        original = performance.run(testCase, false, repetitions, false, false, false, true);
                        changed = performance.run(testCase, true, repetitions, false, false, false, true);
                    }
                    else
                    {
                        changed = performance.run(testCase, true, repetitions, false, false, false, true);
                        original = performance.run(testCase, false, repetitions, false, false, false, true);
                    }
                    passed &= compare(testCase, original, changed, width, height);
                    baselineSum += original.milliseconds;
                    candidateSum += changed.milliseconds;
                    baselineSamples.push_back(original.milliseconds);
                    candidateSamples.push_back(changed.milliseconds);
                }
                std::sort(baselineSamples.begin(), baselineSamples.end());
                std::sort(candidateSamples.begin(), candidateSamples.end());
                const double originalMean = baselineSum / measurements;
                const double changedMean = candidateSum / measurements;
                std::cout << std::fixed << std::setprecision(6) << "GPU text " << testCase.name << " ("
                    << testCase.dispatchWidth << 'x' << testCase.dispatchHeight << ")"
                    << ": baseline=" << originalMean << " ms, candidate=" << changedMean << " ms, change="
                    << (changedMean / originalMean - 1.0) * 100.0 << "% (" << measurements
                    << " interleaved batches x " << repetitions << " repetitions)\n";
                std::cout << "  baseline median=" << baselineSamples[measurements / 2u]
                    << " ms, range=" << baselineSamples.front() << ".." << baselineSamples.back()
                    << "; candidate median=" << candidateSamples[measurements / 2u]
                    << " ms, range=" << candidateSamples.front() << ".." << candidateSamples.back()
                    << "; median change=" << (candidateSamples[measurements / 2u] /
                        baselineSamples[measurements / 2u] - 1.0) * 100.0 << "%\n";
            }
            if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
            return passed && validationErrors.load() == 0u ? 0 : 1;
        }
        if (shape)
        {
            const auto fixtures = shape_fixtures();
            const auto make_shape_case = [](const ShapeFixture& fixture, uint32_t width, uint32_t height) {
                Case testCase;
                testCase.name = fixture.name;
                std::memcpy(&testCase.constants, &fixture.constants, sizeof(PushConstants));
                testCase.dispatchWidth = width;
                testCase.dispatchHeight = height;
                return testCase;
            };
            for (const auto extent : { std::array { 640u, 512u }, std::array { 637u, 509u } })
            {
                Harness harness(extent[0], extent[1], baseline, candidate);
                for (const auto& fixture : fixtures)
                {
                    const auto testCase = make_shape_case(fixture, extent[0], extent[1]);
                    const auto original = harness.run(testCase, false, 1u, false, false, true);
                    const auto changed = harness.run(testCase, true, 1u, false, false, true);
                    passed &= compare(testCase, original, changed, extent[0], extent[1]);
                    ++cases;
                }
            }
            std::cout << "Shape pixel equivalence: " << cases << " fixtures, "
                << (passed ? "PASS" : "FAIL") << '\n';
            if (equivalence_only_requested())
            {
                if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
                return passed && validationErrors.load() == 0u ? 0 : 1;
            }
            constexpr uint32_t width = 640u, height = 512u, repetitions = 32u, measurements = 15u;
            Harness performance(width, height, baseline, candidate);
            for (std::size_t fixtureIndex : { 0u, 1u, 3u })
            {
                const auto testCase = make_shape_case(fixtures[fixtureIndex], width, height);
                for (uint32_t warmup = 0u; warmup < 3u; ++warmup)
                {
                    performance.run(testCase, false, repetitions, false, false, true);
                    performance.run(testCase, true, repetitions, false, false, true);
                }
                double baselineSum = 0.0, candidateSum = 0.0;
                std::vector<double> baselineSamples, candidateSamples;
                for (uint32_t measurement = 0u; measurement < measurements; ++measurement)
                {
                    Result original, changed;
                    if (measurement % 2u == 0u)
                    {
                        original = performance.run(testCase, false, repetitions, false, false, true);
                        changed = performance.run(testCase, true, repetitions, false, false, true);
                    }
                    else
                    {
                        changed = performance.run(testCase, true, repetitions, false, false, true);
                        original = performance.run(testCase, false, repetitions, false, false, true);
                    }
                    passed &= compare(testCase, original, changed, width, height);
                    baselineSum += original.milliseconds;
                    candidateSum += changed.milliseconds;
                    baselineSamples.push_back(original.milliseconds);
                    candidateSamples.push_back(changed.milliseconds);
                }
                std::sort(baselineSamples.begin(), baselineSamples.end());
                std::sort(candidateSamples.begin(), candidateSamples.end());
                const double originalMean = baselineSum / measurements;
                const double changedMean = candidateSum / measurements;
                std::cout << std::fixed << std::setprecision(4) << "GPU shape " << testCase.name
                    << ": baseline=" << originalMean << " ms, candidate=" << changedMean << " ms, change="
                    << (changedMean / originalMean - 1.0) * 100.0 << "% (" << measurements
                    << " batches x " << repetitions << " repetitions)\n";
                std::cout << "  baseline median=" << baselineSamples[measurements / 2u]
                    << " ms, range=" << baselineSamples.front() << ".." << baselineSamples.back()
                    << "; candidate median=" << candidateSamples[measurements / 2u]
                    << " ms, range=" << candidateSamples.front() << ".." << candidateSamples.back() << '\n';
            }
            if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
            return passed && validationErrors.load() == 0u ? 0 : 1;
        }
        for (const auto extent : { std::array { 193u, 117u }, std::array { 5u, 3u } })
        {
            Harness harness(extent[0], extent[1], baseline, candidate);
            for (float radius : { 0.0f, 4.0f, 4.5f, 18.0f, 48.0f })
                for (uint32_t variant = 0u; variant < 5u; ++variant)
                {
                    const Case testCase = make_case("radius=" + std::to_string(radius) +
                        ", variant=" + std::to_string(variant), extent[0], extent[1], radius, variant);
                    const auto original = harness.run(testCase, false);
                    const auto changed = harness.run(testCase, true);
                    passed &= compare(testCase, original, changed, extent[0], extent[1]);
                    ++cases;
                }
        }
        std::cout << "Pixel equivalence: " << cases << " cases, complete scene/scratch/blur images, "
            << (passed ? "PASS" : "FAIL") << '\n';
        if (equivalence_only_requested())
        {
            if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
            return passed && validationErrors.load() == 0u ? 0 : 1;
        }

        constexpr uint32_t width = 640u, height = 360u, repetitions = 8u, measurements = 7u;
        Harness performance(width, height, baseline, candidate);
        for (float radius : { 18.0f, 48.0f })
        {
            Case testCase = make_case("performance radius=" + std::to_string(radius),
                width, height, radius, 0u);
            testCase.constants.data[0] = 1u;
            testCase.constants.effect0[2] = 1.0f;
            testCase.constants.effect0[3] = 1.0f;
            for (bool horizontalOnly : { true, false })
            {
                for (uint32_t warmup = 0u; warmup < 3u; ++warmup)
                {
                    performance.run(testCase, false, repetitions, horizontalOnly);
                    performance.run(testCase, true, repetitions, horizontalOnly);
                }
                double baselineSum = 0.0, candidateSum = 0.0;
                for (uint32_t measurement = 0u; measurement < measurements; ++measurement)
                {
                    // Alternate the first variant so clock drift does not always favour one shader.
                    Result original, changed;
                    if (measurement % 2u == 0u)
                    {
                        original = performance.run(testCase, false, repetitions, horizontalOnly);
                        changed = performance.run(testCase, true, repetitions, horizontalOnly);
                    }
                    else
                    {
                        changed = performance.run(testCase, true, repetitions, horizontalOnly);
                        original = performance.run(testCase, false, repetitions, horizontalOnly);
                    }
                    passed &= compare(testCase, original, changed, width, height);
                    baselineSum += original.milliseconds;
                    candidateSum += changed.milliseconds;
                }
                const double originalMean = baselineSum / measurements;
                const double changedMean = candidateSum / measurements;
                std::cout << std::fixed << std::setprecision(4) << "GPU "
                    << (horizontalOnly ? "horizontal" : "clear+horizontal+vertical+composite")
                    << " radius=" << radius << ": baseline=" << originalMean << " ms, shared="
                    << changedMean << " ms, change=" << (changedMean / originalMean - 1.0) * 100.0
                    << "% (" << measurements << " batches x " << repetitions << " repetitions)\n";
            }
        }
        if (validation_requested()) std::cout << "Validation errors: " << validationErrors.load() << '\n';
        return passed && validationErrors.load() == 0u ? 0 : 1;
    }
    catch (const std::exception& error)
    {
        std::cerr << "Offscreen Vulkan comparison failed: " << error.what() << '\n';
        return 2;
    }
}
#endif

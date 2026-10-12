#include <vulkan/vulkan.h>
#include <vibranceUI/renderer/viewport_fade.h>
#include <array>
#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    void check(VkResult result)
    {
        if (result != VK_SUCCESS) throw std::runtime_error("Vulkan operation failed: " + std::to_string(result));
    }
    struct Harness
    {
        static constexpr unsigned width = 8, height = 256, bytes = width * height * 4;
        VkInstance instance{};
        VkPhysicalDevice physical{};
        VkDevice device{};
        VkQueue queue{};
        VkCommandPool commands{};
        VkPipelineLayout layout{};
        VkPipeline pipeline{};
        VkDescriptorPool descriptors{};
        std::array<VkDescriptorSetLayout, 2> setLayouts{};
        std::array<VkDescriptorSet, 2> sets{};
        std::array<VkImage, 5> images{};
        std::array<VkImageView, 5> views{};
        std::array<VkDeviceMemory, 5> allocations{};
        VkBuffer buffer{};
        VkDeviceMemory bufferMemory{};
        void* pixels{};
        std::uint32_t memory_type(std::uint32_t bits, VkMemoryPropertyFlags flags)
        {
            VkPhysicalDeviceMemoryProperties properties;
            vkGetPhysicalDeviceMemoryProperties(physical, &properties);
            for (unsigned index = 0; index < properties.memoryTypeCount; ++index)
                if ((bits & (1u << index)) && (properties.memoryTypes[index].propertyFlags & flags) == flags)
                    return index;
            throw std::runtime_error("Required Vulkan memory is unavailable");
        }
        Harness(const char* shader)
        {
            VkApplicationInfo application { VK_STRUCTURE_TYPE_APPLICATION_INFO };
            application.apiVersion = VK_API_VERSION_1_3;
            VkInstanceCreateInfo instanceInfo { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
            instanceInfo.pApplicationInfo = &application;
            check(vkCreateInstance(&instanceInfo, nullptr, &instance));
            unsigned count = 0;
            check(vkEnumeratePhysicalDevices(instance, &count, nullptr));
            if (!count) throw std::runtime_error("No Vulkan device");
            std::vector<VkPhysicalDevice> devices(count);
            check(vkEnumeratePhysicalDevices(instance, &count, devices.data()));
            unsigned family = ~0u;
            for (auto candidate : devices)
            {
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, nullptr);
                std::vector<VkQueueFamilyProperties> families(count);
                vkGetPhysicalDeviceQueueFamilyProperties(candidate, &count, families.data());
                for (unsigned index = 0; index < count; ++index)
                    if (families[index].queueFlags & VK_QUEUE_COMPUTE_BIT)
                    { physical = candidate; family = index; break; }
                if (physical) break;
            }
            if (!physical) throw std::runtime_error("No Vulkan compute queue");
            const float priority = 1;
            VkDeviceQueueCreateInfo queueInfo { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
            queueInfo.queueFamilyIndex = family; queueInfo.queueCount = 1; queueInfo.pQueuePriorities = &priority;
            VkDeviceCreateInfo deviceInfo { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
            deviceInfo.queueCreateInfoCount = 1; deviceInfo.pQueueCreateInfos = &queueInfo;
            check(vkCreateDevice(physical, &deviceInfo, nullptr, &device));
            vkGetDeviceQueue(device, family, 0, &queue);
            VkCommandPoolCreateInfo commandInfo { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
            commandInfo.queueFamilyIndex = family;
            commandInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
            check(vkCreateCommandPool(device, &commandInfo, nullptr, &commands));
            for (unsigned index = 0; index < images.size(); ++index)
            {
                VkImageCreateInfo info { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
                info.imageType = VK_IMAGE_TYPE_2D; info.format = VK_FORMAT_R8G8B8A8_UNORM;
                info.extent = { width, height, 1 }; info.mipLevels = info.arrayLayers = 1;
                info.samples = VK_SAMPLE_COUNT_1_BIT;
                info.usage = VK_IMAGE_USAGE_STORAGE_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
                check(vkCreateImage(device, &info, nullptr, &images[index]));
                VkMemoryRequirements requirements;
                vkGetImageMemoryRequirements(device, images[index], &requirements);
                VkMemoryAllocateInfo memory { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
                memory.allocationSize = requirements.size;
                memory.memoryTypeIndex = memory_type(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
                check(vkAllocateMemory(device, &memory, nullptr, &allocations[index]));
                check(vkBindImageMemory(device, images[index], allocations[index], 0));
                VkImageViewCreateInfo view { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
                view.image = images[index]; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = info.format;
                view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                check(vkCreateImageView(device, &view, nullptr, &views[index]));
            }
            VkBufferCreateInfo bufferInfo { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            bufferInfo.size = bytes * 2; bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
            check(vkCreateBuffer(device, &bufferInfo, nullptr, &buffer));
            VkMemoryRequirements requirements;
            vkGetBufferMemoryRequirements(device, buffer, &requirements);
            VkMemoryAllocateInfo memory { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            memory.allocationSize = requirements.size;
            memory.memoryTypeIndex = memory_type(requirements.memoryTypeBits,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
            check(vkAllocateMemory(device, &memory, nullptr, &bufferMemory));
            check(vkBindBufferMemory(device, buffer, bufferMemory, 0));
            check(vkMapMemory(device, bufferMemory, 0, VK_WHOLE_SIZE, 0, &pixels));
            const std::array<unsigned, 6> postBindings { 0, 2, 4, 5, 6, 7 };
            for (unsigned set = 0; set < 2; ++set)
            {
                std::vector<VkDescriptorSetLayoutBinding> bindings;
                if (set == 0) bindings.push_back({ 1, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
                else for (auto binding : postBindings)
                    bindings.push_back({ binding, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 1, VK_SHADER_STAGE_COMPUTE_BIT, nullptr });
                VkDescriptorSetLayoutCreateInfo info { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
                info.bindingCount = static_cast<unsigned>(bindings.size()); info.pBindings = bindings.data();
                check(vkCreateDescriptorSetLayout(device, &info, nullptr, &setLayouts[set]));
            }
            VkDescriptorPoolSize size { VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 7 };
            VkDescriptorPoolCreateInfo pool { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
            pool.maxSets = 2; pool.poolSizeCount = 1; pool.pPoolSizes = &size;
            check(vkCreateDescriptorPool(device, &pool, nullptr, &descriptors));
            VkDescriptorSetAllocateInfo allocate { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
            allocate.descriptorPool = descriptors; allocate.descriptorSetCount = 2; allocate.pSetLayouts = setLayouts.data();
            check(vkAllocateDescriptorSets(device, &allocate, sets.data()));
            const std::array<unsigned, 7> imageIndices { 0, 3, 2, 1, 2, 2, 4 };
            for (unsigned index = 0; index < imageIndices.size(); ++index)
            {
                VkDescriptorImageInfo image { nullptr, views[imageIndices[index]], VK_IMAGE_LAYOUT_GENERAL };
                VkWriteDescriptorSet write { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
                write.dstSet = sets[index == 0 ? 0 : 1]; write.dstBinding = index == 0 ? 1 : postBindings[index - 1];
                write.descriptorCount = 1; write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE; write.pImageInfo = &image;
                vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
            }
            VkPushConstantRange range { VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(Renderer2DCompositeConstants) };
            VkPipelineLayoutCreateInfo layoutInfo { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
            layoutInfo.setLayoutCount = 2; layoutInfo.pSetLayouts = setLayouts.data();
            layoutInfo.pushConstantRangeCount = 1; layoutInfo.pPushConstantRanges = &range;
            check(vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout));
            std::ifstream file(shader, std::ios::binary | std::ios::ate);
            if (!file) throw std::runtime_error("Composite shader is unavailable");
            const auto length = static_cast<size_t>(file.tellg());
            std::vector<std::uint32_t> code(length / 4);
            file.seekg(0); file.read(reinterpret_cast<char*>(code.data()), length);
            VkShaderModuleCreateInfo moduleInfo { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
            moduleInfo.codeSize = length; moduleInfo.pCode = code.data();
            VkShaderModule module;
            check(vkCreateShaderModule(device, &moduleInfo, nullptr, &module));
            VkComputePipelineCreateInfo compute { VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO };
            compute.layout = layout;
            compute.stage = { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0,
                VK_SHADER_STAGE_COMPUTE_BIT, module, "main", nullptr };
            check(vkCreateComputePipelines(device, nullptr, 1, &compute, nullptr, &pipeline));
            vkDestroyShaderModule(device, module, nullptr);
        }
        ~Harness()
        {
            if (!device) return;
            vkDeviceWaitIdle(device);
            vkUnmapMemory(device, bufferMemory);
            vkDestroyBuffer(device, buffer, nullptr); vkFreeMemory(device, bufferMemory, nullptr);
            for (unsigned index = 0; index < images.size(); ++index)
            { vkDestroyImageView(device, views[index], nullptr); vkDestroyImage(device, images[index], nullptr);
                vkFreeMemory(device, allocations[index], nullptr); }
            vkDestroyPipeline(device, pipeline, nullptr); vkDestroyPipelineLayout(device, layout, nullptr);
            vkDestroyDescriptorPool(device, descriptors, nullptr);
            for (auto set : setLayouts) vkDestroyDescriptorSetLayout(device, set, nullptr);
            vkDestroyCommandPool(device, commands, nullptr);
            vkDestroyDevice(device, nullptr); vkDestroyInstance(instance, nullptr);
        }
        void run(const Renderer2DViewportFade& fade, bool firstFrame)
        {
            VkCommandBufferAllocateInfo allocate { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
            allocate.commandPool = commands; allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; allocate.commandBufferCount = 1;
            VkCommandBuffer command;
            check(vkAllocateCommandBuffers(device, &allocate, &command));
            VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            check(vkBeginCommandBuffer(command, &begin));
            if (firstFrame)
            {
                for (unsigned index = 0; index < images.size(); ++index)
                {
                    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
                    barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; barrier.newLayout = VK_IMAGE_LAYOUT_GENERAL;
                    barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
                    barrier.image = images[index]; barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
                    vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                        0, 0, nullptr, 0, nullptr, 1, &barrier);
                    const VkClearColorValue colour = index == 0 ? VkClearColorValue{{ 0, 0, 1, 0.5f }} :
                        index == 1 ? VkClearColorValue{{ 1, 0, 0, 1 }} : VkClearColorValue{{ 0, 0, 0, 0 }};
                    vkCmdClearColorImage(command, images[index], VK_IMAGE_LAYOUT_GENERAL, &colour, 1, &barrier.subresourceRange);
                }
            }
            VkMemoryBarrier ready { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
            ready.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT | VK_ACCESS_MEMORY_READ_BIT;
            ready.dstAccessMask = VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                0, 1, &ready, 0, nullptr, 0, nullptr);
            vkCmdBindPipeline(command, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
            vkCmdBindDescriptorSets(command, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 2, sets.data(), 0, nullptr);
            Renderer2DCompositeConstants constants { { 6u | (fade.enabled ? 16u : 0u), 0u, 0u, 0u }, fade.rect,
                { fade.topHeight, fade.bottomHeight, fade.minimumOpacity, 0 } };
            vkCmdPushConstants(command, layout, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(constants), &constants);
            vkCmdDispatch(command, 1, height / 8, 1);
            ready.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT; ready.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                0, 1, &ready, 0, nullptr, 0, nullptr);
            for (unsigned index = 0; index < 2; ++index)
            {
                VkBufferImageCopy copy{};
                copy.bufferOffset = index * bytes; copy.imageSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
                copy.imageExtent = { width, height, 1 };
                vkCmdCopyImageToBuffer(command, images[index + 3], VK_IMAGE_LAYOUT_GENERAL, buffer, 1, &copy);
            }
            ready.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; ready.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
            vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                0, 1, &ready, 0, nullptr, 0, nullptr);
            check(vkEndCommandBuffer(command));
            VkSubmitInfo submit { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
            check(vkQueueSubmit(queue, 1, &submit, nullptr)); check(vkQueueWaitIdle(queue));
            const auto* data = static_cast<const unsigned char*>(pixels);
            for (unsigned y = 0; y < height; ++y)
                for (unsigned output = 0; output < 2; ++output)
                {
                    const auto* pixel = data + output * bytes + y * width * 4;
                    const float alpha = renderer2d_viewport_fade_opacity(fade, y + 0.5f);
                    if (std::abs(pixel[3] - alpha * 255) > 1.1f ||
                        (output == 1 && std::abs(pixel[0] + pixel[2] - alpha * 255) > 2.1f))
                        throw std::runtime_error("GPU viewport fade or premultiplied alpha differs at row " + std::to_string(y) +
                            " output " + std::to_string(output) + " expected " + std::to_string(alpha * 255) +
                            " RGBA " + std::to_string(pixel[0]) + "," + std::to_string(pixel[1]) + "," +
                            std::to_string(pixel[2]) + "," + std::to_string(pixel[3]));
                }
            vkFreeCommandBuffers(device, commands, 1, &command);
        }
    };
}

int main(int argc, char** argv)
{
    try
    {
        if (argc != 2) throw std::runtime_error("Expected the compiled composite shader path");
        Harness gpu(argv[1]);
        Renderer2DViewportFade fade { { 0, 44, Harness::width, 200 }, 16, 20, 0, true };
        gpu.run(fade, true);
        gpu.run(fade, false); // Retained static/dynamic layers must not lose either gradient.
        fade.rect.w = 150;
        gpu.run(fade, false); // Resize must move the bottom edge without repainting the cached layers.
        fade.enabled = false;
        gpu.run(fade, false);
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

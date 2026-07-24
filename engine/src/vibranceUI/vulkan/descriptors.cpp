#include <vibranceUI/renderer/descriptors.h>
#include <vibranceUI/core/logger.h>

DescriptorSetLayoutBuilder::DescriptorSetLayoutBuilder(vk::Device& logicalDevice) : logicalDevice(logicalDevice) { }

vk::DescriptorSetLayout DescriptorSetLayoutBuilder::build(std::deque<std::function<void(vk::Device)>>& deletionQueue)
{
    // Layout builder assigns bindings in the order entries are added
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.flags = vk::DescriptorSetLayoutCreateFlagBits();
    layoutInfo.bindingCount = layoutBindings.size();
    layoutInfo.pBindings = layoutBindings.data();

    Logger* logger = Logger::fetch_logger();

    auto result = logicalDevice.createDescriptorSetLayout(layoutInfo);

    if (result.result != vk::Result::eSuccess) 
    {
        logger->vulkan("Failed to initialise descriptor set layout.");
        
        return nullptr;
    }

    logger->vulkan("Successfully initialised descriptor set layout.");

    reset();

    VkDescriptorSetLayout handle = result.value;

    deletionQueue.push_back([handle, logger](vk::Device device) {
        device.destroyDescriptorSetLayout(handle);

        logger->vulkan("Destroyed descriptor set layout.");
    });

    return result.value;
}

void DescriptorSetLayoutBuilder::add_entry(vk::ShaderStageFlags stage, vk::DescriptorType type, uint32_t descriptorCount)
{
    vk::DescriptorSetLayoutBinding entry = {};
    entry.setBinding(layoutBindings.size());
    entry.setDescriptorCount(descriptorCount);
    entry.setDescriptorType(type);
    entry.setStageFlags(stage);
    entry.setPImmutableSamplers(nullptr);

    layoutBindings.push_back(entry);
}

void DescriptorSetLayoutBuilder::reset()
{
    layoutBindings.clear();
}

vk::DescriptorPool make_descriptor_pool(
    vk::Device device, uint32_t descriptorSetCount,
    uint32_t bindingCount, vk::DescriptorType* pBindingTypes,
    std::deque<std::function<void(vk::Device)>>& deletionQueue
) {
    // Pool sizes are grouped by descriptor type so repeated bindings do not waste entries
    std::vector<vk::DescriptorPoolSize> poolSizes;

    for (int i = 0; i < bindingCount; i++)
    {
        bool foundType = false;
        for (vk::DescriptorPoolSize& poolSize : poolSizes)
        {
            if (poolSize.type == pBindingTypes[i])
            {
                poolSize.descriptorCount += descriptorSetCount;
                foundType = true;
                break;
            }
        }

        if (!foundType)
        {
            vk::DescriptorPoolSize poolSize;
            poolSize.type = pBindingTypes[i];
            poolSize.descriptorCount = descriptorSetCount;
            poolSizes.push_back(poolSize);
        }
    }

    vk::DescriptorPoolCreateInfo poolInfo;
    poolInfo.flags = vk::DescriptorPoolCreateFlags();
    poolInfo.maxSets = descriptorSetCount;
    poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
    poolInfo.pPoolSizes = poolSizes.data();

    auto result = device.createDescriptorPool(poolInfo);

    Logger* logger = Logger::fetch_logger();

    if (result.result != vk::Result::eSuccess)
    {
        logger->vulkan("Failed to initialise descriptor pool.");

        return nullptr;
    }

    logger->vulkan("Successfully initialised descriptor pool.");
    
    VkDescriptorPool handle = result.value;

    deletionQueue.push_back([handle, logger](vk::Device device) {
        device.destroyDescriptorPool(handle);

        logger->vulkan("Destroyed descriptor pool.");
    });

    return result.value;
}

vk::DescriptorSet allocate_descriptor_set(vk::Device device, vk::DescriptorPool descriptorPool, vk::DescriptorSetLayout layout)
{
    vk::DescriptorSetAllocateInfo allocationInfo;
    allocationInfo.descriptorPool = descriptorPool;
    allocationInfo.descriptorSetCount = 1;
    allocationInfo.pSetLayouts = &layout;

    Logger* logger = Logger::fetch_logger();
    
    auto result = device.allocateDescriptorSets(allocationInfo);

    if (result.result != vk::Result::eSuccess)
    {
        logger->vulkan("Failed to allocate descriptor set.");

        return nullptr;
    }

    logger->vulkan("Successfully allocated descriptor set.");

    return result.value[0];
}

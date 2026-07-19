#include <vibranceUI/renderer/shader.h>
#include <vibranceUI/core/logger.h>
#include <vibranceUI/factories/mesh_factory.h>
#include "embedded_shaders.h"
#include <array>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <vector>

namespace
{
    std::vector<uint32_t> load_embedded_spirv(const std::string& fileName)
    {
        const std::string shaderName = std::filesystem::path(fileName).filename().string();
        const std::span<const std::byte> bytes = vibrance_embedded_shader_spirv(shaderName);
        if (bytes.empty() || (bytes.size() % sizeof(uint32_t)) != 0u)
        {
            if (Logger* logger = Logger::fetch_logger())
            {
                logger->vulkan("Embedded SPIR-V was not found for \"" + shaderName + "\".");
            }
            return {};
        }

        std::vector<uint32_t> spirv(bytes.size() / sizeof(uint32_t));
        std::memcpy(spirv.data(), bytes.data(), bytes.size());
        return spirv;
    }
}

PipelineLayoutBuilder::PipelineLayoutBuilder(vk::Device& logicalDevice) : logicalDevice(logicalDevice) {}

vk::PipelineLayout PipelineLayoutBuilder::build(std::deque<std::function<void(vk::Device)>>& deletionQueue)
{
    // Builder state is single-use so pipeline layouts cannot accidentally share stale ranges
    vk::PipelineLayoutCreateInfo layoutInfo;
    layoutInfo.flags = vk::PipelineLayoutCreateFlags();

    layoutInfo.setLayoutCount = descriptorSetLayouts.size();
    layoutInfo.pSetLayouts = descriptorSetLayouts.data();

    layoutInfo.pushConstantRangeCount = static_cast<uint32_t>(pushConstantRanges.size());
    layoutInfo.pPushConstantRanges = pushConstantRanges.data();

    Logger* logger = Logger::fetch_logger();
    auto result = logicalDevice.createPipelineLayout(layoutInfo);

    if (result.result != vk::Result::eSuccess) {
        logger->vulkan("Failed to create pipeline layout");
        return nullptr;
    }

    logger->vulkan("Successfully created pipeline layout");
    VkPipelineLayout handle = result.value;
    deletionQueue.push_back([handle, logger](vk::Device device) {
        device.destroyPipelineLayout(handle);
        logger->vulkan("Destroyed pipeline layout.");
    });
    reset();
    return result.value;
}

void PipelineLayoutBuilder::add(vk::DescriptorSetLayout descriptorSetLayout)
{
    descriptorSetLayouts.push_back(descriptorSetLayout);
}

void PipelineLayoutBuilder::add_push_constants(vk::ShaderStageFlags stage, uint32_t size, uint32_t offset)
{
    vk::PushConstantRange range = {};
    range.stageFlags = stage;
    range.offset = offset;
    range.size = size;
    pushConstantRanges.push_back(range);
}

void PipelineLayoutBuilder::reset()
{
    descriptorSetLayouts.clear();
    pushConstantRanges.clear();
}

vk::Pipeline make_compute_pipeline(
    vk::Device logicalDevice, const char* name, vk::PipelineLayout pipelineLayout,
    std::deque<std::function<void(vk::Device)>>& deletionQueue
) {
    Logger* logger = Logger::fetch_logger();

    std::stringstream fileNameBuilder;
    std::string fileName;

    fileNameBuilder << "../shaders/" << name << ".comp";
    fileName = fileNameBuilder.str();
    fileNameBuilder.str("");
    
    std::vector<uint32_t> spirv = load_embedded_spirv(fileName);
    if (spirv.empty())
    {
        logger->vulkan("Failed to compile compute shader.");
        return nullptr;
    }

    vk::ShaderModuleCreateInfo moduleInfo = {};
    moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
    moduleInfo.pCode = spirv.data();

    auto moduleResult = logicalDevice.createShaderModule(moduleInfo);
    if (moduleResult.result != vk::Result::eSuccess)
    {
        logger->vulkan("Failed to create compute shader module: " + vk::to_string(moduleResult.result));
        return nullptr;
    }

    vk::ShaderModule shaderModule = moduleResult.value;

    vk::PipelineShaderStageCreateInfo stageInfo = {};
    stageInfo.stage = vk::ShaderStageFlagBits::eCompute;
    stageInfo.module = shaderModule;
    stageInfo.pName = "main";

    vk::ComputePipelineCreateInfo pipelineInfo = {};
    pipelineInfo.stage = stageInfo;
    pipelineInfo.layout = pipelineLayout;

    auto pipelineResult = logicalDevice.createComputePipeline(nullptr, pipelineInfo);
    logicalDevice.destroyShaderModule(shaderModule);

    if (pipelineResult.result == vk::Result::eSuccess)
    {
        logger->vulkan("Successfully created compute pipeline.");
        VkPipeline handle = pipelineResult.value;
        deletionQueue.push_back([logger, handle](vk::Device device) {
            logger->vulkan("Destroyed compute pipeline.");
            device.destroyPipeline(handle);
        });
        return pipelineResult.value;
    }
    else 
    {
        logger->vulkan("Failed to create compute pipeline: " + vk::to_string(pipelineResult.result));
    }

    return nullptr;
}

vk::Pipeline make_graphics_pipeline(
    vk::Device logicalDevice,
    const char* name,
    vk::PipelineLayout pipelineLayout,
    vk::RenderPass renderPass,
    vk::SampleCountFlagBits rasterizationSamples,
    std::deque<std::function<void(vk::Device)>>& deletionQueue
) {
    Logger* logger = Logger::fetch_logger();

    std::stringstream vertexFileNameBuilder;
    vertexFileNameBuilder << "../shaders/" << name << ".vert";
    std::vector<uint32_t> vertexSpirv = load_embedded_spirv(vertexFileNameBuilder.str());
    if (vertexSpirv.empty())
    {
        logger->vulkan("Failed to compile vertex shader.");
        return nullptr;
    }

    std::stringstream fragmentFileNameBuilder;
    fragmentFileNameBuilder << "../shaders/" << name << ".frag";
    std::vector<uint32_t> fragmentSpirv = load_embedded_spirv(fragmentFileNameBuilder.str());
    if (fragmentSpirv.empty())
    {
        logger->vulkan("Failed to compile fragment shader.");
        return nullptr;
    }

    auto make_shader_module = [&](const std::vector<uint32_t>& spirv) -> vk::ShaderModule {
        vk::ShaderModuleCreateInfo moduleInfo = {};
        moduleInfo.codeSize = spirv.size() * sizeof(uint32_t);
        moduleInfo.pCode = spirv.data();

        auto moduleResult = logicalDevice.createShaderModule(moduleInfo);
        if (moduleResult.result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to create graphics shader module: " + vk::to_string(moduleResult.result));
            return nullptr;
        }
        return moduleResult.value;
    };

    vk::ShaderModule vertexModule = make_shader_module(vertexSpirv);
    vk::ShaderModule fragmentModule = make_shader_module(fragmentSpirv);
    if (!vertexModule || !fragmentModule)
    {
        if (vertexModule) logicalDevice.destroyShaderModule(vertexModule);
        if (fragmentModule) logicalDevice.destroyShaderModule(fragmentModule);
        return nullptr;
    }

    std::array<vk::PipelineShaderStageCreateInfo, 2> shaderStages = {};
    shaderStages[0].stage = vk::ShaderStageFlagBits::eVertex;
    shaderStages[0].module = vertexModule;
    shaderStages[0].pName = "main";
    shaderStages[1].stage = vk::ShaderStageFlagBits::eFragment;
    shaderStages[1].module = fragmentModule;
    shaderStages[1].pName = "main";

    vk::VertexInputBindingDescription vertexBinding = {};
    vertexBinding.binding = 0;
    vertexBinding.stride = sizeof(Vertex);
    vertexBinding.inputRate = vk::VertexInputRate::eVertex;

    std::array<vk::VertexInputAttributeDescription, 8> vertexAttributes = {};
    vertexAttributes[0].binding = 0;
    vertexAttributes[0].location = 0;
    vertexAttributes[0].format = vk::Format::eR32G32B32Sfloat;
    vertexAttributes[0].offset = offsetof(Vertex, pos);
    vertexAttributes[1].binding = 0;
    vertexAttributes[1].location = 1;
    vertexAttributes[1].format = vk::Format::eR32G32B32Sfloat;
    vertexAttributes[1].offset = offsetof(Vertex, color);
    vertexAttributes[2].binding = 0;
    vertexAttributes[2].location = 2;
    vertexAttributes[2].format = vk::Format::eR32G32B32Sfloat;
    vertexAttributes[2].offset = offsetof(Vertex, normal);
    vertexAttributes[3].binding = 0;
    vertexAttributes[3].location = 3;
    vertexAttributes[3].format = vk::Format::eR32G32Sfloat;
    vertexAttributes[3].offset = offsetof(Vertex, uv);
    vertexAttributes[4].binding = 0;
    vertexAttributes[4].location = 4;
    vertexAttributes[4].format = vk::Format::eR32G32B32A32Sfloat;
    vertexAttributes[4].offset = offsetof(Vertex, tangent);
    vertexAttributes[5].binding = 0;
    vertexAttributes[5].location = 5;
    vertexAttributes[5].format = vk::Format::eR32G32B32A32Sfloat;
    vertexAttributes[5].offset = offsetof(Vertex, material);
    vertexAttributes[6].binding = 0;
    vertexAttributes[6].location = 6;
    vertexAttributes[6].format = vk::Format::eR32G32B32A32Sfloat;
    vertexAttributes[6].offset = offsetof(Vertex, material2);
    vertexAttributes[7].binding = 0;
    vertexAttributes[7].location = 7;
    vertexAttributes[7].format = vk::Format::eR32G32B32A32Sfloat;
    vertexAttributes[7].offset = offsetof(Vertex, material3);

    vk::PipelineVertexInputStateCreateInfo vertexInput = {};
    vertexInput.vertexBindingDescriptionCount = 1;
    vertexInput.pVertexBindingDescriptions = &vertexBinding;
    vertexInput.vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size());
    vertexInput.pVertexAttributeDescriptions = vertexAttributes.data();

    vk::PipelineInputAssemblyStateCreateInfo inputAssembly = {};
    inputAssembly.topology = vk::PrimitiveTopology::eTriangleList;
    inputAssembly.primitiveRestartEnable = VK_FALSE;

    vk::PipelineViewportStateCreateInfo viewportState = {};
    viewportState.viewportCount = 1;
    viewportState.scissorCount = 1;

    vk::PipelineRasterizationStateCreateInfo rasterizer = {};
    rasterizer.depthClampEnable = VK_FALSE;
    rasterizer.rasterizerDiscardEnable = VK_FALSE;
    rasterizer.polygonMode = vk::PolygonMode::eFill;
    rasterizer.cullMode = vk::CullModeFlagBits::eNone;
    rasterizer.frontFace = vk::FrontFace::eCounterClockwise;
    rasterizer.depthBiasEnable = VK_FALSE;
    rasterizer.lineWidth = 1.0f;

    vk::PipelineMultisampleStateCreateInfo multisampling = {};
    multisampling.rasterizationSamples = rasterizationSamples;
    multisampling.sampleShadingEnable = VK_FALSE;

    vk::PipelineDepthStencilStateCreateInfo depthStencil = {};
    depthStencil.depthTestEnable = VK_TRUE;
    depthStencil.depthWriteEnable = VK_TRUE;
    depthStencil.depthCompareOp = vk::CompareOp::eLessOrEqual;
    depthStencil.depthBoundsTestEnable = VK_FALSE;
    depthStencil.stencilTestEnable = VK_FALSE;

    vk::PipelineColorBlendAttachmentState colorBlendAttachment = {};
    colorBlendAttachment.blendEnable = VK_TRUE;
    colorBlendAttachment.srcColorBlendFactor = vk::BlendFactor::eSrcAlpha;
    colorBlendAttachment.dstColorBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.colorBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.srcAlphaBlendFactor = vk::BlendFactor::eOne;
    colorBlendAttachment.dstAlphaBlendFactor = vk::BlendFactor::eOneMinusSrcAlpha;
    colorBlendAttachment.alphaBlendOp = vk::BlendOp::eAdd;
    colorBlendAttachment.colorWriteMask =
        vk::ColorComponentFlagBits::eR |
        vk::ColorComponentFlagBits::eG |
        vk::ColorComponentFlagBits::eB |
        vk::ColorComponentFlagBits::eA;

    vk::PipelineColorBlendStateCreateInfo colorBlending = {};
    colorBlending.logicOpEnable = VK_FALSE;
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    std::array<vk::DynamicState, 2> dynamicStates = {
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor
    };
    vk::PipelineDynamicStateCreateInfo dynamicState = {};
    dynamicState.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamicState.pDynamicStates = dynamicStates.data();

    vk::GraphicsPipelineCreateInfo pipelineInfo = {};
    pipelineInfo.stageCount = static_cast<uint32_t>(shaderStages.size());
    pipelineInfo.pStages = shaderStages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pDepthStencilState = &depthStencil;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.pDynamicState = &dynamicState;
    pipelineInfo.layout = pipelineLayout;
    pipelineInfo.renderPass = renderPass;
    pipelineInfo.subpass = 0;
    pipelineInfo.basePipelineHandle = nullptr;
    pipelineInfo.basePipelineIndex = -1;

    auto pipelineResult = logicalDevice.createGraphicsPipeline(nullptr, pipelineInfo);

    logicalDevice.destroyShaderModule(vertexModule);
    logicalDevice.destroyShaderModule(fragmentModule);

    if (pipelineResult.result == vk::Result::eSuccess)
    {
        logger->vulkan("Successfully created graphics pipeline.");
        VkPipeline handle = pipelineResult.value;
        deletionQueue.push_back([logger, handle](vk::Device device) {
            logger->vulkan("Destroyed graphics pipeline.");
            device.destroyPipeline(handle);
        });
        return pipelineResult.value;
    }

    logger->vulkan("Failed to create graphics pipeline: " + vk::to_string(pipelineResult.result));
    return nullptr;
}

#pragma once
#include <glm/glm.hpp>
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>
#include <vector>
#include <deque>
#include <functional>
#include <filesystem>
#include <vibranceUI/renderer/buffer.h>

struct Vertex
{
    // Shared vertex layout for built-in 2D triangles and imported 3D meshes
    alignas(16) glm::vec3 pos;
	alignas(16) glm::vec3 color;
	alignas(16) glm::vec3 normal;
	alignas(16) glm::vec2 uv;
	alignas(16) glm::vec4 tangent;
	alignas(16) glm::vec4 material;
	alignas(16) glm::vec4 material2;
	alignas(16) glm::vec4 material3;

    static vk::VertexInputBindingDescription2EXT get_binding_description()
    {
        vk::VertexInputBindingDescription2EXT description = {};

        description.binding = 0;
        description.stride = sizeof(Vertex);
        description.inputRate = vk::VertexInputRate::eVertex;
        description.divisor = 1;

        return description;
    }

    static std::vector<vk::VertexInputAttributeDescription2EXT> get_attribute_descriptions()
    {
        std::vector<vk::VertexInputAttributeDescription2EXT> attributes(8);

		attributes[0].binding = 0;
		attributes[0].location = 0;
		attributes[0].format = vk::Format::eR32G32B32Sfloat;
		attributes[0].offset = offsetof(Vertex, pos);

		attributes[1].binding = 0;
		attributes[1].location = 1;
		attributes[1].format = vk::Format::eR32G32B32Sfloat;
		attributes[1].offset = offsetof(Vertex, color);

		attributes[2].binding = 0;
		attributes[2].location = 2;
		attributes[2].format = vk::Format::eR32G32B32Sfloat;
		attributes[2].offset = offsetof(Vertex, normal);

		attributes[3].binding = 0;
		attributes[3].location = 3;
		attributes[3].format = vk::Format::eR32G32Sfloat;
		attributes[3].offset = offsetof(Vertex, uv);

		attributes[4].binding = 0;
		attributes[4].location = 4;
		attributes[4].format = vk::Format::eR32G32B32A32Sfloat;
		attributes[4].offset = offsetof(Vertex, tangent);

		attributes[5].binding = 0;
		attributes[5].location = 5;
		attributes[5].format = vk::Format::eR32G32B32A32Sfloat;
		attributes[5].offset = offsetof(Vertex, material);

		attributes[6].binding = 0;
		attributes[6].location = 6;
		attributes[6].format = vk::Format::eR32G32B32A32Sfloat;
		attributes[6].offset = offsetof(Vertex, material2);

		attributes[7].binding = 0;
		attributes[7].location = 7;
		attributes[7].format = vk::Format::eR32G32B32A32Sfloat;
		attributes[7].offset = offsetof(Vertex, material3);

        return attributes;
    }
};

// Builds the renderer's fallback triangle buffer used by compute and graphics paths
StorageBuffer build_triangle(VmaAllocator& allocator, std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue, vk::CommandBuffer commandBuffer, vk::Queue queue);
// Loads GLTF meshes into a GPU buffer with material descriptor sets
Model3DAsset load_gltf_mesh(
	const std::filesystem::path& path,
	VmaAllocator& allocator,
	std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
	std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
	vk::CommandBuffer commandBuffer,
	vk::Queue queue,
	vk::Device logicalDevice,
	vk::DescriptorPool textureDescriptorPool,
	vk::DescriptorSetLayout textureDescriptorSetLayout);

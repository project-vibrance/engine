#pragma once
#define VULKAN_HPP_NO_EXCEPTIONS
#include <vulkan/vulkan.hpp>
#include <vma/vk_mem_alloc.h>
#include <deque>
#include <functional>
#include <memory>
#include <vector>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/image.h>

class StorageBuffer 
{
    public:
    // Shared GPU buffer for generated 2D triangles and loaded 3D mesh data
	vk::Buffer buffer = nullptr;
	VmaAllocation allocation = nullptr;
	vk::DescriptorBufferInfo descriptor = {};
	vk::DeviceSize vertexDataOffset = 0;
	uint32_t vertexCount = 0;
	uint32_t triangleCount = 0;
	uint32_t triangleCount2D = 0;
	uint32_t firstTriangle3D = 0;
	uint32_t triangleCount3D = 0;
};

struct Model3DTexture
{
	std::unique_ptr<StorageImage> image;
	vk::Sampler sampler = nullptr;
};

struct Model3DDrawRange
{
	uint32_t firstTriangle = 0;
	uint32_t triangleCount = 0;
	vk::DescriptorSet materialDescriptorSet = nullptr;
};

struct Model3DAsset
{
	// Model asset keeps per-material descriptors next to the uploaded triangle buffer
	StorageBuffer buffer;
	std::vector<Model3DTexture> textures;
	Model3DTexture fallbackBaseColorTexture;
	Model3DTexture fallbackNormalTexture;
	Model3DTexture fallbackMetallicRoughnessTexture;
	Model3DTexture fallbackOcclusionTexture;
	Model3DTexture fallbackEmissiveTexture;
	vk::DescriptorSet fallbackMaterialDescriptorSet = nullptr;
	std::vector<vk::DescriptorSet> materialDescriptorSets;
	std::vector<Model3DDrawRange> drawRanges;

	bool valid() const
	{
		return buffer.buffer && buffer.triangleCount > 0;
	}
};

StorageBuffer make_depth_buffer(
    VmaAllocator& allocator, 
	std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue, 
	vk::Extent2D size
);

void copy(
	vk::Buffer srcBuffer,
	vk::Buffer dstBuffer,
	vk::DeviceSize size,
	vk::Queue queue, vk::CommandBuffer commandBuffer
);

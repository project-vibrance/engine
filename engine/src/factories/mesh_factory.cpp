#include <vibranceUI/factories/mesh_factory.h>
#include <vibranceUI/core/logger.h>
#include <vibranceUI/renderer/buffer.h>
#include <vibranceUI/renderer/descriptors.h>
#include <vibranceUI/renderer/image.h>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/norm.hpp>
#include <fstream>
#include <iterator>
#include <limits>
#include <string_view>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <variant>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

namespace
{
	constexpr uint32_t kNoTexture = UINT32_MAX;

	struct MeshUploadHeader
	{
		alignas(16) uint32_t triangleCount = 0;
		uint32_t padding[3] = {};
	};

	struct LoadedVertex
	{
		glm::vec3 position { 0.0f };
		glm::vec3 normal { 1.0f, 0.0f, 0.0f };
		glm::vec3 color { 1.0f };
		glm::vec2 uv { 0.0f };
		glm::vec4 tangent { 1.0f, 0.0f, 0.0f, 1.0f };
		glm::vec4 material { 1.0f, 1.0f, 1.0f, 1.0f };
		glm::vec4 material2 { 1.0f, 0.0f, 0.0f, 0.0f };
		glm::vec4 material3 { 0.0f, 0.5f, 0.0f, 0.0f };
	};

	struct LoadedSampler
	{
		uint32_t magFilter = 9729;
		uint32_t minFilter = 9987;
		uint32_t wrapS = 10497;
		uint32_t wrapT = 10497;
	};

	struct LoadedTexture
	{
		uint32_t imageIndex = kNoTexture;
		LoadedSampler sampler = {};
	};

	struct LoadedMaterial
	{
		glm::vec4 baseColorFactor { 0.8f, 0.8f, 0.8f, 1.0f };
		float metallicFactor = 1.0f;
		float roughnessFactor = 1.0f;
		float normalScale = 1.0f;
		float occlusionStrength = 1.0f;
		float alphaMode = 0.0f;
		float alphaCutoff = 0.5f;
		glm::vec3 emissiveFactor { 0.0f };
		uint32_t baseColorTexture = kNoTexture;
		uint32_t normalTexture = kNoTexture;
		uint32_t metallicRoughnessTexture = kNoTexture;
		uint32_t occlusionTexture = kNoTexture;
		uint32_t emissiveTexture = kNoTexture;

		glm::vec3 vertex_color() const
		{
			return glm::clamp(glm::vec3(baseColorFactor), glm::vec3(0.0f), glm::vec3(1.0f));
		}

		glm::vec4 shader_factors() const
		{
			return {
				glm::clamp(metallicFactor, 0.0f, 1.0f),
				glm::clamp(roughnessFactor, 0.04f, 1.0f),
				std::max(normalScale, 0.0f),
				glm::clamp(baseColorFactor.a, 0.0f, 1.0f)
			};
		}

		glm::vec4 shader_factors2() const
		{
			return {
				glm::clamp(occlusionStrength, 0.0f, 1.0f),
				std::max(emissiveFactor.r, 0.0f),
				std::max(emissiveFactor.g, 0.0f),
				std::max(emissiveFactor.b, 0.0f)
			};
		}

		glm::vec4 shader_factors3() const
		{
			return {
				glm::clamp(alphaMode, 0.0f, 2.0f),
				glm::clamp(alphaCutoff, 0.0f, 1.0f),
				0.0f,
				0.0f
			};
		}
	};

	struct LoadedPrimitiveRange
	{
		uint32_t firstIndex = 0;
		uint32_t indexCount = 0;
		LoadedMaterial material = {};
	};

	struct LoadedMesh
	{
		std::string name;
		std::vector<uint32_t> indices;
		std::vector<LoadedVertex> vertices;
		std::vector<LoadedPrimitiveRange> ranges;
	};

	struct LoadedImage
	{
		std::string name;
		uint32_t width = 0;
		uint32_t height = 0;
		std::vector<unsigned char> rgba;

		bool valid() const
		{
			return width > 0 && height > 0 && !rgba.empty();
		}
	};

	void calculate_missing_normals(LoadedMesh& mesh);
	void calculate_missing_tangents(LoadedMesh& mesh);

	StorageBuffer upload_vertices(
		const std::vector<Vertex>& vertices,
		VmaAllocator& allocator,
		std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
		vk::CommandBuffer commandBuffer,
		vk::Queue queue,
		uint32_t triangleCount2D = 0)
	{
		Logger* logger = Logger::fetch_logger();

		const MeshUploadHeader header{ static_cast<uint32_t>(vertices.size() / 3), {} };
		const vk::DeviceSize headerSize = sizeof(MeshUploadHeader);
		const vk::DeviceSize vertexBytes = vertices.size() * sizeof(Vertex);
		const vk::DeviceSize uploadSize = headerSize + vertexBytes;

		VkBuffer stagingBuffer;
		VmaAllocation stagingAllocation;

		vk::BufferCreateInfo bufferInfo = {};
		bufferInfo.flags = vk::BufferCreateFlags();
		bufferInfo.size = uploadSize;
		bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		VkBufferCreateInfo bufferInfoHandle = bufferInfo;

		VmaAllocationCreateInfo allocationInfo = {};
		allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;
		allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VmaAllocationInfo stagingAllocationInfo;
		vmaCreateBuffer(allocator, &bufferInfoHandle, &allocationInfo, &stagingBuffer, &stagingAllocation, &stagingAllocationInfo);
		vmaSetAllocationName(allocator, stagingAllocation, "Mesh Staging Buffer");
		vmaGetAllocationInfo(allocator, stagingAllocation, &stagingAllocationInfo);
		logger->log(stagingAllocationInfo);

		void* dst;
		vmaMapMemory(allocator, stagingAllocation, &dst);
		std::memcpy(dst, &header, headerSize);
		if (!vertices.empty())
		{
			std::memcpy(static_cast<std::byte*>(dst) + headerSize, vertices.data(), vertexBytes);
		}
		vmaUnmapMemory(allocator, stagingAllocation);

		StorageBuffer mesh;
		VkBuffer bufferHandle;

		bufferInfo.usage = vk::BufferUsageFlagBits::eStorageBuffer |
			vk::BufferUsageFlagBits::eVertexBuffer |
			vk::BufferUsageFlagBits::eTransferDst;
		bufferInfoHandle = bufferInfo;
		allocationInfo.flags = VMA_ALLOCATION_CREATE_STRATEGY_MIN_MEMORY_BIT;

		VmaAllocationInfo vertexAllocationInfo;
		vmaCreateBuffer(allocator, &bufferInfoHandle, &allocationInfo, &bufferHandle, &(mesh.allocation), &vertexAllocationInfo);
		vmaSetAllocationName(allocator, mesh.allocation, "Mesh Storage Buffer");
		vmaGetAllocationInfo(allocator, mesh.allocation, &vertexAllocationInfo);
		logger->log(vertexAllocationInfo);

		mesh.buffer = bufferHandle;
		mesh.vertexDataOffset = headerSize;
		mesh.vertexCount = static_cast<uint32_t>(vertices.size());
		mesh.triangleCount = header.triangleCount;
		mesh.triangleCount2D = std::min(triangleCount2D, mesh.triangleCount);
		mesh.firstTriangle3D = mesh.triangleCount2D;
		mesh.triangleCount3D = mesh.triangleCount - mesh.triangleCount2D;

		copy(stagingBuffer, bufferHandle, uploadSize, queue, commandBuffer);
		vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);

		vmaDeletionQueue.push_back([mesh](VmaAllocator allocator) {
			vmaDestroyBuffer(allocator, mesh.buffer, mesh.allocation);
		});

		mesh.descriptor.buffer = mesh.buffer;
		mesh.descriptor.offset = 0;
		mesh.descriptor.range = uploadSize;

		return mesh;
	}

	bool upload_rgba_to_image(
		VmaAllocator& allocator,
		vk::CommandBuffer commandBuffer,
		vk::Queue queue,
		StorageImage& image,
		const std::vector<unsigned char>& rgba,
		std::string_view label)
	{
		Logger* logger = Logger::fetch_logger();
		const vk::DeviceSize uploadSize = static_cast<vk::DeviceSize>(rgba.size());
		if (uploadSize == 0)
		{
			return false;
		}

		vk::BufferCreateInfo bufferInfo = {};
		bufferInfo.size = uploadSize;
		bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
		bufferInfo.sharingMode = vk::SharingMode::eExclusive;

		VmaAllocationCreateInfo allocationInfo = {};
		allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
			VMA_ALLOCATION_CREATE_MAPPED_BIT;
		allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

		VkBuffer stagingBuffer = VK_NULL_HANDLE;
		VmaAllocation stagingAllocation = nullptr;
		VmaAllocationInfo stagingInfo = {};
		VkBufferCreateInfo rawBufferInfo = bufferInfo;
		if (vmaCreateBuffer(allocator, &rawBufferInfo, &allocationInfo,
			&stagingBuffer, &stagingAllocation, &stagingInfo) != VK_SUCCESS)
		{
			logger->print("Failed to create 3D texture staging buffer for " + std::string(label) + ".");
			return false;
		}

		std::memcpy(stagingInfo.pMappedData, rgba.data(), rgba.size());

		vk::Result result = commandBuffer.reset();
		if (result != vk::Result::eSuccess)
		{
			logger->print("Failed to reset 3D texture upload command buffer.");
			vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
			return false;
		}

		vk::CommandBufferBeginInfo beginInfo = {};
		beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
		result = commandBuffer.begin(beginInfo);
		if (result != vk::Result::eSuccess)
		{
			logger->print("Failed to begin 3D texture upload command buffer.");
			vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
			return false;
		}

		transition_image_layout(commandBuffer, image.image,
			vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal,
			vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite,
			vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
			vk::ImageAspectFlagBits::eColor, 0, image.mipLevels);

		vk::BufferImageCopy region = {};
		region.bufferOffset = 0;
		region.bufferRowLength = 0;
		region.bufferImageHeight = 0;
		region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
		region.imageSubresource.mipLevel = 0;
		region.imageSubresource.baseArrayLayer = 0;
		region.imageSubresource.layerCount = 1;
		region.imageOffset = vk::Offset3D { 0, 0, 0 };
		region.imageExtent = vk::Extent3D { image.extent.width, image.extent.height, 1 };

		commandBuffer.copyBufferToImage(stagingBuffer, image.image,
			vk::ImageLayout::eTransferDstOptimal, 1, &region);

		if (image.mipLevels > 1u)
		{
			int32_t mipWidth = static_cast<int32_t>(image.extent.width);
			int32_t mipHeight = static_cast<int32_t>(image.extent.height);
			for (uint32_t mipLevel = 1; mipLevel < image.mipLevels; ++mipLevel)
			{
				transition_image_layout(commandBuffer, image.image,
					vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
					vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
					vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
					vk::ImageAspectFlagBits::eColor, mipLevel - 1u, 1u);

				vk::ImageBlit blit = {};
				blit.srcOffsets[0] = vk::Offset3D { 0, 0, 0 };
				blit.srcOffsets[1] = vk::Offset3D { mipWidth, mipHeight, 1 };
				blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				blit.srcSubresource.mipLevel = mipLevel - 1u;
				blit.srcSubresource.baseArrayLayer = 0;
				blit.srcSubresource.layerCount = 1;

				const int32_t nextMipWidth = std::max(1, mipWidth / 2);
				const int32_t nextMipHeight = std::max(1, mipHeight / 2);
				blit.dstOffsets[0] = vk::Offset3D { 0, 0, 0 };
				blit.dstOffsets[1] = vk::Offset3D { nextMipWidth, nextMipHeight, 1 };
				blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
				blit.dstSubresource.mipLevel = mipLevel;
				blit.dstSubresource.baseArrayLayer = 0;
				blit.dstSubresource.layerCount = 1;

				commandBuffer.blitImage(image.image, vk::ImageLayout::eTransferSrcOptimal,
					image.image, vk::ImageLayout::eTransferDstOptimal, 1, &blit, vk::Filter::eLinear);

				transition_image_layout(commandBuffer, image.image,
					vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
					vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eShaderRead,
					vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
					vk::ImageAspectFlagBits::eColor, mipLevel - 1u, 1u);

				mipWidth = nextMipWidth;
				mipHeight = nextMipHeight;
			}

			transition_image_layout(commandBuffer, image.image,
				vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
				vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader,
				vk::ImageAspectFlagBits::eColor, image.mipLevels - 1u, 1u);
		}
		else
		{
			transition_image_layout(commandBuffer, image.image,
				vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
				vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
				vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eFragmentShader);
		}

		result = commandBuffer.end();
		if (result != vk::Result::eSuccess)
		{
			logger->print("Failed to end 3D texture upload command buffer.");
			vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
			return false;
		}

		vk::SubmitInfo submitInfo = {};
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &commandBuffer;
		result = queue.submit(1, &submitInfo, nullptr);
		if (result != vk::Result::eSuccess)
		{
			logger->print("Failed to submit 3D texture upload.");
			vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
			return false;
		}

		result = queue.waitIdle();
		vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
		if (result != vk::Result::eSuccess)
		{
			logger->print("Failed to wait for 3D texture upload.");
			return false;
		}

		return true;
	}

	uint32_t mip_count_for_extent(vk::Extent2D extent)
	{
		const uint32_t longestSide = std::max(extent.width, extent.height);
		return longestSide > 0u
			? static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(longestSide)))) + 1u
			: 1u;
	}

	vk::Filter gltf_filter_to_vk(uint32_t filter, vk::Filter fallback)
	{
		switch (filter)
		{
			case 9728:
			case 9984:
			case 9986:
				return vk::Filter::eNearest;
			case 9729:
			case 9985:
			case 9987:
				return vk::Filter::eLinear;
			default:
				return fallback;
		}
	}

	vk::SamplerMipmapMode gltf_mipmap_mode_to_vk(uint32_t filter)
	{
		switch (filter)
		{
			case 9984:
			case 9985:
				return vk::SamplerMipmapMode::eNearest;
			case 9986:
			case 9987:
			default:
				return vk::SamplerMipmapMode::eLinear;
		}
	}

	vk::SamplerAddressMode gltf_wrap_to_vk(uint32_t wrap)
	{
		switch (wrap)
		{
			case 33071:
				return vk::SamplerAddressMode::eClampToEdge;
			case 33648:
				return vk::SamplerAddressMode::eMirroredRepeat;
			case 10497:
			default:
				return vk::SamplerAddressMode::eRepeat;
		}
	}

	vk::Sampler make_model_sampler(
		vk::Device logicalDevice,
		std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
		const LoadedSampler& sourceSampler,
		uint32_t mipLevels)
	{
		vk::SamplerCreateInfo samplerInfo = {};
		samplerInfo.magFilter = gltf_filter_to_vk(sourceSampler.magFilter, vk::Filter::eLinear);
		samplerInfo.minFilter = gltf_filter_to_vk(sourceSampler.minFilter, vk::Filter::eLinear);
		samplerInfo.mipmapMode = gltf_mipmap_mode_to_vk(sourceSampler.minFilter);
		samplerInfo.addressModeU = gltf_wrap_to_vk(sourceSampler.wrapS);
		samplerInfo.addressModeV = gltf_wrap_to_vk(sourceSampler.wrapT);
		samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
		samplerInfo.mipLodBias = 0.0f;
		samplerInfo.anisotropyEnable = VK_FALSE;
		samplerInfo.maxAnisotropy = 1.0f;
		samplerInfo.compareEnable = VK_FALSE;
		samplerInfo.compareOp = vk::CompareOp::eAlways;
		samplerInfo.minLod = 0.0f;
		samplerInfo.maxLod = static_cast<float>(std::max(1u, mipLevels) - 1u);
		samplerInfo.borderColor = vk::BorderColor::eIntOpaqueWhite;
		samplerInfo.unnormalizedCoordinates = VK_FALSE;

		auto result = logicalDevice.createSampler(samplerInfo);
		if (result.result != vk::Result::eSuccess)
		{
			Logger::fetch_logger()->print("Failed to create 3D model texture sampler.");
			return nullptr;
		}

		VkSampler samplerHandle = result.value;
		deviceDeletionQueue.push_back([samplerHandle](vk::Device device) {
			device.destroySampler(samplerHandle);
		});
		return result.value;
	}

	Model3DTexture make_model_texture(
		const LoadedImage& source,
		const LoadedSampler& samplerSettings,
		vk::Format format,
		VmaAllocator& allocator,
		std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
		std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
		vk::CommandBuffer commandBuffer,
		vk::Queue queue,
		vk::Device logicalDevice)
	{
		Model3DTexture texture = {};
		if (!source.valid())
		{
			return texture;
		}

		const uint32_t mipLevels = mip_count_for_extent(vk::Extent2D { source.width, source.height });
		texture.image = std::make_unique<StorageImage>(
			allocator,
			format,
			vk::Extent2D { source.width, source.height },
			commandBuffer,
			queue,
			logicalDevice,
			vmaDeletionQueue,
			deviceDeletionQueue,
			vk::ImageUsageFlagBits::eSampled,
			mipLevels,
			false);
		if (!upload_rgba_to_image(allocator, commandBuffer, queue, *texture.image, source.rgba, source.name))
		{
			return {};
		}

		texture.sampler = make_model_sampler(logicalDevice, deviceDeletionQueue, samplerSettings, mipLevels);
		return texture;
	}

	LoadedImage make_solid_image(std::string name, unsigned char r, unsigned char g, unsigned char b, unsigned char a)
	{
		LoadedImage image = {};
		image.name = std::move(name);
		image.width = 1;
		image.height = 1;
		image.rgba = { r, g, b, a };
		return image;
	}

	LoadedImage make_fallback_white_image()
	{
		return make_solid_image("3D fallback white texture", 255, 255, 255, 255);
	}

	LoadedImage make_fallback_normal_image()
	{
		return make_solid_image("3D fallback neutral normal texture", 128, 128, 255, 255);
	}

	LoadedImage decode_image_rgba(const std::byte* bytes, std::size_t byteCount, std::string name)
	{
		LoadedImage image = {};
		image.name = std::move(name);
		if (bytes == nullptr || byteCount == 0)
		{
			return image;
		}

		int width = 0;
		int height = 0;
		int channels = 0;
		stbi_uc* pixels = stbi_load_from_memory(
			reinterpret_cast<const stbi_uc*>(bytes),
			static_cast<int>(byteCount),
			&width,
			&height,
			&channels,
			4);
		if (pixels == nullptr || width <= 0 || height <= 0)
		{
			Logger::fetch_logger()->print("Failed to decode 3D texture image " + image.name + ".");
			if (pixels != nullptr)
			{
				stbi_image_free(pixels);
			}
			return image;
		}

		image.width = static_cast<uint32_t>(width);
		image.height = static_cast<uint32_t>(height);
		const std::size_t rgbaSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
		image.rgba.assign(pixels, pixels + rgbaSize);
		stbi_image_free(pixels);
		return image;
	}

	uint32_t to_u32_index(std::size_t index)
	{
		return index > static_cast<std::size_t>(std::numeric_limits<uint32_t>::max())
			? kNoTexture
			: static_cast<uint32_t>(index);
	}

	uint32_t texture_info_index(const fastgltf::Optional<fastgltf::TextureInfo>& textureInfo)
	{
		if (!textureInfo.has_value())
		{
			return kNoTexture;
		}
		return to_u32_index(textureInfo->textureIndex);
	}

	LoadedMaterial primitive_material(const fastgltf::Asset& asset, const fastgltf::Primitive& primitive)
	{
		if (!primitive.materialIndex.has_value() || *primitive.materialIndex >= asset.materials.size())
		{
			return {};
		}

		LoadedMaterial loaded = {};
		const fastgltf::Material& material = asset.materials[*primitive.materialIndex];
		const auto& color = asset.materials[*primitive.materialIndex].pbrData.baseColorFactor;
		loaded.baseColorFactor = {
			static_cast<float>(color[0]),
			static_cast<float>(color[1]),
			static_cast<float>(color[2]),
			static_cast<float>(color[3])
		};
		loaded.metallicFactor = static_cast<float>(material.pbrData.metallicFactor);
		loaded.roughnessFactor = static_cast<float>(material.pbrData.roughnessFactor);
		loaded.alphaCutoff = static_cast<float>(material.alphaCutoff);
		switch (material.alphaMode)
		{
			case fastgltf::AlphaMode::Mask:
				loaded.alphaMode = 1.0f;
				break;
			case fastgltf::AlphaMode::Blend:
				loaded.alphaMode = 2.0f;
				break;
			case fastgltf::AlphaMode::Opaque:
			default:
				loaded.alphaMode = 0.0f;
				break;
		}
		loaded.emissiveFactor = {
			static_cast<float>(material.emissiveFactor[0]),
			static_cast<float>(material.emissiveFactor[1]),
			static_cast<float>(material.emissiveFactor[2])
		};
		loaded.baseColorTexture = texture_info_index(material.pbrData.baseColorTexture);
		loaded.metallicRoughnessTexture = texture_info_index(material.pbrData.metallicRoughnessTexture);
		loaded.emissiveTexture = texture_info_index(material.emissiveTexture);
		if (material.normalTexture.has_value())
		{
			loaded.normalTexture = to_u32_index(material.normalTexture->textureIndex);
			loaded.normalScale = static_cast<float>(material.normalTexture->scale);
		}
		if (material.occlusionTexture.has_value())
		{
			loaded.occlusionTexture = to_u32_index(material.occlusionTexture->textureIndex);
			loaded.occlusionStrength = static_cast<float>(material.occlusionTexture->strength);
		}

		return loaded;
	}

	glm::mat4 to_glm(const fastgltf::math::fmat4x4& matrix)
	{
		glm::mat4 result { 1.0f };
		for (std::size_t column = 0; column < 4; ++column)
		{
			for (std::size_t row = 0; row < 4; ++row)
			{
				result[column][row] = matrix[column][row];
			}
		}
		return result;
	}

	glm::vec3 transformed_normal(const glm::mat4& transform, glm::vec3 normal)
	{
		glm::vec3 transformed = glm::mat3(transform) * normal;
		if (glm::length2(transformed) <= 0.0001f)
		{
			return { 0.0f, 0.0f, 1.0f };
		}
		return glm::normalize(transformed);
	}

	struct ClusterAccumulator
	{
		glm::vec3 position { 0.0f };
		glm::vec3 normal { 0.0f };
		glm::vec3 color { 0.0f };
		glm::vec2 uv { 0.0f };
		glm::vec4 tangent { 0.0f };
		glm::vec4 material { 0.0f };
		glm::vec4 material2 { 0.0f };
		glm::vec4 material3 { 0.0f };
		uint32_t count = 0;
	};

	uint32_t mesh_triangle_count(const LoadedMesh& mesh)
	{
		return static_cast<uint32_t>((mesh.indices.size() - (mesh.indices.size() % 3)) / 3);
	}

	uint32_t cluster_axis(float position, float minPosition, float extent, uint32_t gridResolution)
	{
		if (extent <= 0.0001f)
		{
			return gridResolution / 2u;
		}

		const float normalized = std::clamp((position - minPosition) / extent, 0.0f, 0.999999f);
		return std::min(static_cast<uint32_t>(std::floor(normalized * static_cast<float>(gridResolution))),
			gridResolution - 1u);
	}

	uint64_t cluster_key(glm::vec3 position, glm::vec3 minPos, glm::vec3 extents, uint32_t gridResolution)
	{
		const uint64_t x = cluster_axis(position.x, minPos.x, extents.x, gridResolution);
		const uint64_t y = cluster_axis(position.y, minPos.y, extents.y, gridResolution);
		const uint64_t z = cluster_axis(position.z, minPos.z, extents.z, gridResolution);
		return x | (y << 20u) | (z << 40u);
	}

	uint64_t triangle_key(uint32_t a, uint32_t b, uint32_t c)
	{
		if (a > b)
		{
			std::swap(a, b);
		}
		if (b > c)
		{
			std::swap(b, c);
		}
		if (a > b)
		{
			std::swap(a, b);
		}

		constexpr uint64_t mask = (1ull << 21u) - 1ull;
		return (static_cast<uint64_t>(a) & mask) |
			((static_cast<uint64_t>(b) & mask) << 21u) |
			((static_cast<uint64_t>(c) & mask) << 42u);
	}

	LoadedMesh build_clustered_mesh(const LoadedMesh& mesh, uint32_t gridResolution)
	{
		LoadedMesh result;
		result.name = mesh.name + " preview LOD";
		if (mesh.vertices.empty() || mesh.indices.size() < 3u || gridResolution < 2u)
		{
			return result;
		}

		glm::vec3 minPos(std::numeric_limits<float>::max());
		glm::vec3 maxPos(std::numeric_limits<float>::lowest());
		for (const LoadedVertex& vertex : mesh.vertices)
		{
			minPos = glm::min(minPos, vertex.position);
			maxPos = glm::max(maxPos, vertex.position);
		}
		const glm::vec3 extents = maxPos - minPos;

		std::unordered_map<uint64_t, uint32_t> clusterMap;
		clusterMap.reserve(mesh.vertices.size());
		std::vector<uint32_t> remap(mesh.vertices.size(), 0u);
		std::vector<ClusterAccumulator> clusters;
		clusters.reserve(mesh.vertices.size());

		for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
		{
			const LoadedVertex& vertex = mesh.vertices[i];
			const uint64_t key = cluster_key(vertex.position, minPos, extents, gridResolution);
			auto [it, inserted] = clusterMap.emplace(key, static_cast<uint32_t>(clusters.size()));
			if (inserted)
			{
				clusters.push_back({});
			}

			const uint32_t clusterIndex = it->second;
			remap[i] = clusterIndex;
			ClusterAccumulator& cluster = clusters[clusterIndex];
			cluster.position += vertex.position;
			cluster.normal += vertex.normal;
			cluster.color += vertex.color;
			cluster.uv += vertex.uv;
			cluster.tangent += vertex.tangent;
			cluster.material += vertex.material;
			cluster.material2 += vertex.material2;
			cluster.material3 += vertex.material3;
			++cluster.count;
		}

		result.vertices.resize(clusters.size());
		for (std::size_t i = 0; i < clusters.size(); ++i)
		{
			const ClusterAccumulator& cluster = clusters[i];
			const float invCount = cluster.count > 0u ? 1.0f / static_cast<float>(cluster.count) : 1.0f;
			LoadedVertex vertex = {};
			vertex.position = cluster.position * invCount;
			vertex.normal = glm::length2(cluster.normal) > 0.0001f
				? glm::normalize(cluster.normal)
				: glm::vec3 { 0.0f, 0.0f, 1.0f };
			vertex.color = glm::clamp(cluster.color * invCount, glm::vec3(0.0f), glm::vec3(1.0f));
			vertex.uv = cluster.uv * invCount;
			vertex.tangent = glm::length2(glm::vec3(cluster.tangent)) > 0.0001f
				? glm::vec4(glm::normalize(glm::vec3(cluster.tangent)), cluster.tangent.w >= 0.0f ? 1.0f : -1.0f)
				: glm::vec4 { 1.0f, 0.0f, 0.0f, 1.0f };
			vertex.material = cluster.material * invCount;
			vertex.material2 = cluster.material2 * invCount;
			vertex.material3 = cluster.material3 * invCount;
			result.vertices[i] = vertex;
		}

		std::unordered_set<uint64_t> emittedTriangles;
		emittedTriangles.reserve(mesh.indices.size() / 3u);
		const std::size_t triangleIndexCount = mesh.indices.size() - (mesh.indices.size() % 3);
		for (std::size_t i = 0; i + 2 < triangleIndexCount; i += 3)
		{
			const uint32_t ia = mesh.indices[i];
			const uint32_t ib = mesh.indices[i + 1u];
			const uint32_t ic = mesh.indices[i + 2u];
			if (ia >= remap.size() || ib >= remap.size() || ic >= remap.size())
			{
				continue;
			}

			const uint32_t a = remap[ia];
			const uint32_t b = remap[ib];
			const uint32_t c = remap[ic];
			if (a == b || b == c || c == a)
			{
				continue;
			}

			if (!emittedTriangles.insert(triangle_key(a, b, c)).second)
			{
				continue;
			}

			result.indices.push_back(a);
			result.indices.push_back(b);
			result.indices.push_back(c);
		}

		if (!result.indices.empty())
		{
			result.ranges.push_back({
				0u,
				static_cast<uint32_t>(result.indices.size() - (result.indices.size() % 3)),
				mesh.ranges.empty() ? LoadedMaterial {} : mesh.ranges.front().material
			});
		}

		return result;
	}

	void simplify_mesh_for_preview(LoadedMesh& mesh, uint32_t targetTriangleCount)
	{
		const uint32_t sourceTriangleCount = mesh_triangle_count(mesh);
		if (targetTriangleCount == 0u || sourceTriangleCount <= targetTriangleCount)
		{
			return;
		}

		Logger* logger = Logger::fetch_logger();
		calculate_missing_normals(mesh);

		LoadedMesh bestUnderTarget;
		uint32_t bestUnderTargetCount = 0;
		LoadedMesh smallestOverTarget;
		uint32_t smallestOverTargetCount = sourceTriangleCount;

		for (uint32_t gridResolution = 4u; gridResolution <= 96u; gridResolution += 2u)
		{
			LoadedMesh candidate = build_clustered_mesh(mesh, gridResolution);
			const uint32_t candidateTriangleCount = mesh_triangle_count(candidate);
			if (candidateTriangleCount == 0u || candidateTriangleCount >= sourceTriangleCount)
			{
				continue;
			}

			if (candidateTriangleCount <= targetTriangleCount)
			{
				if (candidateTriangleCount > bestUnderTargetCount)
				{
					bestUnderTarget = std::move(candidate);
					bestUnderTargetCount = candidateTriangleCount;
				}
			}
			else if (candidateTriangleCount < smallestOverTargetCount)
			{
				smallestOverTarget = std::move(candidate);
				smallestOverTargetCount = candidateTriangleCount;
			}
		}

		if (bestUnderTargetCount > 0u)
		{
			logger->print("Simplified preview mesh '" + mesh.name + "' from " +
				std::to_string(sourceTriangleCount) + " to " + std::to_string(bestUnderTargetCount) + " triangles.");
			mesh = std::move(bestUnderTarget);
			return;
		}

		if (smallestOverTargetCount < sourceTriangleCount)
		{
			logger->print("Simplified preview mesh '" + mesh.name + "' from " +
				std::to_string(sourceTriangleCount) + " to " + std::to_string(smallestOverTargetCount) +
				" triangles; target was too low for the current clustering pass.");
			mesh = std::move(smallestOverTarget);
		}
	}

	void normalise_for_demo_view(std::vector<Vertex>& vertices)
	{
		if (vertices.empty())
		{
			return;
		}

		glm::vec3 minPos(std::numeric_limits<float>::max());
		glm::vec3 maxPos(std::numeric_limits<float>::lowest());
		for (const Vertex& vertex : vertices)
		{
			minPos = glm::min(minPos, vertex.pos);
			maxPos = glm::max(maxPos, vertex.pos);
		}

		const glm::vec3 center = 0.5f * (minPos + maxPos);
		const glm::vec3 extents = maxPos - minPos;
		const float maxExtent = std::max({ extents.x, extents.y, extents.z, 0.001f });

		for (Vertex& vertex : vertices)
		{
			const glm::vec3 p = (vertex.pos - center) / maxExtent;
			vertex.pos = glm::vec3(p.x * 1.45f, p.y * 1.45f, p.z * 1.45f);
		}
	}

	template<typename T>
	T read_le(const std::vector<std::byte>& bytes, std::size_t offset)
	{
		T value {};
		std::memcpy(&value, bytes.data() + offset, sizeof(T));
		return value;
	}

	std::size_t find_matching(const std::string& text, std::size_t openPos, char openChar, char closeChar)
	{
		uint32_t depth = 0;
		for (std::size_t i = openPos; i < text.size(); ++i)
		{
			if (text[i] == openChar)
			{
				++depth;
			}
			else if (text[i] == closeChar)
			{
				--depth;
				if (depth == 0)
				{
					return i;
				}
			}
		}
		return std::string::npos;
	}

	std::string extract_array(const std::string& json, std::string_view key)
	{
		const std::string marker = "\"" + std::string(key) + "\":[";
		const std::size_t markerPos = json.find(marker);
		if (markerPos == std::string::npos)
		{
			return {};
		}

		const std::size_t arrayStart = markerPos + marker.size() - 1;
		const std::size_t arrayEnd = find_matching(json, arrayStart, '[', ']');
		if (arrayEnd == std::string::npos)
		{
			return {};
		}

		return json.substr(arrayStart + 1, arrayEnd - arrayStart - 1);
	}

	std::string extract_object(const std::string& json, std::string_view key)
	{
		const std::string marker = "\"" + std::string(key) + "\":{";
		const std::size_t markerPos = json.find(marker);
		if (markerPos == std::string::npos)
		{
			return {};
		}

		const std::size_t objectStart = markerPos + marker.size() - 1;
		const std::size_t objectEnd = find_matching(json, objectStart, '{', '}');
		if (objectEnd == std::string::npos)
		{
			return {};
		}

		return json.substr(objectStart, objectEnd - objectStart + 1);
	}

	std::vector<std::string> collect_objects(const std::string& arrayText)
	{
		std::vector<std::string> objects;
		for (std::size_t i = 0; i < arrayText.size(); ++i)
		{
			if (arrayText[i] != '{')
			{
				continue;
			}

			const std::size_t objectEnd = find_matching(arrayText, i, '{', '}');
			if (objectEnd == std::string::npos)
			{
				break;
			}

			objects.push_back(arrayText.substr(i, objectEnd - i + 1));
			i = objectEnd;
		}
		return objects;
	}

	uint64_t field_u64(const std::string& objectText, std::string_view key, uint64_t fallback = 0)
	{
		const std::string marker = "\"" + std::string(key) + "\":";
		const std::size_t markerPos = objectText.find(marker);
		if (markerPos == std::string::npos)
		{
			return fallback;
		}

		std::size_t valuePos = markerPos + marker.size();
		while (valuePos < objectText.size() && objectText[valuePos] == ' ')
		{
			++valuePos;
		}

		std::size_t valueEnd = valuePos;
		while (valueEnd < objectText.size() && objectText[valueEnd] >= '0' && objectText[valueEnd] <= '9')
		{
			++valueEnd;
		}

		if (valueEnd == valuePos)
		{
			return fallback;
		}
		return std::stoull(objectText.substr(valuePos, valueEnd - valuePos));
	}

	std::string field_string(const std::string& objectText, std::string_view key, std::string_view fallback = {})
	{
		const std::string marker = "\"" + std::string(key) + "\":\"";
		const std::size_t markerPos = objectText.find(marker);
		if (markerPos == std::string::npos)
		{
			return std::string(fallback);
		}

		const std::size_t valuePos = markerPos + marker.size();
		const std::size_t valueEnd = objectText.find('"', valuePos);
		if (valueEnd == std::string::npos)
		{
			return std::string(fallback);
		}
		return objectText.substr(valuePos, valueEnd - valuePos);
	}

	bool field_bool(const std::string& objectText, std::string_view key, bool fallback = false)
	{
		const std::string marker = "\"" + std::string(key) + "\":";
		const std::size_t markerPos = objectText.find(marker);
		if (markerPos == std::string::npos)
		{
			return fallback;
		}
		return objectText.compare(markerPos + marker.size(), 4, "true") == 0;
	}

	std::vector<float> parse_float_array(const std::string& arrayText)
	{
		std::vector<float> values;
		const char* cursor = arrayText.c_str();
		while (*cursor != '\0')
		{
			while (*cursor == ' ' || *cursor == ',' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t')
			{
				++cursor;
			}

			char* end = nullptr;
			const float value = std::strtof(cursor, &end);
			if (end == cursor)
			{
				break;
			}

			values.push_back(value);
			cursor = end;
		}
		return values;
	}

	std::vector<uint32_t> parse_u32_array(const std::string& arrayText)
	{
		std::vector<uint32_t> values;
		const char* cursor = arrayText.c_str();
		while (*cursor != '\0')
		{
			while (*cursor == ' ' || *cursor == ',' || *cursor == '\n' || *cursor == '\r' || *cursor == '\t')
			{
				++cursor;
			}

			char* end = nullptr;
			const unsigned long value = std::strtoul(cursor, &end, 10);
			if (end == cursor)
			{
				break;
			}

			values.push_back(static_cast<uint32_t>(value));
			cursor = end;
		}
		return values;
	}

	std::vector<float> field_float_array(const std::string& objectText, std::string_view key)
	{
		std::string arrayText = extract_array(objectText, key);
		if (arrayText.empty())
		{
			return {};
		}
		return parse_float_array(arrayText);
	}

	float field_float(const std::string& objectText, std::string_view key, float fallback = 0.0f)
	{
		const std::string marker = "\"" + std::string(key) + "\":";
		const std::size_t markerPos = objectText.find(marker);
		if (markerPos == std::string::npos)
		{
			return fallback;
		}

		std::size_t valuePos = markerPos + marker.size();
		while (valuePos < objectText.size() &&
			(objectText[valuePos] == ' ' || objectText[valuePos] == '\n' || objectText[valuePos] == '\r' || objectText[valuePos] == '\t'))
		{
			++valuePos;
		}

		char* end = nullptr;
		const float value = std::strtof(objectText.c_str() + valuePos, &end);
		return end == objectText.c_str() + valuePos ? fallback : value;
	}

	std::vector<uint32_t> field_u32_array(const std::string& objectText, std::string_view key)
	{
		std::string arrayText = extract_array(objectText, key);
		if (arrayText.empty())
		{
			return {};
		}
		return parse_u32_array(arrayText);
	}

	glm::mat4 field_transform(const std::string& nodeText)
	{
		const std::vector<float> matrix = field_float_array(nodeText, "matrix");
		if (matrix.size() >= 16)
		{
			glm::mat4 transform { 1.0f };
			for (std::size_t column = 0; column < 4; ++column)
			{
				for (std::size_t row = 0; row < 4; ++row)
				{
					transform[column][row] = matrix[column * 4 + row];
				}
			}
			return transform;
		}

		glm::mat4 transform { 1.0f };
		const std::vector<float> translation = field_float_array(nodeText, "translation");
		if (translation.size() >= 3)
		{
			transform = glm::translate(transform, glm::vec3 { translation[0], translation[1], translation[2] });
		}

		const std::vector<float> rotation = field_float_array(nodeText, "rotation");
		if (rotation.size() >= 4)
		{
			const glm::quat quaternion(rotation[3], rotation[0], rotation[1], rotation[2]);
			transform *= glm::mat4_cast(quaternion);
		}

		const std::vector<float> scale = field_float_array(nodeText, "scale");
		if (scale.size() >= 3)
		{
			transform = glm::scale(transform, glm::vec3 { scale[0], scale[1], scale[2] });
		}

		return transform;
	}

	float read_component(const std::byte* data, uint64_t componentType, bool normalized)
	{
		switch (componentType)
		{
			case 5120:
			{
				int8_t value;
				std::memcpy(&value, data, sizeof(value));
				return normalized ? std::max(static_cast<float>(value) / 127.0f, -1.0f) : static_cast<float>(value);
			}
			case 5121:
			{
				uint8_t value;
				std::memcpy(&value, data, sizeof(value));
				return normalized ? static_cast<float>(value) / 255.0f : static_cast<float>(value);
			}
			case 5122:
			{
				int16_t value;
				std::memcpy(&value, data, sizeof(value));
				return normalized ? std::max(static_cast<float>(value) / 32767.0f, -1.0f) : static_cast<float>(value);
			}
			case 5123:
			{
				uint16_t value;
				std::memcpy(&value, data, sizeof(value));
				return normalized ? static_cast<float>(value) / 65535.0f : static_cast<float>(value);
			}
			case 5125:
			{
				uint32_t value;
				std::memcpy(&value, data, sizeof(value));
				return static_cast<float>(value);
			}
			case 5126:
			default:
			{
				float value;
				std::memcpy(&value, data, sizeof(value));
				return value;
			}
		}
	}

	uint32_t component_size(uint64_t componentType)
	{
		switch (componentType)
		{
			case 5120:
			case 5121:
				return 1;
			case 5122:
			case 5123:
				return 2;
			case 5125:
			case 5126:
			default:
				return 4;
		}
	}

	uint32_t component_count(std::string_view type)
	{
		if (type == "SCALAR") return 1;
		if (type == "VEC2") return 2;
		if (type == "VEC3") return 3;
		if (type == "VEC4") return 4;
		return 1;
	}

	struct JsonAccessorView
	{
		const std::byte* bytes = nullptr;
		uint64_t componentType = 5126;
		uint32_t components = 1;
		uint32_t stride = 4;
		uint64_t count = 0;
		bool normalized = false;
	};

	bool make_accessor_view(
		const std::vector<std::byte>& binBytes,
		const std::vector<std::string>& accessors,
		const std::vector<std::string>& bufferViews,
		uint64_t accessorIndex,
		JsonAccessorView& view)
	{
		if (accessorIndex >= accessors.size())
		{
			return false;
		}

		const std::string& accessor = accessors[static_cast<std::size_t>(accessorIndex)];
		const uint64_t bufferViewIndex = field_u64(accessor, "bufferView", UINT64_MAX);
		if (bufferViewIndex >= bufferViews.size())
		{
			return false;
		}

		const std::string& bufferView = bufferViews[static_cast<std::size_t>(bufferViewIndex)];

		view.componentType = field_u64(accessor, "componentType", 5126);
		view.components = component_count(field_string(accessor, "type", "SCALAR"));
		view.count = field_u64(accessor, "count");
		view.normalized = field_bool(accessor, "normalized", false);

		const uint64_t bufferViewOffset = field_u64(bufferView, "byteOffset");
		const uint64_t accessorOffset = field_u64(accessor, "byteOffset");
		const uint64_t byteStride = field_u64(bufferView, "byteStride");
		view.stride = static_cast<uint32_t>(byteStride != 0 ? byteStride : view.components * component_size(view.componentType));
		const uint64_t totalOffset = bufferViewOffset + accessorOffset;
		const uint64_t elementSize =
			static_cast<uint64_t>(view.components) * component_size(view.componentType);
		const uint64_t requiredSize = totalOffset + (view.count == 0 ? 0 : (view.count - 1) * view.stride + elementSize);
		if (requiredSize > binBytes.size())
		{
			return false;
		}

		view.bytes = binBytes.data() + totalOffset;
		return true;
	}

	glm::vec3 read_vec3(const JsonAccessorView& view, uint64_t index)
	{
		const std::byte* data = view.bytes + index * view.stride;
		return {
			read_component(data, view.componentType, view.normalized),
			read_component(data + component_size(view.componentType), view.componentType, view.normalized),
			read_component(data + 2 * component_size(view.componentType), view.componentType, view.normalized)
		};
	}

	glm::vec4 read_vec4(const JsonAccessorView& view, uint64_t index)
	{
		const std::byte* data = view.bytes + index * view.stride;
		return {
			read_component(data, view.componentType, view.normalized),
			read_component(data + component_size(view.componentType), view.componentType, view.normalized),
			read_component(data + 2 * component_size(view.componentType), view.componentType, view.normalized),
			read_component(data + 3 * component_size(view.componentType), view.componentType, view.normalized)
		};
	}

	glm::vec2 read_vec2(const JsonAccessorView& view, uint64_t index)
	{
		const std::byte* data = view.bytes + index * view.stride;
		return {
			read_component(data, view.componentType, view.normalized),
			read_component(data + component_size(view.componentType), view.componentType, view.normalized)
		};
	}

	uint32_t read_json_index(const JsonAccessorView& view, uint64_t index)
	{
		const std::byte* data = view.bytes + index * view.stride;
		switch (view.componentType)
		{
			case 5121:
			{
				uint8_t value;
				std::memcpy(&value, data, sizeof(value));
				return value;
			}
			case 5123:
			{
				uint16_t value;
				std::memcpy(&value, data, sizeof(value));
				return value;
			}
			case 5125:
			default:
			{
				uint32_t value;
				std::memcpy(&value, data, sizeof(value));
				return value;
			}
		}
	}

	uint32_t json_texture_info_index(const std::string& materialText, std::string_view key)
	{
		const std::string textureInfo = extract_object(materialText, key);
		const uint64_t textureIndex = field_u64(textureInfo, "index", UINT64_MAX);
		return textureIndex > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
			? kNoTexture
			: static_cast<uint32_t>(textureIndex);
	}

	LoadedMaterial json_material_from_primitive(
		const std::string& primitiveText,
		const std::vector<std::string>& materials)
	{
		const uint64_t materialIndex = field_u64(primitiveText, "material", UINT64_MAX);
		if (materialIndex >= materials.size())
		{
			return {};
		}

		LoadedMaterial loaded = {};
		const std::string& materialText = materials[static_cast<std::size_t>(materialIndex)];
		const std::string pbrText = extract_object(materialText, "pbrMetallicRoughness");
		const std::vector<float> baseColor =
			field_float_array(pbrText.empty() ? materialText : pbrText, "baseColorFactor");
		if (baseColor.size() >= 3u)
		{
			loaded.baseColorFactor.r = baseColor[0];
			loaded.baseColorFactor.g = baseColor[1];
			loaded.baseColorFactor.b = baseColor[2];
			loaded.baseColorFactor.a = baseColor.size() >= 4u ? baseColor[3] : 1.0f;
		}

		loaded.metallicFactor = field_float(pbrText.empty() ? materialText : pbrText, "metallicFactor", 1.0f);
		loaded.roughnessFactor = field_float(pbrText.empty() ? materialText : pbrText, "roughnessFactor", 1.0f);
		const std::string alphaMode = field_string(materialText, "alphaMode", "OPAQUE");
		if (alphaMode == "MASK")
		{
			loaded.alphaMode = 1.0f;
		}
		else if (alphaMode == "BLEND")
		{
			loaded.alphaMode = 2.0f;
		}
		loaded.alphaCutoff = field_float(materialText, "alphaCutoff", 0.5f);
		const std::vector<float> emissiveFactor = field_float_array(materialText, "emissiveFactor");
		if (emissiveFactor.size() >= 3u)
		{
			loaded.emissiveFactor = {
				emissiveFactor[0],
				emissiveFactor[1],
				emissiveFactor[2]
			};
		}
		loaded.baseColorTexture = json_texture_info_index(pbrText.empty() ? materialText : pbrText, "baseColorTexture");
		loaded.metallicRoughnessTexture = json_texture_info_index(pbrText.empty() ? materialText : pbrText, "metallicRoughnessTexture");
		loaded.emissiveTexture = json_texture_info_index(materialText, "emissiveTexture");

		const std::string normalInfo = extract_object(materialText, "normalTexture");
		const uint64_t normalTextureIndex = field_u64(normalInfo, "index", UINT64_MAX);
		loaded.normalTexture = normalTextureIndex > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
			? kNoTexture
			: static_cast<uint32_t>(normalTextureIndex);
		loaded.normalScale = field_float(normalInfo, "scale", 1.0f);

		const std::string occlusionInfo = extract_object(materialText, "occlusionTexture");
		const uint64_t occlusionTextureIndex = field_u64(occlusionInfo, "index", UINT64_MAX);
		loaded.occlusionTexture = occlusionTextureIndex > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
			? kNoTexture
			: static_cast<uint32_t>(occlusionTextureIndex);
		loaded.occlusionStrength = field_float(occlusionInfo, "strength", 1.0f);
		return loaded;
	}

	LoadedSampler json_sampler_from_object(const std::string& samplerText)
	{
		LoadedSampler sampler = {};
		if (samplerText.empty())
		{
			return sampler;
		}

		sampler.magFilter = static_cast<uint32_t>(field_u64(samplerText, "magFilter", sampler.magFilter));
		sampler.minFilter = static_cast<uint32_t>(field_u64(samplerText, "minFilter", sampler.minFilter));
		sampler.wrapS = static_cast<uint32_t>(field_u64(samplerText, "wrapS", sampler.wrapS));
		sampler.wrapT = static_cast<uint32_t>(field_u64(samplerText, "wrapT", sampler.wrapT));
		return sampler;
	}

	std::vector<LoadedTexture> json_textures_from_objects(
		const std::vector<std::string>& textureTexts,
		const std::vector<std::string>& samplerTexts)
	{
		std::vector<LoadedSampler> samplers(samplerTexts.size());
		for (std::size_t i = 0; i < samplerTexts.size(); ++i)
		{
			samplers[i] = json_sampler_from_object(samplerTexts[i]);
		}

		std::vector<LoadedTexture> textures(textureTexts.size());
		for (std::size_t i = 0; i < textureTexts.size(); ++i)
		{
			const std::string& textureText = textureTexts[i];
			LoadedTexture texture = {};
			const uint64_t imageIndex = field_u64(textureText, "source", UINT64_MAX);
			texture.imageIndex = imageIndex > static_cast<uint64_t>(std::numeric_limits<uint32_t>::max())
				? kNoTexture
				: static_cast<uint32_t>(imageIndex);

			const uint64_t samplerIndex = field_u64(textureText, "sampler", UINT64_MAX);
			if (samplerIndex < samplers.size())
			{
				texture.sampler = samplers[static_cast<std::size_t>(samplerIndex)];
			}
			textures[i] = texture;
		}
		return textures;
	}

	std::vector<LoadedImage> decode_glb_images_json(
		const std::vector<std::byte>& binBytes,
		const std::vector<std::string>& bufferViews,
		const std::vector<std::string>& images)
	{
		std::vector<LoadedImage> decoded(images.size());
		for (std::size_t imageIndex = 0; imageIndex < images.size(); ++imageIndex)
		{
			const std::string& imageText = images[imageIndex];
			const uint64_t bufferViewIndex = field_u64(imageText, "bufferView", UINT64_MAX);
			if (bufferViewIndex >= bufferViews.size())
			{
				continue;
			}

			const std::string& bufferView = bufferViews[static_cast<std::size_t>(bufferViewIndex)];
			const uint64_t byteOffset = field_u64(bufferView, "byteOffset");
			const uint64_t byteLength = field_u64(bufferView, "byteLength");
			if (byteLength == 0 || byteOffset + byteLength > binBytes.size())
			{
				continue;
			}

			std::string name = field_string(imageText, "name", "GLB image " + std::to_string(imageIndex));
			decoded[imageIndex] = decode_image_rgba(
				binBytes.data() + byteOffset,
				static_cast<std::size_t>(byteLength),
				std::move(name));
		}
		return decoded;
	}

	std::vector<std::byte> read_binary_bytes(const std::filesystem::path& path)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file)
		{
			return {};
		}

		const std::streamsize size = file.tellg();
		if (size <= 0)
		{
			return {};
		}

		std::vector<std::byte> bytes(static_cast<std::size_t>(size));
		file.seekg(0, std::ios::beg);
		file.read(reinterpret_cast<char*>(bytes.data()), size);
		return bytes;
	}

	std::vector<LoadedImage> decode_fastgltf_images(
		const fastgltf::Asset& asset,
		const std::filesystem::path& directory)
	{
		std::vector<LoadedImage> decoded(asset.images.size());
		for (std::size_t imageIndex = 0; imageIndex < asset.images.size(); ++imageIndex)
		{
			const fastgltf::Image& sourceImage = asset.images[imageIndex];
			const std::string name = sourceImage.name.empty()
				? ("glTF image " + std::to_string(imageIndex))
				: std::string(sourceImage.name);

			if (const auto* bufferView = std::get_if<fastgltf::sources::BufferView>(&sourceImage.data))
			{
				if (bufferView->bufferViewIndex >= asset.bufferViews.size())
				{
					continue;
				}
				auto bytes = fastgltf::DefaultBufferDataAdapter {}(asset, bufferView->bufferViewIndex);
				decoded[imageIndex] = decode_image_rgba(bytes.data(), bytes.size(), name);
			}
			else if (const auto* array = std::get_if<fastgltf::sources::Array>(&sourceImage.data))
			{
				decoded[imageIndex] = decode_image_rgba(array->bytes.data(), array->bytes.size_bytes(), name);
			}
			else if (const auto* vector = std::get_if<fastgltf::sources::Vector>(&sourceImage.data))
			{
				decoded[imageIndex] = decode_image_rgba(vector->bytes.data(), vector->bytes.size(), name);
			}
			else if (const auto* byteView = std::get_if<fastgltf::sources::ByteView>(&sourceImage.data))
			{
				decoded[imageIndex] = decode_image_rgba(byteView->bytes.data(), byteView->bytes.size(), name);
			}
			else if (const auto* uri = std::get_if<fastgltf::sources::URI>(&sourceImage.data))
			{
				const std::filesystem::path imagePath = directory / uri->uri.fspath();
				std::vector<std::byte> bytes = read_binary_bytes(imagePath);
				if (!bytes.empty())
				{
					const std::size_t offset = std::min(uri->fileByteOffset, bytes.size());
					decoded[imageIndex] = decode_image_rgba(bytes.data() + offset, bytes.size() - offset, name);
				}
			}
		}
		return decoded;
	}

	std::vector<LoadedTexture> collect_fastgltf_textures(const fastgltf::Asset& asset)
	{
		std::vector<LoadedTexture> textures(asset.textures.size());
		for (std::size_t textureIndex = 0; textureIndex < asset.textures.size(); ++textureIndex)
		{
			const fastgltf::Texture& source = asset.textures[textureIndex];
			LoadedTexture texture = {};
			if (source.imageIndex.has_value())
			{
				texture.imageIndex = to_u32_index(*source.imageIndex);
			}

			if (source.samplerIndex.has_value() && *source.samplerIndex < asset.samplers.size())
			{
				const fastgltf::Sampler& sampler = asset.samplers[*source.samplerIndex];
				if (sampler.magFilter.has_value())
				{
					texture.sampler.magFilter = static_cast<uint32_t>(*sampler.magFilter);
				}
				if (sampler.minFilter.has_value())
				{
					texture.sampler.minFilter = static_cast<uint32_t>(*sampler.minFilter);
				}
				texture.sampler.wrapS = static_cast<uint32_t>(sampler.wrapS);
				texture.sampler.wrapT = static_cast<uint32_t>(sampler.wrapT);
			}

			textures[textureIndex] = texture;
		}
		return textures;
	}

	LoadedMesh transformed_mesh(const LoadedMesh& source, const glm::mat4& transform)
	{
		LoadedMesh mesh = source;
		for (LoadedVertex& vertex : mesh.vertices)
		{
			vertex.position = glm::vec3(transform * glm::vec4(vertex.position, 1.0f));
			vertex.normal = transformed_normal(transform, vertex.normal);
			const glm::vec3 tangent = glm::mat3(transform) * glm::vec3(vertex.tangent);
			vertex.tangent = glm::length2(tangent) > 0.0001f
				? glm::vec4(glm::normalize(tangent), vertex.tangent.w)
				: glm::vec4 { 1.0f, 0.0f, 0.0f, vertex.tangent.w >= 0.0f ? 1.0f : -1.0f };
		}
		return mesh;
	}

	void append_node_meshes(
		const std::vector<std::string>& nodes,
		const std::vector<LoadedMesh>& sourceMeshes,
		uint32_t nodeIndex,
		const glm::mat4& parentTransform,
		std::vector<LoadedMesh>& outMeshes,
		uint32_t depth = 0)
	{
		if (nodeIndex >= nodes.size() || depth > 128u)
		{
			return;
		}

		const std::string& nodeText = nodes[nodeIndex];
		const glm::mat4 worldTransform = parentTransform * field_transform(nodeText);

		const uint64_t meshIndex = field_u64(nodeText, "mesh", UINT64_MAX);
		if (meshIndex < sourceMeshes.size())
		{
			outMeshes.push_back(transformed_mesh(sourceMeshes[static_cast<std::size_t>(meshIndex)], worldTransform));
		}

		for (uint32_t childIndex : field_u32_array(nodeText, "children"))
		{
			append_node_meshes(nodes, sourceMeshes, childIndex, worldTransform, outMeshes, depth + 1u);
		}
	}

	bool load_glb_meshes_json(
		const std::filesystem::path& path,
		std::vector<LoadedMesh>& outMeshes,
		std::vector<LoadedImage>& outImages,
		std::vector<LoadedTexture>& outTextures)
	{
		std::ifstream file(path, std::ios::binary);
		if (!file)
		{
			return false;
		}

		file.seekg(0, std::ios::end);
		const std::streamsize fileSize = file.tellg();
		file.seekg(0, std::ios::beg);
		if (fileSize <= 0)
		{
			return false;
		}

		std::vector<std::byte> bytes(static_cast<std::size_t>(fileSize));
		file.read(reinterpret_cast<char*>(bytes.data()), fileSize);
		if (bytes.size() < 20 || read_le<uint32_t>(bytes, 0) != 0x46546C67)
		{
			return false;
		}

		std::size_t cursor = 12;
		std::string json;
		std::vector<std::byte> binBytes;
		while (cursor + 8 <= bytes.size())
		{
			const uint32_t chunkLength = read_le<uint32_t>(bytes, cursor);
			const uint32_t chunkType = read_le<uint32_t>(bytes, cursor + 4);
			cursor += 8;
			if (cursor + chunkLength > bytes.size())
			{
				return false;
			}

			if (chunkType == 0x4E4F534A)
			{
				json.assign(reinterpret_cast<const char*>(bytes.data() + cursor), chunkLength);
			}
			else if (chunkType == 0x004E4942)
			{
				binBytes.assign(bytes.begin() + cursor, bytes.begin() + cursor + chunkLength);
			}

			cursor += chunkLength;
		}

		if (json.empty() || binBytes.empty())
		{
			return false;
		}

		const std::vector<std::string> accessors = collect_objects(extract_array(json, "accessors"));
		const std::vector<std::string> bufferViews = collect_objects(extract_array(json, "bufferViews"));
		const std::vector<std::string> materials = collect_objects(extract_array(json, "materials"));
		const std::vector<std::string> textures = collect_objects(extract_array(json, "textures"));
		const std::vector<std::string> samplers = collect_objects(extract_array(json, "samplers"));
		const std::vector<std::string> images = collect_objects(extract_array(json, "images"));
		const std::vector<std::string> meshes = collect_objects(extract_array(json, "meshes"));
		const std::vector<std::string> nodes = collect_objects(extract_array(json, "nodes"));
		const std::vector<std::string> scenes = collect_objects(extract_array(json, "scenes"));
		if (accessors.empty() || bufferViews.empty() || meshes.empty())
		{
			return false;
		}
		outImages = decode_glb_images_json(binBytes, bufferViews, images);
		outTextures = json_textures_from_objects(textures, samplers);

		for (std::size_t meshIndex = 0; meshIndex < meshes.size(); ++meshIndex)
		{
			const std::string& meshText = meshes[meshIndex];
			LoadedMesh mesh;
			mesh.name = field_string(meshText, "name", "Mesh " + std::to_string(meshIndex));

			const std::vector<std::string> primitives = collect_objects(extract_array(meshText, "primitives"));
			for (const std::string& primitive : primitives)
			{
				if (field_u64(primitive, "mode", 4) != 4)
				{
					continue;
				}
				const LoadedMaterial material = json_material_from_primitive(primitive, materials);

				JsonAccessorView positions;
				if (!make_accessor_view(binBytes, accessors, bufferViews, field_u64(primitive, "POSITION", UINT64_MAX), positions))
				{
					continue;
				}

				const std::size_t initialVertex = mesh.vertices.size();
				mesh.vertices.resize(mesh.vertices.size() + static_cast<std::size_t>(positions.count));
				for (uint64_t i = 0; i < positions.count; ++i)
				{
					LoadedVertex vertex;
					vertex.position = read_vec3(positions, i);
					vertex.normal = glm::vec3(0.0f);
					vertex.color = material.vertex_color();
					vertex.material = material.shader_factors();
					vertex.material2 = material.shader_factors2();
					vertex.material3 = material.shader_factors3();
					mesh.vertices[initialVertex + static_cast<std::size_t>(i)] = vertex;
				}

				JsonAccessorView normals;
				if (make_accessor_view(binBytes, accessors, bufferViews, field_u64(primitive, "NORMAL", UINT64_MAX), normals))
				{
					const uint64_t normalCount = std::min(normals.count, positions.count);
					for (uint64_t i = 0; i < normalCount; ++i)
					{
						mesh.vertices[initialVertex + static_cast<std::size_t>(i)].normal = read_vec3(normals, i);
					}
				}

				JsonAccessorView colors;
				if (make_accessor_view(binBytes, accessors, bufferViews, field_u64(primitive, "COLOR_0", UINT64_MAX), colors))
				{
					const uint64_t colorCount = std::min(colors.count, positions.count);
					for (uint64_t i = 0; i < colorCount; ++i)
					{
						mesh.vertices[initialVertex + static_cast<std::size_t>(i)].color =
							glm::clamp(read_vec3(colors, i) * material.vertex_color(), glm::vec3(0.0f), glm::vec3(1.0f));
					}
				}

				JsonAccessorView texCoords;
				if (make_accessor_view(binBytes, accessors, bufferViews, field_u64(primitive, "TEXCOORD_0", UINT64_MAX), texCoords))
				{
					const uint64_t uvCount = std::min(texCoords.count, positions.count);
					for (uint64_t i = 0; i < uvCount; ++i)
					{
						mesh.vertices[initialVertex + static_cast<std::size_t>(i)].uv = read_vec2(texCoords, i);
					}
				}

				JsonAccessorView tangents;
				if (make_accessor_view(binBytes, accessors, bufferViews, field_u64(primitive, "TANGENT", UINT64_MAX), tangents))
				{
					const uint64_t tangentCount = std::min(tangents.count, positions.count);
					for (uint64_t i = 0; i < tangentCount; ++i)
					{
						glm::vec4 tangent = read_vec4(tangents, i);
						const glm::vec3 tangentDirection = glm::vec3(tangent);
						mesh.vertices[initialVertex + static_cast<std::size_t>(i)].tangent =
							glm::length2(tangentDirection) > 0.0001f
								? glm::vec4(glm::normalize(tangentDirection), tangent.w >= 0.0f ? 1.0f : -1.0f)
								: glm::vec4 { 1.0f, 0.0f, 0.0f, 1.0f };
					}
				}

				const uint32_t firstIndex = static_cast<uint32_t>(mesh.indices.size());
				JsonAccessorView indices;
				if (make_accessor_view(binBytes, accessors, bufferViews, field_u64(primitive, "indices", UINT64_MAX), indices))
				{
					mesh.indices.reserve(mesh.indices.size() + static_cast<std::size_t>(indices.count));
					for (uint64_t i = 0; i < indices.count; ++i)
					{
						mesh.indices.push_back(static_cast<uint32_t>(initialVertex) + read_json_index(indices, i));
					}
				}
				else
				{
					mesh.indices.reserve(mesh.indices.size() + static_cast<std::size_t>(positions.count));
					for (uint64_t i = 0; i < positions.count; ++i)
					{
						mesh.indices.push_back(static_cast<uint32_t>(initialVertex + static_cast<std::size_t>(i)));
					}
				}
				const uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size() - firstIndex);
				if (indexCount >= 3u)
				{
					mesh.ranges.push_back({ firstIndex, indexCount - (indexCount % 3u), material });
				}
			}

			if (!mesh.indices.empty() && !mesh.vertices.empty())
			{
				outMeshes.push_back(std::move(mesh));
			}
		}

		if (outMeshes.empty())
		{
			return false;
		}

		if (!nodes.empty() && !scenes.empty())
		{
			std::vector<LoadedMesh> sourceMeshes = std::move(outMeshes);
			std::vector<LoadedMesh> sceneMeshes;
			const uint64_t requestedSceneIndex = field_u64(json, "scene", 0);
			const std::size_t sceneIndex = requestedSceneIndex < scenes.size()
				? static_cast<std::size_t>(requestedSceneIndex)
				: 0u;

			for (uint32_t rootNode : field_u32_array(scenes[sceneIndex], "nodes"))
			{
				append_node_meshes(nodes, sourceMeshes, rootNode, glm::mat4 { 1.0f }, sceneMeshes);
			}

			if (!sceneMeshes.empty())
			{
				outMeshes = std::move(sceneMeshes);
			}
			else
			{
				outMeshes = std::move(sourceMeshes);
			}
		}

		return !outMeshes.empty();
	}

	LoadedMesh merge_loaded_meshes(std::vector<LoadedMesh>& meshes)
	{
		LoadedMesh merged;
		merged.name = "Merged GLB scene";

		std::size_t totalVertices = 0;
		std::size_t totalIndices = 0;
		for (const LoadedMesh& mesh : meshes)
		{
			totalVertices += mesh.vertices.size();
			totalIndices += mesh.indices.size();
		}

		merged.vertices.reserve(totalVertices);
		merged.indices.reserve(totalIndices);
		for (LoadedMesh& mesh : meshes)
		{
			const uint32_t vertexOffset = static_cast<uint32_t>(merged.vertices.size());
			const uint32_t indexOffset = static_cast<uint32_t>(merged.indices.size());
			merged.vertices.insert(merged.vertices.end(),
				std::make_move_iterator(mesh.vertices.begin()),
				std::make_move_iterator(mesh.vertices.end()));

			for (uint32_t index : mesh.indices)
			{
				merged.indices.push_back(vertexOffset + index);
			}

			for (const LoadedPrimitiveRange& range : mesh.ranges)
			{
				if (range.indexCount >= 3u)
				{
					merged.ranges.push_back({
						indexOffset + range.firstIndex,
						range.indexCount - (range.indexCount % 3u),
						range.material
					});
				}
			}
		}

		return merged;
	}

	void calculate_missing_normals(LoadedMesh& mesh)
	{
		std::vector<glm::vec3> accumulated(mesh.vertices.size(), glm::vec3(0.0f));
		for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		{
			const uint32_t ia = mesh.indices[i];
			const uint32_t ib = mesh.indices[i + 1];
			const uint32_t ic = mesh.indices[i + 2];
			if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size())
			{
				continue;
			}

			const glm::vec3 a = mesh.vertices[ia].position;
			const glm::vec3 b = mesh.vertices[ib].position;
			const glm::vec3 c = mesh.vertices[ic].position;
			const glm::vec3 normal = glm::cross(b - a, c - a);
			accumulated[ia] += normal;
			accumulated[ib] += normal;
			accumulated[ic] += normal;
		}

		for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
		{
			if (glm::length2(mesh.vertices[i].normal) <= 0.0001f && glm::length2(accumulated[i]) > 0.0001f)
			{
				mesh.vertices[i].normal = glm::normalize(accumulated[i]);
			}
		}
	}

	void calculate_missing_tangents(LoadedMesh& mesh)
	{
		std::vector<glm::vec3> tangents(mesh.vertices.size(), glm::vec3(0.0f));
		std::vector<glm::vec3> bitangents(mesh.vertices.size(), glm::vec3(0.0f));
		for (std::size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
		{
			const uint32_t ia = mesh.indices[i];
			const uint32_t ib = mesh.indices[i + 1];
			const uint32_t ic = mesh.indices[i + 2];
			if (ia >= mesh.vertices.size() || ib >= mesh.vertices.size() || ic >= mesh.vertices.size())
			{
				continue;
			}

			const glm::vec3 p0 = mesh.vertices[ia].position;
			const glm::vec3 p1 = mesh.vertices[ib].position;
			const glm::vec3 p2 = mesh.vertices[ic].position;
			const glm::vec2 uv0 = mesh.vertices[ia].uv;
			const glm::vec2 uv1 = mesh.vertices[ib].uv;
			const glm::vec2 uv2 = mesh.vertices[ic].uv;

			const glm::vec3 edge1 = p1 - p0;
			const glm::vec3 edge2 = p2 - p0;
			const glm::vec2 deltaUv1 = uv1 - uv0;
			const glm::vec2 deltaUv2 = uv2 - uv0;
			const float determinant = deltaUv1.x * deltaUv2.y - deltaUv2.x * deltaUv1.y;
			if (std::abs(determinant) <= 0.000001f)
			{
				continue;
			}

			const float invDeterminant = 1.0f / determinant;
			const glm::vec3 tangent = (edge1 * deltaUv2.y - edge2 * deltaUv1.y) * invDeterminant;
			const glm::vec3 bitangent = (edge2 * deltaUv1.x - edge1 * deltaUv2.x) * invDeterminant;
			tangents[ia] += tangent;
			tangents[ib] += tangent;
			tangents[ic] += tangent;
			bitangents[ia] += bitangent;
			bitangents[ib] += bitangent;
			bitangents[ic] += bitangent;
		}

		for (std::size_t i = 0; i < mesh.vertices.size(); ++i)
		{
			const glm::vec3 normal = glm::length2(mesh.vertices[i].normal) > 0.0001f
				? glm::normalize(mesh.vertices[i].normal)
				: glm::vec3 { 0.0f, 0.0f, 1.0f };
			glm::vec3 tangent = tangents[i] - normal * glm::dot(normal, tangents[i]);
			if (glm::length2(tangent) <= 0.0001f)
			{
				glm::vec3 helper = std::abs(normal.z) < 0.999f
					? glm::vec3 { 0.0f, 0.0f, 1.0f }
					: glm::vec3 { 0.0f, 1.0f, 0.0f };
				tangent = glm::normalize(glm::cross(helper, normal));
			}
			else
			{
				tangent = glm::normalize(tangent);
			}

			const float sign = glm::dot(glm::cross(normal, tangent), bitangents[i]) < 0.0f ? -1.0f : 1.0f;
			mesh.vertices[i].tangent = glm::vec4(tangent, sign);
		}
	}

	bool load_primitive(
		const fastgltf::Asset& asset,
		const fastgltf::Primitive& primitive,
		LoadedMesh& mesh,
		const glm::mat4& nodeTransform = glm::mat4 { 1.0f })
	{
		if (primitive.type != fastgltf::PrimitiveType::Triangles)
		{
			return false;
		}

		const auto positionIt = primitive.findAttribute("POSITION");
		if (positionIt == primitive.attributes.end() || positionIt->accessorIndex >= asset.accessors.size())
		{
			return false;
		}

		const std::size_t initialVertex = mesh.vertices.size();
		const fastgltf::Accessor& positionAccessor = asset.accessors[positionIt->accessorIndex];
		mesh.vertices.resize(mesh.vertices.size() + positionAccessor.count);
		const LoadedMaterial material = primitive_material(asset, primitive);

		fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, positionAccessor, [&](glm::vec3 position, std::size_t index) {
			LoadedVertex vertex;
			vertex.position = glm::vec3(nodeTransform * glm::vec4(position, 1.0f));
			vertex.normal = glm::vec3(0.0f);
			vertex.color = material.vertex_color();
			vertex.material = material.shader_factors();
			vertex.material2 = material.shader_factors2();
			vertex.material3 = material.shader_factors3();
			mesh.vertices[initialVertex + index] = vertex;
		});

		const auto normalIt = primitive.findAttribute("NORMAL");
		if (normalIt != primitive.attributes.end() && normalIt->accessorIndex < asset.accessors.size())
		{
			fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, asset.accessors[normalIt->accessorIndex], [&](glm::vec3 normal, std::size_t index) {
				if (index >= positionAccessor.count)
				{
					return;
				}
				mesh.vertices[initialVertex + index].normal = transformed_normal(nodeTransform, normal);
			});
		}

		const auto colorIt = primitive.findAttribute("COLOR_0");
		if (colorIt != primitive.attributes.end() && colorIt->accessorIndex < asset.accessors.size())
		{
			const fastgltf::Accessor& colorAccessor = asset.accessors[colorIt->accessorIndex];
			if (colorAccessor.type == fastgltf::AccessorType::Vec4)
			{
				fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, colorAccessor, [&](glm::vec4 color, std::size_t index) {
					if (index >= positionAccessor.count)
				{
					return;
				}
				mesh.vertices[initialVertex + index].color = glm::clamp(glm::vec3(color) * material.vertex_color(), glm::vec3(0.0f), glm::vec3(1.0f));
			});
		}
		else if (colorAccessor.type == fastgltf::AccessorType::Vec3)
			{
				fastgltf::iterateAccessorWithIndex<glm::vec3>(asset, colorAccessor, [&](glm::vec3 color, std::size_t index) {
					if (index >= positionAccessor.count)
				{
					return;
				}
				mesh.vertices[initialVertex + index].color = glm::clamp(color * material.vertex_color(), glm::vec3(0.0f), glm::vec3(1.0f));
			});
		}
		}

		const auto texCoordIt = primitive.findAttribute("TEXCOORD_0");
		if (texCoordIt != primitive.attributes.end() && texCoordIt->accessorIndex < asset.accessors.size())
		{
			fastgltf::iterateAccessorWithIndex<glm::vec2>(asset, asset.accessors[texCoordIt->accessorIndex], [&](glm::vec2 uv, std::size_t index) {
				if (index >= positionAccessor.count)
				{
					return;
				}
				mesh.vertices[initialVertex + index].uv = uv;
			});
		}

		const auto tangentIt = primitive.findAttribute("TANGENT");
		if (tangentIt != primitive.attributes.end() && tangentIt->accessorIndex < asset.accessors.size())
		{
			fastgltf::iterateAccessorWithIndex<glm::vec4>(asset, asset.accessors[tangentIt->accessorIndex], [&](glm::vec4 tangent, std::size_t index) {
				if (index >= positionAccessor.count)
				{
					return;
				}
				const glm::vec3 tangentDirection = glm::mat3(nodeTransform) * glm::vec3(tangent);
				mesh.vertices[initialVertex + index].tangent = glm::length2(tangentDirection) > 0.0001f
					? glm::vec4(glm::normalize(tangentDirection), tangent.w >= 0.0f ? 1.0f : -1.0f)
					: glm::vec4 { 1.0f, 0.0f, 0.0f, 1.0f };
			});
		}

		const uint32_t firstIndex = static_cast<uint32_t>(mesh.indices.size());
		if (primitive.indicesAccessor.has_value() && *primitive.indicesAccessor < asset.accessors.size())
		{
			const fastgltf::Accessor& indexAccessor = asset.accessors[*primitive.indicesAccessor];
			mesh.indices.reserve(mesh.indices.size() + indexAccessor.count);
			fastgltf::iterateAccessor<std::uint32_t>(asset, indexAccessor, [&](std::uint32_t index) {
				mesh.indices.push_back(static_cast<uint32_t>(initialVertex + index));
			});
		}
		else
		{
			mesh.indices.reserve(mesh.indices.size() + positionAccessor.count);
			for (std::size_t i = 0; i < positionAccessor.count; ++i)
			{
				mesh.indices.push_back(static_cast<uint32_t>(initialVertex + i));
			}
		}

		const uint32_t indexCount = static_cast<uint32_t>(mesh.indices.size() - firstIndex);
		if (indexCount >= 3u)
		{
			mesh.ranges.push_back({ firstIndex, indexCount - (indexCount % 3u), material });
		}

		return true;
	}

	std::vector<Vertex> flatten_mesh(LoadedMesh& mesh)
	{
		calculate_missing_normals(mesh);
		calculate_missing_tangents(mesh);

		std::vector<Vertex> flattened;
		const std::size_t triangleIndexCount = mesh.indices.size() - (mesh.indices.size() % 3);
		flattened.reserve(triangleIndexCount);

		for (std::size_t i = 0; i < triangleIndexCount; ++i)
		{
			const uint32_t index = mesh.indices[i];
			if (index >= mesh.vertices.size())
			{
				continue;
			}

			const LoadedVertex& source = mesh.vertices[index];
			const glm::vec3 normal = glm::length2(source.normal) > 0.0001f
				? glm::normalize(source.normal)
				: glm::vec3 { 0.0f, 0.0f, 1.0f };
			flattened.push_back({
				source.position,
				glm::clamp(source.color, glm::vec3(0.0f), glm::vec3(1.0f)),
				normal,
				source.uv,
				source.tangent,
				source.material,
				source.material2,
				source.material3
			});
		}

		return flattened;
	}

	struct TextureBinding
	{
		vk::DescriptorImageInfo imageInfo = {};
		bool valid = false;
	};

	struct TextureBindingSet
	{
		std::vector<TextureBinding> linear;
		std::vector<TextureBinding> srgb;
	};

	TextureBinding make_texture_binding(const Model3DTexture& texture)
	{
		if (!texture.image || !texture.sampler)
		{
			return {};
		}

		TextureBinding binding = {};
		binding.imageInfo = texture.image->descriptor;
		binding.imageInfo.sampler = texture.sampler;
		binding.imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
		binding.valid = true;
		return binding;
	}

	vk::DescriptorSet make_material_descriptor_set(
		vk::Device logicalDevice,
		vk::DescriptorPool descriptorPool,
		vk::DescriptorSetLayout descriptorSetLayout,
		TextureBinding baseColor,
		TextureBinding normal,
		TextureBinding metallicRoughness,
		TextureBinding occlusion,
		TextureBinding emissive)
	{
		if (!descriptorPool || !descriptorSetLayout || !baseColor.valid || !normal.valid || !metallicRoughness.valid || !occlusion.valid || !emissive.valid)
		{
			return nullptr;
		}

		vk::DescriptorSet descriptorSet = allocate_descriptor_set(logicalDevice, descriptorPool, descriptorSetLayout);
		if (!descriptorSet)
		{
			return nullptr;
		}

		std::array<vk::DescriptorImageInfo, 5> imageInfos = {
			baseColor.imageInfo,
			normal.imageInfo,
			metallicRoughness.imageInfo,
			occlusion.imageInfo,
			emissive.imageInfo
		};
		std::array<vk::WriteDescriptorSet, 5> writes = {};
		for (uint32_t binding = 0; binding < static_cast<uint32_t>(writes.size()); ++binding)
		{
			writes[binding].dstSet = descriptorSet;
			writes[binding].dstBinding = binding;
			writes[binding].dstArrayElement = 0;
			writes[binding].descriptorCount = 1;
			writes[binding].descriptorType = vk::DescriptorType::eCombinedImageSampler;
			writes[binding].pImageInfo = &imageInfos[binding];
		}
		logicalDevice.updateDescriptorSets(static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
		return descriptorSet;
	}

	TextureBindingSet upload_model_textures(
		Model3DAsset& asset,
		const std::vector<LoadedImage>& images,
		const std::vector<LoadedTexture>& textures,
		VmaAllocator& allocator,
		std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
		std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
		vk::CommandBuffer commandBuffer,
		vk::Queue queue,
		vk::Device logicalDevice,
		vk::DescriptorPool textureDescriptorPool,
		vk::DescriptorSetLayout textureDescriptorSetLayout)
	{
		const LoadedSampler defaultSampler = {};
		asset.fallbackBaseColorTexture = make_model_texture(
			make_fallback_white_image(),
			defaultSampler,
			vk::Format::eR8G8B8A8Srgb,
			allocator,
			vmaDeletionQueue,
			deviceDeletionQueue,
			commandBuffer,
			queue,
			logicalDevice);
		asset.fallbackNormalTexture = make_model_texture(
			make_fallback_normal_image(),
			defaultSampler,
			vk::Format::eR8G8B8A8Unorm,
			allocator,
			vmaDeletionQueue,
			deviceDeletionQueue,
			commandBuffer,
			queue,
			logicalDevice);
		asset.fallbackMetallicRoughnessTexture = make_model_texture(
			make_fallback_white_image(),
			defaultSampler,
			vk::Format::eR8G8B8A8Unorm,
			allocator,
			vmaDeletionQueue,
			deviceDeletionQueue,
			commandBuffer,
			queue,
			logicalDevice);
		asset.fallbackOcclusionTexture = make_model_texture(
			make_fallback_white_image(),
			defaultSampler,
			vk::Format::eR8G8B8A8Unorm,
			allocator,
			vmaDeletionQueue,
			deviceDeletionQueue,
			commandBuffer,
			queue,
			logicalDevice);
		asset.fallbackEmissiveTexture = make_model_texture(
			make_fallback_white_image(),
			defaultSampler,
			vk::Format::eR8G8B8A8Srgb,
			allocator,
			vmaDeletionQueue,
			deviceDeletionQueue,
			commandBuffer,
			queue,
			logicalDevice);

		const TextureBinding fallbackBase = make_texture_binding(asset.fallbackBaseColorTexture);
		const TextureBinding fallbackNormal = make_texture_binding(asset.fallbackNormalTexture);
		const TextureBinding fallbackMetallicRoughness = make_texture_binding(asset.fallbackMetallicRoughnessTexture);
		const TextureBinding fallbackOcclusion = make_texture_binding(asset.fallbackOcclusionTexture);
		const TextureBinding fallbackEmissive = make_texture_binding(asset.fallbackEmissiveTexture);
		asset.fallbackMaterialDescriptorSet = make_material_descriptor_set(
			logicalDevice,
			textureDescriptorPool,
			textureDescriptorSetLayout,
			fallbackBase,
			fallbackNormal,
			fallbackMetallicRoughness,
			fallbackOcclusion,
			fallbackEmissive);

		TextureBindingSet textureBindings;
		textureBindings.linear.resize(textures.size());
		textureBindings.srgb.resize(textures.size());
		asset.textures.reserve(textures.size() * 2u);
		for (std::size_t i = 0; i < textures.size(); ++i)
		{
			const LoadedTexture& textureInfo = textures[i];
			if (textureInfo.imageIndex >= images.size() || !images[textureInfo.imageIndex].valid())
			{
				continue;
			}

			Model3DTexture linearTexture = make_model_texture(
				images[textureInfo.imageIndex],
				textureInfo.sampler,
				vk::Format::eR8G8B8A8Unorm,
				allocator,
				vmaDeletionQueue,
				deviceDeletionQueue,
				commandBuffer,
				queue,
				logicalDevice);
			TextureBinding linearBinding = make_texture_binding(linearTexture);
			if (linearBinding.valid)
			{
				textureBindings.linear[i] = linearBinding;
			}

			Model3DTexture srgbTexture = make_model_texture(
				images[textureInfo.imageIndex],
				textureInfo.sampler,
				vk::Format::eR8G8B8A8Srgb,
				allocator,
				vmaDeletionQueue,
				deviceDeletionQueue,
				commandBuffer,
				queue,
				logicalDevice);
			TextureBinding srgbBinding = make_texture_binding(srgbTexture);
			if (srgbBinding.valid)
			{
				textureBindings.srgb[i] = srgbBinding;
			}

			asset.textures.push_back(std::move(linearTexture));
			asset.textures.push_back(std::move(srgbTexture));
		}
		return textureBindings;
	}

	void assign_draw_ranges(
		Model3DAsset& asset,
		const LoadedMesh& mesh,
		const TextureBindingSet& textureBindings,
		vk::Device logicalDevice,
		vk::DescriptorPool textureDescriptorPool,
		vk::DescriptorSetLayout textureDescriptorSetLayout)
	{
		asset.drawRanges.clear();
		asset.drawRanges.reserve(mesh.ranges.empty() ? 1u : mesh.ranges.size());
		asset.materialDescriptorSets.clear();
		asset.materialDescriptorSets.reserve(mesh.ranges.size());

		const TextureBinding fallbackBase = make_texture_binding(asset.fallbackBaseColorTexture);
		const TextureBinding fallbackNormal = make_texture_binding(asset.fallbackNormalTexture);
		const TextureBinding fallbackMetallicRoughness = make_texture_binding(asset.fallbackMetallicRoughnessTexture);
		const TextureBinding fallbackOcclusion = make_texture_binding(asset.fallbackOcclusionTexture);
		const TextureBinding fallbackEmissive = make_texture_binding(asset.fallbackEmissiveTexture);

		auto resolve_linear_texture = [&](uint32_t textureIndex, TextureBinding fallback) {
			return textureIndex < textureBindings.linear.size() && textureBindings.linear[textureIndex].valid
				? textureBindings.linear[textureIndex]
				: fallback;
		};
		auto resolve_srgb_texture = [&](uint32_t textureIndex, TextureBinding fallback) {
			return textureIndex < textureBindings.srgb.size() && textureBindings.srgb[textureIndex].valid
				? textureBindings.srgb[textureIndex]
				: fallback;
		};

		for (const LoadedPrimitiveRange& sourceRange : mesh.ranges)
		{
			if (sourceRange.indexCount < 3u)
			{
				continue;
			}

			const TextureBinding baseColor = resolve_srgb_texture(sourceRange.material.baseColorTexture, fallbackBase);
			const TextureBinding normal = resolve_linear_texture(sourceRange.material.normalTexture, fallbackNormal);
			const TextureBinding metallicRoughness = resolve_linear_texture(sourceRange.material.metallicRoughnessTexture, fallbackMetallicRoughness);
			const TextureBinding occlusion = resolve_linear_texture(sourceRange.material.occlusionTexture, fallbackOcclusion);
			const TextureBinding emissive = resolve_srgb_texture(sourceRange.material.emissiveTexture, fallbackEmissive);
			vk::DescriptorSet materialSet = make_material_descriptor_set(
				logicalDevice,
				textureDescriptorPool,
				textureDescriptorSetLayout,
				baseColor,
				normal,
				metallicRoughness,
				occlusion,
				emissive);
			if (!materialSet)
			{
				materialSet = asset.fallbackMaterialDescriptorSet;
			}
			else
			{
				asset.materialDescriptorSets.push_back(materialSet);
			}

			asset.drawRanges.push_back({
				sourceRange.firstIndex / 3u,
				sourceRange.indexCount / 3u,
				materialSet
			});
		}

		if (asset.drawRanges.empty() && asset.buffer.triangleCount > 0)
		{
			asset.drawRanges.push_back({
				asset.buffer.firstTriangle3D,
				asset.buffer.triangleCount3D > 0 ? asset.buffer.triangleCount3D : asset.buffer.triangleCount,
				asset.fallbackMaterialDescriptorSet
			});
		}
	}

	LoadedMesh merge_scene_mesh(fastgltf::Asset& asset)
	{
		Logger* logger = Logger::fetch_logger();
		LoadedMesh merged;

		if (!asset.scenes.empty())
		{
			std::size_t sceneIndex = asset.defaultScene.value_or(0);
			if (sceneIndex >= asset.scenes.size())
			{
				sceneIndex = 0;
			}

			const fastgltf::Scene& scene = asset.scenes[sceneIndex];
			merged.name = scene.name.empty()
				? ("Scene " + std::to_string(sceneIndex))
				: std::string(scene.name);
			logger->print("Merging glTF scene '" + merged.name + "' with " +
				std::to_string(scene.nodeIndices.size()) + " root nodes.");

			uint32_t meshNodeCount = 0;
			uint32_t primitiveCount = 0;
			fastgltf::iterateSceneNodes(asset, sceneIndex, fastgltf::math::fmat4x4 {}, [&](fastgltf::Node& node, const fastgltf::math::fmat4x4& nodeMatrix) {
				if (!node.meshIndex.has_value() || *node.meshIndex >= asset.meshes.size())
				{
					return;
				}

				const fastgltf::Mesh& sourceMesh = asset.meshes[*node.meshIndex];
				const glm::mat4 transform = to_glm(nodeMatrix);
				bool emittedFromNode = false;
				for (const fastgltf::Primitive& primitive : sourceMesh.primitives)
				{
					const std::size_t before = merged.indices.size();
					if (load_primitive(asset, primitive, merged, transform) && merged.indices.size() > before)
					{
						emittedFromNode = true;
						++primitiveCount;
					}
				}

				if (emittedFromNode)
				{
					++meshNodeCount;
				}
			});

			if (!merged.indices.empty() && !merged.vertices.empty())
			{
				logger->print("Merged glTF scene '" + merged.name + "' from " +
					std::to_string(meshNodeCount) + " mesh nodes and " +
					std::to_string(primitiveCount) + " primitives into " +
					std::to_string(mesh_triangle_count(merged)) + " triangles.");
				return merged;
			}
		}

		merged.name = "Merged glTF meshes";
		logger->print("Merging glTF asset without scene nodes from " +
			std::to_string(asset.meshes.size()) + " meshes.");
		uint32_t primitiveCount = 0;
		for (const fastgltf::Mesh& sourceMesh : asset.meshes)
		{
			for (const fastgltf::Primitive& primitive : sourceMesh.primitives)
			{
				const std::size_t before = merged.indices.size();
				if (load_primitive(asset, primitive, merged) && merged.indices.size() > before)
				{
					++primitiveCount;
				}
			}
		}

		if (!merged.indices.empty() && !merged.vertices.empty())
		{
			logger->print("Merged glTF meshes without scene nodes from " +
				std::to_string(primitiveCount) + " primitives into " +
				std::to_string(mesh_triangle_count(merged)) + " triangles.");
		}

		return merged;
	}

}

StorageBuffer build_triangle(VmaAllocator& allocator, std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue, vk::CommandBuffer commandBuffer, vk::Queue queue)
{
	std::vector<Vertex> vertices =
	{
		{{-0.55f,  0.45f, 0.0f}, {1.0f, 0.2f, 0.2f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f, 0.0f}},
		{{ 0.55f,  0.45f, 0.0f}, {0.2f, 1.0f, 0.2f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f, 0.0f}},
		{{ 0.00f, -0.45f, 0.0f}, {0.2f, 0.4f, 1.0f}, {0.0f, 0.0f, 1.0f}, {0.5f, 1.0f}, {1.0f, 0.0f, 0.0f, 1.0f}, {1.0f, 1.0f, 1.0f, 1.0f}, {1.0f, 0.0f, 0.0f, 0.0f}, {0.0f, 0.5f, 0.0f, 0.0f}},
	};

	return upload_vertices(vertices, allocator, vmaDeletionQueue, commandBuffer, queue);
}

Model3DAsset load_gltf_mesh(
	const std::filesystem::path& path,
	VmaAllocator& allocator,
	std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
	std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
	vk::CommandBuffer commandBuffer,
	vk::Queue queue,
	vk::Device logicalDevice,
	vk::DescriptorPool textureDescriptorPool,
	vk::DescriptorSetLayout textureDescriptorSetLayout)
{
	Logger* logger = Logger::fetch_logger();
	const std::filesystem::path absolutePath = std::filesystem::absolute(path);
	logger->print("Loading glTF mesh: " + absolutePath.string());

	auto make_fallback_asset = [&]() {
		Model3DAsset asset = {};
		asset.buffer = build_triangle(allocator, vmaDeletionQueue, commandBuffer, queue);
		upload_model_textures(asset, {}, {}, allocator, vmaDeletionQueue, deviceDeletionQueue,
			commandBuffer, queue, logicalDevice, textureDescriptorPool, textureDescriptorSetLayout);
		if (asset.buffer.triangleCount > 0)
		{
			asset.drawRanges.push_back({
				asset.buffer.firstTriangle3D,
				asset.buffer.triangleCount3D > 0 ? asset.buffer.triangleCount3D : asset.buffer.triangleCount,
				asset.fallbackMaterialDescriptorSet
			});
		}
		return asset;
	};

	if (!std::filesystem::exists(absolutePath))
	{
		logger->print("glTF file does not exist: " + absolutePath.string());
		return make_fallback_asset();
	}

	auto upload_loaded_mesh = [&](LoadedMesh& mesh, const std::vector<LoadedImage>& images, const std::vector<LoadedTexture>& textures, std::string_view label) {
		constexpr uint32_t previewTriangleTarget = 0u;
		simplify_mesh_for_preview(mesh, previewTriangleTarget);
		std::vector<Vertex> modelVertices = flatten_mesh(mesh);
		normalise_for_demo_view(modelVertices);

		Model3DAsset asset = {};
		std::vector<Vertex> vertices = std::move(modelVertices);
		const uint32_t triangleCount2D = 0;

		logger->print("Uploading " + std::string(label) + " with " +
			std::to_string(vertices.size() / 3) + " 3D triangles and " +
			std::to_string(triangleCount2D) + " 2D triangles.");
		asset.buffer = upload_vertices(vertices, allocator, vmaDeletionQueue, commandBuffer, queue, triangleCount2D);
		TextureBindingSet textureBindings = upload_model_textures(
			asset,
			images,
			textures,
			allocator,
			vmaDeletionQueue,
			deviceDeletionQueue,
			commandBuffer,
			queue,
			logicalDevice,
			textureDescriptorPool,
			textureDescriptorSetLayout);
		assign_draw_ranges(asset, mesh, textureBindings, logicalDevice, textureDescriptorPool, textureDescriptorSetLayout);
		logger->print("Prepared " + std::to_string(asset.drawRanges.size()) +
			" material draw ranges and " + std::to_string(asset.textures.size()) +
			" uploaded 3D textures.");
		return asset;
	};

	const std::string extension = absolutePath.extension().string();
	if (extension == ".glb" || extension == ".GLB")
	{
		std::vector<LoadedMesh> glbMeshes;
		std::vector<LoadedImage> glbImages;
		std::vector<LoadedTexture> glbTextures;
		if (!load_glb_meshes_json(absolutePath, glbMeshes, glbImages, glbTextures))
		{
			logger->print("Failed to extract mesh data from GLB file: " + absolutePath.string());
			return make_fallback_asset();
		}

		LoadedMesh mergedMesh = merge_loaded_meshes(glbMeshes);
		if (mergedMesh.indices.empty() || mergedMesh.vertices.empty())
		{
			logger->print("No renderable triangles found in GLB file: " + absolutePath.string());
			return make_fallback_asset();
		}

		logger->print("Merged GLB file from " + std::to_string(glbMeshes.size()) +
			" meshes into " + std::to_string(mesh_triangle_count(mergedMesh)) + " triangles.");
		return upload_loaded_mesh(mergedMesh, glbImages, glbTextures, "GLB scene mesh");
	}

	fastgltf::Expected<fastgltf::GltfDataBuffer> data = fastgltf::GltfDataBuffer::FromPath(absolutePath);
	if (data.error() != fastgltf::Error::None)
	{
		logger->print("Failed to read glTF file: " + absolutePath.string());
		return make_fallback_asset();
	}

	fastgltf::Parser parser(fastgltf::Extensions::KHR_mesh_quantization);
	constexpr auto options = fastgltf::Options::LoadExternalBuffers | fastgltf::Options::LoadExternalImages;
	logger->print("Parsing JSON glTF with fastgltf.");
	fastgltf::Expected<fastgltf::Asset> loadedAsset =
		parser.loadGltf(data.get(), absolutePath.parent_path(), options);

	if (loadedAsset.error() != fastgltf::Error::None)
	{
		logger->print("fastgltf failed: " + std::string(fastgltf::getErrorMessage(loadedAsset.error())));
		return make_fallback_asset();
	}

	fastgltf::Asset& asset = loadedAsset.get();
	logger->print("Parsed glTF asset with " +
		std::to_string(asset.scenes.size()) + " scenes, " +
		std::to_string(asset.nodes.size()) + " nodes, " +
		std::to_string(asset.meshes.size()) + " meshes, and " +
		std::to_string(asset.materials.size()) + " materials.");
	LoadedMesh mergedMesh = merge_scene_mesh(asset);
	if (mergedMesh.indices.empty() || mergedMesh.vertices.empty())
	{
		logger->print("No renderable triangles found in glTF file: " + absolutePath.string());
		return make_fallback_asset();
	}

	std::vector<LoadedImage> images = decode_fastgltf_images(asset, absolutePath.parent_path());
	std::vector<LoadedTexture> textures = collect_fastgltf_textures(asset);
	return upload_loaded_mesh(mergedMesh, images, textures, "glTF scene mesh");
}

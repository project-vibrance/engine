#include <vibranceUI/factories/mesh_factory.h>
#include <vibranceUI/renderer/descriptors.h>
#include "../renderer/common/mesh_decode.h"
namespace {
	StorageBuffer upload_vertices(
		const std::vector<Vertex>& vertices,
		VmaAllocator& allocator,
		std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
		vk::CommandBuffer commandBuffer,
		vk::Queue queue)
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
				0u,
				asset.buffer.triangleCount,
				asset.fallbackMaterialDescriptorSet
			});
		}
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
				0u,
				asset.buffer.triangleCount,
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
		logger->print("Uploading " + std::string(label) + " with " +
			std::to_string(vertices.size() / 3) + " triangles.");
		asset.buffer = upload_vertices(vertices, allocator, vmaDeletionQueue, commandBuffer, queue);
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

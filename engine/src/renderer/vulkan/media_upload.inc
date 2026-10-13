    vk::Sampler make_media_sampler(
        vk::Device logicalDevice,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
        uint32_t mipLevels)
    {
        vk::SamplerCreateInfo samplerInfo = {};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(std::max(1u, mipLevels) - 1u);
        samplerInfo.borderColor = vk::BorderColor::eIntTransparentBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        auto result = logicalDevice.createSampler(samplerInfo);
        if (result.result != vk::Result::eSuccess)
        {
            Logger::fetch_logger()->vulkan("Failed to create 2D media sampler.");
            return nullptr;
        }

        VkSampler samplerHandle = result.value;
        deviceDeletionQueue.push_back([samplerHandle](vk::Device device) {
            device.destroySampler(samplerHandle);
        });
        return result.value;
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
            logger->vulkan("Failed to create 2D media staging buffer for " + std::string(label) + ".");
            return false;
        }

        std::memcpy(stagingInfo.pMappedData, rgba.data(), rgba.size());

        vk::Result result = commandBuffer.reset();
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to reset 2D media upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        result = commandBuffer.begin(beginInfo);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to begin 2D media upload command buffer.");
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
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                    vk::ImageAspectFlagBits::eColor, mipLevel - 1u, 1u);

                mipWidth = nextMipWidth;
                mipHeight = nextMipHeight;
            }

            transition_image_layout(commandBuffer, image.image,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                vk::ImageAspectFlagBits::eColor, image.mipLevels - 1u, 1u);
        }
        else
        {
            transition_image_layout(commandBuffer, image.image,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader);
        }

        result = commandBuffer.end();
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to end 2D media upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        vk::SubmitInfo submitInfo = {};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        result = queue.submit(1, &submitInfo, nullptr);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to submit 2D media upload.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        result = queue.waitIdle();
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to wait for 2D media upload.");
            return false;
        }

        return true;
    }

    bool make_media_texture(
        const DecodedMediaFrame& frame,
        const Media2DLoadOptions& options,
        VmaAllocator& allocator,
        std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        vk::Device logicalDevice,
        vk::DescriptorPool descriptorPool,
        vk::DescriptorSetLayout descriptorSetLayout,
        Media2DAsset& asset)
    {
        if (!frame.valid() || !descriptorPool || !descriptorSetLayout)
        {
            return false;
        }

        std::vector<unsigned char> rgba = frame.rgba;
        if (options.premultiplyAlpha)
        {
            bleed_transparent_rgb(rgba, frame.width, frame.height);
            premultiply_alpha(rgba);
        }

        const uint32_t mipLevels = options.generateMipmaps
            ? mip_count_for_extent(vk::Extent2D { frame.width, frame.height })
            : 1u;
        asset.image = std::make_unique<StorageImage>(
            allocator,
            options.srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm,
            vk::Extent2D { frame.width, frame.height },
            commandBuffer,
            queue,
            logicalDevice,
            vmaDeletionQueue,
            deviceDeletionQueue,
            vk::ImageUsageFlagBits::eSampled,
            mipLevels,
            false);
        if (!upload_rgba_to_image(allocator, commandBuffer, queue, *asset.image, rgba, asset.name))
        {
            asset.image.reset();
            return false;
        }

        asset.sampler = make_media_sampler(logicalDevice, deviceDeletionQueue, mipLevels);
        if (!asset.sampler)
        {
            asset.image.reset();
            return false;
        }

        asset.descriptorSet = allocate_descriptor_set(logicalDevice, descriptorPool, descriptorSetLayout);
        if (!asset.descriptorSet)
        {
            asset.image.reset();
            asset.sampler = nullptr;
            return false;
        }

        vk::DescriptorImageInfo imageInfo = asset.image->descriptor;
        imageInfo.sampler = asset.sampler;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        asset.image->descriptor = imageInfo;

        vk::WriteDescriptorSet write = {};
        write.dstSet = asset.descriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &imageInfo;
        logicalDevice.updateDescriptorSets(1, &write, 0, nullptr);
        asset.drawable = true;
        return true;
    }

    bool make_extra_media_frame_texture(
        const DecodedMediaFrame& frame,
        const Media2DLoadOptions& options,
        VmaAllocator& allocator,
        std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        vk::Device logicalDevice,
        vk::DescriptorPool descriptorPool,
        vk::DescriptorSetLayout descriptorSetLayout,
        Media2DAsset& asset)
    {
        if (!frame.valid() || !asset.sampler || !descriptorPool || !descriptorSetLayout)
        {
            return false;
        }

        std::vector<unsigned char> rgba = frame.rgba;
        if (options.premultiplyAlpha)
        {
            bleed_transparent_rgb(rgba, frame.width, frame.height);
            premultiply_alpha(rgba);
        }

        const uint32_t mipLevels = options.generateMipmaps
            ? mip_count_for_extent(vk::Extent2D { frame.width, frame.height })
            : 1u;
        auto image = std::make_unique<StorageImage>(
            allocator,
            options.srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm,
            vk::Extent2D { frame.width, frame.height },
            commandBuffer,
            queue,
            logicalDevice,
            vmaDeletionQueue,
            deviceDeletionQueue,
            vk::ImageUsageFlagBits::eSampled,
            mipLevels,
            false);
        if (!upload_rgba_to_image(allocator, commandBuffer, queue, *image, rgba, asset.name))
        {
            return false;
        }

        vk::DescriptorSet descriptorSet = allocate_descriptor_set(logicalDevice, descriptorPool, descriptorSetLayout);
        if (!descriptorSet)
        {
            return false;
        }

        vk::DescriptorImageInfo imageInfo = image->descriptor;
        imageInfo.sampler = asset.sampler;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image->descriptor = imageInfo;

        vk::WriteDescriptorSet write = {};
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &imageInfo;
        logicalDevice.updateDescriptorSets(1, &write, 0, nullptr);

        asset.extraFrameImages.push_back(std::move(image));
        asset.frameDescriptorSets.push_back(descriptorSet);
        return true;
    }


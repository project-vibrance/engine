    bool upload_atlas_to_image(
        VmaAllocator& allocator,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        StorageImage& image,
        const std::vector<unsigned char>& packedSdf)
    {
        // Font atlas upload uses a short-lived staging buffer and keeps the image shader-readable
        Logger* logger = Logger::fetch_logger();
        const vk::DeviceSize uploadSize = static_cast<vk::DeviceSize>(packedSdf.size());
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
            logger->vulkan("Failed to create font atlas staging buffer.");
            return false;
        }

        std::memcpy(stagingInfo.pMappedData, packedSdf.data(), packedSdf.size());

        vk::Result result = commandBuffer.reset();
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to reset font atlas upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        result = commandBuffer.begin(beginInfo);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to begin font atlas upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        transition_image_layout(commandBuffer, image.image,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal,
            vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite,
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer);

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

        transition_image_layout(commandBuffer, image.image,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader);

        result = commandBuffer.end();
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to end font atlas upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        vk::SubmitInfo submitInfo = {};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        result = queue.submit(1, &submitInfo, nullptr);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to submit font atlas upload.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        result = queue.waitIdle();
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to wait for font atlas upload.");
            return false;
        }

        return true;
    }

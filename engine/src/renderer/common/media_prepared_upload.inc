// Included after PreparedMedia2D::Data and the shared decoder helpers.
std::size_t prepared_media_2d_upload_bytes(const PreparedMedia2D& prepared)
{
    const auto* data = Media2DPreparedAccess::get(prepared);
    if (!data) return 0u;
    constexpr std::size_t alignment = 64u * 1024u;
    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    std::size_t total = alignment; // Command/descriptor pools and bookkeeping.
    std::size_t staging = 0u;
    for (const auto& frame : data->animation.frames)
    {
        std::size_t imageBytes = frame.rgba.size();
        staging = std::max(staging, imageBytes);
        if (data->options.generateMipmaps)
        {
            uint32_t width = frame.width, height = frame.height;
            while (width > 1u || height > 1u)
            {
                width = std::max(width / 2u, 1u);
                height = std::max(height / 2u, 1u);
                const auto bytes = static_cast<std::size_t>(width) * height * 4u;
                if (bytes > maximum - imageBytes) return maximum;
                imageBytes += bytes;
            }
        }
        if (imageBytes > maximum - alignment) return maximum;
        imageBytes = (imageBytes + alignment - 1u) / alignment * alignment;
        if (imageBytes > maximum - total) return maximum;
        total += imageBytes;
    }
    return staging > maximum - total ? maximum : total + staging;
}

bool prepared_media_2d_upload_retired(vk::Device device, const PreparedMedia2DAsyncUpload& state)
{
    if (!state.submitted) return true;
    // A lost device also permits destruction; it can never publish this media.
    return device.getFenceStatus(state.fence) != vk::Result::eNotReady;
}

void release_prepared_media_2d_upload(
    VmaAllocator allocator, vk::Device device, PreparedMedia2DAsyncUpload& state)
{
    // The caller must first observe retirement, or wait during Engine teardown.
    if (state.stagingBuffer) vmaDestroyBuffer(allocator, state.stagingBuffer, state.stagingAllocation);
    auto& asset = state.publishedAsset ? *state.publishedAsset : state.asset;
    if (state.descriptorPool) device.destroyDescriptorPool(state.descriptorPool);
    if (asset.sampler) device.destroySampler(asset.sampler);
    const auto releaseImage = [&](const std::unique_ptr<StorageImage>& image) {
        if (!image) return;
        if (image->view) device.destroyImageView(image->view);
        if (image->image) vmaDestroyImage(allocator, image->image, image->memory);
    };
    releaseImage(asset.image);
    for (const auto& image : asset.extraFrameImages) releaseImage(image);
    if (state.fence) device.destroyFence(state.fence);
    if (state.commandPool) device.destroyCommandPool(state.commandPool);
    asset = {};
    state = {};
}

namespace
{
    bool initialise_prepared_upload(vk::Device device, uint32_t queueFamilyIndex,
        uint32_t frameCount, PreparedMedia2DAsyncUpload& state)
    {
        vk::CommandPoolCreateInfo poolInfo;
        poolInfo.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer |
            vk::CommandPoolCreateFlagBits::eTransient;
        poolInfo.queueFamilyIndex = queueFamilyIndex;
        const auto pool = device.createCommandPool(poolInfo);
        if (pool.result != vk::Result::eSuccess) return false;
        state.commandPool = pool.value;
        vk::CommandBufferAllocateInfo allocation;
        allocation.commandPool = state.commandPool;
        allocation.level = vk::CommandBufferLevel::ePrimary;
        allocation.commandBufferCount = 1u;
        const auto command = device.allocateCommandBuffers(allocation);
        if (command.result != vk::Result::eSuccess || command.value.empty()) return false;
        state.commandBuffer = command.value.front();
        const auto fence = device.createFence({});
        if (fence.result != vk::Result::eSuccess) return false;
        state.fence = fence.value;
        vk::DescriptorPoolSize size { vk::DescriptorType::eCombinedImageSampler, std::max(frameCount, 1u) };
        vk::DescriptorPoolCreateInfo descriptors;
        descriptors.maxSets = std::max(frameCount, 1u);
        descriptors.poolSizeCount = 1u;
        descriptors.pPoolSizes = &size;
        const auto descriptorPool = device.createDescriptorPool(descriptors);
        if (descriptorPool.result != vk::Result::eSuccess) return false;
        state.descriptorPool = descriptorPool.value;
        return true;
    }

    bool record_prepared_frame(const DecodedMediaFrame& frame, const Media2DLoadOptions& options,
        VmaAllocator allocator, vk::Device device, vk::DescriptorSetLayout descriptorSetLayout,
        PreparedMedia2DAsyncUpload& state)
    {
        auto& asset = state.asset;
        StorageImage* image = nullptr;
        if (state.completedFrames == 0u)
        {
            asset.image = std::make_unique<StorageImage>();
            image = asset.image.get();
        }
        else
        {
            asset.extraFrameImages.push_back(std::make_unique<StorageImage>());
            image = asset.extraFrameImages.back().get();
        }
        image->format = options.srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm;
        image->extent = vk::Extent2D(frame.width, frame.height);
        image->mipLevels = options.generateMipmaps ? mip_count_for_extent(image->extent) : 1u;
        ImageInputChunk input;
        input.format = image->format;
        input.extent = image->extent;
        input.mipLevels = image->mipLevels;
        input.tiling = vk::ImageTiling::eOptimal;
        input.usage = vk::ImageUsageFlagBits::eTransferSrc | vk::ImageUsageFlagBits::eTransferDst |
            vk::ImageUsageFlagBits::eSampled;
        input.memoryProperties = vk::MemoryPropertyFlagBits::eDeviceLocal;
        make_image(allocator, input, image->image, image->memory);
        if (!image->image || !image->memory) return false;
        image->view = create_image_view(device, image->image, image->format,
            vk::ImageAspectFlagBits::eColor, image->mipLevels);
        if (!image->view) return false;

        VkBufferCreateInfo buffer { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        buffer.size = frame.rgba.size();
        buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo memory {};
        memory.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        memory.usage = VMA_MEMORY_USAGE_AUTO;
        VmaAllocationInfo mapped {};
        if (vmaCreateBuffer(allocator, &buffer, &memory, &state.stagingBuffer,
            &state.stagingAllocation, &mapped) != VK_SUCCESS || !mapped.pMappedData) return false;
        std::memcpy(mapped.pMappedData, frame.rgba.data(), frame.rgba.size());
        if (vmaFlushAllocation(allocator, state.stagingAllocation, 0u, frame.rgba.size()) != VK_SUCCESS) return false;

        auto command = state.commandBuffer;
        if (command.reset() != vk::Result::eSuccess) return false;
        vk::CommandBufferBeginInfo begin;
        begin.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        if (command.begin(begin) != vk::Result::eSuccess) return false;
        transition_image_layout(command, image->image, vk::ImageLayout::eUndefined,
            vk::ImageLayout::eTransferDstOptimal, {}, vk::AccessFlagBits::eTransferWrite,
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
            vk::ImageAspectFlagBits::eColor, 0u, image->mipLevels);
        vk::BufferImageCopy region;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.layerCount = 1u;
        region.imageExtent = vk::Extent3D(frame.width, frame.height, 1u);
        command.copyBufferToImage(state.stagingBuffer, image->image,
            vk::ImageLayout::eTransferDstOptimal, 1u, &region);
        int32_t width = static_cast<int32_t>(frame.width), height = static_cast<int32_t>(frame.height);
        for (uint32_t level = 1u; level < image->mipLevels; ++level)
        {
            transition_image_layout(command, image->image, vk::ImageLayout::eTransferDstOptimal,
                vk::ImageLayout::eTransferSrcOptimal, vk::AccessFlagBits::eTransferWrite,
                vk::AccessFlagBits::eTransferRead, vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eTransfer, vk::ImageAspectFlagBits::eColor, level - 1u, 1u);
            const int32_t nextWidth = std::max(width / 2, 1), nextHeight = std::max(height / 2, 1);
            vk::ImageBlit blit;
            blit.srcOffsets[1] = vk::Offset3D(width, height, 1);
            blit.srcSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level - 1u, 0u, 1u);
            blit.dstOffsets[1] = vk::Offset3D(nextWidth, nextHeight, 1);
            blit.dstSubresource = vk::ImageSubresourceLayers(vk::ImageAspectFlagBits::eColor, level, 0u, 1u);
            command.blitImage(image->image, vk::ImageLayout::eTransferSrcOptimal,
                image->image, vk::ImageLayout::eTransferDstOptimal, 1u, &blit, vk::Filter::eLinear);
            transition_image_layout(command, image->image, vk::ImageLayout::eTransferSrcOptimal,
                vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eTransferRead,
                vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eTransfer,
                vk::PipelineStageFlagBits::eComputeShader, vk::ImageAspectFlagBits::eColor, level - 1u, 1u);
            width = nextWidth;
            height = nextHeight;
        }
        transition_image_layout(command, image->image, vk::ImageLayout::eTransferDstOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits::eTransferWrite,
            vk::AccessFlagBits::eShaderRead, vk::PipelineStageFlagBits::eTransfer,
            vk::PipelineStageFlagBits::eComputeShader, vk::ImageAspectFlagBits::eColor, image->mipLevels - 1u, 1u);
        if (command.end() != vk::Result::eSuccess) return false;

        if (!asset.sampler)
        {
            vk::SamplerCreateInfo sampler;
            sampler.magFilter = sampler.minFilter = vk::Filter::eLinear;
            sampler.mipmapMode = vk::SamplerMipmapMode::eLinear;
            sampler.addressModeU = sampler.addressModeV = sampler.addressModeW = vk::SamplerAddressMode::eClampToEdge;
            sampler.maxLod = static_cast<float>(image->mipLevels - 1u);
            const auto created = device.createSampler(sampler);
            if (created.result != vk::Result::eSuccess) return false;
            asset.sampler = created.value;
        }
        const auto descriptor = allocate_descriptor_set(device, state.descriptorPool, descriptorSetLayout);
        if (!descriptor) return false;
        image->descriptor = vk::DescriptorImageInfo(asset.sampler, image->view, vk::ImageLayout::eShaderReadOnlyOptimal);
        vk::WriteDescriptorSet write;
        write.dstSet = descriptor;
        write.descriptorCount = 1u;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &image->descriptor;
        device.updateDescriptorSets(1u, &write, 0u, nullptr);
        if (state.completedFrames == 0u) asset.descriptorSet = descriptor;
        asset.frameDescriptorSets.push_back(descriptor);
        return true;
    }
}

Media2DUploadResult upload_prepared_media_2d_asset_async(
    const PreparedMedia2D& prepared, uint32_t maxFrameUploads, uint32_t mediaId,
    VmaAllocator allocator, vk::Device device, vk::Queue queue,
    uint32_t queueFamilyIndex, vk::DescriptorSetLayout descriptorSetLayout,
    PreparedMedia2DAsyncUpload& state)
{
    const auto* data = Media2DPreparedAccess::get(prepared);
    if (!data || state.cancelled) return { {}, true };
    if (state.submitted)
    {
        const auto status = device.getFenceStatus(state.fence);
        if (status == vk::Result::eNotReady) return {};
        if (status != vk::Result::eSuccess) return { {}, true };
        state.submitted = false;
        vmaDestroyBuffer(allocator, state.stagingBuffer, state.stagingAllocation);
        state.stagingBuffer = VK_NULL_HANDLE;
        state.stagingAllocation = nullptr;
        ++state.completedFrames;
        if (state.completedFrames == data->animation.frames.size())
        {
            state.asset.drawable = true;
            return { state.asset.handle(), true };
        }
    }
    if (maxFrameUploads == 0u) return {};
    if (state.asset.id == 0u)
    {
        const auto& metadata = data->metadata;
        state.asset.id = mediaId;
        state.asset.path = metadata.path;
        state.asset.name = metadata.name;
        state.asset.sourceType = metadata.sourceType;
        state.asset.pixelSize = metadata.pixelSize;
        state.asset.durationSeconds = metadata.durationSeconds;
        state.asset.frameRate = metadata.frameRate;
        state.asset.frameCount = metadata.frameCount;
        state.asset.animated = metadata.animated;
        state.asset.hasBlackBackground = metadata.hasBlackBackground;
        state.asset.premultipliedAlpha = metadata.premultipliedAlpha;
        state.asset.frameDurationsSeconds = metadata.frameDurationsSeconds;
        state.asset.frameDescriptorSets.reserve(data->animation.frames.size());
        if (data->animation.frames.size() > 1u)
            state.asset.extraFrameImages.reserve(data->animation.frames.size() - 1u);
    }
    if (!data->animation.valid()) return { state.asset.handle(), true };
    if (!state.commandPool && !initialise_prepared_upload(device, queueFamilyIndex,
        static_cast<uint32_t>(data->animation.frames.size()), state)) return { {}, true };
    if (!record_prepared_frame(data->animation.frames[state.completedFrames], data->options,
        allocator, device, descriptorSetLayout, state)) return { {}, true };
    if (device.resetFences(1u, &state.fence) != vk::Result::eSuccess) return { {}, true };
    vk::SubmitInfo submit;
    submit.commandBufferCount = 1u;
    submit.pCommandBuffers = &state.commandBuffer;
    if (queue.submit(1u, &submit, state.fence) != vk::Result::eSuccess) return { {}, true };
    state.submitted = true;
    return {};
}

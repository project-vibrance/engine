#define VMA_IMPLEMENTATION
#include "../src/renderer/common/media_prepared_upload.h"
#include <vibranceUI/core/logger.h>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <thread>

int main()
{
    LoggerOutputOptions logging;
    logging.directory = std::filesystem::current_path() / "media-fence-logs";
    Logger::configure_output(logging);
    Logger::fetch_logger()->set_minimum_level(LogLevel::eWarning);
    const auto instanceResult = vk::createInstance({});
    if (instanceResult.result != vk::Result::eSuccess) return 77;
    const auto instance = instanceResult.value;
    const auto physicals = instance.enumeratePhysicalDevices();
    if (physicals.result != vk::Result::eSuccess || physicals.value.empty())
    { instance.destroy(); return 77; }
    const auto physical = physicals.value.front();
    const auto families = physical.getQueueFamilyProperties();
    uint32_t family = 0u;
    for (; family < families.size(); ++family)
        if (families[family].queueFlags & vk::QueueFlagBits::eGraphics) break;
    if (family == families.size()) { instance.destroy(); return 77; }
    const float priority = 1.0f;
    vk::DeviceQueueCreateInfo queueInfo;
    queueInfo.queueFamilyIndex = family;
    queueInfo.queueCount = 1u;
    queueInfo.pQueuePriorities = &priority;
    vk::DeviceCreateInfo create;
    create.queueCreateInfoCount = 1u;
    create.pQueueCreateInfos = &queueInfo;
    const auto deviceResult = physical.createDevice(create);
    if (deviceResult.result != vk::Result::eSuccess) { instance.destroy(); return 77; }
    const auto device = deviceResult.value;
    const auto queue = device.getQueue(family, 0u);
    const auto path = std::filesystem::temp_directory_path() /
        ("vibrance-upload-fence-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()) + ".ppm");
    { std::ofstream image(path); image << "P3\n2 2\n255\n255 0 0 0 255 0 0 0 255 255 255 255\n"; }
    const auto prepared = prepare_media_2d(path);
    std::error_code ignored;
    std::filesystem::remove(path, ignored);
    bool passed = prepared.valid();
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    PreparedMedia2DAsyncUpload pending;
    const auto fence = device.createFence({});
    if (fence.result != vk::Result::eSuccess) { device.destroy(); instance.destroy(); return 77; }
    pending.fence = fence.value;
    pending.submitted = true;
    PreparedMediaUploadBudget budget;
    constexpr auto reservation = PreparedMediaUploadBudget::limit / 3u;
    expect(budget.reserve(reservation) && budget.reserve(reservation) && budget.reserve(reservation),
        "Three prepared reservations must fit their shared byte budget");
    expect(!budget.reserve(1u), "Further work must wait while pending and cancelled submissions retain the budget");
    PreparedMediaUploadBudget tinyRequests;
    for (std::size_t index = 0u; index < PreparedMediaUploadBudget::maximumActiveUploads; ++index)
        expect(tinyRequests.reserve(1u), "Small pending requests must use one admission slot each");
    expect(!tinyRequests.reserve(1u), "Repeated tiny cancellations must also obey the shared request-count limit");

    // No GPU work signals this fence for 100 ms. Each production pump and
    // retirement check must return while the GPU is still unready.
    using Clock = std::chrono::steady_clock;
    const auto started = Clock::now();
    double maximumPumpMilliseconds = 0.0;
    unsigned pumps = 0u;
    while (Clock::now() - started < std::chrono::milliseconds(100))
    {
        const auto before = Clock::now();
        const auto result = upload_prepared_media_2d_asset_async(prepared, 1u, 1u,
            nullptr, device, queue, family, nullptr, pending);
        const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - before).count();
        maximumPumpMilliseconds = std::max(maximumPumpMilliseconds, elapsed);
        expect(!result.finished && !result.handle.valid(), "An unsignalled upload must remain unpublished");
        expect(!prepared_media_2d_upload_retired(device, pending),
            "A cancelled upload must retain its resources while its fence is unsignalled");
        ++pumps;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    pending.cancelled = true;
    expect(!prepared_media_2d_upload_retired(device, pending),
        "Cancellation must not treat an in-flight submission as complete");
    expect(budget.reservedBytes == PreparedMediaUploadBudget::limit,
        "Cancelling in-flight media must retain its admission reservation");
    expect(pumps > 10u && maximumPumpMilliseconds < 25.0,
        "Prepared upload polling must return promptly throughout a 100 ms GPU stall");
    const auto submitted = queue.submit(0u, nullptr, pending.fence);
    expect(submitted == vk::Result::eSuccess, "The fixture must signal its fence after the simulated GPU delay");
    const auto deadline = Clock::now() + std::chrono::seconds(2);
    while (!prepared_media_2d_upload_retired(device, pending) && Clock::now() < deadline)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    expect(prepared_media_2d_upload_retired(device, pending), "Completed cancelled media must become reclaimable");
    release_prepared_media_2d_upload(nullptr, device, pending);
    budget.release(reservation);
    expect(budget.reserve(reservation), "Retired media must release capacity for the next request");

    // Exercise the new image allocation, transition, staging and mipmap path
    // itself, rather than checking only the resulting handle's metadata.
    VmaAllocatorCreateInfo allocatorInfo {};
    allocatorInfo.instance = instance;
    allocatorInfo.physicalDevice = physical;
    allocatorInfo.device = device;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_0;
    VmaAllocator allocator = nullptr;
    if (vmaCreateAllocator(&allocatorInfo, &allocator) != VK_SUCCESS)
    { device.destroy(); instance.destroy(); return passed ? 77 : 1; }
    vk::DescriptorSetLayoutBinding binding;
    binding.descriptorType = vk::DescriptorType::eCombinedImageSampler;
    binding.descriptorCount = 1u;
    binding.stageFlags = vk::ShaderStageFlagBits::eCompute;
    vk::DescriptorSetLayoutCreateInfo layoutInfo;
    layoutInfo.bindingCount = 1u;
    layoutInfo.pBindings = &binding;
    const auto layoutResult = device.createDescriptorSetLayout(layoutInfo);
    if (layoutResult.result != vk::Result::eSuccess)
    { vmaDestroyAllocator(allocator); device.destroy(); instance.destroy(); return passed ? 77 : 1; }
    const auto layout = layoutResult.value;
    PreparedMedia2DAsyncUpload actual;
    Media2DUploadResult uploaded;
    const auto uploadDeadline = Clock::now() + std::chrono::seconds(2);
    while (!uploaded.finished && Clock::now() < uploadDeadline)
    {
        uploaded = upload_prepared_media_2d_asset_async(prepared, 1u, 1u,
            allocator, device, queue, family, layout, actual);
        if (!uploaded.finished) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    expect(uploaded.finished && uploaded.handle.drawable && actual.asset.image &&
        actual.asset.image->mipLevels == 2u, "A complete prepared image must upload its original pixels and mip chain");
    if (uploaded.finished && uploaded.handle.drawable && actual.asset.image)
    {
        VkBufferCreateInfo bufferInfo { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        bufferInfo.size = 20u;
        bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VmaAllocationCreateInfo allocationInfo {};
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = nullptr;
        VmaAllocationInfo mapped {};
        const auto madeBuffer = vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo,
            &buffer, &allocation, &mapped);
        expect(madeBuffer == VK_SUCCESS && mapped.pMappedData, "The GPU readback buffer must be available");
        if (madeBuffer == VK_SUCCESS && mapped.pMappedData)
        {
            vk::CommandPoolCreateInfo poolInfo;
            poolInfo.queueFamilyIndex = family;
            const auto pool = device.createCommandPool(poolInfo);
            vk::CommandBufferAllocateInfo commandInfo;
            commandInfo.commandPool = pool.value;
            commandInfo.commandBufferCount = 1u;
            commandInfo.level = vk::CommandBufferLevel::ePrimary;
            const auto commands = device.allocateCommandBuffers(commandInfo);
            const auto command = commands.value.front();
            vk::CommandBufferBeginInfo readBegin;
            (void)command.begin(readBegin);
            vk::ImageMemoryBarrier barrier;
            barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
            barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
            barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
            barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
            barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            barrier.image = actual.asset.image->image;
            barrier.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
            barrier.subresourceRange.levelCount = 2u;
            barrier.subresourceRange.layerCount = 1u;
            command.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                vk::PipelineStageFlagBits::eTransfer, {}, 0u, nullptr, 0u, nullptr, 1u, &barrier);
            vk::BufferImageCopy regions[2];
            regions[0].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            regions[0].imageSubresource.layerCount = 1u;
            regions[0].imageExtent = vk::Extent3D(2u, 2u, 1u);
            regions[1].bufferOffset = 16u;
            regions[1].imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
            regions[1].imageSubresource.mipLevel = 1u;
            regions[1].imageSubresource.layerCount = 1u;
            regions[1].imageExtent = vk::Extent3D(1u, 1u, 1u);
            command.copyImageToBuffer(actual.asset.image->image, vk::ImageLayout::eTransferSrcOptimal,
                buffer, 2u, regions);
            (void)command.end();
            const auto readFence = device.createFence({});
            vk::SubmitInfo submission;
            submission.commandBufferCount = 1u;
            submission.pCommandBuffers = &command;
            const auto submittedReadback = queue.submit(1u, &submission, readFence.value);
            expect(submittedReadback == vk::Result::eSuccess, "The GPU readback must submit successfully");
            const auto readDeadline = Clock::now() + std::chrono::seconds(2);
            while (device.getFenceStatus(readFence.value) == vk::Result::eNotReady && Clock::now() < readDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            expect(device.getFenceStatus(readFence.value) == vk::Result::eSuccess, "The GPU readback must retire");
            (void)vmaInvalidateAllocation(allocator, allocation, 0u, 20u);
            const auto* pixels = static_cast<const unsigned char*>(mapped.pMappedData);
            const unsigned char expected[16] = {255,0,0,255, 0,255,0,255, 0,0,255,255, 255,255,255,255};
            expect(std::equal(std::begin(expected), std::end(expected), pixels),
                "Every uploaded base-level RGBA byte must match its authored source");
            expect(std::abs(int(pixels[16]) - 128) <= 1 && std::abs(int(pixels[17]) - 128) <= 1 &&
                std::abs(int(pixels[18]) - 128) <= 1 && pixels[19] == 255u,
                "The uploaded mip must retain the original bilinear colour and alpha");
            device.destroyFence(readFence.value);
            device.destroyCommandPool(pool.value);
        }
        if (buffer) vmaDestroyBuffer(allocator, buffer, allocation);
    }
    // A failed test still drains its test-owned device before releasing data.
    (void)device.waitIdle();
    release_prepared_media_2d_upload(allocator, device, actual);
    device.destroyDescriptorSetLayout(layout);
    vmaDestroyAllocator(allocator);
    device.destroy();
    instance.destroy();
    std::cout << "Unsignalled fence: " << pumps << " nonblocking pumps, maximum "
        << maximumPumpMilliseconds << " ms; cancellation retained its resources and budget until retirement; "
        << "GPU base pixels and mip colour verified.\n";
    return passed ? 0 : 1;
}

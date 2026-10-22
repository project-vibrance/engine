#include <vibranceUI/renderer/image.h>
#include <vibranceUI/core/logger.h>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <unordered_set>

namespace
{
enum class Failure { eNone, eAllocation, eView, eEmptyView, eReset, eBegin, eEnd, eSubmit, eWait };
Failure failure = Failure::eNone;
VkResult allocationFailure = VK_ERROR_OUT_OF_DEVICE_MEMORY;
unsigned allocationCalls = 0, viewCalls = 0, resetCalls = 0, beginCalls = 0;
unsigned barrierCalls = 0, endCalls = 0, submitCalls = 0, waitCalls = 0;
std::unordered_set<VkImage> liveImages;
std::unordered_set<VkImageView> liveViews;

template<class Handle> Handle handle(std::uintptr_t value)
{
    if constexpr (std::is_pointer_v<Handle>) return reinterpret_cast<Handle>(value);
    else return static_cast<Handle>(value);
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void reset(Failure next)
{
    require(liveImages.empty() && liveViews.empty(), "A preceding failure leaked an image or view");
    failure = next;
    allocationCalls = viewCalls = resetCalls = beginCalls = barrierCalls = endCalls = submitCalls = waitCalls = 0;
}

template<class Operation> std::string exception(Operation operation)
{
    try { operation(); }
    catch (const std::runtime_error& error) { return error.what(); }
    throw std::runtime_error("A failed image operation did not report failure");
}
}

// This target compiles the production image owner against deterministic VMA
// and Vulkan failures. No physical device, command buffer or GPU allocation is
// created, so every failure stage is tested without exhausting host resources.
extern "C" VkResult vmaCreateImage(VmaAllocator, const VkImageCreateInfo*,
    const VmaAllocationCreateInfo*, VkImage* image, VmaAllocation* memory, VmaAllocationInfo*)
{
    ++allocationCalls;
    if (failure == Failure::eAllocation) return allocationFailure;
    *image = handle<VkImage>(allocationCalls + 100u);
    *memory = handle<VmaAllocation>(allocationCalls + 200u);
    liveImages.insert(*image);
    return VK_SUCCESS;
}
extern "C" void vmaDestroyImage(VmaAllocator, VkImage image, VmaAllocation memory)
{
    require(memory && liveImages.erase(image) == 1u, "An invalid image was released or an allocation was freed twice");
}
extern "C" void vmaSetAllocationName(VmaAllocator, VmaAllocation, const char*) {}
extern "C" void vmaGetAllocationInfo(VmaAllocator, VmaAllocation, VmaAllocationInfo* info)
{
    std::memset(info, 0, sizeof(*info));
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkCreateImageView(VkDevice,
    const VkImageViewCreateInfo* info, const VkAllocationCallbacks*, VkImageView* view)
{
    ++viewCalls;
    require(liveImages.contains(info->image), "View creation received an unallocated image");
    if (failure == Failure::eView) return VK_ERROR_OUT_OF_HOST_MEMORY;
    if (failure == Failure::eEmptyView) { *view = VK_NULL_HANDLE; return VK_SUCCESS; }
    *view = handle<VkImageView>(viewCalls + 300u);
    liveViews.insert(*view);
    return VK_SUCCESS;
}
extern "C" VKAPI_ATTR void VKAPI_CALL vkDestroyImageView(VkDevice, VkImageView view, const VkAllocationCallbacks*)
{
    require(liveViews.erase(view) == 1u, "An invalid image view was released or freed twice");
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkResetCommandBuffer(VkCommandBuffer, VkCommandBufferResetFlags)
{ ++resetCalls; return failure == Failure::eReset ? VK_ERROR_DEVICE_LOST : VK_SUCCESS; }
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkBeginCommandBuffer(VkCommandBuffer, const VkCommandBufferBeginInfo*)
{ ++beginCalls; return failure == Failure::eBegin ? VK_ERROR_DEVICE_LOST : VK_SUCCESS; }
extern "C" VKAPI_ATTR void VKAPI_CALL vkCmdPipelineBarrier(VkCommandBuffer,
    VkPipelineStageFlags, VkPipelineStageFlags, VkDependencyFlags, uint32_t,
    const VkMemoryBarrier*, uint32_t, const VkBufferMemoryBarrier*, uint32_t count, const VkImageMemoryBarrier* images)
{
    ++barrierCalls;
    require(count == 1u && liveImages.contains(images->image), "A command barrier received an unallocated image");
}
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkEndCommandBuffer(VkCommandBuffer)
{ ++endCalls; return failure == Failure::eEnd ? VK_ERROR_DEVICE_LOST : VK_SUCCESS; }
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueueSubmit(VkQueue, uint32_t, const VkSubmitInfo*, VkFence)
{ ++submitCalls; return failure == Failure::eSubmit ? VK_ERROR_DEVICE_LOST : VK_SUCCESS; }
extern "C" VKAPI_ATTR VkResult VKAPI_CALL vkQueueWaitIdle(VkQueue)
{ ++waitCalls; return failure == Failure::eWait ? VK_ERROR_DEVICE_LOST : VK_SUCCESS; }

int main()
try
{
    LoggerOutputOptions logging;
    logging.directory = std::filesystem::current_path() / "image-allocation-failure-logs";
    Logger::configure_output(logging);
    Logger::fetch_logger()->set_minimum_level(LogLevel::eWarning);
    VmaAllocator allocator = handle<VmaAllocator>(1u);
    vk::Device device(handle<VkDevice>(2u));
    vk::CommandBuffer command(handle<VkCommandBuffer>(3u));
    vk::Queue queue(handle<VkQueue>(4u));
    std::deque<std::function<void(VmaAllocator)>> allocations;
    std::deque<std::function<void(vk::Device)>> views;
    const auto release = [&] {
        while (!views.empty()) { views.back()(device); views.pop_back(); }
        while (!allocations.empty()) { allocations.back()(allocator); allocations.pop_back(); }
        require(liveViews.empty() && liveImages.empty(), "Cleanup did not retire every retained resource");
    };
    const auto construct = [&](unsigned kind) {
        if (kind == 0u)
            StorageImage image(allocator, vk::Format::eR8G8B8A8Unorm, {64u,64u}, command, queue, device, allocations, views);
        else if (kind == 1u)
            DepthImage image(allocator, vk::Format::eD32Sfloat, {64u,64u}, vk::SampleCountFlagBits::e1, device, allocations, views);
        else
            ColorAttachmentImage image(allocator, vk::Format::eR8G8B8A8Unorm, {64u,64u}, vk::SampleCountFlagBits::e1, device, allocations, views);
    };
    for (const auto error : {VK_ERROR_OUT_OF_HOST_MEMORY, VK_ERROR_OUT_OF_DEVICE_MEMORY})
    {
        allocationFailure = error;
        for (unsigned kind=0; kind<3; ++kind)
        {
            reset(Failure::eAllocation);
            const auto detail = exception([&] { construct(kind); });
            require(detail.find("64x64") != std::string::npos, "Allocation error omitted its image extent");
            require(allocationCalls == 1u && viewCalls == 0u && resetCalls == 0u && submitCalls == 0u,
                "Allocation failure attempted view creation or GPU work");
            require(allocations.empty() && views.empty(), "Failed allocation entered a cleanup queue");
        }
    }
    reset(Failure::eAllocation);
    vk::Image image(handle<VkImage>(99u));
    VmaAllocation memory = handle<VmaAllocation>(99u);
    ImageInputChunk input;
    input.extent = vk::Extent2D(64u,64u);
    exception([&] { make_image(allocator, input, image, memory); });
    require(!image && !memory, "Allocation failure retained stale output handles");
    for (const auto stage : {Failure::eView, Failure::eEmptyView})
        for (unsigned kind=0; kind<3; ++kind)
        {
            reset(stage);
            exception([&] { construct(kind); });
            require(allocationCalls == 1u && viewCalls == 1u && resetCalls == 0u && submitCalls == 0u,
                "View failure attempted GPU work");
            require(liveImages.empty() && liveViews.empty() && allocations.empty() && views.empty(),
                "Failed view creation did not release its unsubmitted allocation");
        }
    for (const auto stage : {Failure::eReset,Failure::eBegin,Failure::eEnd,Failure::eSubmit,Failure::eWait})
    {
        reset(stage);
        exception([&] { construct(0u); });
        require(allocations.size() == 1u && views.size() == 1u && liveImages.size() == 1u && liveViews.size() == 1u,
            "Initialisation failure lost ownership of potentially submitted resources");
        if (stage == Failure::eReset) require(beginCalls == 0u, "Reset failure began recording");
        if (stage == Failure::eBegin) require(barrierCalls == 0u, "Begin failure recorded a barrier");
        if (stage == Failure::eEnd) require(submitCalls == 0u, "End failure submitted work");
        if (stage == Failure::eSubmit) require(waitCalls == 0u, "Failed submission waited for invalid work");
        release();
    }
    // A later failed image must preserve earlier successful resources for the
    // owner to release, with no double-free from the failed constructor.
    reset(Failure::eNone);
    construct(0u);
    failure = Failure::eAllocation;
    exception([&] { construct(0u); });
    require(allocations.size() == 1u && views.size() == 1u, "Later failure damaged preceding ownership");
    release();
    reset(Failure::eNone);
    for (unsigned kind=0; kind<3; ++kind) construct(kind);
    require(submitCalls == 1u && waitCalls == 1u, "Attachment construction submitted unnecessary GPU transitions");
    release();
    std::cout << "Image allocation, view and initialisation failure ownership passed.\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }

#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include "device.h"
#include <stdexcept>
#include <string>
#include <limits>
#include <cstring>
std::string_view vibrance_metal_shader(std::string_view name);
namespace vibrance::metal
{
namespace
{
Owned<MTL::Device> supported_device()
{
    auto preferred = own(MTL::CreateSystemDefaultDevice());
    if (preferred && preferred->supportsFamily(MTL::GPUFamilyMetal3))
        return preferred;
    auto devices = own(MTL::CopyAllDevices());
    for (NS::UInteger index = 0; devices && index < devices->count(); ++index)
    {
        auto candidate = devices->object<MTL::Device>(index);
        if (candidate->supportsFamily(MTL::GPUFamilyMetal3))
            return NS::RetainPtr(candidate);
    }
    return {};
}
void require(bool success, const char *message, NS::Error *error = nullptr)
{
    if (!success)
        throw std::runtime_error(
            std::string(message) +
            (error ? ": " + std::string(error->localizedDescription()->utf8String()) : ""));
}
} // namespace
Device::Device(bool preferMetal4)
{
    auto pool = own(NS::AutoreleasePool::alloc()->init());
    gpu = supported_device();
    require(bool(gpu), "No Metal 3-capable GPU is available");
    require(gpu->supportsFamily(MTL::GPUFamilyMetal3), "This GPU does not support Metal 3");
    if (__builtin_available(macOS 26.0, *))
        metal4 = preferMetal4 && gpu->supportsFamily(MTL::GPUFamilyMetal4);
    queue = own(gpu->newCommandQueue());
    require(bool(queue), "Cannot create Metal command queue");
    auto descriptor = own(MTL::SamplerDescriptor::alloc()->init());
    descriptor->setMinFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMagFilter(MTL::SamplerMinMagFilterLinear);
    descriptor->setMipFilter(MTL::SamplerMipFilterLinear);
    descriptor->setSAddressMode(MTL::SamplerAddressModeClampToEdge);
    descriptor->setTAddressMode(MTL::SamplerAddressModeClampToEdge);
    descriptor->setSupportArgumentBuffers(true);
    sampler = own(gpu->newSamplerState(descriptor.get()));
    if (metal4)
    {
        queue4 = own(gpu->newMTL4CommandQueue());
        allocator4 = own(gpu->newCommandAllocator());
        commands4 = own(gpu->newCommandBuffer());
        completed = own(gpu->newSharedEvent());
        auto rd = own(MTL::ResidencySetDescriptor::alloc()->init());
        NS::Error *error = nullptr;
        residency = own(gpu->newResidencySet(rd.get(), &error));
        require(queue4 && allocator4 && commands4 && completed && residency, "Cannot initialise Metal 4",
                error);
        queue4->addResidencySet(residency.get());
        auto bindings = own(MTL4::ArgumentTableDescriptor::alloc()->init());
        bindings->setMaxBufferBindCount(1);
        bindings->setMaxTextureBindCount(boundTextures.size());
        bindings->setMaxSamplerStateBindCount(1);
        bindings->setInitializeBindings(true);
        arguments = own(gpu->newArgumentTable(bindings.get(), &error));
        require(bool(arguments), "Cannot allocate Metal argument table", error);
        arguments->setSamplerState(sampler->gpuResourceID(), 0);
    }
}
Device::~Device()
{
    if (metal4 && inFlight)
    {
        completed->waitUntilSignaledValue(serial, 60000);
    }
    if (!metal4)
        for (auto &frame : metal3Frames)
            if (frame.pending)
                frame.pending->waitUntilCompleted();
}
Owned<MTL::Function> Device::function(const char *name)
{
    NS::Error *error = nullptr;
    auto options = own(MTL::CompileOptions::alloc()->init());
    options->setLanguageVersion(MTL::LanguageVersion3_0);
    const auto source = vibrance_metal_shader(name);
    auto library = own(gpu->newLibrary(string(source.data()), options.get(), &error));
    require(bool(library), name, error);
    auto result = own(library->newFunction(string(name)));
    require(bool(result), name);
    return result;
}
MTL::ComputePipelineState *Device::pipeline(const char *name)
{
    auto found = pipelines.find(name);
    if (found != pipelines.end())
        return found->second.get();
    auto fn = function(name);
    NS::Error *error = nullptr;
    auto state = own(gpu->newComputePipelineState(fn.get(), &error));
    require(bool(state), name, error);
    return pipelines.emplace(name, std::move(state)).first->second.get();
}
Owned<MTL::Texture> Device::target(uint32_t width, uint32_t height, MTL::PixelFormat format)
{
    auto descriptor = MTL::TextureDescriptor::texture2DDescriptor(format, width, height, false);
    descriptor->setStorageMode(MTL::StorageModePrivate);
    descriptor->setUsage(MTL::TextureUsageShaderRead | MTL::TextureUsageShaderWrite |
                         MTL::TextureUsageRenderTarget);
    auto result = own(gpu->newTexture(descriptor));
    require(bool(result), "Cannot allocate Metal render target");
    return result;
}
std::unique_ptr<StorageImage> Device::upload(uint32_t width, uint32_t height,
                                             std::span<const unsigned char> bytes, bool srgb, bool mipmaps)
{
    if (!width || !height || uint64_t(width) * height > SIZE_MAX / 4 ||
        bytes.size() != uint64_t(width) * height * 4)
        return {};
    auto pool = own(NS::AutoreleasePool::alloc()->init());
    auto descriptor = MTL::TextureDescriptor::texture2DDescriptor(
        srgb ? MTL::PixelFormatRGBA8Unorm_sRGB : MTL::PixelFormatRGBA8Unorm, width, height, mipmaps);
    descriptor->setStorageMode(gpu->hasUnifiedMemory() ? MTL::StorageModeShared : MTL::StorageModeManaged);
    descriptor->setUsage(MTL::TextureUsageShaderRead);
    auto image = own(gpu->newTexture(descriptor));
    if (!image)
        return {};
    image->replaceRegion(MTL::Region(0, 0, width, height), 0, bytes.data(), size_t(width) * 4);
    if (mipmaps && image->mipmapLevelCount() > 1)
    {
        auto cb = queue->commandBuffer();
        auto blit = cb->blitCommandEncoder();
        blit->generateMipmaps(image.get());
        blit->endEncoding();
        cb->commit();
        cb->waitUntilCompleted();
        require(cb->status() != MTL::CommandBufferStatusError, "Metal texture upload failed", cb->error());
    }
    auto result = std::make_unique<StorageImage>();
    result->extent = {width, height};
    result->native =
        std::shared_ptr<void>(image->retain(), [](void *p) { static_cast<MTL::Texture *>(p)->release(); });
    return result;
}
void Device::use_resource(MTL::Resource *resource)
{
    auto [entry, inserted] = residentResources.try_emplace(resource);
    if (inserted)
    {
        entry->second.resource = NS::RetainPtr(resource);
        residency->addAllocation(resource);
        residencyChanged = true;
    }
    entry->second.submission = serial;
}
std::pair<MTL::Buffer *, size_t> Device::write_constants(const void *data, size_t size)
{
    const size_t alignedSize = (size + 255u) & ~size_t(255u);
    auto &pages = metal4 ? constantPages : metal3Frames[metal3Frame].constantPages;
    auto &pageIndex = metal4 ? constantPage : metal3Frames[metal3Frame].constantPage;
    while (pageIndex < pages.size() &&
           pages[pageIndex].buffer->length() - pages[pageIndex].used < alignedSize)
        ++pageIndex;
    if (pageIndex == pages.size())
    {
        auto buffer =
            own(gpu->newBuffer(std::max(size_t(65536), alignedSize), MTL::ResourceStorageModeShared));
        require(bool(buffer), "Cannot allocate Metal constants");
        pages.push_back({std::move(buffer), 0});
    }
    auto &page = pages[pageIndex];
    const auto offset = page.used;
    std::memcpy(static_cast<unsigned char *>(page.buffer->contents()) + offset, data, size);
    page.used += alignedSize;
    return {page.buffer.get(), offset};
}
void Device::dispatch(const char *name, const void *data, size_t size, uint32_t width, uint32_t height,
                      std::initializer_list<MTL::Texture *> textures, uint32_t count)
{
    if (!width || !height || !count)
        return;
    require(textures.size() <= boundTextures.size(), "Too many Metal texture bindings");
    auto state = pipeline(name);
    if (!recording)
    {
        if (metal4)
        {
            wait_idle();
            allocator4->reset();
            commands4->beginCommandBuffer(allocator4.get());
            encoder4 = NS::RetainPtr(commands4->computeCommandEncoder());
            encoder4->setArgumentTable(arguments.get());
            encoder4->barrierAfterQueueStages(MTL::StageAll, MTL::StageDispatch,
                                              MTL4::VisibilityOptionDevice);
        }
        else
        {
            retire_metal3_frame(metal3Frames[metal3Frame]);
            commands = NS::RetainPtr(queue->commandBuffer());
            encoder = NS::RetainPtr(commands->computeCommandEncoder());
            encoder->setSamplerState(sampler.get(), 0);
            boundMetal3Textures.fill(nullptr);
        }
        boundPipeline = nullptr;
        recording = true;
    }
    if (metal4)
    {
        const auto [buffer, offset] = write_constants(data, size);
        use_resource(buffer);
        arguments->setAddress(buffer->gpuAddress() + offset, 0);
        size_t index = 0;
        for (auto image : textures)
        {
            require(image != nullptr, "Missing Metal texture binding");
            use_resource(image);
            const auto id = image->gpuResourceID();
            if (boundTextures[index]._impl != id._impl)
            {
                arguments->setTexture(id, index);
                boundTextures[index] = id;
            }
            ++index;
        }
        encoder4->barrierAfterEncoderStages(MTL::StageDispatch, MTL::StageDispatch,
                                            MTL4::VisibilityOptionDevice);
        if (boundPipeline != state)
            encoder4->setComputePipelineState(state);
        encoder4->dispatchThreads(MTL::Size(width, height, count), MTL::Size(8, 8, 1));
    }
    else
    {
        encoder->memoryBarrier(MTL::BarrierScopeTextures);
        if (boundPipeline != state)
            encoder->setComputePipelineState(state);
        if (size <= 4096)
            encoder->setBytes(data, size, 0);
        else
        {
            const auto [buffer, offset] = write_constants(data, size);
            encoder->setBuffer(buffer, offset, 0);
        }
        size_t index = 0;
        for (auto image : textures)
        {
            if (boundMetal3Textures[index] != image)
            {
                encoder->setTexture(image, index);
                boundMetal3Textures[index] = image;
            }
            ++index;
        }
        encoder->dispatchThreads(MTL::Size(width, height, count), MTL::Size(8, 8, 1));
    }
    boundPipeline = state;
}
void Device::submit(CA::MetalDrawable *drawable, bool presentWithTransaction)
{
    if (!recording)
        return;
    if (metal4)
    {
        encoder4->endEncoding();
        encoder4.reset();
        // Retain stable allocations across frames and retire textures no longer
        // referenced by this submission, including old drawables and resized targets.
        std::erase_if(residentResources, [&](const auto &entry) {
            if (entry.second.submission == serial)
                return false;
            residency->removeAllocation(entry.first);
            residencyChanged = true;
            return true;
        });
        if (residencyChanged)
        {
            residency->commit();
            residencyChanged = false;
        }
        commands4->endCommandBuffer();
        auto command = commands4.get();
        queue4->commit(&command, 1);
        queue4->signalEvent(completed.get(), ++serial);
        if (drawable)
        {
            queue4->signalDrawable(drawable);
            drawable->present();
        }
    }
    else
    {
        encoder->endEncoding();
        encoder.reset();
        if (drawable && !presentWithTransaction)
            commands->presentDrawable(drawable);
        commands->commit();
        if (drawable && presentWithTransaction)
        {
            commands->waitUntilScheduled();
            drawable->present();
        }
        metal3Frames[metal3Frame].pending = std::move(commands);
        metal3Frame = (metal3Frame + 1) % metal3Frames.size();
    }
    recording = false;
    if (metal4)
        inFlight = true;
}
void Device::retire_metal3_frame(Metal3Frame &frame)
{
    if (!frame.pending)
        return;
    if (frame.pending->status() != MTL::CommandBufferStatusCompleted)
        frame.pending->waitUntilCompleted();
    require(frame.pending->status() != MTL::CommandBufferStatusError, "Metal GPU submission failed",
            frame.pending->error());
    frame.pending.reset();
    frame.constantPage = 0;
    for (auto &page : frame.constantPages)
        page.used = 0;
}
void Device::wait_idle()
{
    if (!metal4)
    {
        for (auto &frame : metal3Frames)
            retire_metal3_frame(frame);
        return;
    }
    if (!inFlight)
        return;
    // Read the shared completion value first. Normal frame pacing lets the
    // GPU finish before reuse, avoiding a blocking IOKit call per frame.
    if (completed->signaledValue() < serial)
        require(completed->waitUntilSignaledValue(serial, 60000), "Metal 4 GPU submission timed out");
    inFlight = false;
    constantPage = 0;
    for (auto &page : constantPages)
        page.used = 0;
}
void Device::flush()
{
    submit();
    wait_idle();
}
} // namespace vibrance::metal

#include <vibranceUI/renderer/metal/capabilities.h>
unsigned vibrance_metal_version() noexcept
{
    auto pool = vibrance::metal::own(NS::AutoreleasePool::alloc()->init());
    auto device = vibrance::metal::supported_device();
    if (!device)
        return 0;
    if (__builtin_available(macOS 26.0, *))
        if (device->supportsFamily(MTL::GPUFamilyMetal4))
            return 4;
    return device->supportsFamily(MTL::GPUFamilyMetal3) ? 3 : 0;
}

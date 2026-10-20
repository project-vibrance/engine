#define NS_PRIVATE_IMPLEMENTATION
#define MTL_PRIVATE_IMPLEMENTATION
#define CA_PRIVATE_IMPLEMENTATION
#include "device.h"
#include <stdexcept>
#include <string>
#include <limits>
#include <cstring>
#include <cstdlib>
#include <cmath>
#include <chrono>
#include <algorithm>
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
Device::Device()
{
    const char* mode = std::getenv("VIBRANCE_RENDER_DIAGNOSTICS");
    diagnosticsEnabled = mode && (std::string_view(mode) == "1" || std::string_view(mode) == "2");
    gpuDiagnosticsEnabled = mode && std::string_view(mode) == "1";
    auto pool = own(NS::AutoreleasePool::alloc()->init());
    gpu = supported_device();
    require(bool(gpu), "No Metal 3-capable GPU is available");
    require(gpu->supportsFamily(MTL::GPUFamilyMetal3), "This GPU does not support Metal 3");
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
}
Device::~Device()
{
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
Device::TextureUpload Device::upload_async(uint32_t width, uint32_t height,
                                             std::span<const unsigned char> bytes, bool srgb, bool mipmaps)
{
    TextureUpload result;
    if (!width || !height || uint64_t(width) * height > SIZE_MAX / 4 ||
        bytes.size() != uint64_t(width) * height * 4)
        return result;
    auto pool = own(NS::AutoreleasePool::alloc()->init());
    auto descriptor = MTL::TextureDescriptor::texture2DDescriptor(
        srgb ? MTL::PixelFormatRGBA8Unorm_sRGB : MTL::PixelFormatRGBA8Unorm, width, height, mipmaps);
    descriptor->setStorageMode(gpu->hasUnifiedMemory() ? MTL::StorageModeShared : MTL::StorageModeManaged);
    descriptor->setUsage(MTL::TextureUsageShaderRead);
    auto image = own(gpu->newTexture(descriptor));
    if (!image) return result;
    image->replaceRegion(MTL::Region(0, 0, width, height), 0, bytes.data(), size_t(width) * 4);
    result.image = std::make_unique<StorageImage>();
    result.image->extent = { width, height };
    result.image->native =
        std::shared_ptr<void>(image->retain(), [](void* p) { static_cast<MTL::Texture*>(p)->release(); });
    // Retain the texture independently of the asset. A later metadata/vector
    // allocation can fail after submission without releasing an in-flight image.
    result.ticket.image = image;
    if (mipmaps && image->mipmapLevelCount() > 1)
    {
        result.ticket.commands = NS::RetainPtr(queue->commandBuffer());
        require(bool(result.ticket.commands), "Cannot allocate Metal media upload command buffer");
        auto blit = result.ticket.commands->blitCommandEncoder();
        require(blit != nullptr, "Cannot allocate Metal media upload encoder");
        blit->generateMipmaps(image.get());
        blit->endEncoding();
        result.ticket.commands->commit();
    }
    return result;
}
std::pair<MTL::Buffer *, size_t> Device::write_constants(const void *data, size_t size)
{
    const size_t alignedSize = (size + 255u) & ~size_t(255u);
    auto &pages = metal3Frames[metal3Frame].constantPages;
    auto &pageIndex = metal3Frames[metal3Frame].constantPage;
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
    require(textures.size() <= boundMetal3Textures.size(), "Too many Metal texture bindings");
    auto state = pipeline(name);
    if (diagnosticsEnabled)
    {
        ++diagnostics.dispatches;
        diagnostics.dispatchPixels += uint64_t(width) * height * count;
        if (std::string_view(name) == "text_msdf_comp" || std::string_view(name) == "text_msdf_batch_comp")
        {
            ++diagnostics.textDispatches;
            diagnostics.textGlyphs += count;
        }
    }
    if (!recording)
    {
        retire_metal3_frame(metal3Frames[metal3Frame]);
        commands = NS::RetainPtr(queue->commandBuffer());
        encoder = NS::RetainPtr(commands->computeCommandEncoder());
        encoder->setSamplerState(sampler.get(), 0);
        boundMetal3Textures.fill(nullptr);
        boundPipeline = nullptr;
        recording = true;
    }
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
    boundPipeline = state;
}
void Device::submit(CA::MetalDrawable *drawable, bool presentWithTransaction)
{
    submit(drawable ? std::span<CA::MetalDrawable* const>(&drawable, 1) :
        std::span<CA::MetalDrawable* const>{}, presentWithTransaction);
}
void Device::submit(std::span<CA::MetalDrawable* const> drawables, bool presentWithTransaction)
{
    if (!recording)
        return;
    if (diagnosticsEnabled) ++diagnostics.submissions;
    encoder->endEncoding();
    encoder.reset();
    if (!presentWithTransaction)
        for (auto drawable : drawables) commands->presentDrawable(drawable);
    commands->commit();
    if (!drawables.empty() && presentWithTransaction)
    {
        commands->waitUntilScheduled();
        for (auto drawable : drawables) drawable->present();
    }
    metal3Frames[metal3Frame].sampleGpu = gpuDiagnosticsEnabled && (++diagnosticSubmissions % 16u == 0u);
    metal3Frames[metal3Frame].pending = std::move(commands);
    metal3Frame = (metal3Frame + 1) % metal3Frames.size();
    recording = false;
}
void Device::retire_metal3_frame(Metal3Frame &frame)
{
    if (!frame.pending)
        return;
    if (frame.pending->status() != MTL::CommandBufferStatusCompleted)
        frame.pending->waitUntilCompleted();
    const auto failed = frame.pending->status() == MTL::CommandBufferStatusError;
    auto error = failed ? NS::RetainPtr(frame.pending->error()) : Owned<NS::Error>{};
    if (!failed && frame.sampleGpu)
        record_gpu_sample(frame.pending->GPUStartTime(), frame.pending->GPUEndTime());
    frame.sampleGpu = false;
    frame.pending.reset();
    frame.constantPage = 0;
    for (auto &page : frame.constantPages)
        page.used = 0;
    // Retire the failed command buffer before surfacing its error. Otherwise
    // every later frame retries this same failure and rendering cannot recover.
    if (failed)
        throw SubmissionError(std::string("Metal GPU submission failed") +
            (error ? ": " + std::string(error->localizedDescription()->utf8String()) : ""));
    ++successfulCompletions;
}
void Device::record_gpu_sample(double begin, double end)
{
    if (begin > 0 && end >= begin && std::isfinite(begin) && std::isfinite(end))
    {
        const double elapsed = (end - begin) * 1000.0;
        diagnostics.gpuMilliseconds += elapsed;
        diagnostics.gpuMaximumMilliseconds = std::max(diagnostics.gpuMaximumMilliseconds, elapsed);
        ++diagnostics.gpuSamples;
    }
}
void Device::wait_idle()
{
    for (auto &frame : metal3Frames)
        retire_metal3_frame(frame);
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
    return device->supportsFamily(MTL::GPUFamilyMetal3) ? 3 : 0;
}

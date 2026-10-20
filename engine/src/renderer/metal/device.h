#pragma once
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <vibranceUI/renderer/metal/resources.h>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <array>
#include <span>
#include <stdexcept>
#include "media_upload_lifecycle.h"

namespace vibrance::metal
{
template <class T> using Owned = NS::SharedPtr<T>;
template <class T> Owned<T> own(T *object)
{
    return NS::TransferPtr(object);
}
inline NS::String *string(const char *value)
{
    return NS::String::string(value, NS::UTF8StringEncoding);
}
inline MTL::Texture *texture(const StorageImage *image)
{
    return image ? static_cast<MTL::Texture *>(image->native.get()) : nullptr;
}
// Cocoa owns attachment to the view; all rendering uses Metal-cpp.
CA::MetalLayer *attach_surface(void *window, MTL::Device *device, bool transparent);
void detach_surface(void *window, CA::MetalLayer *layer);
bool surface_presents_with_transaction(CA::MetalLayer *layer);

// A completed submission can fail without invalidating the renderer or its
// allocations. Callers can invalidate retained pixels and retry a later frame.
class SubmissionError : public std::runtime_error
{
  public:
    using std::runtime_error::runtime_error;
};

class Device
{
  public:
    struct TextureUploadTicket
    {
        Owned<MTL::CommandBuffer> commands;
        Owned<MTL::Texture> image;
        MediaUploadCompletion completion() const
        {
            if (!commands) return MediaUploadCompletion::eSucceeded;
            const auto status = commands->status();
            if (status == MTL::CommandBufferStatusCompleted) return MediaUploadCompletion::eSucceeded;
            if (status == MTL::CommandBufferStatusError) return MediaUploadCompletion::eFailed;
            return MediaUploadCompletion::ePending;
        }
    };
    struct TextureUpload
    {
        std::unique_ptr<StorageImage> image;
        TextureUploadTicket ticket;
    };
    Device();
    ~Device();
    bool diagnosticsEnabled = false;
    bool gpuDiagnosticsEnabled = false;
    struct Diagnostics
    {
        double gpuMilliseconds = 0, gpuMaximumMilliseconds = 0;
        uint64_t gpuSamples = 0, dispatches = 0, dispatchPixels = 0, textDispatches = 0,
                 textGlyphs = 0, submissions = 0;
    } diagnostics;
    uint64_t diagnosticSubmissions = 0;
    // Only GPU retirement advances this counter. Enqueuing a new frame does
    // not establish that a previous submission succeeded.
    uint64_t successfulCompletions = 0;
    Owned<MTL::Device> gpu;
    Owned<MTL::CommandQueue> queue;
    Owned<MTL::SamplerState> sampler;
    Owned<MTL::CommandBuffer> commands;
    Owned<MTL::ComputeCommandEncoder> encoder;
    MTL::ComputePipelineState *boundPipeline = nullptr;
    std::array<MTL::Texture *, 5> boundMetal3Textures{};
    struct ConstantPage
    {
        Owned<MTL::Buffer> buffer;
        size_t used = 0;
    };
    struct Metal3Frame
    {
        Owned<MTL::CommandBuffer> pending;
        std::vector<ConstantPage> constantPages;
        size_t constantPage = 0;
        bool sampleGpu = false;
    };
    // The offscreen colour and scratch textures stay shared: Metal 3 command
    // buffers submitted to one queue execute in order. Only CPU-written
    // constants and command-buffer references need separate frame storage.
    std::array<Metal3Frame, 2> metal3Frames;
    size_t metal3Frame = 0;
    struct PipelineNameHash
    {
        using is_transparent = void;
        size_t operator()(std::string_view name) const noexcept
        {
            return std::hash<std::string_view>{}(name);
        }
    };
    struct PipelineNameEqual
    {
        using is_transparent = void;
        bool operator()(std::string_view left, std::string_view right) const noexcept
        {
            return left == right;
        }
    };
    // Lookups run for every compute pass; accepting string_view avoids an
    // allocation for shader names longer than std::string's inline capacity.
    std::unordered_map<std::string, Owned<MTL::ComputePipelineState>, PipelineNameHash,
                       PipelineNameEqual> pipelines;
    bool recording = false;
    Owned<MTL::Function> function(const char *name);
    MTL::ComputePipelineState *pipeline(const char *name);
    Owned<MTL::Texture> target(uint32_t width, uint32_t height,
                               MTL::PixelFormat format = MTL::PixelFormatRGBA8Unorm);
    std::unique_ptr<StorageImage> upload(uint32_t width, uint32_t height,
                                         std::span<const unsigned char> bytes, bool srgb, bool mipmaps);
    // Prepared media owns this ticket until its status retires. Existing
    // synchronous font/model/resource uploads retain their current contract.
    TextureUpload upload_async(uint32_t width, uint32_t height,
                               std::span<const unsigned char> bytes, bool srgb, bool mipmaps);
    void dispatch(const char *name, const void *data, size_t size, uint32_t width, uint32_t height,
                  std::initializer_list<MTL::Texture *> textures, uint32_t count = 1);
    void flush();
    void submit(CA::MetalDrawable *drawable = nullptr, bool presentWithTransaction = false);
    void submit(std::span<CA::MetalDrawable* const> drawables, bool presentWithTransaction);

  private:
    std::pair<MTL::Buffer *, size_t> write_constants(const void *data, size_t size);
    void retire_metal3_frame(Metal3Frame &frame);
    void record_gpu_sample(double begin, double end);
    void wait_idle();
};
} // namespace vibrance::metal

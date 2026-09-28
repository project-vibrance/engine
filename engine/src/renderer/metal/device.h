#pragma once
#include <Metal/Metal.hpp>
#include <QuartzCore/QuartzCore.hpp>
#include <vibranceUI/renderer/metal/resources.h>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <array>

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

class Device
{
  public:
    explicit Device(bool preferMetal4 = true);
    ~Device();
    bool metal4 = false;
    Owned<MTL::Device> gpu;
    Owned<MTL::CommandQueue> queue;
    Owned<MTL::SamplerState> sampler;
    Owned<MTL4::CommandQueue> queue4;
    Owned<MTL4::CommandAllocator> allocator4;
    Owned<MTL4::CommandBuffer> commands4;
    Owned<MTL::ResidencySet> residency;
    Owned<MTL::SharedEvent> completed;
    Owned<MTL::CommandBuffer> commands;
    Owned<MTL::ComputeCommandEncoder> encoder;
    Owned<MTL4::ComputeCommandEncoder> encoder4;
    MTL::ComputePipelineState *boundPipeline = nullptr;
    std::array<MTL::Texture *, 5> boundMetal3Textures{};
    struct ConstantPage
    {
        Owned<MTL::Buffer> buffer;
        size_t used = 0;
    };
    // A submission owns its constant ranges until GPU completion. Argument
    // tables are snapshotted by Metal at each dispatch, so one table suffices.
    std::vector<ConstantPage> constantPages;
    size_t constantPage = 0;
    struct Metal3Frame
    {
        Owned<MTL::CommandBuffer> pending;
        std::vector<ConstantPage> constantPages;
        size_t constantPage = 0;
    };
    // The offscreen colour and scratch textures stay shared: Metal 3 command
    // buffers submitted to one queue execute in order. Only CPU-written
    // constants and command-buffer references need separate frame storage.
    std::array<Metal3Frame, 2> metal3Frames;
    size_t metal3Frame = 0;
    Owned<MTL4::ArgumentTable> arguments;
    std::array<MTL::ResourceID, 5> boundTextures{};
    struct ResidentResource
    {
        Owned<MTL::Resource> resource;
        uint64_t submission = 0;
    };
    std::unordered_map<MTL::Resource *, ResidentResource> residentResources;
    bool residencyChanged = false;
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
    uint64_t serial = 0;
    bool recording = false;
    bool inFlight = false;
    Owned<MTL::Function> function(const char *name);
    MTL::ComputePipelineState *pipeline(const char *name);
    Owned<MTL::Texture> target(uint32_t width, uint32_t height,
                               MTL::PixelFormat format = MTL::PixelFormatRGBA8Unorm);
    std::unique_ptr<StorageImage> upload(uint32_t width, uint32_t height,
                                         std::span<const unsigned char> bytes, bool srgb, bool mipmaps);
    void dispatch(const char *name, const void *data, size_t size, uint32_t width, uint32_t height,
                  std::initializer_list<MTL::Texture *> textures, uint32_t count = 1);
    void flush();
    void submit(CA::MetalDrawable *drawable = nullptr, bool presentWithTransaction = false);

  private:
    void use_resource(MTL::Resource *resource);
    std::pair<MTL::Buffer *, size_t> write_constants(const void *data, size_t size);
    void retire_metal3_frame(Metal3Frame &frame);
    void wait_idle();
};
} // namespace vibrance::metal

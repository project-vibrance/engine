#pragma once
#include <cstdint>
#include <memory>
#include <span>
#include <functional>
struct RendererExtent
{
    uint32_t width = 0, height = 0;
};
// The shared owner retains the native texture without exposing Objective-C.
class StorageImage
{
  public:
    RendererExtent extent;
    std::shared_ptr<void> native;
};
using MetalTextureUpload = std::function<std::unique_ptr<StorageImage>(
    uint32_t, uint32_t, std::span<const unsigned char>, bool, bool)>;

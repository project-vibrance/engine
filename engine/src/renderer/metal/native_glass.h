#pragma once
#include "device.h"
#include <vibranceUI/graphics/backdrop.h>
#include <glm/vec4.hpp>
#include <memory>
#include <span>
#include <string>

namespace vibrance::metal
{
struct NativeGlassRegion
{
    uint64_t identity = 0;
    glm::vec4 rect, clip;
    glm::vec4 contentRect;
    bool hasContent = false;
    float cornerRadius = 0, opacity = 1;
    SystemBackdropColor tint;
    // Transparent foreground planes preserve paint order for menus and other
    // content above a lens without introducing another native glass material.
    bool material = true;
};

// Each native lens owns a compact Metal content view for its descendants.
// The main scene remains one retained framebuffer behind AppKit's material.
class NativeGlassSurface
{
public:
    NativeGlassSurface(void* window, MTL::Device* device, CA::MetalLayer* base);
    ~NativeGlassSurface();
    void begin_frame(std::span<const NativeGlassRegion> regions, uint32_t width, uint32_t height);
    CA::MetalLayer* layer(size_t index) const;
    bool visible(size_t index) const;
    std::string describe(CA::MetalLayer* layer) const;
    void end_frame();
    bool active() const;
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
}

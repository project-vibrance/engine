// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

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


    bool material = true;
    bool clear = false;
};



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

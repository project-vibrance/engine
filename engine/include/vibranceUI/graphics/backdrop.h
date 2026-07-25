#pragma once

#include <vibranceUI/export.h>

#include <cstdint>

// Backdrop material names are platform-neutral. Backdrop rendering is only
// available when a platform presenter explicitly reports support; otherwise
// every requested material resolves to eOff.
enum class SystemBackdropMaterial : std::uint32_t
{
    eOff = 0u,
    eBlur = 1u,
    eFrosted = 2u,
    // Reserved for the Windows-private backdrop backend. Until that backend
    // is available, liquid requests deliberately resolve to eOff.
    eLiquid = 3u
};

enum class SystemBackdropProvider : std::uint32_t
{
    // The engine builds a supported Windows Composition effect graph.
    eEngine = 0u,
    // Windows-owned materials. These are Windows-only and policy-controlled.
    eWindowsAcrylic = 1u,
    eWindowsMica = 2u
};

enum class SystemBackdropShape : std::uint32_t
{
    eRectangle = 0u,
    eRoundedRectangle = 1u,
    eEllipse = 2u,
    eSquircle = 3u
};

struct SystemBackdropColor
{
    float red = 1.0f;
    float green = 1.0f;
    float blue = 1.0f;
    float alpha = 0.0f;
};

struct SystemBackdropRegion
{
    SystemBackdropMaterial material = SystemBackdropMaterial::eOff;
    SystemBackdropProvider provider = SystemBackdropProvider::eEngine;
    SystemBackdropShape shape = SystemBackdropShape::eRoundedRectangle;
    // Most regions follow their renderer entity automatically. Set this false
    // when the Vulkan paint geometry is clipped by a parent but the independent
    // platform backdrop visual needs an explicit safety clip of its own.
    bool deriveShapeFromEntity = true;

    // Framebuffer-space rectangle. It is deliberately independent from glm so
    // the DirectX bridge can retain a POD-only ABI.
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
    float cornerRadius = 0.0f;
    float topLeftRadius = 0.0f;
    float topRightRadius = 0.0f;
    float bottomRightRadius = 0.0f;
    float bottomLeftRadius = 0.0f;
    float squircleAmount = 1.0f;
    float squirclePower = 4.0f;

    // Engine material controls. Windows Acrylic/Mica may ignore these values.
    float blurRadius = 24.0f;
    float saturation = 1.0f;
    SystemBackdropColor tint { 1.0f, 1.0f, 1.0f, 0.14f };
};

VIBRANCE_ENGINE_API const char* system_backdrop_material_name(SystemBackdropMaterial material);
VIBRANCE_ENGINE_API const char* system_backdrop_provider_name(SystemBackdropProvider provider);
VIBRANCE_ENGINE_API bool system_backdrop_platform_supported();

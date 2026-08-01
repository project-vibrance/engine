#pragma once

#include <vibranceUI/export.h>

#include <cstdint>

// Glass material names are platform-neutral, but their owners are explicit:
// eSystemGlass is supplied by the platform compositor while eLiquid belongs
// to the engine renderer. Neither material silently substitutes for the other.
enum class GlassMaterial : std::uint32_t
{
    eOff = 0u,
    eSystemGlass = 1u,
    eLiquid = 2u
};

enum class LiquidGlassBackend : std::uint32_t
{
    // Renderer-owned implementation used by the cross-platform engine.
    eEngineRenderer = 0u,
    // Reserved for Apple's native Liquid Glass implementation. The slot is
    // intentionally present now, but reports unavailable until implemented.
    eMacOSNative = 1u
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
    eSquircle = 3u,
    eNotchedSquircle = 4u
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
    // Only eOff and eSystemGlass are valid here. eLiquid is represented by a
    // LiquidGlassComponent and is never forwarded to a platform compositor.
    GlassMaterial material = GlassMaterial::eOff;
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
    // Animated notch geometry used by island-style surfaces. Composition uses
    // these values to flatten the leading corners with the Vulkan shape.
    float notchAmount = 0.0f;
    float notchDepth = 0.0f;
    // Fraction of the shape height kept free of material from the top. The
    // outer shape clip still covers the complete entity.
    float verticalStart = 0.0f;

    // System-glass controls. Windows Acrylic/Mica may ignore these values.
    float blurRadius = 24.0f;
    float saturation = 1.0f;
    SystemBackdropColor tint { 1.0f, 1.0f, 1.0f, 0.14f };
};

VIBRANCE_ENGINE_API const char* glass_material_name(GlassMaterial material);
VIBRANCE_ENGINE_API const char* system_backdrop_provider_name(SystemBackdropProvider provider);
VIBRANCE_ENGINE_API bool system_backdrop_platform_supported();
VIBRANCE_ENGINE_API LiquidGlassBackend default_liquid_glass_backend();
VIBRANCE_ENGINE_API bool liquid_glass_backend_supported(LiquidGlassBackend backend);

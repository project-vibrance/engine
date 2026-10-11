#pragma once

#include <vibranceUI/export.h>

#include <cstdint>
#include <array>

// Glass material names are platform-neutral, but their owners are explicit:
// eSystemGlass is supplied by the platform compositor. On Windows, eLiquid
// uses a shaped live Composition backdrop and an optional DWM shader pass.
enum class GlassMaterial : std::uint32_t
{
    eOff = 0u,
    eSystemGlass = 1u,
    eLiquid = 2u
};

enum class LiquidGlassBackend : std::uint32_t
{
    // Existing Windows Composition approximation. Kept during migration.
    eEngineRenderer = 0u,
    // Apple's native AppKit Liquid Glass (macOS 26 and later).
    eMacOSNative = 1u,
    // Windows DWM HLSL path; reports unavailable until its extension is ready.
    eWindowsCompositionShader = 2u
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
    eNotchedSquircle = 4u,
    eLiquidBridge = 5u
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
    // Stable renderer identity, retained across region sorting and animation.
    std::uint64_t identity = 0u;
    // eLiquid is forwarded for the Windows live backdrop underlay. The
    // optical shader request remains a separate LiquidGlassComponent.
    GlassMaterial material = GlassMaterial::eOff;
    LiquidGlassBackend liquidBackend = LiquidGlassBackend::eEngineRenderer;
    SystemBackdropProvider provider = SystemBackdropProvider::eEngine;
    SystemBackdropShape shape = SystemBackdropShape::eRoundedRectangle;
    // Most regions follow their renderer entity automatically. Set this false
    // when the Vulkan paint geometry is clipped by a parent but the independent
    // platform backdrop visual needs an explicit safety clip of its own.
    bool deriveShapeFromEntity = true;
    // Control lenses retain the sampling halo but clip its identity paint to
    // the control itself, so a halo cannot escape the enclosing interface.
    bool clipRawBackdropToShape = false;

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
    // eLiquidBridge reuses notchAmount as its smooth-union merge amount.
    float notchAmount = 0.0f;
    float notchDepth = 0.0f;
    // Fraction of the shape height kept free of material from the top. The
    // outer shape clip still covers the complete entity.
    float verticalStart = 0.0f;

    // Effective visual opacity resolved from the same inherited renderer
    // transitions as the Vulkan entity. Platform composition backends use it
    // to keep native material and Vulkan content on one animation timeline.
    float opacity = 1.0f;

    // System-glass controls. Windows Acrylic/Mica may ignore these values.
    float blurRadius = 24.0f;
    float saturation = 1.0f;
    SystemBackdropColor tint { 1.0f, 1.0f, 1.0f, 0.14f };
    // Normalised optical controls for the native Liquid Glass shader.
    float liquidRefraction = 0.70f;
    float liquidBlur = 0.15f;
    float liquidRim = 0.60f;
    float liquidDispersion = 0.35f;
    SystemBackdropColor liquidTint { 213.0f / 255.0f, 231.0f / 255.0f, 1.0f, 0.0f };
    // Contours use coordinates relative to this region's framebuffer bounds.
    struct Contour
    {
        float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
        float cornerRadius = 0.0f;
        float topLeftRadius = 0.0f, topRightRadius = 0.0f;
        float bottomRightRadius = 0.0f, bottomLeftRadius = 0.0f;
        float squircleAmount = 1.0f, squirclePower = 4.0f;
        float notchAmount = 0.0f, notchDepth = 0.0f;
        std::uint32_t shape = 0u;
    };
    std::array<Contour, 3> contours {};
    std::uint32_t contourCount = 0u;
    Contour exclusion {};
    bool hasExclusion = false;
    bool paintCapGradient = false;
    std::array<float, 3> capColour { 0.0f, 0.0f, 0.0f };
    // Top/bottom opacity, start/end fraction of the merged material height.
    std::array<float, 4> capGradient { 0.98f, 0.015f, 0.0f, 1.0f };
};
static_assert(sizeof(SystemBackdropRegion::Contour) == 56u);

VIBRANCE_ENGINE_API const char* glass_material_name(GlassMaterial material);
VIBRANCE_ENGINE_API const char* system_backdrop_provider_name(SystemBackdropProvider provider);
VIBRANCE_ENGINE_API bool system_backdrop_platform_supported();
VIBRANCE_ENGINE_API LiquidGlassBackend default_liquid_glass_backend();
VIBRANCE_ENGINE_API bool liquid_glass_backend_supported(LiquidGlassBackend backend);

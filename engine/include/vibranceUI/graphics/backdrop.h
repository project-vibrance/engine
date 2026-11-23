// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/export.h>

#include <cstdint>
#include <array>




enum class GlassMaterial : std::uint32_t
{
    eOff = 0u,
    eSystemGlass = 1u,
    eLiquid = 2u
};

enum class LiquidGlassBackend : std::uint32_t
{

    eEngineRenderer = 0u,

    eMacOSNative = 1u,

    eWindowsCompositionShader = 2u
};

enum class SystemBackdropProvider : std::uint32_t
{

    eEngine = 0u,

    eWindowsAcrylic = 1u,
    eWindowsMica = 2u,
    eDesktopCompositor = 3u
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

    std::uint64_t identity = 0u;


    GlassMaterial material = GlassMaterial::eOff;
    LiquidGlassBackend liquidBackend = LiquidGlassBackend::eEngineRenderer;
    SystemBackdropProvider provider = SystemBackdropProvider::eEngine;
    SystemBackdropShape shape = SystemBackdropShape::eRoundedRectangle;



    bool deriveShapeFromEntity = true;


    bool clipRawBackdropToShape = false;



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



    float notchAmount = 0.0f;
    float notchDepth = 0.0f;


    float verticalStart = 0.0f;




    float opacity = 1.0f;


    float blurRadius = 24.0f;
    float saturation = 1.0f;
    SystemBackdropColor tint { 1.0f, 1.0f, 1.0f, 0.14f };

    float liquidRefraction = 0.70f;
    float liquidBlur = 0.15f;
    float liquidRim = 0.60f;
    bool liquidRimAnimated = true;
    float liquidDispersion = 0.35f;
    SystemBackdropColor liquidTint { 213.0f / 255.0f, 231.0f / 255.0f, 1.0f, 0.0f };

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



    std::array<float, 4> viewportRect {};
    std::array<float, 4> viewportFade {};

    std::array<float, 4> capGradient { 0.98f, 0.015f, 0.0f, 1.0f };

    bool liquidPreserveBackdropColour = false;
};
static_assert(sizeof(SystemBackdropRegion::Contour) == 56u);

VIBRANCE_ENGINE_API const char* glass_material_name(GlassMaterial material);
VIBRANCE_ENGINE_API const char* system_backdrop_provider_name(SystemBackdropProvider provider);
VIBRANCE_ENGINE_API bool system_backdrop_platform_supported();
VIBRANCE_ENGINE_API LiquidGlassBackend default_liquid_glass_backend();
VIBRANCE_ENGINE_API bool liquid_glass_backend_supported(LiquidGlassBackend backend);

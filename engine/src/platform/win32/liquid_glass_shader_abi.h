#pragma once

#include <cstddef>
#include <cstdint>

// Constant-buffer layouts shared by the Windows Liquid Glass HLSL passes.
// These are independent of CPU architecture and must match register(b0).
struct alignas(16) LiquidGlassContourParameters
{
    float rect[4];
    float radii[4];
    float params[4];
    float kind[4];
};

struct alignas(16) LiquidGlassShaderParameters
{
    float panelSize[2];
    float backdropTexelSize[2];
    float cornerRadius;
    float edgeThickness;
    float refractionStrength;
    float dispersionStrength;
    float blurRadius;
    float rimStrength;
    float lightPhase;
    float opacity;
    float cornerExponent;
    float tintColour[3];
    float backdropUvOrigin[2];
    float backdropUvScale[2];
    float cornerRadii[4];
    float squircleAmount;
    float notchAmount;
    float notchDepth;
    float shape;
    float panelOrigin[2];
    float drawSize[2];
    float tintStrength;
    std::uint32_t contourCount;
    std::uint32_t hasExclusion;
    // Bit 0 enables the cap; upper three bytes contain its RGB8 colour.
    std::uint32_t paintCapGradient;
    float capGradient[4];
    LiquidGlassContourParameters contours[3];
    LiquidGlassContourParameters exclusion;
};

struct alignas(16) LiquidGlassBlurParameters
{
    float texelSize[2];
    float centreWeight;
    std::uint32_t pairCount;
    // Adjacent Gaussian taps share one linear fetch: offset, weight, padding.
    float pairs[15][4];
};

static_assert(sizeof(LiquidGlassContourParameters) == 64);
static_assert(sizeof(LiquidGlassShaderParameters) == 416);
static_assert(offsetof(LiquidGlassShaderParameters, cornerExponent) == 48);
static_assert(offsetof(LiquidGlassShaderParameters, backdropUvOrigin) == 64);
static_assert(offsetof(LiquidGlassShaderParameters, cornerRadii) == 80);
static_assert(offsetof(LiquidGlassShaderParameters, panelOrigin) == 112);
static_assert(offsetof(LiquidGlassShaderParameters, tintStrength) == 128);
static_assert(offsetof(LiquidGlassShaderParameters, capGradient) == 144);
static_assert(offsetof(LiquidGlassShaderParameters, contours) == 160);
static_assert(offsetof(LiquidGlassShaderParameters, exclusion) == 352);
static_assert(sizeof(LiquidGlassBlurParameters) == 256);
static_assert(offsetof(LiquidGlassBlurParameters, pairs) == 16);

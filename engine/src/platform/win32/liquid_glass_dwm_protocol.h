#pragma once

#include <cstdint>
#include <string>

namespace vibrance::win32
{
inline constexpr wchar_t liquidDwmRegionName[] =
    L"Local\\VibranceLiquidGlassDwmRegionV14";
inline constexpr wchar_t liquidDwmReadyName[] =
    L"Local\\VibranceLiquidGlassDwmReadyV14";
inline constexpr wchar_t liquidDwmConfigName[] =
    L"Local\\VibranceLiquidGlassDwmConfigV14";
inline constexpr wchar_t liquidDwmStatusName[] =
    L"Local\\VibranceLiquidGlassDwmStatusV16";
inline constexpr std::uint32_t liquidDwmProtocolVersion = 14;
// Words 0-159 hold hook diagnostics; words 160-223 count each lens's frames.
inline constexpr std::uint32_t liquidDwmStatusWords = 256;
inline constexpr std::uint32_t liquidDwmUiBuffers = 2;
inline constexpr std::uint32_t liquidDwmRegionSlots = 64;
inline constexpr std::uint32_t liquidDwmRegionStatusBase = 160;

inline std::wstring liquid_dwm_ui_name(std::uint32_t ownerPid,
    std::uint64_t window, std::uint32_t index)
{
    return L"Local\\VibranceLiquidGlassUiV14_" +
        std::to_wstring(ownerPid) + L"_" + std::to_wstring(window) +
        L"_" + std::to_wstring(index);
}

inline std::wstring liquid_dwm_cpu_ui_name(std::uint32_t ownerPid,
    std::uint64_t window, std::uint32_t width, std::uint32_t height)
{
    return L"Local\\VibranceLiquidGlassUiCpuV14_" +
        std::to_wstring(ownerPid) + L"_" + std::to_wstring(window) +
        L"_" + std::to_wstring(width) + L"_" +
        std::to_wstring(height);
}

struct alignas(8) LiquidDwmCpuUiHeader
{
    std::uint32_t sequence = 0;
    std::uint32_t version = liquidDwmProtocolVersion;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::uint32_t rowPitch = 0;
    std::uint32_t ownerPid = 0;
    std::uint32_t frame = 0;
    std::uint32_t reserved = 0;
    std::uint64_t hwnd = 0;
};

static_assert(sizeof(LiquidDwmCpuUiHeader) == 40);

// A translucent near-black brush marks the exact spot in DWM's visual tree where
// the live lens replaces the brush draw. Its colour bytes are deliberately
// uncommon and are checked together with the published island rectangle.
inline constexpr std::uint8_t liquidMarkerRed = 1;
inline constexpr std::uint8_t liquidMarkerGreen = 2;
inline constexpr std::uint8_t liquidMarkerBlue = 3;
inline constexpr std::uint8_t liquidMarkerAlpha = 128;

struct LiquidDwmContour
{
    float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
    float cornerRadius = 0.0f;
    float topLeftRadius = 0.0f, topRightRadius = 0.0f;
    float bottomRightRadius = 0.0f, bottomLeftRadius = 0.0f;
    float squircleAmount = 1.0f, squirclePower = 4.0f;
    float notchAmount = 0.0f, notchDepth = 0.0f;
    std::uint32_t shape = 0u;
};
static_assert(sizeof(LiquidDwmContour) == 56u);

struct alignas(8) LiquidDwmRegion
{
    std::uint32_t sequence = 0;
    std::uint32_t version = liquidDwmProtocolVersion;
    std::uint64_t hwnd = 0;
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float cornerRadius = 0.0f;
    float edgeThickness = 0.0f;
    float refractionStrength = 0.0f;
    float dispersionStrength = 0.0f;
    float blurRadius = 0.0f;
    float rimStrength = 0.0f;
    float opacity = 0.0f;
    float cornerExponent = 4.0f;
    std::uint32_t active = 0;
    std::uint32_t ownerPid = 0;
    std::uint32_t uiBufferIndex = 0;
    std::uint32_t uiFrame = 0;
    std::int32_t screenX = 0;
    std::int32_t screenY = 0;
    std::int32_t monitorLeft = 0;
    std::int32_t monitorTop = 0;
    std::uint32_t monitorWidth = 0;
    std::uint32_t monitorHeight = 0;
    std::uint32_t clientWidth = 0;
    std::uint32_t clientHeight = 0;
    std::uint32_t shape = 3u;
    float squircleAmount = 1.0f;
    float notchAmount = 0.0f;
    float notchDepth = 0.0f;
    float topLeftRadius = -1.0f;
    float topRightRadius = -1.0f;
    float bottomRightRadius = -1.0f;
    float bottomLeftRadius = -1.0f;
    // Native marker bounds also cover the notch's flared leading corners.
    float markerInsetX = 0.0f;
    float reserved = 0.0f;
    float tintRed = 213.0f / 255.0f;
    float tintGreen = 231.0f / 255.0f;
    float tintBlue = 1.0f;
    float tintAlpha = 0.08f;
    // Allocation token distinguishes a retained entry from a reused slot.
    std::uint64_t regionId = 0u;
    std::uint32_t contourCount = 0u;
    std::uint32_t hasExclusion = 0u;
    // Bit 0 enables the cap; upper three bytes contain its RGB8 colour.
    std::uint32_t paintCapGradient = 0u;
    std::uint32_t maskReserved = 0u;
    float capGradient[4] { 0.98f, 0.015f, 0.0f, 1.0f };
    LiquidDwmContour contours[3] {};
    LiquidDwmContour exclusion {};
    // Body-relative clip rectangle; top/bottom fade heights, minimum opacity, enabled.
    float viewportRect[4] {};
    float viewportFade[4] {};
};

static_assert(sizeof(LiquidDwmRegion) == 464);

struct alignas(8) LiquidDwmRegionTable
{
    LiquidDwmRegion regions[liquidDwmRegionSlots];
};

struct alignas(8) LiquidDwmHookConfig
{
    std::uint32_t version = liquidDwmProtocolVersion;
    std::uint32_t moduleMachine = 0;
    std::uint64_t presentRva = 0;
    std::uint64_t displaySwapchainVtableRva = 0;
    std::uint64_t displayPhysicalRva = 0;
    std::uint64_t displayBufferVtableRva = 0;
    std::uint64_t displayResourceRva = 0;
    std::uint64_t legacySwapchainVtableRva = 0;
    std::uint64_t legacyPhysicalRva = 0;
    std::uint64_t legacyBufferVtableRva = 0;
    std::uint64_t legacyResourceRva = 0;
    std::uint64_t windowRenderRva = 0;
    std::uint64_t windowGetHwndRva = 0;
    std::uint64_t drawingFlushRva = 0;
    std::uint8_t dwmcoreSha256[32] {};
    std::uint64_t captureRenderRva = 0;
    std::uint64_t offscreenRenderRva = 0;
    std::uint64_t capturePresentRva = 0;
    std::uint64_t captureCurrentFrameBufferRva = 0;
    std::uint64_t ddaRenderRva = 0;
    std::uint64_t ddaPresentRva = 0;
    std::uint64_t ddaGetBackBufferRva = 0;
    std::uint64_t ddaGetTexture2DRva = 0;
    std::uint64_t ddaSwapchainVtableRva = 0;
    std::uint64_t ddaTargetVtableRva = 0;
    std::uint64_t contentDrawRva = 0;
    std::uint64_t colourBrushVtableRva = 0;
    std::uint64_t applyRenderStateRva = 0;
    std::uint64_t drawListEntryRenderRva = 0;
    std::uint64_t batchCommandEmitRva = 0;
    std::uint64_t commonEffectEmitRva = 0;
    std::uint64_t brushEffectEmitRva = 0;
    std::uint64_t batchFlushRva = 0;
    std::uint64_t drawListEntryVtableRva = 0;
    std::uint64_t brushDrawRva = 0;
};

static_assert(sizeof(LiquidDwmHookConfig) == 296);
}

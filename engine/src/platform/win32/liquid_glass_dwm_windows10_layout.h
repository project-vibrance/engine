#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <span>

namespace vibrance::win32
{
inline constexpr wchar_t liquidDwmWindows10ConfigName[] =
    L"Local\\VibranceLiquidGlassDwmWindows10ConfigV2";

// A separate mapping leaves the existing Windows 11 hook configuration ABI
// unchanged. The native adapter is restricted to Windows 10 22H2 on x64.
struct LiquidDwmWindows10Config
{
    std::uint32_t version = 2u;
    std::uint32_t windowsBuild = 19045u;
    std::uint64_t setLocalToWorldRva = 0u;
    std::uint64_t brushParametersRva = 0u;
    std::uint64_t colourConstructorRva = 0u;
    std::uint64_t entryBoundsRva = 0u;
    std::uint64_t entryEffectRva = 0u;
    std::uint64_t emitGeometryImplRva = 0u;
    std::uint8_t dwmcoreSha256[32] {};
};
static_assert(sizeof(LiquidDwmWindows10Config) == 88u);

inline bool liquid_dwm_native_hooks_allowed(std::uint32_t build, std::uint32_t machine = 0x8664u) noexcept
{
    return build >= 22000u || (build == 19045u && machine == 0x8664u);
}

inline bool liquid_dwm_uses_raw_backdrop_dependency(std::uint32_t build) noexcept
{
    return build != 19045u;
}

inline bool liquid_dwm_uses_windows10_adapter(std::uint32_t build,
                                             std::uint32_t machine) noexcept
{
    return build == 19045u && machine == 0x8664u;
}

// Verify the field accesses in the exact fingerprinted image, rather than
// assuming that a matching public function name implies a matching object ABI.
inline bool validate_liquid_windows10_layout(std::span<const std::uint8_t> image,
    const LiquidDwmWindows10Config& layout, std::uint64_t commonEmitRva,
    std::uint64_t brushDrawRva, std::uint64_t entryRenderRva) noexcept
{
    const auto code = [&](std::uint64_t rva, std::size_t length) {
        return rva && rva < image.size() && length <= image.size() - rva ?
            image.subspan(static_cast<std::size_t>(rva), length) :
            std::span<const std::uint8_t> {};
    };
    const auto contains = [](auto bytes, const auto& pattern) {
        return std::search(bytes.begin(), bytes.end(), pattern.begin(), pattern.end()) != bytes.end();
    };
    constexpr std::array<std::uint8_t, 19> bounds {
        0x48,0x8b,0x81,0xb8,0,0,0,0x0f,0x10,0x40,0x48,
        0x48,0x8b,0xc2,0xf3,0x0f,0x7f,0x02,0xc3
    };
    constexpr std::array<std::uint8_t, 8> effect { 0x48,0x8b,0x81,0xb0,0,0,0,0xc3 };
    constexpr std::array<std::uint8_t, 4> baseAdjustment { 0x49,0x8d,0x40,0xf0 };
    constexpr std::array<std::uint8_t, 4> brushColour { 0x0f,0x10,0x43,0x50 };
    constexpr std::array<std::uint8_t, 4> brushRoot { 0x4c,0x8d,0x77,0x98 };
    constexpr std::array<std::uint8_t, 4> renderDevice { 0x48,0x8b,0x46,0x08 };
    constexpr std::array<std::uint8_t, 7> deviceContext { 0x48,0x8b,0x88,0x58,0x02,0,0 };
    constexpr std::array<std::uint8_t, 5> matrixStart { 0x0f,0x11,0x7c,0x18,0x08 };
    constexpr std::array<std::uint8_t, 6> matrixEnd { 0x44,0x0f,0x11,0x54,0x18,0x38 };
    constexpr std::array<std::uint8_t, 5> queueAppend { 0x48,0x83,0x46,0x48,0x4c };
    constexpr std::array<std::uint8_t, 4> queueBegin { 0x4c,0x8b,0x4f,0x40 };
    constexpr std::array<std::uint8_t, 5> queuePop { 0x48,0x83,0x47,0x48,0xb4 };
    const auto boundsCode = code(layout.entryBoundsRva, bounds.size());
    const auto effectCode = code(layout.entryEffectRva, effect.size());
    const auto emitCode = code(commonEmitRva, baseAdjustment.size());
    if (layout.version != 2u || layout.windowsBuild != 19045u ||
        !std::equal(boundsCode.begin(), boundsCode.end(), bounds.begin(), bounds.end()) ||
        !std::equal(effectCode.begin(), effectCode.end(), effect.begin(), effect.end()) ||
        !std::equal(emitCode.begin(), emitCode.end(), baseAdjustment.begin(), baseAdjustment.end()) ||
        !contains(code(layout.brushParametersRva, 64u), brushColour) ||
        !contains(code(brushDrawRva, 128u), brushRoot) ||
        !contains(code(layout.setLocalToWorldRva, 192u), renderDevice) ||
        !contains(code(layout.setLocalToWorldRva, 192u), deviceContext) ||
        !contains(code(entryRenderRva, 512u), matrixStart) ||
        !contains(code(entryRenderRva, 512u), matrixEnd) ||
        !contains(code(entryRenderRva, 512u), queueAppend) ||
        !contains(code(layout.emitGeometryImplRva, 512u), queueBegin) ||
        !contains(code(layout.emitGeometryImplRva, 512u), queuePop)) return false;

    // CBrush::Draw receives the IUnknown base at +0x68, after the IContent
    // thunk's -16 adjustment. Colour at +0x50 therefore uses a -0x18 read.
    const auto constructor = code(layout.colourConstructorRva, 32u);
    for (std::size_t index = 0u; index + 7u <= constructor.size(); ++index)
    {
        if (constructor[index] != 0x48 || constructor[index + 1u] != 0x8d ||
            constructor[index + 2u] != 0x05) continue;
        std::int32_t displacement = 0;
        std::memcpy(&displacement, constructor.data() + index + 3u, 4u);
        const auto address = static_cast<std::int64_t>(layout.colourConstructorRva + index + 7u) + displacement;
        if (address < 0) continue;
        const auto table = code(static_cast<std::uint64_t>(address), 12u);
        std::array<std::int32_t, 3> offsets {};
        if (table.size() == sizeof(offsets))
        {
            std::memcpy(offsets.data(), table.data(), sizeof(offsets));
            if (offsets == std::array<std::int32_t, 3> { -0x38, 0x30, 0x40 }) return true;
        }
    }
    return false;
}
}

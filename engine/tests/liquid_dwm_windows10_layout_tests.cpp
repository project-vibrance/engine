#include "../src/platform/win32/liquid_glass_dwm_windows10_layout.h"

#include <iostream>
#include <vector>

int main()
{
    using namespace vibrance::win32;
    int failures = 0;
    const auto check = [&](bool value, const char* message) {
        if (!value) { std::cerr << message << '\n'; ++failures; }
    };
    for (auto build : { 0u, 17763u, 19041u, 19044u, 20348u })
        check(!liquid_dwm_native_hooks_allowed(build), "Unverified OS builds must not activate native hooks");
    for (auto build : { 19045u, 22000u, 22621u, 26100u, 28000u })
        check(liquid_dwm_native_hooks_allowed(build), "Verified Windows builds must allow hook activation");
    check(!liquid_dwm_native_hooks_allowed(19045u, 0xaa64u), "Windows 10 ARM64 must retain its fallback");
    check(liquid_dwm_uses_windows10_adapter(19045u, 0x8664u), "Windows 10 22H2 x64 must select its adapter");
    for (auto build : { 0u, 17763u, 19041u, 19044u, 20348u, 22000u, 22621u, 26100u, 28000u })
        check(!liquid_dwm_uses_windows10_adapter(build, 0x8664u), "Other builds must retain their existing hook path");
    check(!liquid_dwm_uses_windows10_adapter(19045u, 0xaa64u), "Unverified Windows 10 ARM64 layouts must not be enabled");
    check(!liquid_dwm_uses_raw_backdrop_dependency(19045u), "Windows 10 must avoid the opaque raw backdrop dependency");
    check(liquid_dwm_uses_raw_backdrop_dependency(22621u) && liquid_dwm_uses_raw_backdrop_dependency(26100u),
        "Windows 11 must retain its raw backdrop dependency");
    std::vector<std::uint8_t> image(2048u);
    LiquidDwmWindows10Config layout;
    layout.entryBoundsRva = 32u;
    layout.entryEffectRva = 64u;
    layout.brushParametersRva = 128u;
    layout.setLocalToWorldRva = 256u;
    layout.colourConstructorRva = 512u;
    layout.emitGeometryImplRva = 1024u;
    constexpr std::uint64_t renderRva = 1536u;
    constexpr std::array<std::uint8_t, 19> bounds {
        0x48,0x8b,0x81,0xb8,0,0,0,0x0f,0x10,0x40,0x48,0x48,0x8b,0xc2,0xf3,0x0f,0x7f,0x02,0xc3 };
    constexpr std::array<std::uint8_t, 8> effect { 0x48,0x8b,0x81,0xb0,0,0,0,0xc3 };
    constexpr std::array<std::uint8_t, 4> colour { 0x0f,0x10,0x43,0x50 };
    constexpr std::array<std::uint8_t, 4> device { 0x48,0x8b,0x46,0x08 };
    constexpr std::array<std::uint8_t, 7> context { 0x48,0x8b,0x88,0x58,0x02,0,0 };
    constexpr std::array<std::uint8_t, 4> adjustment { 0x49,0x8d,0x40,0xf0 };
    std::copy(bounds.begin(), bounds.end(), image.begin() + layout.entryBoundsRva);
    std::copy(effect.begin(), effect.end(), image.begin() + layout.entryEffectRva);
    std::copy(colour.begin(), colour.end(), image.begin() + layout.brushParametersRva);
    std::copy(device.begin(), device.end(), image.begin() + layout.setLocalToWorldRva);
    std::copy(context.begin(), context.end(), image.begin() + layout.setLocalToWorldRva + 16u);
    std::copy(adjustment.begin(), adjustment.end(), image.begin() + 768u);
    constexpr std::array<std::uint8_t, 4> brushRoot { 0x4c,0x8d,0x77,0x98 };
    std::copy(brushRoot.begin(), brushRoot.end(), image.begin() + 800u);
    constexpr std::array<std::uint8_t, 16> renderQueue {
        0x0f,0x11,0x7c,0x18,0x08, 0x44,0x0f,0x11,0x54,0x18,0x38, 0x48,0x83,0x46,0x48,0x4c };
    constexpr std::array<std::uint8_t, 9> emitQueue {
        0x4c,0x8b,0x4f,0x40, 0x48,0x83,0x47,0x48,0xb4 };
    std::copy(renderQueue.begin(), renderQueue.end(), image.begin() + renderRva);
    std::copy(emitQueue.begin(), emitQueue.end(), image.begin() + layout.emitGeometryImplRva);
    image[512u] = 0x48; image[513u] = 0x8d; image[514u] = 0x05;
    const std::int32_t displacement = 600 - 519;
    std::memcpy(image.data() + 515u, &displacement, sizeof(displacement));
    const std::array<std::int32_t, 3> bases { -0x38, 0x30, 0x40 };
    std::memcpy(image.data() + 600u, bases.data(), sizeof(bases));
    check(validate_liquid_windows10_layout(image, layout, 768u, 800u, renderRva), "Known Windows 10 access layout should validate");
    for (auto offset : { 35u, 43u, 67u, 131u, 259u, 275u, 608u, 771u, 803u, 1027u, 1032u, 1540u, 1546u, 1551u })
    {
        auto changed = image;
        changed[offset] ^= 1u;
        check(!validate_liquid_windows10_layout(changed, layout, 768u, 800u, renderRva), "A changed private object ABI must be rejected");
    }
    for (auto invalid : std::array<std::uint64_t, 3> { 0u, 1024u, UINT64_MAX })
    {
        auto changed = layout;
        changed.entryBoundsRva = invalid;
        check(!validate_liquid_windows10_layout(image, changed, 768u, 800u, renderRva), "Invalid RVAs must be rejected without reading outside the image");
    }
    layout.version = 3u;
    check(!validate_liquid_windows10_layout(image, layout, 768u, 800u, renderRva), "Unknown layout versions must be rejected");
    return failures ? 1 : 0;
}

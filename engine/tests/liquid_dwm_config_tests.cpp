#include "../src/platform/win32/liquid_glass_dwm_protocol.h"

#include <cstddef>
#include <iostream>
#include <limits>

int main()
{
    using namespace vibrance::win32;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    // Reproduce the reported compositor: display and marker-rendering symbols
    // exist, but the obsolete COverlayContext::Present lookup is unavailable.
    LiquidDwmHookConfig config {};
    config.moduleMachine = 0x8664u;
    config.displaySwapchainVtableRva = 0x100u;
    config.displayPhysicalRva = 0x200u;
    config.displayBufferVtableRva = 0x300u;
    config.displayResourceRva = 0x400u;
    config.windowRenderRva = 0x500u;
    config.windowGetHwndRva = 0x600u;
    config.drawingFlushRva = 0x700u;
    config.contentDrawRva = 0x800u;
    config.colourBrushVtableRva = 0x900u;
    config.applyRenderStateRva = 0xa00u;
    config.drawListEntryRenderRva = 0xb00u;
    config.batchCommandEmitRva = 0xc00u;
    config.commonEffectEmitRva = 0xd00u;
    config.brushEffectEmitRva = 0xe00u;
    config.batchFlushRva = 0xf00u;
    config.drawListEntryVtableRva = 0x1000u;
    config.brushDrawRva = 0x1100u;
    check(config.presentRva == 0 && liquid_dwm_windows11_config_complete(config),
        "A complete marker renderer must start without a presentation-counter symbol");
    config.presentRva = std::numeric_limits<std::uint64_t>::max();
    check(liquid_dwm_windows11_config_complete(config),
        "The reserved presentation word must not affect renderer admission");
    auto incomplete = config;
    incomplete.batchCommandEmitRva = 0;
    check(!liquid_dwm_windows11_config_complete(incomplete),
        "A missing optical batch entry point must still reject the compositor");
    incomplete = config;
    incomplete.displayResourceRva = 0;
    check(!liquid_dwm_windows11_config_complete(incomplete),
        "A missing display resource accessor must still reject the compositor");
    incomplete = config;
    incomplete.windowGetHwndRva = 0;
    check(!liquid_dwm_windows11_config_complete(incomplete),
        "A missing window identity accessor must still reject the compositor");
    static_assert(offsetof(LiquidDwmHookConfig, presentRva) == 8);
    static_assert(offsetof(LiquidDwmHookConfig, brushDrawRva) == 288);
    static_assert(sizeof(LiquidDwmHookConfig) == 296);
    return passed ? 0 : 1;
}

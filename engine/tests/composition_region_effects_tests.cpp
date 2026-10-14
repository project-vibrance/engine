#include "../src/platform/win32/composition_region_effects.h"

#include <iostream>

int main()
{
    using namespace vibrance::win32;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    VibranceCompositionRegion notification {};
    notification.material = 2u;
    notification.liquidBackend = 2u;
    notification.width = 376.0f;
    notification.height = 80.0f;
    notification.liquidTintRed = 58.0f / 255.0f;
    notification.liquidTintGreen = 58.0f / 255.0f;
    notification.liquidTintBlue = 64.0f / 255.0f;
    notification.tintAlpha = 0.0f;
    notification.liquidTintAlpha = 0.8f;
    const auto tint = composition_fallback_tint(notification);
    check(tint == std::array<float, 4> { 58.0f / 255.0f, 58.0f / 255.0f, 64.0f / 255.0f, 0.8f },
        "Notification fallback must retain its Liquid Glass tint when system tint is transparent");
    for (const float intensity : { 0.0f, 0.25f, 1.0f, 0.8f })
    {
        auto changed = notification;
        changed.liquidTintAlpha = intensity;
        check(composition_fallback_tint(changed)[3] == intensity,
            "Fallback tint must honour clear, intermediate and opaque slider values");
        check(same_composition_region_effect_graph(notification, changed, false) == (intensity == 0.8f),
            "A changed fallback tint must invalidate retained card visuals");
        check(same_composition_region_effect_graph(notification, changed, true),
            "A DWM tint uniform update must retain its native marker graph");
    }
    auto coloured = notification;
    coloured.liquidTintRed = 0.9f;
    check(!same_composition_region_effect_graph(notification, coloured, false),
        "Changing the notification palette must refresh fallback tint colour");
    auto moved = notification;
    moved.x = 20.0f;
    moved.y = 40.0f;
    moved.width = 320.0f;
    moved.height = 90.0f;
    moved.topLeftRadius = 18.0f;
    moved.opacity = 0.5f;
    check(same_composition_region_effect_graph(notification, moved, false),
        "Notification scroll, resize and fade must retain a compatible fallback graph");
    auto system = notification;
    system.material = 1u;
    system.tintRed = 0.1f;
    system.tintGreen = 0.2f;
    system.tintBlue = 0.3f;
    system.tintAlpha = 0.4f;
    check(composition_fallback_tint(system) == std::array<float, 4> { 0.1f, 0.2f, 0.3f, 0.4f },
        "System glass must keep its own independent tint");
    system.tintAlpha = 0.5f;
    auto changedSystem = system;
    changedSystem.tintAlpha = 0.6f;
    check(!same_composition_region_effect_graph(system, changedSystem, false),
        "System-glass tint changes must still invalidate its graph");
    return passed ? 0 : 1;
}

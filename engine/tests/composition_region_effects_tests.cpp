#include "../src/platform/win32/composition_region_effects.h"

#include <iostream>
#include <cmath>
#include <limits>

int main()
{
    using namespace vibrance::win32;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    check(liquid_glass_cap_fully_opaque(true, 1.0f, 1.0f) &&
        liquid_glass_cap_fully_opaque(true, 2.0f, 3.0f),
        "A painted cap is fully opaque when both gradient endpoints clamp to one");
    const float translucent = std::nextafter(1.0f, 0.0f);
    check(!liquid_glass_cap_fully_opaque(false, 1.0f, 1.0f) &&
        !liquid_glass_cap_fully_opaque(true, translucent, 1.0f) &&
        !liquid_glass_cap_fully_opaque(true, 1.0f, translucent) &&
        !liquid_glass_cap_fully_opaque(true, std::numeric_limits<float>::quiet_NaN(), 1.0f),
        "Absent or even slightly translucent cap paint must retain its optical material");
    VibranceCompositionRegion rim {};
    rim.liquidRim = 0.6f;
    rim.opacity = 1.0f;
    check(composition_liquid_rim_clock_required(rim),
        "A visible animated glass rim must retain its native clock");
    rim.paintCapGradient = 0xDDEEFF01u;
    rim.capGradient[0] = rim.capGradient[1] = 1.0f;
    check(!composition_liquid_rim_clock_required(rim),
        "Fully opaque cap paint must stop the invisible native rim clock");
    rim.opacity = 0.35f;
    check(!composition_liquid_rim_clock_required(rim),
        "Fading a solid shell must not expose optical material or restart its rim clock");
    rim.capGradient[1] = translucent;
    check(composition_liquid_rim_clock_required(rim),
        "A cap becoming translucent during expansion must restart visible rim animation");
    rim.flags |= 2u;
    check(!composition_liquid_rim_clock_required(rim),
        "A static rim must remain still on a translucent cap");
    rim.flags = 0u;
    rim.opacity = 0.0f;
    check(!composition_liquid_rim_clock_required(rim),
        "An invisible shell must not animate its rim");
    rim.opacity = 1.0f;
    rim.liquidRim = 0.0f;
    check(!composition_liquid_rim_clock_required(rim),
        "A disabled rim must not schedule its native clock");
    rim.liquidRim = 0.6f;
    rim.paintCapGradient &= ~1u;
    rim.capGradient[1] = 1.0f;
    check(composition_liquid_rim_clock_required(rim),
        "Packed cap colour without the paint flag must not suppress a visible rim");
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

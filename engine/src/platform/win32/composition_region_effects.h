#pragma once

#include "composition_bridge_abi.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace vibrance::win32
{
inline std::array<float, 4> composition_fallback_tint(
    const VibranceCompositionRegion& region) noexcept
{
    // A failed native lens retains its requested tint. System-glass tint is a
    // separate material control and may deliberately be transparent for liquid.
    if (region.material == 2u)
        return { region.liquidTintRed, region.liquidTintGreen,
            region.liquidTintBlue, region.liquidTintAlpha };
    return { region.tintRed, region.tintGreen, region.tintBlue, region.tintAlpha };
}

inline bool same_composition_region_effect_graph(VibranceCompositionRegion left,
    VibranceCompositionRegion right, bool shaderOnly) noexcept
{
    // A working DWM lens publishes optical controls and tint as live uniforms.
    // Its identity and shader-slot compatibility are checked by the caller.
    if (shaderOnly) return true;
    // Position and shape are updated on retained visuals. Material/provider,
    // blur, saturation, tint and vertical split require a new fallback graph.
    const auto clearGeometry = [](VibranceCompositionRegion& region) {
        region.shape = 0u;
        region.x = region.y = region.width = region.height = 0.0f;
        region.cornerRadius = region.topLeftRadius = region.topRightRadius = 0.0f;
        region.bottomRightRadius = region.bottomLeftRadius = 0.0f;
        region.squircleAmount = region.squirclePower = 0.0f;
        region.notchAmount = region.notchDepth = region.opacity = 0.0f;
        region.contourCount = 0u;
        region.hasExclusion = 0u;
        std::fill_n(region.viewportRect, 4u, 0.0f);
        std::fill_n(region.viewportFade, 3u, 0.0f);
        region.paintCapGradient = 0u;
        region.flags &= 1u;
        std::fill_n(region.capGradient, 4u, 0.0f);
        std::fill_n(region.contours, 3u, VibranceCompositionContour {});
        region.exclusion = {};
    };
    clearGeometry(left);
    clearGeometry(right);
    if (left.material == 2u && right.material == 2u &&
        left.liquidBackend == 2u && right.liquidBackend == 2u)
    {
        // These optical effects are unavailable in the shaped-blur fallback.
        // Tint is available there, so it must remain in the comparison.
        left.liquidRefraction = right.liquidRefraction = 0.0f;
        left.liquidBlur = right.liquidBlur = 0.0f;
        left.liquidRim = right.liquidRim = 0.0f;
        left.liquidDispersion = right.liquidDispersion = 0.0f;
    }
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}
}

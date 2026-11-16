// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "composition_bridge_abi.h"
#include "liquid_glass_cap_policy.h"
#include "liquid_glass_dwm_protocol.h"

#include <algorithm>
#include <array>
#include <cstring>

namespace vibrance::win32
{
inline std::uint32_t composition_supported_material_flags(
    std::uint32_t requested, std::uint32_t capabilities) noexcept
{


    return capabilities & liquidDwmPreserveBackdropColour ? requested : requested & ~2u;
}

inline bool composition_liquid_rim_clock_required(
    const VibranceCompositionRegion& region) noexcept
{
    return region.liquidRim > 0.0f && (region.flags & 2u) == 0u &&
        region.opacity > 0.001f && !liquid_glass_cap_fully_opaque(
            (region.paintCapGradient & 1u) != 0u,
            region.capGradient[0], region.capGradient[1]);
}

inline std::array<float, 4> composition_fallback_tint(
    const VibranceCompositionRegion& region) noexcept
{


    if (region.material == 2u)
        return { region.liquidTintRed, region.liquidTintGreen,
            region.liquidTintBlue, region.liquidTintAlpha };
    return { region.tintRed, region.tintGreen, region.tintBlue, region.tintAlpha };
}

inline bool same_composition_region_effect_graph(VibranceCompositionRegion left,
    VibranceCompositionRegion right, bool shaderOnly) noexcept
{


    if (shaderOnly) return true;


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


        left.liquidRefraction = right.liquidRefraction = 0.0f;
        left.liquidRim = right.liquidRim = 0.0f;
        left.liquidDispersion = right.liquidDispersion = 0.0f;
    }
    return std::memcmp(&left, &right, sizeof(left)) == 0;
}
}

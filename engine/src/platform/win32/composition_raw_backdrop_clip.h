#pragma once

#include "composition_bridge_abi.h"
#include "liquid_glass_cap_policy.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace vibrance::win32
{
inline VibranceCompositionRegion composition_raw_backdrop_source_region(
    VibranceCompositionRegion region) noexcept
{
    // Card occlusion controls where the optical result is visible, not the
    // source pixels needed by refraction and blur. A backing card still
    // samples through the card in front, beyond its exposed lower strip.
    // Keep that full lens dependency; only the optical shader cuts it out.
    region.hasExclusion = 0u;
    region.exclusion = {};
    return region;
}

inline bool composition_raw_backdrop_required(const VibranceCompositionRegion& region) noexcept
{
    return !liquid_glass_cap_fully_opaque((region.paintCapGradient & 1u) != 0u,
        region.capGradient[0], region.capGradient[1]);
}

inline float composition_bridge_clip_distance(float x, float y, float width,
    float height, float amount) noexcept
{
    const float radius = std::max(height * 0.5f, 0.5f);
    const float offset = std::max(width * 0.5f - radius, 0.0f);
    x -= width * 0.5f;
    const float left = std::hypot(x + offset, y) - radius;
    const float right = std::hypot(x - offset, y) - radius;
    const auto smooth = [](float low, float high, float value) {
        const float t = std::clamp((value - low) / std::max(high - low, 0.0001f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    const float gap = std::max(offset * 2.0f - radius * 2.0f, 0.0f);
    const float smoothing = radius * 1.6f * smooth(0, 1, amount) *
        smooth(0, radius * 0.9f, offset * 2.0f) *
        (1.0f - smooth(0, radius * 0.9f, gap)) *
        (1.0f - smooth(radius * 0.65f, radius, std::abs(y)));
    if (smoothing <= 0.001f) return std::min(left, right);
    const float blend = std::clamp(0.5f + 0.5f * (right - left) / smoothing, 0.0f, 1.0f);
    return right + (left - right) * blend - smoothing * blend * (1.0f - blend);
}

// Sample only a one-dimensional boundary, not a framebuffer mask. A fixed
// angular grid clusters samples at the caps' outer extremes; bisection gives
// the vertical boundary and cleanly separates disconnected figures.
inline std::vector<std::vector<std::array<float, 2>>> composition_bridge_clip_profiles(
    float width, float height, float amount)
{
    std::vector<std::vector<std::array<float, 2>>> figures;
    if (width <= 0 || height <= 0) return figures;
    constexpr unsigned segments = 128u;
    constexpr float pi = 3.14159265358979323846f;
    std::vector<std::array<float, 2>> profile;
    float previousX = 0.0f;
    bool previousInside = false;
    const auto edge = [&](float x) {
        float low = 0, high = height * 0.5f;
        for (unsigned iteration = 0; iteration < 14; ++iteration)
        {
            const float middle = (low + high) * 0.5f;
            if (composition_bridge_clip_distance(x, middle, width, height, amount) <= 0) low = middle;
            else high = middle;
        }
        return (low + high) * 0.5f;
    };
    for (unsigned index = 0; index <= segments; ++index)
    {
        const float x = width * 0.5f * (1.0f - std::cos(pi * index / segments));
        const bool inside = composition_bridge_clip_distance(x, 0, width, height, amount) <= 0;
        if (index > 0 && inside != previousInside)
        {
            float low = previousX, high = x;
            for (unsigned iteration = 0; iteration < 14; ++iteration)
            {
                const float middle = (low + high) * 0.5f;
                if ((composition_bridge_clip_distance(middle, 0, width, height, amount) <= 0) == previousInside) low = middle;
                else high = middle;
            }
            profile.push_back({(low + high) * 0.5f, 0});
            if (!inside) { figures.push_back(std::move(profile)); profile = {}; }
        }
        if (inside) profile.push_back({x, edge(x)});
        previousX = x;
        previousInside = inside;
    }
    if (!profile.empty()) figures.push_back(std::move(profile));
    return figures;
}
}

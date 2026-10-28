#pragma once

#include "liquid_glass_d3d11.h"

#if defined(_WIN32)
#include <algorithm>
#include <cmath>

namespace vibrance::win32
{
// Restrict output work, never backdrop availability. Uniform rounded boxes and
// squircles are convex, so four points inside the fully excluded sublevel set
// prove that their whole rectangle contributes no optical pixels.
inline D3D11_RECT liquid_glass_output_bounds(const LiquidGlassRenderRegion& region,
    D3D11_RECT bounds) noexcept
{
    if (!region.hasExclusion || region.contourCount != 0u ||
        (region.shape != 0u && region.shape != 1u && region.shape != 3u) ||
        region.markerInsetX < 0.0f || region.markerInsetX * 2.0f >= region.width)
        return bounds;
    const auto& cutout = region.exclusion;
    if ((cutout.shape != 0u && cutout.shape != 1u && cutout.shape != 3u) ||
        cutout.width <= 0.0f || cutout.height <= 0.0f)
        return bounds;
    const float halfWidth = std::max(cutout.width * 0.5f, 0.5f);
    const float halfHeight = std::max(cutout.height * 0.5f, 0.5f);
    const auto radiusFor = [&](float value) {
        return std::clamp(value > 0.0f ? value : cutout.cornerRadius,
            0.0f, std::min(halfWidth, halfHeight));
    };
    const float radius = cutout.shape == 0u ? 0.0f : radiusFor(cutout.topLeftRadius);
    if (cutout.shape != 0u && (radiusFor(cutout.topRightRadius) != radius ||
            radiusFor(cutout.bottomRightRadius) != radius ||
            radiusFor(cutout.bottomLeftRadius) != radius))
        return bounds;
    const float amount = cutout.shape == 3u ? std::clamp(cutout.squircleAmount, 0.0f, 1.0f) : 0.0f;
    const float exponent = 2.0f + (std::clamp(cutout.squirclePower, 2.0f, 5.0f) - 2.0f) * amount;
    const auto distance = [&](float x, float y) {
        const float localX = x - region.x - region.markerInsetX - cutout.x - halfWidth;
        const float localY = y - region.y - cutout.y - halfHeight;
        const float qx = std::abs(localX) - std::max(halfWidth - radius, 0.0f);
        const float qy = std::abs(localY) - std::max(halfHeight - radius, 0.0f);
        const float ox = std::max(qx, 0.0f), oy = std::max(qy, 0.0f);
        const float euclidean = std::hypot(ox, oy);
        const float norm = ox > 0.0f && oy > 0.0f ?
            std::pow(std::pow(ox, exponent) + std::pow(oy, exponent), 1.0f / exponent) : ox + oy;
        return euclidean + (norm - euclidean) * amount + std::min(std::max(qx, qy), 0.0f) - radius;
    };
    // Marker padding is not optical paint. Keep a full pixel beyond the real
    // panel so the proof also includes the shader's half-pixel antialias band.
    const float left = std::max(float(bounds.left), float(region.x) + region.markerInsetX - 1.0f);
    const float right = std::min(float(bounds.right), float(region.x) + region.width - region.markerInsetX + 1.0f);
    const auto covered = [&](float y) {
        // Shader coverage vanishes at -0.5. The extra half pixel here plus a
        // retained output row keep floating-point boundary rounding harmless.
        return distance(left, y) <= -1.0f && distance(right, y) <= -1.0f;
    };
    if (left >= right || !covered(float(bounds.top))) return bounds;
    if (covered(float(bounds.bottom))) { bounds.bottom = bounds.top; return bounds; }
    float low = float(bounds.top), high = float(bounds.bottom);
    for (unsigned iteration = 0u; iteration < 16u; ++iteration)
    {
        const float middle = (low + high) * 0.5f;
        if (covered(middle)) low = middle;
        else high = middle;
    }
    bounds.top = std::clamp(static_cast<LONG>(std::floor(low)) - 1, bounds.top, bounds.bottom);
    return bounds;
}
}
#endif

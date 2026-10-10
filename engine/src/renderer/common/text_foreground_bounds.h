#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <optional>

namespace vibrance::renderer
{
    // Generated monochrome glyphs contain zero-alpha bitmap padding. Outside
    // the bitmap their stored distance is at most the negative distance to it,
    // with <= 1/15 atlas pixel rounding error. Two atlas pixels cover that error,
    // bilinear support and floating-point reconstruction. The foreground still
    // evaluates all sixteen coverage samples inside the returned rectangle.
    inline std::optional<std::array<float, 4>> sdf_foreground_bounds(
        const std::array<float, 4>& rect,
        const std::array<float, 2>& atlasGlyphSize,
        float padding, float pixelRange, float weightExpansion, float outlineWidth)
    {
        for (const float value : rect)
            if (!std::isfinite(value) || std::abs(value) > 32768.0f) return std::nullopt;
        for (const float value : atlasGlyphSize)
            if (!std::isfinite(value)) return std::nullopt;
        if (!std::isfinite(padding) || !std::isfinite(pixelRange) ||
            !std::isfinite(weightExpansion) || !std::isfinite(outlineWidth) ||
            padding <= 2.0f || rect[2] < 1.0f || rect[3] < 1.0f ||
            atlasGlyphSize[0] <= padding * 2.0f ||
            atlasGlyphSize[1] <= padding * 2.0f)
            return std::nullopt;

        const float ratioX = atlasGlyphSize[0] / rect[2];
        const float ratioY = atlasGlyphSize[1] / rect[3];
        const float distanceScale = std::max(std::min(ratioX, ratioY), 0.001f);
        const float screenSupport = 0.72f + std::max(weightExpansion, 0.0f) +
            std::max(outlineWidth, 0.0f);
        // The public text range may differ from the generator's padding/range.
        // Account for that change before excluding any source distance values.
        const float sourceMargin = screenSupport * distanceScale * padding /
            std::max(pixelRange, 1.0f) + 2.0f;
        const float sourceInset = padding - sourceMargin;
        if (!(sourceInset > 0.0f)) return std::nullopt;

        // Coverage reaches +/- 0.375 screen pixels. Retain another 0.01 for
        // reconstruction at large framebuffer coordinates and fractional poses.
        const float insetX = std::max(sourceInset / ratioX - 0.385f, 0.0f);
        const float insetY = std::max(sourceInset / ratioY - 0.385f, 0.0f);
        return std::array<float, 4> {
            rect[0] + insetX, rect[1] + insetY,
            rect[2] - 2.0f * insetX, rect[3] - 2.0f * insetY
        };
    }
}

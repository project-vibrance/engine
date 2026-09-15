#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vibrance::animation
{
    struct LottieSvgFrame
    {
        std::string svg;
        double durationSeconds = 0.0;
    };

    struct LottieSvgAnimation
    {
        std::vector<LottieSvgFrame> frames;
        uint32_t width = 0;
        uint32_t height = 0;
        double durationSeconds = 0.0;
        double frameRate = 0.0;
        uint32_t ignoredExpressionCount = 0;
        uint32_t unsupportedFeatureCount = 0;
        std::string error;

        bool valid() const
        {
            return !frames.empty() && width > 0u && height > 0u;
        }
    };

    // Parses the portable subset used by UI icon animations and evaluates it
    // into standalone SVG frames. Rasterisation and GPU upload remain owned by
    // Media2D, so the Lottie parser is independent of Vulkan and future
    // rendering backends.
    LottieSvgAnimation make_lottie_svg_animation(
        std::string_view json,
        uint32_t requestedFrameCount = 0u,
        uint32_t maximumFrameCount = 0u);
}

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstdint>

struct CompositionReadbackRegion
{
    std::uint32_t x = 0u, y = 0u, width = 0u, height = 0u;
    std::uint64_t byteOffset = 0u, byteSpan = 0u;
};



inline CompositionReadbackRegion composition_readback_region(
    std::uint32_t surfaceWidth, std::uint32_t surfaceHeight,
    std::uint32_t x, std::uint32_t y, std::uint32_t width,
    std::uint32_t height, bool firstUse)
{
    if (firstUse)
    {
        x = y = 0u;
        width = surfaceWidth;
        height = surfaceHeight;
    }
    x = std::min(x, surfaceWidth);
    y = std::min(y, surfaceHeight);
    width = std::min(width, surfaceWidth - x);
    height = std::min(height, surfaceHeight - y);
    if (width == 0u || height == 0u) return {};
    return { x, y, width, height,
        (std::uint64_t(y) * surfaceWidth + x) * 4u,
        (std::uint64_t(height - 1u) * surfaceWidth + width) * 4u };
}

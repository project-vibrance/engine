// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cstdint>



inline uint32_t renderer2d_dispatch_tail(uint32_t width, uint32_t height)
{
    return ((8u - width % 8u) % 8u) | (((8u - height % 8u) % 8u) << 3u);
}

inline uint32_t renderer2d_primitive_dispatch_data(uint32_t primitive, uint32_t width, uint32_t height)
{
    return (primitive & 255u) | (renderer2d_dispatch_tail(width, height) << 8u);
}

inline uint32_t renderer2d_blur_dispatch_flags(uint32_t flags, uint32_t width, uint32_t height)
{

    return (flags & ~126u) | (renderer2d_dispatch_tail(width, height) << 1u);
}


inline uint32_t renderer2d_dispatch_axis_extent(
    uint32_t origin, uint32_t length, uint32_t surfaceLength)
{
    return origin < surfaceLength ? std::min(length, surfaceLength - origin) : 0u;
}





inline uint32_t renderer2d_dispatch_axis_coverage(
    uint32_t origin, uint32_t length, uint32_t surfaceLength)
{
    if (origin >= surfaceLength || length == 0u)
    {
        return 0u;
    }
    constexpr uint64_t groupSize = 8u;
    const uint64_t rounded =
        ((static_cast<uint64_t>(length) + groupSize - 1u) / groupSize) * groupSize;
    return static_cast<uint32_t>(std::min<uint64_t>(
        rounded, surfaceLength - origin));
}

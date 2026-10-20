#pragma once

#include <algorithm>
#include <cstdint>

// Reject the spare lanes in an 8x8 compute workgroup without extending the
// 128-byte push constants or limiting draw extents. Origins remain unchanged.
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
    // Bits 1..6 are unused by the blur shader. Its cap colour occupies data.x.
    return (flags & ~126u) | (renderer2d_dispatch_tail(width, height) << 1u);
}

// Shader-visible extent excludes spare invocations in the last workgroup.
inline uint32_t renderer2d_dispatch_axis_extent(
    uint32_t origin, uint32_t length, uint32_t surfaceLength)
{
    return origin < surfaceLength ? std::min(length, surfaceLength - origin) : 0u;
}

// Compute workgroups are relative to the dispatch origin, which is not
// necessarily aligned to the screen's 8-pixel grid. Retained clears and damage
// tracking must include trailing invocations, even when they extend past the
// requested rectangle (for example at a clipped scrollbar edge).
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

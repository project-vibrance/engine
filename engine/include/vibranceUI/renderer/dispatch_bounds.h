#pragma once

#include <algorithm>
#include <cstdint>

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

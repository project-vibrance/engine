#pragma once

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>

namespace vibrance::renderer_diagnostics
{
    inline bool supports_timestamp_clock(uint32_t validBits, double periodNanoseconds)
    {
        return validBits > 0u && validBits <= 64u &&
            std::isfinite(periodNanoseconds) && periodNanoseconds > 0.0;
    }

    // The clock must be supported and the interval must span less than one
    // complete counter period; additional wraps cannot be inferred from two values.
    inline double timestamp_elapsed_milliseconds(
        uint64_t begin,
        uint64_t end,
        uint32_t validBits,
        double periodNanoseconds)
    {
        assert(supports_timestamp_clock(validBits, periodNanoseconds));
        const uint64_t mask = validBits == 64u ?
            std::numeric_limits<uint64_t>::max() :
            (uint64_t { 1u } << validBits) - 1u;
        const uint64_t elapsedTicks = (end - begin) & mask;
        return static_cast<double>(elapsedTicks) * periodNanoseconds / 1000000.0;
    }
}

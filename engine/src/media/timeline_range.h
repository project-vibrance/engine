#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace vibrance::media
{
    struct TimelineRange
    {
        std::int64_t origin = 0;
        std::int64_t position = 0;
        std::int64_t duration = 0;
        bool hasItemEnd = false;
    };

    // EndTime is the full item boundary, while MaxSeekTime may only describe
    // a buffered range. Use the latter only when no item end is available.
    inline TimelineRange timeline_range(std::int64_t start, std::int64_t end,
        std::int64_t maximumSeek, std::int64_t position,
        double elapsedMilliseconds = 0.0, double playbackRate = 1.0,
        std::int64_t knownDuration = 0)
    {
        constexpr auto limit = std::numeric_limits<std::int64_t>::max() / 10'000;
        const auto validEnd = [start](std::int64_t value) {
            return value > start && value < limit;
        };
        TimelineRange result;
        result.origin = std::clamp(start, -limit, limit);
        result.hasItemEnd = validEnd(end);
        const auto last = result.hasItemEnd ? end : validEnd(maximumSeek) ? maximumSeek : result.origin;
        result.duration = std::max(last - result.origin, std::int64_t(0));
        if (!result.hasItemEnd) result.duration = std::max(result.duration, std::clamp(knownDuration, std::int64_t(0), limit));
        const double elapsed = std::isfinite(elapsedMilliseconds) ? std::max(elapsedMilliseconds, 0.0) : 0.0;
        const double rate = std::isfinite(playbackRate) && playbackRate >= 0.0 ? playbackRate : 1.0;
        const double relative = static_cast<double>(position) - result.origin + elapsed * rate;
        result.position = static_cast<std::int64_t>(std::clamp(relative, 0.0,
            result.duration > 0 ? static_cast<double>(result.duration) : static_cast<double>(limit)));
        return result;
    }
}

#include "../src/media/timeline_range.h"
#include <iostream>

int main()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    };
    using vibrance::media::timeline_range;
    auto range = timeline_range(0, 240'000, 60'000, 20'000);
    expect(range.duration == 240'000,
        "The full media end must take precedence over a partially buffered seek range");
    range = timeline_range(0, 0, 60'000, 90'000, 0, 1, 240'000);
    expect(range.duration == 240'000 && range.position == 90'000,
        "A temporarily absent item end must not let a buffered range truncate a known full duration or position");
    range = timeline_range(0, 0, 240'000, 20'000);
    expect(range.duration == 240'000 && range.position == 20'000,
        "A browser exposing only MaxSeekTime must still provide a usable timeline");
    range = timeline_range(10'000, 250'000, 80'000, 30'000);
    expect(range.origin == 10'000 && range.duration == 240'000 && range.position == 20'000,
        "Non-zero provider timestamps must normalise to zero through the full item duration");
    range = timeline_range(0, 100'000, 100'000, 20'000, 3000, 2);
    expect(range.position == 26'000, "Timestamp age and playback speed must recover the current position");
    expect(timeline_range(0, 100'000, 100'000, 99'000, 5000).position == 100'000 &&
        timeline_range(10'000, 100'000, 0, 5000).position == 0,
        "Timeline positions must remain between zero and the final duration");
    expect(timeline_range(0, 0, 0, 0).duration == 0 &&
        timeline_range(0, 0, std::numeric_limits<std::int64_t>::max() / 10'000, 0).duration == 0,
        "Unavailable and unbounded live timelines must not manufacture a finite duration");
    return failures ? 1 : 0;
}

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "../src/media/playback_seek_policy.h"
#include <array>
#include <iostream>
#include <limits>

int main()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    };
    using vibrance::media::PlaybackSlider;
    using vibrance::media::playback_slider_index;
    using vibrance::media::playback_slider_matches;
    using vibrance::media::playback_slider_value;
    using vibrance::media::PlaybackSeekIdentity;
    const PlaybackSeekIdentity item { L"player", L"Track", L"Artist", L"Album" };
    expect(item == PlaybackSeekIdentity { L"player", L"Track", L"Artist", L"Album" },
        "An unchanged published media item must retain its queued seek");
    expect(item != PlaybackSeekIdentity { L"another-player", L"Track", L"Artist", L"Album" } &&
        item != PlaybackSeekIdentity { L"player", L"Another track", L"Artist", L"Album" } &&
        item != PlaybackSeekIdentity { L"player", L"Track", L"Another artist", L"Album" } &&
        item != PlaybackSeekIdentity { L"player", L"Track", L"Artist", L"Another album" },
        "A queued seek must not transfer to another app, track, artist or album");
    const std::wstring longTitle = std::wstring(512u, L'\u65e5') + L"\U0001f3b5";
    const std::wstring anotherLongTitle = std::wstring(512u, L'\u65e5') + L"\U0001f3b6";
    expect(PlaybackSeekIdentity { L"player", longTitle, L"\u97f3\u697d", L"Album" } !=
        PlaybackSeekIdentity { L"player", anotherLongTitle, L"\u97f3\u697d", L"Album" },
        "Full Unicode item identity must distinguish long titles beyond the snapshot text buffers");
    const PlaybackSlider scrubber { L"", L"Scrubber", 0.0, 138.0, true, true, false };
    const PlaybackSlider volume { L"Volume", L"volume", 0.0, 100.0, true, true, false };
    expect(playback_slider_matches(scrubber, 138'000),
        "An unlabelled writable scrubber must be recognised by its accessible identity and track duration");
    expect(playback_slider_value(scrubber, 69'000, 138'000) == 69.0,
        "A seconds-based playback control must receive the requested time");
    PlaybackSlider percent { L"Playback position", L"", 0.0, 100.0, true, true, false };
    expect(playback_slider_matches(percent, 138'000) &&
        playback_slider_value(percent, 69'000, 138'000) == 50.0,
        "A percentage playback control must receive the normalised target");
    PlaybackSlider milliseconds { L"Seek", L"", 10'000.0, 148'000.0, true, true, false };
    expect(playback_slider_matches(milliseconds, 138'000) &&
        playback_slider_value(milliseconds, 69'000, 138'000) == 79'000.0,
        "A millisecond playback control with a non-zero origin must map the full range");
    expect(!playback_slider_matches(volume, 100'000),
        "A volume control must not be selected even when its range matches the track length");
    PlaybackSlider wrongDuration = scrubber;
    wrongDuration.maximum = 240.0;
    expect(!playback_slider_matches(wrongDuration, 138'000),
        "An unrelated or stale scrubber range must not receive a seek");
    for (unsigned state = 0u; state < 3u; ++state)
    {
        PlaybackSlider unavailable = scrubber;
        if (state == 0u) unavailable.enabled = false;
        if (state == 1u) unavailable.visible = false;
        if (state == 2u) unavailable.readOnly = true;
        expect(!playback_slider_matches(unavailable, 138'000),
            "Disabled, hidden and read-only controls must never receive a seek");
    }
    const std::array candidates { volume, scrubber };
    expect(playback_slider_index(candidates, 138'000) == 1u,
        "The playback scrubber must be selected without changing the player's volume");
    const std::array ambiguous { scrubber, scrubber };
    expect(!playback_slider_index(ambiguous, 138'000),
        "Ambiguous playback controls must fail closed");
    PlaybackSlider invalid = scrubber;
    invalid.maximum = std::numeric_limits<double>::infinity();
    expect(!playback_slider_matches(invalid, 138'000) &&
        !playback_slider_matches(scrubber, 0),
        "Unbounded playback ranges and live streams must fail closed");
    expect(playback_slider_value(scrubber, -1000, 138'000) == 0.0 &&
        playback_slider_value(scrubber, 200'000, 138'000) == 138.0,
        "Seek targets must remain inside the selected control's range");
    return failures ? 1 : 0;
}

#include "../src/media/playback_quality_detection.h"
#include <vibranceUI/media/session.h>
#include <iostream>
#include <vector>

int main()
{
    using namespace media_quality_detection;
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    };
    std::vector<Node> player {
        { -1, Role::eWindow, true, L"Any music player" },
        { 0, Role::eContainer, true, L"Playback" },
        { 1, Role::eText, true, L"Current track" },
        { 1, Role::eText, true, L"Current artist" },
        { 1, Role::eButton, true, L"Pause" },
        { 1, Role::eButton, true, L"Lossless" }
    };
    const auto detectCurrent = [&] { return detect(player, L"Current track", L"Current artist"); };
    expect(detectCurrent() == MediaPlaybackQuality::eLossless,
        "A matching playback badge must identify lossless for any player");
    player.push_back({ 1, Role::eText, true, L"Dolby Atmos" });
    expect(detectCurrent() == MediaPlaybackQuality::eDolbyAtmos,
        "Dolby Atmos must take precedence when both badges are present");
    expect(detect(player, L"Next track", L"Current artist") == MediaPlaybackQuality::eUnknown &&
        detect(player, L"Current track", L"Another artist") == MediaPlaybackQuality::eUnknown,
        "Track changes must reject old playback evidence immediately");
    expect(detect(player, L"", L"") == MediaPlaybackQuality::eUnknown,
        "Missing metadata must not accept a capability badge");
    for (const auto missing : { L"", L" ", L"\t\r\n " })
        expect(detect(player, missing, L"Current artist") == MediaPlaybackQuality::eUnknown &&
            detect(player, L"Current track", missing) == MediaPlaybackQuality::eUnknown,
            "Either empty normalised identity field must reject playback quality evidence");
    auto namedTrack = player;
    namedTrack[2].name = L"Dolby Atmos";
    expect(detect(namedTrack, L"Dolby Atmos", L"Current artist") == MediaPlaybackQuality::eLossless,
        "A repeated title named Dolby Atmos must not override a real Lossless badge");
    expect(detect(player, L"Current track", L"Current artist", L"Dolby Atmos") == MediaPlaybackQuality::eLossless,
        "An album name must not be interpreted as a quality badge");
    player.back().visible = false;
    expect(detectCurrent() == MediaPlaybackQuality::eLossless,
        "Hidden Atmos controls must not override visible lossless playback");
    player[5].name = L"24-bit / 192 kHz";
    expect(detectCurrent() == MediaPlaybackQuality::eUnknown,
        "High sample rates or PCM bit depths are not lossless evidence");
    for (const auto label : { L"Dolby Atmos supported", L"Enable lossless",
        L"Lossless off", L"Dolby Atmos unavailable", L"Lossless downloads" })
    {
        player[5].name = label;
        expect(detectCurrent() == MediaPlaybackQuality::eUnknown,
            "Preferences and capability descriptions must not be playback badges");
    }
    player[5].name = L" Hi-Res Lossless ";
    expect(detectCurrent() == MediaPlaybackQuality::eLossless,
        "Hi-res lossless playback badges should share the Lossless presentation");
    player[1].role = Role::eExcluded;
    expect(detectCurrent() == MediaPlaybackQuality::eUnknown,
        "Catalogue rows and quality menus must not qualify as current playback");
    player[1].role = Role::eContainer;
    player[4].name = L"Settings";
    expect(detectCurrent() == MediaPlaybackQuality::eUnknown,
        "A playback container must also contain a transport control");
    player[4].automationId = L"PlayPauseButton";
    expect(detectCurrent() == MediaPlaybackQuality::eLossless,
        "A localised transport control can be recognised by its automation ID");
    player[5].parent = 0;
    expect(detectCurrent() == MediaPlaybackQuality::eUnknown,
        "Unrelated window-wide lossless labels must not associate with the current item");
    player[5].parent = 1;
    player[1].visible = false;
    expect(detectCurrent() == MediaPlaybackQuality::eUnknown,
        "Offscreen playback containers must fail closed");
    player[1].visible = true;
    player[1].parent = 1;
    expect(!belongs_to(player, 5, 0), "Malformed cyclic trees must terminate safely");
    player[1].parent = 0;
    for (int i = 0; i < 100; ++i) player.push_back({ 1, Role::eText, true, L"Other content" });
    expect(detectCurrent() == MediaPlaybackQuality::eUnknown,
        "Broad page containers must not be accepted as playback evidence");
    expect(MediaSessionSnapshot {}.playbackQuality == MediaPlaybackQuality::eUnknown &&
        media_playback_quality_label(MediaPlaybackQuality::eUnknown).empty(),
        "Unsupported providers must default to no badge");
    expect(media_playback_quality_label(media_playback_quality(true, true)) == "Dolby Atmos" &&
        media_playback_quality_label(media_playback_quality(false, true)) == "Lossless",
        "Badge labels must match the resolved playback quality");
    return failures ? 1 : 0;
}

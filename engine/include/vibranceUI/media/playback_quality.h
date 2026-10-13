#pragma once

#include <cstdint>
#include <string_view>

// Describes the source player's current item, never the PCM capture format or
// the output device's spatial-audio configuration. Unknown has no UI badge.
enum class MediaPlaybackQuality : std::uint8_t
{
    eUnknown,
    eLossless,
    eDolbyAtmos
};

constexpr MediaPlaybackQuality media_playback_quality(bool dolbyAtmos, bool lossless)
{
    return dolbyAtmos ? MediaPlaybackQuality::eDolbyAtmos :
        lossless ? MediaPlaybackQuality::eLossless : MediaPlaybackQuality::eUnknown;
}

constexpr std::string_view media_playback_quality_label(MediaPlaybackQuality quality)
{
    switch (quality)
    {
        case MediaPlaybackQuality::eDolbyAtmos: return "Dolby Atmos";
        case MediaPlaybackQuality::eLossless: return "Lossless";
        default: return {};
    }
}

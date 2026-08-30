#pragma once

#include <vibranceUI/export.h>

#include <cstdint>
#include <memory>
#include <string>

enum class MediaSessionPlaybackStatus : std::uint8_t
{
    eUnknown,
    eClosed,
    eOpened,
    eChanging,
    eStopped,
    ePlaying,
    ePaused
};

enum class MediaSessionCommand : std::uint8_t
{
    eTogglePlayPause,
    eSkipNext,
    eSkipPrevious
};

// Platform-neutral provider snapshot. Engine and application views can share
// this value without importing Win32, COM, WinRT, or a bridge ABI.
struct MediaSessionSnapshot
{
    bool bridgeLoaded = false;
    bool serviceReady = false;
    bool sessionAvailable = false;
    bool canTogglePlayPause = false;
    bool canSkipNext = false;
    bool canSkipPrevious = false;
    std::uint64_t revision = 0u;
    std::uint64_t thumbnailRevision = 0u;
    std::int64_t positionMilliseconds = 0;
    std::int64_t durationMilliseconds = 0;
    MediaSessionPlaybackStatus playbackStatus =
        MediaSessionPlaybackStatus::eUnknown;
    std::uint32_t sourceProcessId = 0u;
    std::string sourceApp;
    std::string title;
    std::string artist;
    std::string album;
    std::string thumbnailPath;
    std::string diagnostic;
};

// Reusable portable facade for the optional platform media-session provider.
// Unsupported platforms and missing runtime companions fail closed while
// preserving the same snapshot and command API.
class VIBRANCE_ENGINE_API GlobalMediaSession
{
public:
    GlobalMediaSession();
    ~GlobalMediaSession();

    GlobalMediaSession(const GlobalMediaSession&) = delete;
    GlobalMediaSession& operator=(const GlobalMediaSession&) = delete;

    GlobalMediaSession(GlobalMediaSession&&) noexcept;
    GlobalMediaSession& operator=(GlobalMediaSession&&) noexcept;

    static bool platform_supported();
    bool bridge_loaded() const;
    bool refresh();
    bool send(MediaSessionCommand command);
    bool seek(std::int64_t positionMilliseconds);
    const MediaSessionSnapshot& snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

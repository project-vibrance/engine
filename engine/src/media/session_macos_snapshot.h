#pragma once

#import <Foundation/Foundation.h>
#include <vibranceUI/media/session.h>
#include <algorithm>
#include <cmath>
#include <limits>

// Adapter JSON is an external boundary: absent/null/malformed fields must not
// turn into Objective-C selector errors or overflowing timeline values.
inline MediaSessionSnapshot mac_media_snapshot(id payload)
{
    MediaSessionSnapshot snapshot;
    snapshot.bridgeLoaded = true;
    snapshot.serviceReady = payload == NSNull.null || [payload isKindOfClass:NSDictionary.class];
    if (!snapshot.serviceReady)
    {
        snapshot.diagnostic = "macOS Now Playing returned invalid data or timed out.";
        return snapshot;
    }
    if (payload == NSNull.null) return snapshot;
    NSDictionary* info = payload;
    const auto text = [&](NSString* key) -> std::string {
        id value = info[key];
        return [value isKindOfClass:NSString.class] ? std::string([value UTF8String]) : std::string();
    };
    const auto number = [&](NSString* key) -> double {
        id value = info[key];
        const double result = [value isKindOfClass:NSNumber.class] ? [value doubleValue] : 0;
        return std::isfinite(result) ? result : 0;
    };
    const auto milliseconds = [&](NSString* key) -> std::int64_t {
        // Keep a safe bound for subsequent conversion back to microseconds.
        return static_cast<std::int64_t>(std::clamp(number(key) / 1000.0,
            0.0, 1.0e12));
    };
    snapshot.sourceApp = text(@"bundleIdentifier");
    if (snapshot.sourceApp.empty()) snapshot.sourceApp = text(@"parentApplicationBundleIdentifier");
    snapshot.sourceProcessId = static_cast<std::uint32_t>(std::clamp(number(@"processIdentifier"),
        0.0, static_cast<double>(std::numeric_limits<std::int32_t>::max())));
    snapshot.title = text(@"title");
    snapshot.artist = text(@"artist");
    snapshot.album = text(@"album");
    snapshot.sessionAvailable = snapshot.sourceProcessId != 0 || !snapshot.sourceApp.empty() || !snapshot.title.empty();
    if (!snapshot.sessionAvailable) return snapshot;
    snapshot.durationMilliseconds = milliseconds(@"durationMicros");
    snapshot.positionMilliseconds = milliseconds(info[@"elapsedTimeNowMicros"] ? @"elapsedTimeNowMicros" : @"elapsedTimeMicros");
    if (snapshot.durationMilliseconds > 0)
        snapshot.positionMilliseconds = std::min(snapshot.positionMilliseconds, snapshot.durationMilliseconds);
    snapshot.playbackStatus = number(@"playing") != 0 ? MediaSessionPlaybackStatus::ePlaying : MediaSessionPlaybackStatus::ePaused;
    snapshot.canTogglePlayPause = true;
    snapshot.canSkipNext = snapshot.canSkipPrevious = number(@"prohibitsSkip") == 0;
    return snapshot;
}

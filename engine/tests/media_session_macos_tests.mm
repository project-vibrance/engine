#import "../src/media/session_macos_snapshot.h"
#import "../src/media/session_macos_artwork.h"
#include <iostream>

int main()
{
    @autoreleasepool
    {
        int failures = 0;
        const auto expect = [&](bool value, const char* message) {
            if (!value) { std::cerr << message << '\n'; ++failures; }
        };
        auto empty = mac_media_snapshot(NSNull.null);
        expect(empty.bridgeLoaded && empty.serviceReady && !empty.sessionAvailable,
            "No player should clear the session while keeping the service ready");
        auto invalid = mac_media_snapshot(@"invalid");
        expect(!invalid.serviceReady && !invalid.canTogglePlayPause && !invalid.diagnostic.empty(),
            "Invalid data must fail closed with a diagnostic");
        auto playing = mac_media_snapshot(@{
            @"bundleIdentifier": @"com.example.player", @"processIdentifier": @123,
            @"title": @"Björk 🎵", @"artist": @"Artist", @"album": @"Album",
            @"playing": @YES, @"durationMicros": @10000000,
            @"elapsedTimeMicros": @1000000, @"elapsedTimeNowMicros": @2500000});
        expect(playing.sessionAvailable && playing.sourceProcessId == 123 &&
            playing.title == "Björk 🎵" && playing.playbackStatus == MediaSessionPlaybackStatus::ePlaying,
            "Playing metadata and UTF-8 should survive conversion");
        expect(playing.positionMilliseconds == 2500 && playing.durationMilliseconds == 10000,
            "The current elapsed estimate takes precedence and converts microseconds");
        auto paused = mac_media_snapshot(@{
            @"title": @"Paused", @"playing": @NO, @"prohibitsSkip": @YES,
            @"durationMicros": @1000000, @"elapsedTimeMicros": @2000000});
        expect(paused.playbackStatus == MediaSessionPlaybackStatus::ePaused &&
            paused.canTogglePlayPause && !paused.canSkipNext && !paused.canSkipPrevious &&
            paused.positionMilliseconds == 1000, "Paused state, skip restrictions and timeline bounds");
        auto malformed = mac_media_snapshot(@{
            @"title": NSNull.null, @"artist": @42, @"bundleIdentifier": @"test",
            @"durationMicros": @(-5), @"elapsedTimeMicros": @(NAN),
            @"processIdentifier": @(-100), @"playing": @"yes"});
        expect(malformed.title.empty() && malformed.artist.empty() &&
            malformed.durationMilliseconds == 0 && malformed.positionMilliseconds == 0 &&
            malformed.sourceProcessId == 0, "Malformed optional fields should be sanitised");
        NSString* cacheDirectory = [NSTemporaryDirectory() stringByAppendingPathComponent:NSUUID.UUID.UUIDString];
        [NSFileManager.defaultManager createDirectoryAtPath:cacheDirectory withIntermediateDirectories:YES attributes:nil error:nil];
        MacMediaArtworkCache cache;
        auto track = playing;
        cache.update(track, @{@"uniqueIdentifier": @"one", @"artworkData": @"aW1hZ2U="}, cacheDirectory);
        const auto originalPath = track.thumbnailPath;
        const auto originalRevision = track.thumbnailRevision;
        track.artist = "Updated metadata";
        cache.update(track, @{@"uniqueIdentifier": @"one"}, cacheDirectory);
        expect(!originalPath.empty() && track.thumbnailPath == originalPath && track.thumbnailRevision == originalRevision,
            "A stable item ID must preserve artwork when optional metadata changes");
        cache.update(track, @{@"artworkData": @"not base64"}, cacheDirectory);
        expect(track.thumbnailPath == originalPath, "Missing item IDs and malformed artwork must preserve the cached cover");
        cache.update(track, @{@"uniqueIdentifier": @"one", @"artworkData": @"bmV3"}, nil);
        expect(track.thumbnailPath == originalPath, "A failed artwork write must leave the known cover available");
        cache.update(track, @{@"uniqueIdentifier": @"two"}, cacheDirectory);
        expect(track.thumbnailPath == originalPath && track.thumbnailRevision == originalRevision,
            "Spotify transport ID changes with identical metadata must preserve the displayed cover");
        for (int index = 0; index < 24; ++index)
        {
            track.title = std::to_string(index);
            cache.update(track, @{@"artworkData": @"aW1hZ2U="}, cacheDirectory);
        }
        expect(cache.entries.size() == 8 && cache.retired.size() <= 8 &&
            [NSFileManager.defaultManager contentsOfDirectoryAtPath:cacheDirectory error:nil].count <= 16,
            "Track caching and asynchronous-load history must bound both memory and temporary files");
        [NSFileManager.defaultManager removeItemAtPath:cacheDirectory error:nil];
        return failures ? 1 : 0;
    }
}

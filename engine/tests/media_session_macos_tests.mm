#import "../src/media/session_macos_snapshot.h"
#import "../src/media/session_macos_artwork.h"
#import "../src/media/playback_quality_macos.h"
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
        expect(mac_media_quality::wide(@"Björk 🎵") == L"Björk 🎵",
            "Accessibility matching must preserve non-ASCII names and surrogate pairs");
        expect(mac_media_quality::role(@"AXStaticText") == media_quality_detection::Role::eText &&
            mac_media_quality::role(@"AXButton") == media_quality_detection::Role::eButton &&
            mac_media_quality::role(@"AXTable") == media_quality_detection::Role::eExcluded &&
            mac_media_quality::role(@"AXWindow") == media_quality_detection::Role::eWindow,
            "Native roles must exclude catalogue tables and window-wide quality matching");
        MacPlaybackQualityProbe qualityProbe;
        expect(qualityProbe.refresh(empty) == MediaPlaybackQuality::eUnknown,
            "An absent media session must not acquire a playback quality badge");
        using namespace media_quality_detection;
        const auto nativeAttributes = [](NSString* nativeRole, id value, id description) {
            return @[nativeRole, NSNull.null, NSNull.null, @"", value, description,
                @"", NSNull.null];
        };
        std::vector<Node> playback {
            {-1, Role::eWindow, true, L"Player"},
            {0, Role::eContainer, true, L"Playback"}
        };
        for (NSArray* attributes in @[
            nativeAttributes(@"AXStaticText", @"Current track", @"Title"),
            nativeAttributes(@"AXStaticText", @"Current artist", @"Artist"),
            nativeAttributes(@"AXButton", @0, @"Pause"),
            nativeAttributes(@"AXButton", @0, @"Lossless")])
        {
            Node node;
            expect(mac_media_quality::read_node(attributes, 1, node),
                "Optional unavailable attributes must not discard native playback controls");
            playback.push_back(std::move(node));
        }
        expect(detect(playback, L"Current track", L"Current artist") == MediaPlaybackQuality::eLossless,
            "Native text values and button descriptions must identify current lossless playback");
        Node atmos;
        expect(mac_media_quality::read_node(nativeAttributes(@"AXImage", @0, @"Dolby Atmos"), 1, atmos),
            "Native quality images must expose their spoken descriptions");
        playback.push_back(atmos);
        expect(detect(playback, L"Current track", L"Current artist") == MediaPlaybackQuality::eDolbyAtmos,
            "Native Atmos evidence must take precedence over lossless");
        expect(detect(playback, L"Different track", L"Current artist") == MediaPlaybackQuality::eUnknown,
            "Native playback evidence must match the current track");
        std::vector<Node> music {
            {-1, Role::eWindow, true, L"Music"},
            {0, Role::eContainer, true, L"Mini Player"},
            {1, Role::eButton, true, L"Pause", L"Music.miniPlayer.playbackTransportControl"},
            {1, Role::eContainer, true, L"", L"Music.miniPlayer.metadataRegion[state=populated]"},
            {3, Role::eButton, true, L"Current track", L"Music.miniPlayer.title"},
            {3, Role::eButton, true, L"Current artist — Current album", L"Music.miniPlayer.subtitle"},
            {3, Role::eText, true, L"Dolby Atmos", L"Music.miniPlayer.audioBadge"}
        };
        const auto musicQuality = [&](std::wstring_view track, std::wstring_view artist, std::wstring_view album) {
            auto nodes = music;
            return mac_media_quality::detect(nodes, track, artist, album);
        };
        expect(musicQuality(L"Current track", L"Current artist", L"Current album") == MediaPlaybackQuality::eDolbyAtmos,
            "Music's combined artist/album subtitle must identify the active Atmos track");
        expect(musicQuality(L"Current track", L"Current", L"Current album") == MediaPlaybackQuality::eUnknown &&
            musicQuality(L"Current track", L"Current artist", L"Other album") == MediaPlaybackQuality::eUnknown &&
            musicQuality(L"Other track", L"Current artist", L"Current album") == MediaPlaybackQuality::eUnknown &&
            musicQuality(L"Current track", L"Current artist", L"") == MediaPlaybackQuality::eUnknown,
            "Combined subtitles must match the full current artist and album with the current title");
        music.back().name = L"Hi-Res Lossless";
        expect(musicQuality(L"Current track", L"Current artist", L"Current album") == MediaPlaybackQuality::eLossless,
            "Music's combined subtitle must also identify lossless playback");
        music[1].role = Role::eExcluded;
        expect(musicQuality(L"Current track", L"Current artist", L"Current album") == MediaPlaybackQuality::eUnknown,
            "Matching combined metadata must never admit catalogue rows or menus");
        std::vector<Node> genericPlayer {
            {-1, Role::eWindow, true, L"Any player"},
            {0, Role::eContainer, true, L"Playback"},
            {1, Role::eButton, true, L"Current track"},
            {1, Role::eContainer, true, L""},
            {1, Role::eText, true, L"Lossless"},
            {1, Role::eButton, true, L"Pause"},
            {1, Role::eButton, true, L"Previous track"},
            {1, Role::eButton, true, L"Next track"},
            {1, Role::eSlider, true, L"Track position"}
        };
        const auto genericQuality = [&](std::wstring_view title = L"Current track") {
            auto nodes = genericPlayer;
            return mac_media_quality::detect(nodes, title, L"Current artist", L"Current album");
        };
        expect(genericQuality() == MediaPlaybackQuality::eLossless,
            "An unlabelled subtitle in any complete playback region must identify explicit lossless playback without player identifiers");
        expect(detect(genericPlayer, L"Current track", L"Current artist") == MediaPlaybackQuality::eUnknown,
            "The default shared matcher must retain the Windows title-and-artist policy");
        genericPlayer[4].name = L"Dolby Atmos";
        expect(genericQuality() == MediaPlaybackQuality::eDolbyAtmos,
            "Generic unlabelled metadata must also identify an explicit Atmos playback badge");
        expect(genericQuality(L"Other track") == MediaPlaybackQuality::eUnknown,
            "A generic playback region must match the current source title exactly");
        genericPlayer[8].name = L"Volume";
        expect(genericQuality() == MediaPlaybackQuality::eUnknown,
            "A volume slider cannot substitute for a track-position control");
        genericPlayer[8].name = L"Track position";
        genericPlayer[7].visible = false;
        expect(genericQuality() == MediaPlaybackQuality::eUnknown,
            "A missing or hidden skip control must reject an incomplete playback region");
        genericPlayer[7].visible = true;
        genericPlayer[1].role = Role::eExcluded;
        expect(genericQuality() == MediaPlaybackQuality::eUnknown,
            "A complete catalogue or preferences container must still be excluded");
        genericPlayer[1].role = Role::eContainer;
        genericPlayer[4].parent = 0;
        expect(genericQuality() == MediaPlaybackQuality::eUnknown,
            "A window-wide quality label must not associate with an otherwise complete playback region");
        expect(mac_media_quality::role(@"AXSlider") == Role::eSlider,
            "Native sliders must retain their role for generic playback-region matching");
        Node explicitTrack;
        expect(mac_media_quality::read_node(nativeAttributes(@"AXButton", @"Explicit", @"Current track"), 1, explicitTrack) &&
            explicitTrack.name == L"Current track",
            "Button descriptions must take precedence over supplementary native values such as Explicit");
        std::vector<Node> spotify {
            {-1, Role::eWindow, true, L"Spotify Premium"},
            {0, Role::eContainer, true, L"Now playing bar"},
            {1, Role::eContainer, true, L"Now playing: Current track by First artist, Second artist"},
            {2, Role::eText, true, L"Current track"},
            {2, Role::eText, true, L"First artist"},
            {2, Role::eText, true, L", "},
            {2, Role::eText, true, L"Second artist"},
            {2, Role::eButton, true, L"Lossless"},
            {1, Role::eContainer, true, L"Player controls"},
            {8, Role::eButton, true, L"Play"}
        };
        const auto spotifyQuality = [&](std::wstring_view track, std::wstring_view artist) {
            auto nodes = spotify;
            return mac_media_quality::detect(nodes, track, artist, L"");
        };
        expect(spotifyQuality(L"Current track", L"First artist, Second artist") == MediaPlaybackQuality::eLossless,
            "Spotify's complete Now Playing label must associate split artist links with the lossless badge");
        expect(spotifyQuality(L"Current track", L"First artist") == MediaPlaybackQuality::eLossless,
            "Spotify's primary-artist-only MediaRemote metadata must match a complete artist link in the playback group");
        expect(spotifyQuality(L"Current track", L"First") == MediaPlaybackQuality::eUnknown &&
            spotifyQuality(L"Current track", L"Unrelated artist") == MediaPlaybackQuality::eUnknown &&
            spotifyQuality(L"Other track", L"First artist, Second artist") == MediaPlaybackQuality::eUnknown,
            "Spotify's playback identity must match the entire current title and a complete artist label");
        spotify.back().name = L"Settings";
        expect(spotifyQuality(L"Current track", L"First artist, Second artist") == MediaPlaybackQuality::eUnknown,
            "A combined playback identity must still require a neighbouring transport control");
        NSMutableArray* hidden = [nativeAttributes(@"AXImage", @0, @"Dolby Atmos") mutableCopy];
        hidden[1] = @YES;
        expect(mac_media_quality::read_node(hidden, 1, playback.back()) &&
            detect(playback, L"Current track", L"Current artist") == MediaPlaybackQuality::eLossless,
            "A hidden native Atmos badge must not override lossless");
        NSMutableArray* minimised = [nativeAttributes(@"AXWindow", NSNull.null, NSNull.null) mutableCopy];
        minimised[2] = @YES;
        expect(mac_media_quality::read_node(minimised, -1, playback[0]) &&
            !playback[0].visible,
            "Native minimisation must mark the window invisible so traversal skips its children");
        for (const AXError error : { kAXErrorAttributeUnsupported, kAXErrorNoValue,
            kAXErrorCannotComplete, kAXErrorInvalidUIElement })
        {
            AXError nativeError = error;
            id value = CFBridgingRelease(AXValueCreate(static_cast<AXValueType>(kAXValueAXErrorType), &nativeError));
            NSMutableArray* attributes = [nativeAttributes(@"AXButton", @0, @"Lossless") mutableCopy];
            attributes[1] = value;
            Node node;
            expect(mac_media_quality::read_node(attributes, 1, node) ==
                (error == kAXErrorAttributeUnsupported || error == kAXErrorNoValue),
                "Unsupported optional attributes are allowed; failed native reads must reject evidence");
        }
        Node malformedNode;
        expect(!mac_media_quality::read_node(@[], 1, malformedNode) &&
            !mac_media_quality::read_node(nativeAttributes((id)NSNull.null, @0, @"Lossless"), 1, malformedNode),
            "Malformed native snapshots must fail closed");
        MediaSessionSnapshot missingSource;
        missingSource.sourceApp = "com.vibrance.nonexistent-quality-fixture";
        expect(mac_media_quality::ui_process(missingSource) == 0,
            "A missing application must not select unrelated UI for quality detection");
        missingSource.sourceProcessId = std::numeric_limits<uint32_t>::max();
        expect(mac_media_quality::ui_process(missingSource) == 0,
            "An invalid capture PID must not resolve through a bundle fallback");
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

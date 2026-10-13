#import <Foundation/Foundation.h>
#include "../src/media/session_macos.h"
#include <chrono>
#include <iostream>
#include <thread>

int main(int argc, char** argv)
{
    @autoreleasepool
    {
        NSString* directory = [[NSString stringWithUTF8String:argv[0]] stringByDeletingLastPathComponent];
        NSString* framework = [directory stringByAppendingPathComponent:@"VibranceMediaRemoteAdapter.framework"];
        NSFileManager* files = NSFileManager.defaultManager;
        [files createDirectoryAtPath:framework withIntermediateDirectories:YES attributes:nil error:nil];
        [@"fixture" writeToFile:[framework stringByAppendingPathComponent:@"VibranceMediaRemoteAdapter"]
            atomically:YES encoding:NSUTF8StringEncoding error:nil];
        NSString* script = @"use strict; use warnings; my ($dir,$cmd,$value)=@ARGV; "
            "if ($cmd eq 'get') { open(my $in, '<', \"$dir/response\") or die; local $/; "
            "my $body=<$in>; if ($body eq 'hang') {sleep 30;} print $body; } "
            "else { open(my $log, '>>', \"$dir/commands\") or die; print $log \"$cmd $value\\n\"; }";
        [script writeToFile:[directory stringByAppendingPathComponent:@"mediaremote-adapter.pl"]
            atomically:YES encoding:NSUTF8StringEncoding error:nil];
        NSString* response = [framework stringByAppendingPathComponent:@"response"];
        NSString* logPath = [framework stringByAppendingPathComponent:@"commands"];
        [files removeItemAtPath:logPath error:nil];
        const auto write = [&](NSString* json) {
            [json writeToFile:response atomically:YES encoding:NSUTF8StringEncoding error:nil];
        };
        int failures = 0;
        const auto expect = [&](bool value, const char* message) {
            if (!value) { std::cerr << message << '\n'; ++failures; }
        };
        const auto wait = [&](MacMediaSession& provider, auto predicate) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            MediaSessionSnapshot snapshot;
            do {
                provider.refresh(snapshot);
                if (predicate(snapshot)) return true;
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            } while (std::chrono::steady_clock::now() < deadline);
            return false;
        };
        std::string artworkPath;
        write(@"{\"title\":\"Fixture\",\"playing\":true,\"uniqueIdentifier\":\"track-1\",\"durationMicros\":10000000,\"artworkData\":\"aW1hZ2U=\"}");
        {
            MacMediaSession provider;
            expect(wait(provider, [](const auto& s) {return s.sessionAvailable && !s.thumbnailPath.empty();}),
                "Background polling should publish metadata and artwork");
            MediaSessionSnapshot snapshot;
            provider.refresh(snapshot);
            artworkPath = snapshot.thumbnailPath;
            const auto revision = snapshot.revision;
            expect(!provider.refresh(snapshot) && snapshot.revision == revision,
                "Reading unchanged snapshots must not bump revisions");
            expect(provider.send(MediaSessionCommand::eTogglePlayPause) &&
                provider.send(MediaSessionCommand::eSkipNext) &&
                provider.send(MediaSessionCommand::eSkipPrevious) && provider.seek(20000),
                "Valid commands should be queued");
            expect(wait(provider, [&](const auto&) {
                NSString* log = [NSString stringWithContentsOfFile:logPath encoding:NSUTF8StringEncoding error:nil];
                return [log containsString:@"send 2\n"] && [log containsString:@"send 4\n"] &&
                    [log containsString:@"send 5\n"] && [log containsString:@"seek 10000000\n"];
            }), "Transport IDs and clamped microsecond seek should reach the adapter");
            const auto thumbnailRevision = snapshot.thumbnailRevision;
            write(@"{\"title\":\"Fixture\",\"playing\":false,\"uniqueIdentifier\":\"pause-id\",\"durationMicros\":10000000}");
            expect(wait(provider, [](const auto& s) { return s.playbackStatus == MediaSessionPlaybackStatus::ePaused; }),
                "Pause response should be observed even without artwork");
            provider.refresh(snapshot);
            expect(snapshot.thumbnailPath == artworkPath && snapshot.thumbnailRevision == thumbnailRevision,
                "Pausing with a changed transport ID and no artwork must preserve the cached path and revision");
            write(@"{\"title\":\"Fixture\",\"playing\":true,\"durationMicros\":10000000,\"artworkData\":\"aW1hZ2U=\"}");
            expect(wait(provider, [](const auto& s) { return s.playbackStatus == MediaSessionPlaybackStatus::ePlaying; }),
                "Resume should be observed");
            provider.refresh(snapshot);
            expect(snapshot.thumbnailPath == artworkPath && snapshot.thumbnailRevision == thumbnailRevision,
                "Resuming with identical bytes must not rewrite or transition artwork");
            write(@"null");
            std::this_thread::sleep_for(std::chrono::milliseconds(700));
            provider.refresh(snapshot);
            expect(snapshot.sessionAvailable && snapshot.thumbnailPath == artworkPath,
                "Brief item disappearance must not flash the placeholder");
            write(@"{\"title\":\"Different track\",\"playing\":true}");
            expect(wait(provider, [](const auto& s) { return s.title == "Different track" && s.thumbnailPath.empty(); }),
                "A genuinely different track must not inherit the previous cover");
            write(@"{\"title\":\"Fixture\",\"playing\":true}");
            expect(wait(provider, [&](const auto& s) { return s.title == "Fixture" && s.thumbnailPath == artworkPath; }),
                "Revisiting a track without artwork should reuse the track cache");
            write(@"null");
            expect(wait(provider, [](const auto& s) { return s.serviceReady && !s.sessionAvailable && s.thumbnailPath.empty(); }),
                "Session disappearance must clear metadata and artwork");
            expect(!provider.send(MediaSessionCommand::eTogglePlayPause) && !provider.seek(100),
                "Commands without a session must fail closed");
            write(@"malformed");
            expect(wait(provider, [](const auto& s) { return !s.serviceReady && !s.diagnostic.empty(); }),
                "Malformed subprocess output must become a diagnostic");
            write(@"null");
            expect(wait(provider, [](const auto& s) { return s.serviceReady; }), "Polling should recover after failure");
        }
        expect(![files fileExistsAtPath:[NSString stringWithUTF8String:artworkPath.c_str()]],
            "Provider shutdown must remove temporary artwork");
        write(@"hang");
        const auto start = std::chrono::steady_clock::now();
        {
            MacMediaSession provider;
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
        }
        expect(std::chrono::steady_clock::now() - start < std::chrono::seconds(2),
            "Shutdown must cancel a stuck adapter promptly");
        [files removeItemAtPath:framework error:nil];
        [files removeItemAtPath:[directory stringByAppendingPathComponent:@"mediaremote-adapter.pl"] error:nil];
        return failures ? 1 : 0;
    }
}

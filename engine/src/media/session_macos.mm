#include "session_macos.h"
#include "session_macos_snapshot.h"
#include "session_macos_artwork.h"

#import <Foundation/Foundation.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <signal.h>
#include <unistd.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <tuple>

namespace
{
    // Resolve relative to this dylib, both in an SDK and in Contents/Frameworks.
    void media_library_location() {}

    bool same_snapshot(const MediaSessionSnapshot& a, const MediaSessionSnapshot& b)
    {
        const auto fields = [](const MediaSessionSnapshot& s) {
            return std::tie(s.bridgeLoaded, s.serviceReady, s.sessionAvailable,
                s.canTogglePlayPause, s.canSkipNext, s.canSkipPrevious,
                s.thumbnailRevision, s.positionMilliseconds, s.durationMilliseconds,
                s.playbackStatus, s.sourceProcessId, s.sourceApp, s.title,
                s.artist, s.album, s.thumbnailPath, s.diagnostic);
        };
        return fields(a) == fields(b);
    }
}

struct MacMediaSession::State
{
    std::mutex mutex;
    std::condition_variable wake;
    std::atomic<bool> stopping {false};
    std::thread worker;
    std::deque<std::pair<std::string, std::string>> commands;
    MediaSessionSnapshot snapshot;
    NSString* framework = nil;
    NSString* script = nil;
    NSString* artworkDirectory = nil;
    MacMediaArtworkCache artworkCache;
    std::chrono::steady_clock::time_point missingSince {};

    State()
    {
        @autoreleasepool
        {
            Dl_info location {};
            dladdr(reinterpret_cast<void*>(&media_library_location), &location);
            NSString* directory = location.dli_fname ?
                [[NSString stringWithUTF8String:location.dli_fname] stringByDeletingLastPathComponent] : nil;
            framework = [directory stringByAppendingPathComponent:@"VibranceMediaRemoteAdapter.framework"];
            script = [directory stringByAppendingPathComponent:@"mediaremote-adapter.pl"];
            NSFileManager* files = NSFileManager.defaultManager;
            if (![files fileExistsAtPath:script])
                script = [[directory stringByDeletingLastPathComponent]
                    stringByAppendingPathComponent:@"Resources/mediaremote-adapter.pl"];
            snapshot.bridgeLoaded = [files fileExistsAtPath:[framework stringByAppendingPathComponent:@"VibranceMediaRemoteAdapter"]] &&
                [files fileExistsAtPath:script] && [files isExecutableFileAtPath:@"/usr/bin/perl"];
            snapshot.revision = 1;
            if (!snapshot.bridgeLoaded)
            {
                snapshot.diagnostic = "The macOS MediaRemote companion is missing beside the engine library.";
                return;
            }
            snapshot.diagnostic = "Connecting to macOS Now Playing.";
            artworkDirectory = [NSTemporaryDirectory() stringByAppendingPathComponent:
                [@"vibrance-media-" stringByAppendingString:NSUUID.UUID.UUIDString]];
            if (![files createDirectoryAtPath:artworkDirectory withIntermediateDirectories:YES
                attributes:@{NSFilePosixPermissions: @0700} error:nil])
                artworkDirectory = nil;
            worker = std::thread([this] { run(); });
        }
    }

    ~State()
    {
        stopping = true;
        wake.notify_all();
        if (worker.joinable()) worker.join();
        @autoreleasepool
        {
            if (artworkDirectory)
                [NSFileManager.defaultManager removeItemAtPath:artworkDirectory error:nil];
        }
    }

    // No shell, bounded output, and a cancellable deadline. Neither a stuck
    // media service nor a full artwork pipe can block the UI or shutdown.
    NSData* invoke(NSArray<NSString*>* arguments)
    {
        NSTask* task = [NSTask new];
        task.executableURL = [NSURL fileURLWithPath:@"/usr/bin/perl"];
        task.arguments = [@[script, framework] arrayByAddingObjectsFromArray:arguments];
        NSPipe* pipe = [NSPipe pipe];
        task.standardOutput = pipe;
        // Adapter timeouts may print an error and still exit successfully with null.
        // Combining stderr makes that response invalid instead of a healthy idle session.
        task.standardError = pipe;
        NSError* error = nil;
        if (![task launchAndReturnError:&error]) return nil;
        [pipe.fileHandleForWriting closeFile];
        const int descriptor = pipe.fileHandleForReading.fileDescriptor;
        fcntl(descriptor, F_SETFL, fcntl(descriptor, F_GETFL) | O_NONBLOCK);
        NSMutableData* output = [NSMutableData data];
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        bool failed = false;
        while (true)
        {
            char buffer[16384];
            const ssize_t count = read(descriptor, buffer, sizeof(buffer));
            if (count > 0) [output appendBytes:buffer length:static_cast<NSUInteger>(count)];
            if (stopping || std::chrono::steady_clock::now() >= deadline || output.length > 8 * 1024 * 1024)
            {
                failed = true;
                if (task.running) kill(task.processIdentifier, SIGKILL);
                break;
            }
            if (count == 0) break;
            if (count < 0)
            {
                if (errno != EAGAIN && errno != EINTR) { failed = true; break; }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        // EOF need not mean the child has exited yet.
        while (task.running)
        {
            if (stopping || failed || std::chrono::steady_clock::now() >= deadline)
            {
                failed = true;
                kill(task.processIdentifier, SIGKILL);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        [task waitUntilExit];
        [pipe.fileHandleForReading closeFile];
        return !failed && task.terminationStatus == 0 ? output : nil;
    }

    void publish(MediaSessionSnapshot next)
    {
        std::lock_guard lock(mutex);
        next.revision = snapshot.revision + (same_snapshot(snapshot, next) ? 0 : 1);
        snapshot = std::move(next);
    }

    void poll()
    {
        NSData* output = invoke(@[@"get", @"--now", @"--micros", @"--allow-missing-title"]);
        id payload = output ? [NSJSONSerialization JSONObjectWithData:output
            options:NSJSONReadingFragmentsAllowed error:nil] : nil;
        MediaSessionSnapshot next = mac_media_snapshot(payload);
        // MediaRemote can briefly lose its item while switching playback state.
        // Require sustained absence before clearing the cover and process target.
        if (!next.sessionAvailable)
        {
            const auto now = std::chrono::steady_clock::now();
            if (missingSince == std::chrono::steady_clock::time_point{}) missingSince = now;
            if (now - missingSince < std::chrono::milliseconds(1500))
            {
                std::lock_guard lock(mutex);
                if (snapshot.sessionAvailable) return;
            }
        }
        else
        {
            missingSince = {};
            // Some players omit source identity in playback-only responses.
            std::lock_guard lock(mutex);
            if (next.sourceApp.empty() && !next.sourceProcessId && snapshot.sessionAvailable &&
                next.title == snapshot.title && next.artist == snapshot.artist && next.album == snapshot.album)
            {
                next.sourceApp = snapshot.sourceApp;
                next.sourceProcessId = snapshot.sourceProcessId;
            }
        }
        artworkCache.update(next,
            [payload isKindOfClass:NSDictionary.class] ? payload : nil,
            artworkDirectory);
        publish(std::move(next));
    }

    void run()
    {
        while (!stopping)
        {
            @autoreleasepool
            {
                @try
                {
                    std::deque<std::pair<std::string, std::string>> pending;
                    {
                        std::lock_guard lock(mutex);
                        pending.swap(commands);
                    }
                    for (const auto& [action, value] : pending)
                    {
                        if (stopping) break;
                        if (!invoke(@[[NSString stringWithUTF8String:action.c_str()],
                            [NSString stringWithUTF8String:value.c_str()]]))
                        {
                            // A command failure does not invalidate known metadata/artwork.
                            std::lock_guard lock(mutex);
                            snapshot.diagnostic = "The macOS media command failed or timed out.";
                            ++snapshot.revision;
                        }
                    }
                    if (!stopping) poll();
                }
                @catch (NSException*)
                {
                    std::lock_guard lock(mutex);
                    snapshot.diagnostic = "macOS Now Playing could not be refreshed.";
                    ++snapshot.revision;
                }
            }
            std::unique_lock lock(mutex);
            // Each refresh starts a Perl process and loads the companion
            // framework. An absent or paused session needs a much slower
            // fallback poll; commands still wake the worker immediately.
            const auto interval = !snapshot.sessionAvailable ?
                std::chrono::milliseconds(2000) :
                snapshot.playbackStatus == MediaSessionPlaybackStatus::ePlaying ?
                    std::chrono::milliseconds(500) :
                    std::chrono::milliseconds(1000);
            wake.wait_for(lock, interval,
                [this] { return stopping || !commands.empty(); });
        }
    }

    bool enqueue(std::string action, std::string value)
    {
        // Caller holds mutex; acceptance means queued, not acknowledged by the player.
        if (!snapshot.serviceReady || !snapshot.sessionAvailable || commands.size() >= 32) return false;
        commands.emplace_back(std::move(action), std::move(value));
        wake.notify_one();
        return true;
    }
};

MacMediaSession::MacMediaSession() : state(std::make_unique<State>()) {}
MacMediaSession::~MacMediaSession() = default;

bool MacMediaSession::refresh(MediaSessionSnapshot& snapshot)
{
    std::lock_guard lock(state->mutex);
    const bool changed = snapshot.revision != state->snapshot.revision;
    snapshot = state->snapshot;
    return changed;
}

bool MacMediaSession::send(MediaSessionCommand command)
{
    std::lock_guard lock(state->mutex);
    switch (command)
    {
        case MediaSessionCommand::eTogglePlayPause:
            return state->snapshot.canTogglePlayPause && state->enqueue("send", "2");
        case MediaSessionCommand::eSkipNext:
            return state->snapshot.canSkipNext && state->enqueue("send", "4");
        case MediaSessionCommand::eSkipPrevious:
            return state->snapshot.canSkipPrevious && state->enqueue("send", "5");
    }
    return false;
}

bool MacMediaSession::seek(std::int64_t milliseconds)
{
    std::lock_guard lock(state->mutex);
    if (state->snapshot.durationMilliseconds <= 0) return false;
    const auto position = std::clamp(milliseconds, std::int64_t(0), state->snapshot.durationMilliseconds);
    return state->enqueue("seek", std::to_string(position * 1000));
}

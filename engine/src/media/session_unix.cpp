// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "session_unix.h"

#include <gio/gio.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <string_view>
#include <thread>
#include <tuple>
#include <vector>

namespace
{
constexpr char playerPath[] = "/org/mpris/MediaPlayer2";
constexpr char playerInterface[] = "org.mpris.MediaPlayer2.Player";
constexpr std::string_view servicePrefix = "org.mpris.MediaPlayer2.";
constexpr int callTimeoutMilliseconds = 300;

using Variant = std::unique_ptr<GVariant, decltype(&g_variant_unref)>;

Variant call(GDBusConnection* bus, const char* service, const char* path,
    const char* interface, const char* method, GVariant* arguments, GCancellable* cancel)
{
    return {g_dbus_connection_call_sync(bus, service, path, interface, method, arguments,
        nullptr, G_DBUS_CALL_FLAGS_NO_AUTO_START, callTimeoutMilliseconds, cancel, nullptr),
        g_variant_unref};
}

const char* text_property(GVariant* values, const char* key, const char* format = "&s")
{
    const char* result = "";
    g_variant_lookup(values, key, format, &result);
    return result;
}

bool boolean_property(GVariant* values, const char* key)
{
    gboolean result = FALSE;
    g_variant_lookup(values, key, "b", &result);
    return result;
}

std::int64_t time_property(GVariant* values, const char* key)
{
    gint64 microseconds = 0;
    g_variant_lookup(values, key, "x", &microseconds);
    return std::max<std::int64_t>(0, microseconds / 1000);
}

std::string artists(GVariant* metadata)
{
    Variant values(g_variant_lookup_value(metadata, "xesam:artist", G_VARIANT_TYPE_STRING_ARRAY), g_variant_unref);
    if (!values) return {};
    std::string result;
    GVariantIter iterator;
    g_variant_iter_init(&iterator, values.get());
    const char* artist = nullptr;
    while (g_variant_iter_next(&iterator, "&s", &artist))
    {
        if (!result.empty()) result += ", ";
        result += artist;
    }
    return result;
}

std::string artwork_path(GVariant* metadata)
{
    const char* uri = text_property(metadata, "mpris:artUrl");
    std::unique_ptr<char, decltype(&g_free)> filename(g_filename_from_uri(uri, nullptr, nullptr), g_free);
    return filename ? filename.get() : "";
}

struct Player
{
    MediaSessionSnapshot snapshot;
    std::string service;
    std::string track;
    bool canSeek = false;
};

Player read_player(GDBusConnection* bus, const char* service, GCancellable* cancel)
{
    auto reply = call(bus, service, playerPath, "org.freedesktop.DBus.Properties",
        "GetAll", g_variant_new("(s)", playerInterface), cancel);
    if (!reply || !g_variant_is_of_type(reply.get(), G_VARIANT_TYPE("(a{sv})"))) return {};
    Variant values(g_variant_get_child_value(reply.get(), 0), g_variant_unref);
    const std::string_view status = text_property(values.get(), "PlaybackStatus");
    if (status.empty()) return {};

    Player player;
    player.service = service;
    auto& snapshot = player.snapshot;
    snapshot.sessionAvailable = true;
    snapshot.sourceApp = std::string_view(service).substr(servicePrefix.size());
    snapshot.playbackStatus = status == "Playing" ? MediaSessionPlaybackStatus::ePlaying :
        status == "Paused" ? MediaSessionPlaybackStatus::ePaused : MediaSessionPlaybackStatus::eStopped;
    snapshot.positionMilliseconds = time_property(values.get(), "Position");
    if (boolean_property(values.get(), "CanControl"))
    {
        snapshot.canTogglePlayPause = boolean_property(values.get(),
            status == "Playing" ? "CanPause" : "CanPlay");
        snapshot.canSkipNext = boolean_property(values.get(), "CanGoNext");
        snapshot.canSkipPrevious = boolean_property(values.get(), "CanGoPrevious");
        player.canSeek = boolean_property(values.get(), "CanSeek");
    }
    Variant metadata(g_variant_lookup_value(values.get(), "Metadata", G_VARIANT_TYPE_VARDICT), g_variant_unref);
    if (metadata)
    {
        snapshot.title = text_property(metadata.get(), "xesam:title");
        snapshot.artist = artists(metadata.get());
        snapshot.album = text_property(metadata.get(), "xesam:album");
        snapshot.durationMilliseconds = time_property(metadata.get(), "mpris:length");
        snapshot.thumbnailPath = artwork_path(metadata.get());
        player.track = text_property(metadata.get(), "mpris:trackid", "&o");
    }
    return player;
}

std::uint32_t player_process(GDBusConnection* bus, const std::string& service, GCancellable* cancel)
{
    auto reply = call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "GetConnectionUnixProcessID", g_variant_new("(s)", service.c_str()), cancel);
    guint32 pid = 0;
    if (reply && g_variant_is_of_type(reply.get(), G_VARIANT_TYPE("(u)"))) g_variant_get(reply.get(), "(u)", &pid);
    return pid;
}

Player read_session(GDBusConnection* bus, const Player& previous, GCancellable* cancel)
{
    Player selected;
    auto reply = call(bus, "org.freedesktop.DBus", "/org/freedesktop/DBus", "org.freedesktop.DBus",
        "ListNames", nullptr, cancel);
    if (!reply || !g_variant_is_of_type(reply.get(), G_VARIANT_TYPE("(as)")))
    {
        selected.snapshot.diagnostic = "The desktop session bus is unavailable.";
        return selected;
    }
    Variant names(g_variant_get_child_value(reply.get(), 0), g_variant_unref);
    GVariantIter iterator;
    g_variant_iter_init(&iterator, names.get());
    const char* name = nullptr;
    const auto priority = [&previous](const Player& player) {
        return std::tuple(player.snapshot.sessionAvailable,
            player.snapshot.playbackStatus == MediaSessionPlaybackStatus::ePlaying,
            player.snapshot.playbackStatus == MediaSessionPlaybackStatus::ePaused,
            player.service == previous.service);
    };
    while (g_variant_iter_next(&iterator, "&s", &name) && !g_cancellable_is_cancelled(cancel))
    {
        if (!std::string_view(name).starts_with(servicePrefix)) continue;
        Player player = read_player(bus, name, cancel);
        if (priority(player) > priority(selected)) selected = std::move(player);
    }
    if (selected.snapshot.sessionAvailable)
    {
        selected.snapshot.sourceProcessId = selected.service == previous.service && previous.snapshot.sourceProcessId ?
            previous.snapshot.sourceProcessId : player_process(bus, selected.service, cancel);
    }
    selected.snapshot.bridgeLoaded = true;
    selected.snapshot.serviceReady = true;
    return selected;
}

auto snapshot_fields(const MediaSessionSnapshot& value)
{
    return std::tie(value.bridgeLoaded, value.serviceReady, value.sessionAvailable, value.canTogglePlayPause,
        value.canSkipNext, value.canSkipPrevious, value.positionMilliseconds, value.durationMilliseconds,
        value.playbackStatus, value.sourceProcessId, value.sourceApp, value.title, value.artist, value.album,
        value.thumbnailPath, value.diagnostic);
}

struct Request
{
    std::string service;
    const char* method = nullptr;
    std::string track;
    std::int64_t position = 0;
};
}

struct UnixMediaSession::Impl
{
    std::mutex mutex;
    std::condition_variable_any wake;
    Player current;
    Player displayed;
    std::vector<Request> pending;
    std::unique_ptr<GCancellable, decltype(&g_object_unref)> cancel {g_cancellable_new(), g_object_unref};
    std::jthread worker {[this](std::stop_token stop) { run(stop); }};

    ~Impl()
    {
        worker.request_stop();
        g_cancellable_cancel(cancel.get());
    }

    void run(std::stop_token stop)
    {
        std::unique_ptr<GDBusConnection, decltype(&g_object_unref)> bus(nullptr, g_object_unref);
        Player previous;
        while (!stop.stop_requested())
        {
            std::vector<Request> requests;
            {
                std::lock_guard lock(mutex);
                requests.swap(pending);
            }
            if (!bus || g_dbus_connection_is_closed(bus.get()))
                bus.reset(g_bus_get_sync(G_BUS_TYPE_SESSION, cancel.get(), nullptr));
            Player next;
            if (bus)
            {
                for (const Request& request : requests)
                {
                    auto* arguments = request.track.empty() ? nullptr :
                        g_variant_new("(ox)", request.track.c_str(), request.position);
                    call(bus.get(), request.service.c_str(), playerPath, playerInterface,
                        request.method, arguments, cancel.get());
                }
                next = read_session(bus.get(), previous, cancel.get());
            }
            else next.snapshot.diagnostic = "The desktop session bus is unavailable.";

            auto& snapshot = next.snapshot;
            snapshot.thumbnailRevision = previous.snapshot.thumbnailRevision +
                (snapshot.thumbnailPath != previous.snapshot.thumbnailPath);
            snapshot.revision = previous.snapshot.revision +
                (snapshot_fields(snapshot) != snapshot_fields(previous.snapshot) || next.track != previous.track);
            {
                std::unique_lock lock(mutex);
                current = next;
                previous = std::move(next);
                wake.wait_for(lock, stop, std::chrono::milliseconds(300), [this] { return !pending.empty(); });
            }
        }
    }
};

UnixMediaSession::UnixMediaSession() : impl(std::make_unique<Impl>()) {}
UnixMediaSession::~UnixMediaSession() = default;

bool UnixMediaSession::refresh(MediaSessionSnapshot& snapshot)
{
    std::lock_guard lock(impl->mutex);
    impl->displayed = impl->current;
    const bool changed = snapshot.revision != impl->displayed.snapshot.revision;
    if (changed) snapshot = impl->displayed.snapshot;
    return changed;
}

bool UnixMediaSession::send(const MediaSessionSnapshot& snapshot, MediaSessionCommand command)
{
    const char* method = nullptr;
    switch (command)
    {
    case MediaSessionCommand::eTogglePlayPause: if (snapshot.canTogglePlayPause) method = "PlayPause"; break;
    case MediaSessionCommand::eSkipNext: if (snapshot.canSkipNext) method = "Next"; break;
    case MediaSessionCommand::eSkipPrevious: if (snapshot.canSkipPrevious) method = "Previous"; break;
    }
    if (!method) return false;
    std::lock_guard lock(impl->mutex);
    impl->pending.push_back({impl->displayed.service, method, {}, 0});
    impl->wake.notify_one();
    return true;
}

bool UnixMediaSession::seek(const MediaSessionSnapshot& snapshot, std::int64_t position)
{
    std::lock_guard lock(impl->mutex);
    const auto& player = impl->displayed;
    if (!player.canSeek || player.track.empty() || player.track == "/org/mpris/MediaPlayer2/TrackList/NoTrack")
        return false;
    const auto maximum = std::numeric_limits<std::int64_t>::max() / 1000;
    const auto duration = snapshot.durationMilliseconds;
    const auto target = std::clamp<std::int64_t>(position, 0, duration > 0 ? std::min(duration, maximum) : maximum);
    impl->pending.push_back({player.service, "SetPosition", player.track, target * 1000});
    impl->wake.notify_one();
    return true;
}

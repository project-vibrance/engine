// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "session_unix.h"

#ifdef VIBRANCE_HAS_ARTWORK_CURL
#include "session_unix_artwork.h"
#endif

#ifdef VIBRANCE_HAS_MPRIS_CODEC
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
}
#endif

#include <gio/gio.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <fstream>
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
    if (g_variant_lookup(values, key, "x", &microseconds))
        return std::max<std::int64_t>(0, microseconds / 1000);
    guint64 unsignedMicroseconds = 0;
    g_variant_lookup(values, key, "t", &unsignedMicroseconds);
    return static_cast<std::int64_t>(unsignedMicroseconds / 1000);
}

std::string artists(GVariant* metadata, const char* key)
{
    Variant values(g_variant_lookup_value(metadata, key, G_VARIANT_TYPE_STRING_ARRAY), g_variant_unref);
    if (!values) return text_property(metadata, key);
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

std::string local_path(const std::string& uri)
{
    if (!std::string_view(uri).starts_with("file:")) return {};
    char* hostname = nullptr;
    const std::unique_ptr<char, decltype(&g_free)> filename(g_filename_from_uri(uri.c_str(), &hostname, nullptr), g_free);
    const std::unique_ptr<char, decltype(&g_free)> host(hostname, g_free);
    return filename && (!host || !*host || g_ascii_strcasecmp(host.get(), "localhost") == 0) &&
        g_file_test(filename.get(), G_FILE_TEST_IS_REGULAR) ? filename.get() : "";
}

MediaPlaybackQuality reported_quality(GVariant* metadata)
{
    bool lossless = false;
    for (const char* key : {"xesam:audioCodec", "xesam:audioCodecType", "xesam:audioQuality"})
    {
        const std::unique_ptr<char, decltype(&g_free)> label(g_ascii_strdown(text_property(metadata, key), -1), g_free);
        const std::string_view format = g_strstrip(label.get());
        if (format == "dolby atmos" || format == "e-ac-3 joc" || format == "eac3 joc" ||
            format == "truehd atmos") return MediaPlaybackQuality::eDolbyAtmos;
        lossless |= format == "lossless" || format == "hi-res lossless" || format == "flac" ||
            format == "alac" || format == "apple lossless";
    }
    return media_playback_quality(false, lossless);
}

MediaPlaybackQuality local_quality(const std::string& uri)
{
    const auto path = local_path(uri);
    if (path.empty()) return MediaPlaybackQuality::eUnknown;
#ifdef VIBRANCE_HAS_MPRIS_CODEC
    auto* format = avformat_alloc_context();
    if (!format) return MediaPlaybackQuality::eUnknown;
    auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(150);
    format->interrupt_callback = {+[](void* value) {
        return std::chrono::steady_clock::now() >= *static_cast<decltype(deadline)*>(value) ? 1 : 0;
    }, &deadline};
    AVDictionary* options = nullptr;
    av_dict_set(&options, "protocol_whitelist", "file", 0);
    av_dict_set_int(&options, "probesize", 256 * 1024, 0);
    av_dict_set_int(&options, "analyzeduration", 100000, 0);
    const int opened = avformat_open_input(&format, path.c_str(), nullptr, &options);
    av_dict_free(&options);
    if (opened < 0) return MediaPlaybackQuality::eUnknown;
    auto deleter = [](AVFormatContext* context) { avformat_close_input(&context); };
    const std::unique_ptr<AVFormatContext, decltype(deleter)> input(format, deleter);
    avformat_find_stream_info(format, nullptr);
    const AVCodecParameters* audio = nullptr;
    for (unsigned index = 0; index < format->nb_streams; ++index)
    {
        const auto* parameters = format->streams[index]->codecpar;
        if (parameters->codec_type != AVMEDIA_TYPE_AUDIO) continue;
        if (audio) return MediaPlaybackQuality::eUnknown;
        audio = parameters;
    }
    if (!audio) return MediaPlaybackQuality::eUnknown;
#if defined(AV_PROFILE_EAC3_DDP_ATMOS) && defined(AV_PROFILE_TRUEHD_ATMOS)
    if ((audio->codec_id == AV_CODEC_ID_EAC3 && audio->profile == AV_PROFILE_EAC3_DDP_ATMOS) ||
        (audio->codec_id == AV_CODEC_ID_TRUEHD && audio->profile == AV_PROFILE_TRUEHD_ATMOS))
        return MediaPlaybackQuality::eDolbyAtmos;
#elif defined(FF_PROFILE_EAC3_DDP_ATMOS) && defined(FF_PROFILE_TRUEHD_ATMOS)
    if ((audio->codec_id == AV_CODEC_ID_EAC3 && audio->profile == FF_PROFILE_EAC3_DDP_ATMOS) ||
        (audio->codec_id == AV_CODEC_ID_TRUEHD && audio->profile == FF_PROFILE_TRUEHD_ATMOS))
        return MediaPlaybackQuality::eDolbyAtmos;
#endif
    const auto* codec = avcodec_descriptor_get(audio->codec_id);
    return media_playback_quality(false, codec && (codec->props & AV_CODEC_PROP_LOSSLESS) &&
        !(codec->props & AV_CODEC_PROP_LOSSY));
#else
    std::ifstream file(path, std::ios::binary);
    std::array<char, 4> signature {};
    return media_playback_quality(false, file.read(signature.data(), signature.size()) &&
        std::string_view(signature.data(), signature.size()) == "fLaC");
#endif
}

struct Player
{
    MediaSessionSnapshot snapshot;
    std::string service;
    std::string track;
    std::string artworkUri;
    std::string mediaUri;
    MediaPlaybackQuality localQuality = MediaPlaybackQuality::eUnknown;
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
        snapshot.artist = artists(metadata.get(), "xesam:artist");
        if (snapshot.artist.empty()) snapshot.artist = artists(metadata.get(), "xesam:albumArtist");
        snapshot.album = text_property(metadata.get(), "xesam:album");
        snapshot.durationMilliseconds = time_property(metadata.get(), "mpris:length");
        player.artworkUri = text_property(metadata.get(), "mpris:artUrl");
        player.mediaUri = text_property(metadata.get(), "xesam:url");
        snapshot.playbackQuality = reported_quality(metadata.get());
        player.track = text_property(metadata.get(), "mpris:trackid", "&o");
        if (player.track.empty())
        {
            const char* track = text_property(metadata.get(), "mpris:trackid");
            if (g_variant_is_object_path(track)) player.track = track;
        }
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
        auto& snapshot = selected.snapshot;
        snapshot.sourceProcessId = player_process(bus, selected.service, cancel);
        if (selected.service == previous.service && snapshot.sourceProcessId == previous.snapshot.sourceProcessId)
            snapshot.sourceApp = previous.snapshot.sourceApp;
        else
        {
            const auto entry = call(bus, selected.service.c_str(), playerPath, "org.freedesktop.DBus.Properties",
                "Get", g_variant_new("(ss)", "org.mpris.MediaPlayer2", "DesktopEntry"), cancel);
            if (entry && g_variant_is_of_type(entry.get(), G_VARIANT_TYPE("(v)")))
            {
                GVariant* raw = nullptr;
                g_variant_get(entry.get(), "(v)", &raw);
                const Variant name(raw, g_variant_unref);
                if (g_variant_is_of_type(name.get(), G_VARIANT_TYPE_STRING) && g_variant_get_string(name.get(), nullptr)[0])
                    snapshot.sourceApp = g_variant_get_string(name.get(), nullptr);
            }
        }
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
        value.thumbnailPath, value.diagnostic, value.playbackQuality);
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
#ifdef VIBRANCE_HAS_ARTWORK_CURL
    UnixMediaArtwork artwork;
#endif
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
            snapshot.thumbnailPath = local_path(next.artworkUri);
#ifdef VIBRANCE_HAS_ARTWORK_CURL
            auto downloaded = artwork.resolve(next.artworkUri);
            if (snapshot.thumbnailPath.empty()) snapshot.thumbnailPath = std::move(downloaded);
#endif
            if (snapshot.playbackQuality == MediaPlaybackQuality::eUnknown && !next.mediaUri.empty())
            {
                const bool sameTrack = next.service == previous.service && next.track == previous.track &&
                    next.mediaUri == previous.mediaUri && previous.snapshot.playbackQuality == previous.localQuality;
                next.localQuality = sameTrack ? previous.localQuality : local_quality(next.mediaUri);
                snapshot.playbackQuality = next.localQuality;
            }
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

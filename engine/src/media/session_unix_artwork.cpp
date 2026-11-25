// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "session_unix_artwork.h"

#include <curl/curl.h>
#include <glib.h>
#include <glib/gstdio.h>
#include <stb_image.h>

#include <algorithm>
#include <memory>
#include <vector>

namespace
{
constexpr std::size_t maximumBytes = 8u * 1024u * 1024u;
constexpr std::size_t maximumArtwork = 12;

struct Download
{
    std::vector<unsigned char> bytes;
    std::stop_token stop;
    const std::atomic<std::uint64_t>& revision;
    std::uint64_t requestedRevision;

    bool cancelled() const { return stop.stop_requested() || revision.load() != requestedRevision; }
};

bool fetch(const std::string& uri, Download& download)
{
    static const auto initialisation = curl_global_init(CURL_GLOBAL_DEFAULT);
    if (initialisation != CURLE_OK) return false;
    const std::unique_ptr<CURL, decltype(&curl_easy_cleanup)> curl(curl_easy_init(), curl_easy_cleanup);
    if (!curl) return false;
    auto* handle = curl.get();
    curl_easy_setopt(handle, CURLOPT_URL, uri.c_str());
#if LIBCURL_VERSION_NUM >= 0x075500
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS_STR, uri.starts_with("https:") ? "https" : "http,https");
#else
    curl_easy_setopt(handle, CURLOPT_PROTOCOLS, CURLPROTO_HTTP | CURLPROTO_HTTPS);
    curl_easy_setopt(handle, CURLOPT_REDIR_PROTOCOLS,
        uri.starts_with("https:") ? CURLPROTO_HTTPS : CURLPROTO_HTTP | CURLPROTO_HTTPS);
#endif
    curl_easy_setopt(handle, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXREDIRS, 4L);
    curl_easy_setopt(handle, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(handle, CURLOPT_CONNECTTIMEOUT_MS, 2000L);
    curl_easy_setopt(handle, CURLOPT_TIMEOUT_MS, 5000L);
    curl_easy_setopt(handle, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(handle, CURLOPT_MAXFILESIZE_LARGE, static_cast<curl_off_t>(maximumBytes));
    curl_easy_setopt(handle, CURLOPT_WRITEDATA, &download);
    curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION,
        +[](char* data, std::size_t size, std::size_t count, void* state) -> std::size_t {
            auto& result = *static_cast<Download*>(state);
            if (!size || count > (maximumBytes - result.bytes.size()) / size || result.cancelled()) return 0;
            const auto bytes = size * count;
            try
            {
                result.bytes.insert(result.bytes.end(), data, data + bytes);
                return bytes;
            }
            catch (...) { return 0; }
        });
    curl_easy_setopt(handle, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(handle, CURLOPT_XFERINFODATA, &download);
    curl_easy_setopt(handle, CURLOPT_XFERINFOFUNCTION,
        +[](void* state, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
            return static_cast<Download*>(state)->cancelled() ? 1 : 0;
        });
    if (curl_easy_perform(handle) != CURLE_OK || download.cancelled() || download.bytes.empty()) return false;
    int width = 0, height = 0, channels = 0;
    return stbi_info_from_memory(download.bytes.data(), static_cast<int>(download.bytes.size()), &width, &height, &channels) &&
        width > 0 && height > 0 && static_cast<std::int64_t>(width) * height <= 16 * 1024 * 1024;
}
}

UnixMediaArtwork::~UnixMediaArtwork()
{
    worker.request_stop();
    worker.join();
    for (const auto& [uri, path] : cached) g_unlink(path.c_str());
    if (!directory.empty()) g_rmdir(directory.c_str());
}

std::string UnixMediaArtwork::resolve(std::string_view uri)
{
    if ((!uri.starts_with("https://") && !uri.starts_with("http://")) || uri.size() > 8192) uri = {};
    std::lock_guard lock(mutex);
    if (requested != uri)
    {
        requested = uri;
        ++revision;
        pending = false;
        nextAttempt = {};
    }
    const auto ready = std::find_if(cached.begin(), cached.end(), [uri](const auto& entry) {
        return entry.first == uri;
    });
    if (ready != cached.end()) return ready->second;
    if (!uri.empty() && std::chrono::steady_clock::now() >= nextAttempt)
    {
        nextAttempt = std::chrono::steady_clock::time_point::max();
        pending = true;
        wake.notify_one();
    }
    return {};
}

void UnixMediaArtwork::run(std::stop_token stop)
{
    std::unique_lock lock(mutex);
    while (wake.wait(lock, stop, [this] { return pending; }) && !stop.stop_requested())
    {
        pending = false;
        const std::string uri = requested;
        if (uri.empty()) continue;
        Download download {{}, stop, revision, revision.load()};
        lock.unlock();
        bool fetched = fetch(uri, download);
        if (fetched && directory.empty())
        {
            const std::unique_ptr<char, decltype(&g_free)> path(g_dir_make_tmp("vibrance-artwork-XXXXXX", nullptr), g_free);
            if (path) directory = path.get();
        }
        std::string path;
        if (fetched && !directory.empty())
        {
            path = directory + "/" + std::to_string(download.requestedRevision) + ".image";
            fetched = g_file_set_contents(path.c_str(), reinterpret_cast<const char*>(download.bytes.data()),
                static_cast<gssize>(download.bytes.size()), nullptr);
        }
        else fetched = false;
        lock.lock();
        if (download.cancelled())
        {
            if (fetched) g_unlink(path.c_str());
        }
        else if (fetched)
        {
            cached.emplace_back(uri, std::move(path));
            if (cached.size() > maximumArtwork)
            {
                g_unlink(cached.front().second.c_str());
                cached.pop_front();
            }
        }
        else nextAttempt = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    }
}

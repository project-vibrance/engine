#pragma once
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>
#include <cstdint>
#include <cstdlib>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <csignal>
#include <unistd.h>
#endif

namespace runtime_log_storage
{
inline constexpr std::uintmax_t fileLimit = 5u * 1024u * 1024u;
inline constexpr std::uintmax_t directoryLimit = 50u * 1024u * 1024u;
inline constexpr unsigned archives = 4;
inline constexpr unsigned retainedFiles = 100;
inline constexpr unsigned retainedDumps = 3;

inline bool digits(std::string_view text)
{
    return !text.empty() && std::all_of(text.begin(), text.end(),
        [](char value) { return value >= '0' && value <= '9'; });
}

struct FileName
{
    bool owned = false;
    bool archive = false;
    bool dump = false;
    unsigned long process = 0;
};

inline FileName identify(std::string_view name, std::string_view customPrefix = {})
{
    FileName result;
    const auto extension = name.find('.');
    if (extension == std::string_view::npos) return result;
    const auto stem = name.substr(0, extension);
    const auto suffix = name.substr(extension);
    const bool text = suffix == ".txt" || suffix == ".log";
    const bool crash = suffix == ".txt.crash.txt" || suffix == ".log.crash.txt";
    result.dump = suffix == ".txt.dmp" || suffix == ".log.dmp";
    result.archive = (suffix.starts_with(".txt.") || suffix.starts_with(".log.")) &&
        digits(suffix.substr(5));
    if (!text && !crash && !result.dump && !result.archive) return {};
    const auto matches = [&](std::string_view prefix) {
        if (prefix.empty() || stem.size() <= prefix.size() + 1 || !stem.starts_with(prefix)) return false;
        const auto timestamp = stem.substr(prefix.size() + 1);
        if (stem[prefix.size()] == '_')
            return timestamp.size() >= 19 && digits(timestamp.substr(0, 4)) && timestamp[4] == '-' &&
                digits(timestamp.substr(5, 2)) && timestamp[7] == '-' && digits(timestamp.substr(8, 2)) &&
                timestamp[10] == '_' && digits(timestamp.substr(11, 2)) && timestamp[13] == '-' &&
                digits(timestamp.substr(14, 2)) && timestamp[16] == '-' && digits(timestamp.substr(17, 2)) &&
                (timestamp.size() == 19 || timestamp[19] == '_');
        if (stem[prefix.size()] == '-')
            return timestamp.size() > 16 && digits(timestamp.substr(0, 8)) && timestamp[8] == '-' &&
                digits(timestamp.substr(9, 6)) && timestamp[15] == '-' && digits(timestamp.substr(16));
        return false;
    };
    result.owned = matches(customPrefix);
    for (const auto prefix : { "vibrance-app", "vibrance-installer", "vibrance-installer-bootstrap",
            "vibrance_log", "vibrance-updater", "vibrance-uninstaller", "vibrance-startup-service" })
        result.owned |= matches(prefix);
    if (!result.owned) return {};
    const auto last = stem.rfind('_');
    if (last != std::string_view::npos && digits(stem.substr(last + 1)))
    {
        const auto previous = stem.rfind('_', last - 1);
        if (previous != std::string_view::npos && digits(stem.substr(previous + 1, last - previous - 1)))
            result.process = std::strtoul(std::string(stem.substr(previous + 1, last - previous - 1)).c_str(), nullptr, 10);
    }
    else if (stem.find('_') == std::string_view::npos)
    {
        const auto dash = stem.rfind('-');
        if (dash != std::string_view::npos && digits(stem.substr(dash + 1)))
            result.process = std::strtoul(std::string(stem.substr(dash + 1)).c_str(), nullptr, 10);
    }
    return result;
}

inline bool running(unsigned long process)
{
    if (!process) return false;
#if defined(_WIN32)
    HANDLE handle = OpenProcess(SYNCHRONIZE, FALSE, process);
    if (!handle) return GetLastError() == ERROR_ACCESS_DENIED;
    const bool active = WaitForSingleObject(handle, 0) != WAIT_OBJECT_0;
    CloseHandle(handle);
    return active;
#else
    return ::kill(static_cast<pid_t>(process), 0) == 0 || errno == EPERM;
#endif
}

inline std::filesystem::path archive_path(const std::filesystem::path& active, unsigned part)
{
    auto path = active;
    path += "." + std::to_string(part);
    return path;
}

// The caller closes its writer first. Failure leaves the existing files intact
// and must not be followed by truncation of the current log.
inline bool rotate(const std::filesystem::path& active)
{
    std::error_code error;
    std::filesystem::remove(archive_path(active, archives), error);
    if (error) return false;
    for (unsigned part = archives; part > 1; --part)
    {
        const auto previous = archive_path(active, part - 1);
        if (!std::filesystem::exists(previous, error)) { if (error) return false; continue; }
        std::filesystem::rename(previous, archive_path(active, part), error);
        if (error) return false;
    }
    std::filesystem::rename(active, archive_path(active, 1), error);
    return !error;
}

// Only recognised runtime artefacts in this directory are eligible. Compilation
// transcripts and other user files are never part of this budget. Live session
// files are preserved, including their emergency reports and pending dumps.
inline void prune(const std::filesystem::path& directory,
    const std::filesystem::path& active, std::string_view customPrefix = {}) noexcept
{
    try
    {
        struct Entry { std::filesystem::path path; std::uintmax_t size; std::filesystem::file_time_type time; bool protectedFile; bool dump; };
        std::vector<Entry> entries;
        std::uintmax_t total = 0;
        unsigned dumps = 0;
        std::error_code error;
        for (std::filesystem::directory_iterator iterator(directory, error), end;
            !error && iterator != end; iterator.increment(error))
        {
            const auto& entry = *iterator;
            const auto kind = identify(entry.path().filename().string(), customPrefix);
            if (!kind.owned || !entry.is_regular_file(error) || entry.is_symlink(error)) continue;
            const auto size = entry.file_size(error);
            if (error) { error.clear(); continue; }
            const auto time = entry.last_write_time(error);
            if (error) { error.clear(); continue; }
            const auto activeName = active.filename().string();
            const auto name = entry.path().filename().string();
            const bool current = name == activeName || name == activeName + ".crash.txt" || name == activeName + ".dmp";
            const bool protect = current || (!kind.archive && running(kind.process));
            entries.push_back({ entry.path(), size, time, protect, kind.dump });
            total += size;
            dumps += kind.dump;
        }
        std::sort(entries.begin(), entries.end(), [](const Entry& left, const Entry& right) { return left.time < right.time; });
        const auto expiry = std::filesystem::file_time_type::clock::now() - std::chrono::hours(24 * 7);
        auto count = entries.size();
        // Reserve room for the active file to grow before the next rotation.
        const auto budget = directoryLimit - fileLimit;
        for (const auto& entry : entries)
        {
            if (entry.protectedFile) continue;
            if (total <= budget && count <= retainedFiles && entry.time >= expiry &&
                (!entry.dump || dumps <= retainedDumps)) continue;
            if (std::filesystem::remove(entry.path, error))
            {
                total -= entry.size;
                --count;
                if (entry.dump) --dumps;
            }
            error.clear();
        }
    }
    catch (...) {} // Cleanup is best-effort; diagnostics must remain usable.
}
}

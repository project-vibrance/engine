#pragma once

// Startup diagnostics must not depend on the shell or redirected Documents.
// These paths are resolved entirely from the process environment and kernel APIs.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <string>
#include <vector>

namespace windows_diagnostic_paths
{
inline std::filesystem::path environment_path(const wchar_t* name)
{
    const DWORD size = GetEnvironmentVariableW(name, nullptr, 0u);
    if (!size || size > 32768u) return {};
    std::wstring value(size, L'\0');
    const DWORD length = GetEnvironmentVariableW(name, value.data(), size);
    if (!length || length >= size) return {};
    value.resize(length);
    std::filesystem::path path(value);
    return path.is_absolute() ? path : std::filesystem::path {};
}

inline std::vector<std::filesystem::path> candidates(
    const std::filesystem::path& relative = L"Vibrance/logs",
    const std::filesystem::path& preferred = {})
{
    std::vector<std::filesystem::path> directories;
    const auto append = [&](const std::filesystem::path& directory) {
        if (directory.empty()) return;
        for (const auto& existing : directories) if (existing == directory) return;
        directories.push_back(directory);
    };
    append(preferred);
    if (const auto local = environment_path(L"LOCALAPPDATA"); !local.empty()) append(local / relative);
    if (const auto profile = environment_path(L"USERPROFILE"); !profile.empty())
        append(profile / L"AppData/Local" / relative);
    wchar_t temporary[MAX_PATH + 1] {};
    const DWORD size = GetTempPathW(MAX_PATH + 1u, temporary);
    if (size && size <= MAX_PATH) append(std::filesystem::path(temporary) / relative);
    else if (size > MAX_PATH && size < 32768u)
    {
        std::wstring buffer(size + 1u, L'\0');
        const DWORD length = GetTempPathW(static_cast<DWORD>(buffer.size()), buffer.data());
        if (length && length < buffer.size())
        {
            buffer.resize(length);
            append(std::filesystem::path(buffer) / relative);
        }
    }
    return directories;
}
}

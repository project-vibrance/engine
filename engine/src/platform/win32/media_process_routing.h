// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>

namespace vibrance::win32
{
inline std::wstring_view media_executable_name(std::wstring_view path) noexcept
{
    const auto separator = path.find_last_of(L"\\/");
    return separator == std::wstring_view::npos ? path : path.substr(separator + 1u);
}

inline std::wstring_view media_source_package_family(std::wstring_view sourceApp) noexcept
{
    const auto separator = sourceApp.find(L'!');
    return separator == std::wstring_view::npos || separator == 0u ?
        std::wstring_view {} : sourceApp.substr(0u, separator);
}

template <typename EqualIdentity>
bool media_process_identity_matches(std::wstring_view sourceApp,
    std::wstring_view registeredPath, std::wstring_view processAppId,
    std::wstring_view processPath, std::wstring_view processPackageFamily,
    EqualIdentity equal, std::wstring_view sourcePackageFamily = {})
{
    if (sourceApp.empty()) return false;
    if (!processAppId.empty() && equal(processAppId, sourceApp)) return true;
    if (!processPath.empty() && (equal(processPath, sourceApp) ||
        equal(media_executable_name(processPath), sourceApp) ||
        (!registeredPath.empty() && equal(processPath, registeredPath)))) return true;
    if (sourcePackageFamily.empty())
        sourcePackageFamily = media_source_package_family(sourceApp);
    return !sourcePackageFamily.empty() && !processPackageFamily.empty() &&
        equal(sourcePackageFamily, processPackageFamily);
}

template <typename MatchesIdentity, typename ParentProcess>
bool media_audio_process_matches_source(std::uint32_t processId,
    MatchesIdentity matchesIdentity, ParentProcess parentProcess)
{
    for (unsigned depth = 0u; processId != 0u && depth < 32u; ++depth)
    {
        if (matchesIdentity(processId)) return true;
        const auto parent = parentProcess(processId);
        if (parent == processId) break;
        processId = parent;
    }
    return false;
}

struct MediaAudioProcessSelection
{
    std::uint32_t activeProcessId = 0u;
    std::uint32_t inactiveProcessId = 0u;

    void consider(std::uint32_t processId, bool active,
        std::uint32_t preferredProcessId = 0u) noexcept
    {
        if (processId == 0u) return;
        auto& selected = active ? activeProcessId : inactiveProcessId;
        if (selected == 0u || processId == preferredProcessId) selected = processId;
    }

    std::uint32_t process_id() const noexcept
    {
        return activeProcessId != 0u ? activeProcessId : inactiveProcessId;
    }
};
}

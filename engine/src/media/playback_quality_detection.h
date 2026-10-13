#pragma once

#include <vibranceUI/media/playback_quality.h>
#include <algorithm>
#include <cwctype>
#include <span>
#include <string>
#include <string_view>

namespace media_quality_detection
{
enum class Role { eContainer, eText, eButton, eExcluded, eWindow, eSlider };

struct Node
{
    int parent = -1;
    Role role = Role::eContainer;
    bool visible = true;
    std::wstring name;
    std::wstring automationId;
};

inline std::wstring normalise(std::wstring_view text)
{
    std::wstring result;
    for (const wchar_t character : text)
    {
        if (std::iswspace(character))
        {
            if (!result.empty() && result.back() != L' ') result += L' ';
        }
        else result += static_cast<wchar_t>(std::towlower(character));
    }
    if (!result.empty() && result.back() == L' ') result.pop_back();
    return result;
}

inline MediaPlaybackQuality badge_quality(std::wstring_view name)
{
    const std::wstring label = normalise(name);
    // Exact playback badges only. Capability descriptions, settings, codec
    // sample rates and track titles mentioning these words are not evidence.
    if (label == L"dolby atmos") return MediaPlaybackQuality::eDolbyAtmos;
    if (label == L"lossless" || label == L"hi-res lossless" ||
        label == L"hi res lossless") return MediaPlaybackQuality::eLossless;
    return MediaPlaybackQuality::eUnknown;
}

inline bool belongs_to(std::span<const Node> nodes, int child, int ancestor)
{
    for (std::size_t depth = 0; child >= 0 && depth < nodes.size(); ++depth)
    {
        if (static_cast<std::size_t>(child) >= nodes.size()) return false;
        const Node& node = nodes[child];
        if (!node.visible || node.role == Role::eExcluded) return false;
        if (child == ancestor) return true;
        child = node.parent;
    }
    return false;
}

inline bool playback_control(const Node& node)
{
    if (node.role != Role::eButton) return false;
    const auto name = normalise(node.name);
    const auto id = normalise(node.automationId);
    return name == L"play" || name == L"pause" || name == L"play/pause" ||
        name == L"play or pause" || name.starts_with(L"play (") ||
        name.starts_with(L"pause (") || id.find(L"playpause") != id.npos ||
        id.find(L"play_pause") != id.npos;
}

inline bool complete_playback_controls(std::span<const Node> nodes, int scope)
{
    bool next = false, previous = false, position = false;
    for (std::size_t child = 0; child < nodes.size(); ++child)
    {
        if (!belongs_to(nodes, static_cast<int>(child), scope)) continue;
        const auto& node = nodes[child];
        if (node.role != Role::eButton && node.role != Role::eSlider) continue;
        const auto name = normalise(node.name);
        const auto id = normalise(node.automationId);
        if (node.role == Role::eButton)
        {
            next |= name == L"next" || name.starts_with(L"next ") ||
                name.starts_with(L"skip to next") || id.find(L"next") != id.npos;
            previous |= name == L"previous" || name.starts_with(L"previous ") ||
                name.starts_with(L"skip to previous") || id.find(L"previous") != id.npos;
        }
        else
            for (const auto keyword : { L"position", L"progress", L"seek", L"timeline" })
                position |= name.find(keyword) != name.npos || id.find(keyword) != id.npos;
    }
    return next && previous && position;
}

// The default policy remains unchanged for Windows. macOS can additionally
// identify a complete player when its artist metadata is an unlabelled image.
template <bool AllowUnlabelledMetadata = false>
inline MediaPlaybackQuality detect(std::span<const Node> nodes,
    std::wstring_view currentTitle, std::wstring_view currentArtist,
    std::wstring_view currentAlbum = {})
{
    const auto title = normalise(currentTitle);
    const auto artist = normalise(currentArtist);
    const auto album = normalise(currentAlbum);
    if (title.empty() || artist.empty()) return MediaPlaybackQuality::eUnknown;
    bool atmos = false;
    bool lossless = false;
    for (std::size_t badge = 0; badge < nodes.size(); ++badge)
    {
        if (nodes[badge].role != Role::eText && nodes[badge].role != Role::eButton)
            continue;
        const auto badgeName = normalise(nodes[badge].name);
        if (badgeName == title || badgeName == artist ||
            (!album.empty() && badgeName == album)) continue;
        const auto quality = badge_quality(nodes[badge].name);
        if (quality == MediaPlaybackQuality::eUnknown) continue;
        // The badge must be in a small playback container with this item's
        // title, artist and transport control. Never use the entire window,
        // catalogue rows, menus or quality preferences as the matching scope.
        int scope = nodes[badge].parent;
        for (unsigned depth = 0; scope >= 0 && depth < 6u; ++depth)
        {
            if (static_cast<std::size_t>(scope) >= nodes.size()) break;
            const auto& container = nodes[scope];
            if (!container.visible || container.role == Role::eExcluded ||
                container.role == Role::eWindow) break;
            if (container.role == Role::eContainer &&
                belongs_to(nodes, static_cast<int>(badge), scope))
            {
                bool titleMatches = false;
                bool artistMatches = false;
                bool transport = false;
                unsigned count = 0;
                for (std::size_t i = 0; i < nodes.size(); ++i)
                {
                    if (!belongs_to(nodes, static_cast<int>(i), scope)) continue;
                    if (++count > 96u) break;
                    const auto name = normalise(nodes[i].name);
                    titleMatches |= i != badge && name == title;
                    artistMatches |= i != badge && name == artist;
                    transport |= playback_control(nodes[i]);
                }
                bool identityMatches = titleMatches && artistMatches;
                if constexpr (AllowUnlabelledMetadata)
                    identityMatches |= count <= 96u && titleMatches && complete_playback_controls(nodes, scope);
                if (count <= 96u && identityMatches && transport)
                {
                    atmos |= quality == MediaPlaybackQuality::eDolbyAtmos;
                    lossless |= quality == MediaPlaybackQuality::eLossless;
                    break;
                }
            }
            scope = container.parent;
        }
    }
    return media_playback_quality(atmos, lossless);
}
}

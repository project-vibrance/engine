// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "playback_quality_detection.h"
#include <cmath>
#include <cstdint>
#include <optional>
#include <span>

namespace vibrance::media
{
struct PlaybackSeekIdentity
{
    std::wstring_view sourceApp;
    std::wstring_view title;
    std::wstring_view artist;
    std::wstring_view album;

    bool operator==(const PlaybackSeekIdentity&) const = default;
};

struct PlaybackSlider
{
    std::wstring name;
    std::wstring automationId;
    double minimum = 0.0;
    double maximum = 0.0;
    bool enabled = false;
    bool visible = false;
    bool readOnly = true;
};

inline bool playback_slider_matches(const PlaybackSlider& slider,
    std::int64_t durationMilliseconds)
{
    if (!slider.enabled || !slider.visible || slider.readOnly ||
        durationMilliseconds <= 0 || !std::isfinite(slider.minimum) ||
        !std::isfinite(slider.maximum) || slider.maximum <= slider.minimum) return false;
    const auto label = media_quality_detection::normalise(slider.name + L" " + slider.automationId);
    for (const auto keyword : { L"volume", L"balance", L"speed", L"rate", L"rating", L"zoom", L"equalis", L"equaliz" })
        if (label.find(keyword) != label.npos) return false;
    bool timeline = false;
    for (const auto keyword : { L"scrub", L"seek", L"position", L"timeline", L"progress" })
        timeline |= label.find(keyword) != label.npos;
    if (!timeline) return false;
    const double span = slider.maximum - slider.minimum;
    const auto matchesRange = [span](double expected) {
        return std::abs(span - expected) <= std::max(1.0, expected * 0.015);
    };
    return matchesRange(static_cast<double>(durationMilliseconds) / 1000.0) ||
        matchesRange(static_cast<double>(durationMilliseconds)) ||
        (std::abs(slider.minimum) < 0.0001 &&
            (std::abs(slider.maximum - 100.0) < 0.0001 ||
                std::abs(slider.maximum - 1.0) < 0.0001));
}

inline std::optional<std::size_t> playback_slider_index(std::span<const PlaybackSlider> sliders,
    std::int64_t durationMilliseconds)
{
    std::optional<std::size_t> match;
    for (std::size_t index = 0u; index < sliders.size(); ++index)
    {
        if (!playback_slider_matches(sliders[index], durationMilliseconds)) continue;
        if (match) return std::nullopt;
        match = index;
    }
    return match;
}

inline double playback_slider_value(const PlaybackSlider& slider,
    std::int64_t positionMilliseconds, std::int64_t durationMilliseconds)
{
    const double progress = std::clamp(static_cast<double>(positionMilliseconds) /
        static_cast<double>(durationMilliseconds), 0.0, 1.0);
    return slider.minimum + (slider.maximum - slider.minimum) * progress;
}
}

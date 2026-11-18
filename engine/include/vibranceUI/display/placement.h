// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/glfw/window.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>




enum class DisplayTargetMode : std::uint8_t
{
    ePrimary,
    eMonitor,
    eEvery
};

struct DisplayTargetPreference
{
    DisplayTargetMode mode = DisplayTargetMode::ePrimary;

    std::string monitorId;
};

using DisplayMonitorTarget = GlfwMonitorInfo;

enum class MissingDisplayFallback : std::uint8_t
{
    ePrimary,
    eFirstNonPrimary
};

VIBRANCE_ENGINE_API std::string_view display_target_mode_code(
    DisplayTargetMode mode);
VIBRANCE_ENGINE_API DisplayTargetMode display_target_mode_from_code(
    std::string_view code,
    DisplayTargetMode fallback = DisplayTargetMode::ePrimary);
VIBRANCE_ENGINE_API bool display_target_preference_matches(
    const DisplayTargetPreference& left,
    const DisplayTargetPreference& right);

VIBRANCE_ENGINE_API std::vector<DisplayMonitorTarget>
connected_display_monitors();
VIBRANCE_ENGINE_API std::vector<DisplayMonitorTarget> resolve_display_targets(
    const DisplayTargetPreference& preference,
    const std::vector<DisplayMonitorTarget>& monitors,
    MissingDisplayFallback missingFallback =
        MissingDisplayFallback::ePrimary);
VIBRANCE_ENGINE_API std::vector<DisplayMonitorTarget> resolve_display_targets(
    const DisplayTargetPreference& preference,
    MissingDisplayFallback missingFallback =
        MissingDisplayFallback::ePrimary);
VIBRANCE_ENGINE_API bool display_monitor_target_matches(
    const DisplayMonitorTarget& left,
    const DisplayMonitorTarget& right);
VIBRANCE_ENGINE_API bool display_monitor_topology_matches(
    const std::vector<DisplayMonitorTarget>& left,
    const std::vector<DisplayMonitorTarget>& right);

struct DisplayTargetRefreshResult
{
    bool topologyChanged = false;
    bool targetsChanged = false;
};



class VIBRANCE_ENGINE_API DisplayTargetTracker
{
public:
    explicit DisplayTargetTracker(
        DisplayTargetPreference preference = {},
        MissingDisplayFallback missingFallback =
            MissingDisplayFallback::ePrimary);

    bool set_preference(DisplayTargetPreference preference);
    const DisplayTargetPreference& preference() const;
    DisplayTargetRefreshResult refresh();
    DisplayTargetRefreshResult update(
        std::vector<DisplayMonitorTarget> detectedMonitors);
    const std::vector<DisplayMonitorTarget>& monitors() const;
    const std::vector<DisplayMonitorTarget>& targets() const;

private:
    DisplayTargetPreference selectedPreference {};
    MissingDisplayFallback fallback = MissingDisplayFallback::ePrimary;
    std::vector<DisplayMonitorTarget> connectedMonitors {};
    std::vector<DisplayMonitorTarget> selectedTargets {};
};



enum class DisplayWindowEdge : std::uint8_t
{
    eTop,
    eRight,
    eBottom,
    eLeft
};

struct DisplayWindowPlacementOptions
{
    DisplayWindowEdge edge = DisplayWindowEdge::eTop;

    int thickness = 1;


    bool scaleThicknessWithDpi = false;

    int edgeInset = 0;

    int startInset = 0;
    int endInset = 0;
    bool decorated = false;
    bool alwaysOnTop = true;
};

VIBRANCE_ENGINE_API DisplayWindowPlacementOptions
display_top_strip_placement(int height);
VIBRANCE_ENGINE_API DisplayWindowPlacementOptions
display_right_sidebar_placement(int width);



VIBRANCE_ENGINE_API std::optional<GlfwWindowPlacement>
make_display_window_placement(
    const DisplayMonitorTarget& target,
    const DisplayWindowPlacementOptions& options);
VIBRANCE_ENGINE_API std::optional<GlfwWindowCreateInfo>
make_display_window_create_info(
    const DisplayMonitorTarget& target,
    const DisplayWindowPlacementOptions& options,
    const char* name,
    bool transparentFramebuffer = true);
VIBRANCE_ENGINE_API bool apply_display_window_placement(
    GLFWwindow* window,
    const DisplayMonitorTarget& target,
    const DisplayWindowPlacementOptions& options);

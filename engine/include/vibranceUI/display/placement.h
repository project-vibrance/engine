#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/glfw/window.h>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// Monitor selection is independent from the interface being displayed. A
// feature may target the primary display, one stable monitor id, or all of
// the currently connected displays.
enum class DisplayTargetMode : std::uint8_t
{
    ePrimary,
    eMonitor,
    eEvery
};

struct DisplayTargetPreference
{
    DisplayTargetMode mode = DisplayTargetMode::ePrimary;
    // Used only by eMonitor. Platform ids remain stable across layout changes.
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

// A feature owns one tracker. Trackers are independent, so the island and a
// future right sidebar can use different preferences over the same topology.
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

// Edge placement supports both the current top island and future sidebars or
// bottom/left surfaces without putting platform-specific co-ordinates in UI code.
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
    // Height for top/bottom edges and width for left/right edges.
    int thickness = 1;
    // Fixed-DPI UI dimensions are logical units; Windows window bounds use
    // physical pixels. Opt in to converting thickness with the monitor DPI.
    bool scaleThicknessWithDpi = false;
    // Gap inward from the selected monitor edge.
    int edgeInset = 0;
    // Insets along the spanning axis. Both zero means full width or height.
    int startInset = 0;
    int endInset = 0;
    bool decorated = false;
    bool alwaysOnTop = true;
};

VIBRANCE_ENGINE_API DisplayWindowPlacementOptions
display_top_strip_placement(int height);
VIBRANCE_ENGINE_API DisplayWindowPlacementOptions
display_right_sidebar_placement(int width);

// Geometry can be calculated before a native window exists, which keeps
// creation dimensions and subsequent monitor-change placement consistent.
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

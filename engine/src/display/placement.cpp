#include <vibranceUI/display/placement.h>

#include <algorithm>
#include <cmath>
#include <utility>

namespace
{
    const DisplayMonitorTarget* primary_monitor(
        const std::vector<DisplayMonitorTarget>& monitors)
    {
        const auto primary = std::find_if(
            monitors.begin(),
            monitors.end(),
            [](const DisplayMonitorTarget& monitor) {
                return monitor.primary;
            });
        return primary == monitors.end() ? &monitors.front() : &*primary;
    }

    const DisplayMonitorTarget* fallback_monitor(
        const std::vector<DisplayMonitorTarget>& monitors,
        MissingDisplayFallback fallback)
    {
        if (fallback == MissingDisplayFallback::eFirstNonPrimary)
        {
            const auto secondary = std::find_if(
                monitors.begin(),
                monitors.end(),
                [](const DisplayMonitorTarget& monitor) {
                    return !monitor.primary;
                });
            if (secondary != monitors.end())
            {
                return &*secondary;
            }
        }
        return primary_monitor(monitors);
    }

    int clamped_inset(int value, int maximum)
    {
        return std::clamp(value, 0, std::max(maximum, 0));
    }
}

std::string_view display_target_mode_code(DisplayTargetMode mode)
{
    switch (mode)
    {
        case DisplayTargetMode::eMonitor:
            return "monitor";
        case DisplayTargetMode::eEvery:
            return "every";
        default:
            return "primary";
    }
}

DisplayTargetMode display_target_mode_from_code(
    std::string_view code,
    DisplayTargetMode fallback)
{
    if (code == "primary")
    {
        return DisplayTargetMode::ePrimary;
    }
    if (code == "monitor")
    {
        return DisplayTargetMode::eMonitor;
    }
    if (code == "every")
    {
        return DisplayTargetMode::eEvery;
    }
    return fallback;
}

bool display_target_preference_matches(
    const DisplayTargetPreference& left,
    const DisplayTargetPreference& right)
{
    return left.mode == right.mode &&
        (left.mode != DisplayTargetMode::eMonitor ||
            left.monitorId == right.monitorId);
}

std::vector<DisplayMonitorTarget> connected_display_monitors()
{
    std::vector<DisplayMonitorTarget> result = glfw_connected_monitors();
    std::stable_sort(
        result.begin(),
        result.end(),
        [](const DisplayMonitorTarget& left,
            const DisplayMonitorTarget& right) {
            return left.primary && !right.primary;
        });
    return result;
}

std::vector<DisplayMonitorTarget> resolve_display_targets(
    const DisplayTargetPreference& preference,
    const std::vector<DisplayMonitorTarget>& monitors,
    MissingDisplayFallback missingFallback)
{
    if (monitors.empty() || preference.mode == DisplayTargetMode::eEvery)
    {
        return monitors;
    }
    if (preference.mode == DisplayTargetMode::eMonitor &&
        !preference.monitorId.empty())
    {
        const auto selected = std::find_if(
            monitors.begin(),
            monitors.end(),
            [&preference](const DisplayMonitorTarget& monitor) {
                return monitor.id == preference.monitorId;
            });
        if (selected != monitors.end())
        {
            return { *selected };
        }
    }
    if (preference.mode == DisplayTargetMode::eMonitor)
    {
        return { *fallback_monitor(monitors, missingFallback) };
    }
    return { *primary_monitor(monitors) };
}

std::vector<DisplayMonitorTarget> resolve_display_targets(
    const DisplayTargetPreference& preference,
    MissingDisplayFallback missingFallback)
{
    return resolve_display_targets(
        preference,
        connected_display_monitors(),
        missingFallback);
}

bool display_monitor_target_matches(
    const DisplayMonitorTarget& left,
    const DisplayMonitorTarget& right)
{
    return left.id == right.id &&
        left.position.x == right.position.x &&
        left.position.y == right.position.y &&
        left.size.x == right.size.x &&
        left.size.y == right.size.y &&
        left.workPosition == right.workPosition &&
        left.workSize == right.workSize &&
        left.contentScale == right.contentScale;
}

bool display_monitor_topology_matches(
    const std::vector<DisplayMonitorTarget>& left,
    const std::vector<DisplayMonitorTarget>& right)
{
    if (left.size() != right.size())
    {
        return false;
    }
    for (std::size_t index = 0u; index < left.size(); ++index)
    {
        if (!display_monitor_target_matches(left[index], right[index]) ||
            left[index].primary != right[index].primary ||
            left[index].name != right[index].name)
        {
            return false;
        }
    }
    return true;
}

DisplayTargetTracker::DisplayTargetTracker(
    DisplayTargetPreference preference,
    MissingDisplayFallback missingFallback) :
    selectedPreference(std::move(preference)),
    fallback(missingFallback)
{
}

bool DisplayTargetTracker::set_preference(
    DisplayTargetPreference preference)
{
    if (display_target_preference_matches(
            selectedPreference,
            preference))
    {
        return false;
    }
    selectedPreference = std::move(preference);
    return true;
}

const DisplayTargetPreference& DisplayTargetTracker::preference() const
{
    return selectedPreference;
}

DisplayTargetRefreshResult DisplayTargetTracker::refresh()
{
    return update(connected_display_monitors());
}

DisplayTargetRefreshResult DisplayTargetTracker::update(
    std::vector<DisplayMonitorTarget> detectedMonitors)
{
    std::vector<DisplayMonitorTarget> nextTargets =
        resolve_display_targets(
            selectedPreference,
            detectedMonitors,
            fallback);
    DisplayTargetRefreshResult result = {};
    result.topologyChanged = !display_monitor_topology_matches(
        connectedMonitors,
        detectedMonitors);
    result.targetsChanged = !display_monitor_topology_matches(
        selectedTargets,
        nextTargets);
    connectedMonitors = std::move(detectedMonitors);
    selectedTargets = std::move(nextTargets);
    return result;
}

const std::vector<DisplayMonitorTarget>&
DisplayTargetTracker::monitors() const
{
    return connectedMonitors;
}

const std::vector<DisplayMonitorTarget>&
DisplayTargetTracker::targets() const
{
    return selectedTargets;
}

DisplayWindowPlacementOptions display_top_strip_placement(int height)
{
    DisplayWindowPlacementOptions options = {};
    options.edge = DisplayWindowEdge::eTop;
    options.thickness = height;
    return options;
}

DisplayWindowPlacementOptions display_right_sidebar_placement(int width)
{
    DisplayWindowPlacementOptions options = {};
    options.edge = DisplayWindowEdge::eRight;
    options.thickness = width;
    return options;
}

std::optional<GlfwWindowPlacement> make_display_window_placement(
    const DisplayMonitorTarget& target,
    const DisplayWindowPlacementOptions& options)
{
    if (target.size.x <= 0 || target.size.y <= 0 ||
        options.thickness <= 0)
    {
        return std::nullopt;
    }

    const bool horizontalEdge =
        options.edge == DisplayWindowEdge::eTop ||
        options.edge == DisplayWindowEdge::eBottom;
    const int span = horizontalEdge ? target.size.x : target.size.y;
    const int depth = horizontalEdge ? target.size.y : target.size.x;
    const int startInset = clamped_inset(options.startInset, span - 1);
    const int endInset = clamped_inset(
        options.endInset,
        span - startInset - 1);
    const int edgeInset = clamped_inset(options.edgeInset, depth - 1);
    const int availableSpan = std::max(1, span - startInset - endInset);
    float thicknessScale = 1.0f;
#if defined(_WIN32)
    // Match make_fixed_layout_scale. macOS window bounds are points and its
    // framebuffer already supplies the backing scale, so do not scale twice.
    if (options.scaleThicknessWithDpi)
    {
        thicknessScale = std::max(
            std::min(target.contentScale.x, target.contentScale.y), 0.25f);
    }
#endif
    const int thickness = std::clamp(
        static_cast<int>(std::ceil(options.thickness * thicknessScale)),
        1,
        std::max(1, depth - edgeInset));

    GlfwWindowPlacement placement = {};
    placement.decorated = options.decorated;
    placement.alwaysOnTop = options.alwaysOnTop;
    switch (options.edge)
    {
        case DisplayWindowEdge::eRight:
            placement.position = {
                target.position.x + target.size.x - edgeInset - thickness,
                target.position.y + startInset
            };
            placement.size = { thickness, availableSpan };
            break;
        case DisplayWindowEdge::eBottom:
            placement.position = {
                target.position.x + startInset,
                target.position.y + target.size.y - edgeInset - thickness
            };
            placement.size = { availableSpan, thickness };
            break;
        case DisplayWindowEdge::eLeft:
            placement.position = {
                target.position.x + edgeInset,
                target.position.y + startInset
            };
            placement.size = { thickness, availableSpan };
            break;
        case DisplayWindowEdge::eTop:
        default:
            placement.position = {
                target.position.x + startInset,
                target.position.y + edgeInset
            };
            placement.size = { availableSpan, thickness };
            break;
    }
    return placement;
}

std::optional<GlfwWindowCreateInfo> make_display_window_create_info(
    const DisplayMonitorTarget& target,
    const DisplayWindowPlacementOptions& options,
    const char* name,
    bool transparentFramebuffer)
{
    const std::optional<GlfwWindowPlacement> placement =
        make_display_window_placement(target, options);
    if (!placement)
    {
        return std::nullopt;
    }
    GlfwWindowCreateInfo createInfo = {};
    createInfo.width = placement->size.x;
    createInfo.height = placement->size.y;
    createInfo.name = name;
    createInfo.transparentFramebuffer = transparentFramebuffer;
    createInfo.decorated = placement->decorated;
    createInfo.alwaysOnTop = placement->alwaysOnTop;
    return createInfo;
}

bool apply_display_window_placement(
    GLFWwindow* window,
    const DisplayMonitorTarget& target,
    const DisplayWindowPlacementOptions& options)
{
    if (!window || !target.handle)
    {
        return false;
    }
    const std::optional<GlfwWindowPlacement> placement =
        make_display_window_placement(target, options);
    return placement && apply_glfw_window_placement(window, *placement);
}

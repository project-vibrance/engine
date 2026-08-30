#include <vibranceUI/display/display.h>

#include <filesystem>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "display placement test failed: " << message << '\n';
        }
        return condition;
    }

    bool same_point(glm::ivec2 value, int x, int y)
    {
        return value.x == x && value.y == y;
    }
}

int main()
{
    DisplayMonitorTarget primary = {};
    primary.id = "primary";
    primary.name = "Primary display";
    primary.position = { 0, 0 };
    primary.size = { 1920, 1080 };
    primary.primary = true;

    DisplayMonitorTarget secondary = {};
    secondary.id = "secondary";
    secondary.name = "Secondary display";
    secondary.position = { 1920, -120 };
    secondary.size = { 2560, 1440 };

    const std::vector<DisplayMonitorTarget> monitors {
        primary,
        secondary
    };
    bool passed = true;

    const glm::ivec2 centeredWindow = glfw_aligned_window_position(
        { 1920, -120 },
        { 2560, 1400 },
        { 1000, 850 },
        GlfwWindowAlignment::eCenter);
    passed &= expect(
        same_point(centeredWindow, 2700, 155),
        "center alignment should use the monitor work area");

    const glm::ivec2 offsetTopLeft = glfw_aligned_window_position(
        { 1920, -120 },
        { 2560, 1400 },
        { 1000, 850 },
        GlfwWindowAlignment::eTopLeft,
        { 20, 30, 40, 50 },
        { 15, 25 });
    passed &= expect(
        same_point(offsetTopLeft, 1955, -65),
        "alignment should compose margins and caller offsets");

    const glm::ivec2 clampedBottomRight = glfw_aligned_window_position(
        { 1920, -120 },
        { 2560, 1400 },
        { 1000, 850 },
        GlfwWindowAlignment::eBottomRight,
        { 20, 30, 40, 50 },
        { 500, 500 });
    passed &= expect(
        same_point(clampedBottomRight, 3440, 380),
        "aligned windows should remain inside the inset work area");

    const std::vector<DisplayMonitorTarget> primaryTargets =
        resolve_display_targets(
            { DisplayTargetMode::ePrimary, {} },
            monitors);
    passed &= expect(
        primaryTargets.size() == 1u &&
            primaryTargets.front().id == primary.id,
        "primary mode should select the primary monitor");

    const std::vector<DisplayMonitorTarget> selectedTargets =
        resolve_display_targets(
            { DisplayTargetMode::eMonitor, secondary.id },
            monitors);
    passed &= expect(
        selectedTargets.size() == 1u &&
            selectedTargets.front().id == secondary.id,
        "monitor mode should select a stable monitor id");

    const std::vector<DisplayMonitorTarget> everyTargets =
        resolve_display_targets(
            { DisplayTargetMode::eEvery, {} },
            monitors);
    passed &= expect(
        everyTargets.size() == monitors.size(),
        "every mode should retain an arbitrary monitor count");

    const std::vector<DisplayMonitorTarget> missingPrimaryFallback =
        resolve_display_targets(
            { DisplayTargetMode::eMonitor, "disconnected" },
            monitors);
    passed &= expect(
        missingPrimaryFallback.size() == 1u &&
            missingPrimaryFallback.front().id == primary.id,
        "new interfaces should safely fall back to primary");

    const std::vector<DisplayMonitorTarget> missingSecondaryFallback =
        resolve_display_targets(
            { DisplayTargetMode::eMonitor, "disconnected" },
            monitors,
            MissingDisplayFallback::eFirstNonPrimary);
    passed &= expect(
        missingSecondaryFallback.size() == 1u &&
            missingSecondaryFallback.front().id == secondary.id,
        "legacy island behavior should retain its secondary fallback");

    DisplayTargetTracker sidebarTargets(
        { DisplayTargetMode::eMonitor, secondary.id });
    const DisplayTargetRefreshResult initialRefresh =
        sidebarTargets.update(monitors);
    passed &= expect(
        initialRefresh.topologyChanged && initialRefresh.targetsChanged &&
            sidebarTargets.targets().size() == 1u &&
            sidebarTargets.targets().front().id == secondary.id,
        "a feature tracker should detect topology and resolve its own target");
    sidebarTargets.set_preference({ DisplayTargetMode::eEvery, {} });
    const DisplayTargetRefreshResult everyRefresh =
        sidebarTargets.update(monitors);
    passed &= expect(
        !everyRefresh.topologyChanged && everyRefresh.targetsChanged &&
            sidebarTargets.targets().size() == monitors.size(),
        "a tracker should report selection changes independently of topology");

    const std::optional<GlfwWindowPlacement> topPlacement =
        make_display_window_placement(
            secondary,
            display_top_strip_placement(300));
    passed &= expect(
        topPlacement &&
            same_point(topPlacement->position, 1920, -120) &&
            same_point(topPlacement->size, 2560, 300) &&
            !topPlacement->decorated && topPlacement->alwaysOnTop,
        "top placement should span the monitor width without decoration");

    const std::optional<GlfwWindowPlacement> rightPlacement =
        make_display_window_placement(
            secondary,
            display_right_sidebar_placement(420));
    passed &= expect(
        rightPlacement &&
            same_point(rightPlacement->position, 4060, -120) &&
            same_point(rightPlacement->size, 420, 1440) &&
            !rightPlacement->decorated && rightPlacement->alwaysOnTop,
        "right placement should anchor to the monitor edge at full height");

    const std::optional<GlfwWindowCreateInfo> rightCreateInfo =
        make_display_window_create_info(
            secondary,
            display_right_sidebar_placement(420),
            "Right interface",
            true);
    passed &= expect(
        rightCreateInfo && rightCreateInfo->width == 420 &&
            rightCreateInfo->height == 1440 &&
            !rightCreateInfo->decorated && rightCreateInfo->alwaysOnTop &&
            rightCreateInfo->transparentFramebuffer,
        "right window should be created borderless at its final dimensions");

    DisplayWindowPlacementOptions insetRight =
        display_right_sidebar_placement(420);
    insetRight.edgeInset = 10;
    insetRight.startInset = 20;
    insetRight.endInset = 30;
    const std::optional<GlfwWindowPlacement> insetPlacement =
        make_display_window_placement(secondary, insetRight);
    passed &= expect(
        insetPlacement &&
            same_point(insetPlacement->position, 4050, -100) &&
            same_point(insetPlacement->size, 420, 1390),
        "right placement should support safe edge and span insets");

    const std::filesystem::path preferencePath =
        std::filesystem::current_path() /
        "display-placement-test-preferences.json";
    DisplayPreferenceStorage sidebarStorage = {};
    sidebarStorage.path = preferencePath;
    sidebarStorage.modeKey = "sidebarDisplay";
    sidebarStorage.monitorKey = "sidebarMonitor";
    const DisplayTargetPreference sidebarPreference {
        DisplayTargetMode::eMonitor,
        secondary.id
    };
    passed &= expect(
        save_display_target_preference(
            sidebarStorage,
            sidebarPreference),
        "a future interface should save an independent monitor preference");
    passed &= expect(
        display_target_preference_matches(
            load_display_target_preference(sidebarStorage),
            sidebarPreference),
        "a future interface should reload its independent monitor preference");
    std::error_code removeError;
    std::filesystem::remove(preferencePath, removeError);

    return passed ? 0 : 1;
}

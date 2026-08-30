#include <vibranceUI/ui/context_menu.h>
#include <vibranceUI/ui/dialog_controls.h>

#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "popup controls test failed: "
                      << message << '\n';
        }
        return condition;
    }

    bool close(float left, float right)
    {
        return std::abs(left - right) <= 0.001f;
    }
}

int main()
{
    bool passed = true;

    UiContextMenuItem first = {};
    first.id = "first";
    first.label = "First";
    UiContextMenuItem heading = {};
    heading.id = "heading";
    heading.label = "Section";
    heading.role = UiContextMenuItemRole::eHeading;
    heading.separatorBefore = true;
    UiContextMenuItem last = {};
    last.id = "last";
    last.label = "Last";

    UiContextMenuMetrics metrics = {};
    metrics.width = 300.0f;
    metrics.rowHeight = 30.0f;
    metrics.headingHeight = 20.0f;
    metrics.rowGap = 2.0f;
    metrics.separatorGap = 10.0f;
    metrics.padding = glm::vec4(8.0f);
    const UiContextMenuLayout layout = ui_context_menu_layout(
        { first, heading, last },
        metrics);
    passed &= expect(
        layout.rows.size() == 3u,
        "every menu item should receive a measured row");
    passed &= expect(
        close(layout.rows[0].y, 0.0f) &&
            close(layout.rows[1].y, 42.0f) &&
            close(layout.rows[2].y, 64.0f),
        "headings and separator gaps should advance the vertical stack");
    passed &= expect(
        layout.rows[1].separatorY &&
            close(*layout.rows[1].separatorY, 37.0f),
        "separator placement should stay centred in its extra gap");
    passed &= expect(
        close(layout.size.x, 300.0f) &&
            close(layout.size.y, 110.0f),
        "menu measurement should include vertical surface padding");

    const UiContextMenuOptions compact =
        ui_compact_context_menu_options();
    passed &= expect(
        close(compact.metrics.width, 168.0f) &&
            close(compact.metrics.rowHeight, 20.0f) &&
            close(compact.metrics.rowGap, 2.0f) &&
            close(compact.metrics.separatorGap, 4.0f) &&
            close(compact.fontSize, 12.0f),
        "compact context menus should share the tray menu dimensions");

    passed &= expect(
        ui_sanitise_verification_code("1a2-34x567", 6u) == "123456",
        "verification input should keep only the requested ASCII digits");
    passed &= expect(
        ui_sanitise_verification_code("9876", 0u).empty(),
        "a zero-length verification code should stay empty");
    return passed ? 0 : 1;
}

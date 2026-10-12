#include <vibranceUI/ui/context_menu.h>
#include <vibranceUI/ui/dialog_controls.h>
#include <vibranceUI/ui/liquid_controls.h>
#include <vibranceUI/glfw/panel_window.h>

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
    for (const float magnification : { 1.0f, 2.0f })
    {
        Renderer2DScene scene;
        LayoutScale scale;
        scale.factor = glm::vec2(magnification);
        UiBuilder ui(scene, scale);
        Renderer2DFontAtlas atlas;
        const auto parent = scene.create_shape({}, { 1200.0f, 1200.0f });
        const auto slider = ui_create_slider(ui, parent, UiAlignment::eTopLeft,
            {}, { 100.0f, 7.0f }, 2, 0u);
        auto& drag = scene.registry().get<SliderInputComponent>(slider.root);
        drag.liquidThumbScale = 1.12f;
        drag.liquidThumbVelocity = 0.25f;
        UiConfirmationDialogOptions options;
        options.theme = ui_dark_theme();
        options.title = "Reset preferences?";
        unsigned confirmations = 0, cancellations = 0;
        options.onConfirm = [&] { ++confirmations; };
        options.onCancel = [&] { ++cancellations; };
        const auto plain = ui_create_confirmation_dialog(ui, atlas, parent,
            UiAlignment::eCenter, {}, options);
        options.icon.id = 1u;
        const auto illustrated = ui_create_confirmation_dialog(ui, atlas, parent,
            UiAlignment::eCenter, {}, options);
        passed &= expect(close(drag.liquidThumbScale, 1.12f) &&
            close(drag.liquidThumbVelocity, 0.25f),
            "creating glass actions must not reset another control's active drag");
        auto& registry = scene.registry();
        passed &= expect(plain.iconSlot == entt::null && illustrated.iconSlot != entt::null,
            "iconless alerts must not create an empty icon placeholder");
        const auto& plainTitle = registry.get<Layout2DComponent>(plain.titleText);
        const auto& iconTitle = registry.get<Layout2DComponent>(illustrated.titleText);
        passed &= expect(close(plainTitle.offset.y, 28.0f * magnification) &&
            close(iconTitle.offset.y - plainTitle.offset.y, 84.0f * magnification),
            "only illustrated alerts should reserve the icon and its gap at every scale");
        const auto& window = GlfwPanelWindowTemplateOptions{};
        const auto& shape = registry.get<ShapeComponent>(plain.root);
        passed &= expect(shape.cornerRadius > window.cornerRadius * magnification &&
            close(shape.cornerRadius, UiDialogShape::radius * magnification),
            "alerts need their broader native corners without reshaping the main window");
        const auto& primary = registry.get<Layout2DComponent>(plain.confirmButton);
        const auto& secondary = registry.get<Layout2DComponent>(plain.cancelButton);
        passed &= expect(primary.offset.x > secondary.offset.x &&
            close(primary.offset.y, secondary.offset.y),
            "two-action alerts should keep secondary left and primary right");
        passed &= expect(registry.get<TextStyleComponent>(plain.cancelLabel).color0 ==
            renderer2d_hex_color(options.theme.text),
            "secondary labels should use the chosen theme");
        const auto& buttonPalette =
#if defined(__APPLE__)
            *registry.get<UiLiquidControlPaint>(plain.cancelButton).buttonVisual;
#else
            registry.get<ButtonVisualComponent>(plain.cancelButton);
#endif
        passed &= expect(buttonPalette.disabled.color0 ==
            renderer2d_hex_color(options.theme.surfaceMuted),
            "disabled actions should retain the chosen theme");
        ui_update_liquid_controls(scene, false, ui_liquid_glass_options());
        passed &= expect(registry.any_of<LiquidGlassComponent>(plain.cancelButton) &&
            registry.any_of<LiquidGlassComponent>(plain.confirmButton),
            "dialog buttons must retain their own Liquid Glass when island glass is disabled");
        registry.get<ButtonInputComponent>(plain.confirmButton).onClick({});
        registry.get<ButtonInputComponent>(plain.cancelButton).onClick({});
        passed &= expect(confirmations == 1 && cancellations == 1,
            "reshaping alerts must preserve both action callbacks");
        options.cancelLabel.clear();
        const auto single = ui_create_confirmation_dialog(ui, atlas, parent,
            UiAlignment::eCenter, {}, options);
        passed &= expect(single.cancelButton == entt::null,
            "single-action alerts must not reserve an unused secondary action");
        options.icon = {};
        options.cancelLabel = "Cancel";
        options.confirmLabel = "OK";
        options.additionalLabel = "OK. Don't Show Again";
        unsigned remembered = 0u;
        options.onAdditional = [&] { ++remembered; };
        const auto three = ui_create_confirmation_dialog(ui, atlas, parent,
            UiAlignment::eCenter, {}, options);
        const auto& accept = registry.get<Layout2DComponent>(three.confirmButton);
        const auto& remember = registry.get<Layout2DComponent>(three.additionalButton);
        const auto& cancel = registry.get<Layout2DComponent>(three.cancelButton);
        passed &= expect(close(accept.offset.x, 0.0f) &&
            close(remember.offset.x, 0.0f) && close(cancel.offset.x, 0.0f) &&
            accept.offset.y < remember.offset.y && remember.offset.y < cancel.offset.y &&
            close(remember.offset.y - accept.offset.y, 50.0f * magnification),
            "three-action alerts should stack full-width actions in their requested order");
        passed &= expect(close(registry.get<ShapeComponent>(three.root).size.y -
            registry.get<ShapeComponent>(plain.root).size.y, 100.0f * magnification),
            "three-action alerts should reserve both extra rows at every scale");
        registry.get<ButtonInputComponent>(three.additionalButton).onClick({});
        passed &= expect(remembered == 1u,
            "the additional confirmation action should invoke its own callback");
    }
    return passed ? 0 : 1;
}

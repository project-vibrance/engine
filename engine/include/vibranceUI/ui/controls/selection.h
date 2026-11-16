// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>

struct UiRadioControlComponent
{
    bool checked = false;
    entt::entity outer = entt::null;
    entt::entity indicator = entt::null;
    entt::entity label = entt::null;
    std::string groupId;
    bool singleSelect = false;
    ShapeStyleComponent enabledIdle;
    ShapeStyleComponent enabledHovered;
    ShapeStyleComponent enabledPressed;
    ShapeStyleComponent disabled;
    ShapeStyleComponent enabledIndicator;
    ShapeStyleComponent disabledIndicator;
    TextStyleComponent enabledLabel;
    TextStyleComponent disabledLabel;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

struct UiRadioOptions
{

    bool checked = false;
    bool enabled = true;
    bool singleSelect = false;
    std::string groupId;
    std::string enabledColor = "#0D6FFFFF";
    std::string hoveredColor = "#2A82FFFF";
    std::string pressedColor = "#0061D5FF";
    std::string disabledColor = "#B7BBC2FF";
    std::string outlineColor = "#00000014";
    std::string disabledOutlineColor = "#0000000F";
    std::string indicatorColor = "#FFFFFFFF";
    std::string disabledIndicatorColor = "#F7F7F7FF";
    std::string labelColor = "#1D1D1FFF";
    std::string disabledLabelColor = "#92969DFF";
    float diameter = 14.0f;
    float indicatorDiameter = 6.0f;
    float outlineWidth = 0.8f;
    float disabledOutlineWidth = 0.6f;
    float labelFontSize = 13.0f;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

struct UiCheckboxControlComponent
{
    bool checked = false;
    entt::entity box = entt::null;
    entt::entity indicator = entt::null;
    ShapeStyleComponent uncheckedStyle;
    ShapeStyleComponent checkedStyle;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

struct UiCheckboxOptions
{

    bool checked = false;
    bool enabled = true;
    Media2DHandle checkIcon {};
    std::string uncheckedColor = "#FFFFFFFF";
    std::string checkedColor = "#0D6FFFFF";
    std::string outlineColor = "#8D8D92FF";
    std::string checkTint = "#FFFFFFFF";
    std::string labelColor = "#1D1D1FFF";
    float labelFontSize = 13.0f;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

inline void ui_update_radio_visual(
    Renderer2DScene& scene,
    entt::entity entity)
{
    entt::registry& registry = scene.registry();
    UiRadioControlComponent* control =
        registry.try_get<UiRadioControlComponent>(entity);
    ButtonInputComponent* button =
        registry.try_get<ButtonInputComponent>(entity);
    if (!control || !button)
    {
        return;
    }

    const bool enabled = button->enabled;
    if (control->outer != entt::null && registry.valid(control->outer))
    {
        if (ShapeStyleComponent* style =
                registry.try_get<ShapeStyleComponent>(control->outer))
        {
            if (!enabled)
            {
                *style = control->disabled;
            }
            else if (button->leftPressed || button->rightPressed)
            {
                *style = control->enabledPressed;
            }
            else if (button->hovered)
            {
                *style = control->enabledHovered;
            }
            else
            {
                *style = control->enabledIdle;
            }
        }
        scene.mark_dirty(control->outer);
    }

    if (control->indicator != entt::null &&
        registry.valid(control->indicator))
    {
        if (ShapeStyleComponent* style =
                registry.try_get<ShapeStyleComponent>(control->indicator))
        {
            *style = enabled ?
                control->enabledIndicator :
                control->disabledIndicator;
            style->opacity = control->checked ? 1.0f : 0.0f;
        }
        scene.mark_dirty(control->indicator);
    }

    if (control->label != entt::null && registry.valid(control->label))
    {
        if (TextStyleComponent* style =
                registry.try_get<TextStyleComponent>(control->label))
        {
            *style = enabled ? control->enabledLabel : control->disabledLabel;
        }
        scene.mark_dirty(control->label);
    }
}

inline void ui_set_radio_checked(
    Renderer2DScene& scene,
    entt::entity entity,
    bool checked)
{
    entt::registry& registry = scene.registry();
    UiRadioControlComponent* control = registry.try_get<UiRadioControlComponent>(entity);
    if (!control)
    {
        return;
    }

    control->checked = checked;
    if (control->indicator != entt::null && registry.valid(control->indicator))
    {
        if (ShapeStyleComponent* indicatorStyle = registry.try_get<ShapeStyleComponent>(control->indicator))
        {
            indicatorStyle->opacity = checked ? 1.0f : 0.0f;
        }
        scene.mark_dirty(control->indicator);
    }
    ui_update_radio_visual(scene, entity);
    scene.mark_dirty(entity);
}

inline void ui_set_radio_enabled(
    Renderer2DScene& scene,
    entt::entity entity,
    bool enabled)
{
    ButtonInputComponent* button =
        scene.registry().try_get<ButtonInputComponent>(entity);
    if (!button)
    {
        return;
    }

    button->enabled = enabled;
    if (!enabled)
    {
        button->hovered = false;
        button->leftPressed = false;
        button->rightPressed = false;
    }
    ui_update_radio_visual(scene, entity);
    ui_update_button_visual(scene, entity);
}

inline void ui_set_checkbox_checked(
    Renderer2DScene& scene,
    entt::entity entity,
    bool checked)
{
    entt::registry& registry = scene.registry();
    UiCheckboxControlComponent* control = registry.try_get<UiCheckboxControlComponent>(entity);
    if (!control)
    {
        return;
    }

    control->checked = checked;
    if (control->box != entt::null && registry.valid(control->box))
    {
        if (ShapeStyleComponent* boxStyle = registry.try_get<ShapeStyleComponent>(control->box))
        {
            *boxStyle = checked ? control->checkedStyle : control->uncheckedStyle;
        }
        scene.mark_dirty(control->box);
    }
    if (control->indicator != entt::null && registry.valid(control->indicator))
    {
        if (RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(control->indicator))
        {
            layer->visible = checked;
        }
        if (ShapeStyleComponent* indicatorStyle = registry.try_get<ShapeStyleComponent>(control->indicator))
        {
            indicatorStyle->opacity = checked ? 1.0f : 0.0f;
        }
        scene.mark_dirty(control->indicator);
    }
    scene.mark_dirty(entity);
}

inline UiControlHandle ui_create_radio(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    std::string label,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiRadioOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        ui_clear_surface_style(0.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, 6.0f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(handle.root, parent, alignment, scaled_offset(offset.x, offset.y, ui.scale()), scaled_size(size.x, size.y, ui.scale()));

    const float diameter = std::max(options.diameter, 8.0f);
    const float indicatorDiameter = std::clamp(
        options.indicatorDiameter,
        2.0f,
        diameter - 2.0f);
    const ShapeStyleComponent enabledIdle = ui_macos26_control_style(
        ui,
        options.enabledColor,
        options.outlineColor,
        options.outlineWidth,
        1.0f);
    const ShapeStyleComponent enabledHovered = ui_macos26_control_style(
        ui,
        options.hoveredColor,
        options.outlineColor,
        options.outlineWidth,
        1.0f);
    const ShapeStyleComponent enabledPressed = ui_macos26_control_style(
        ui,
        options.pressedColor,
        options.outlineColor,
        options.outlineWidth,
        1.0f);
    const ShapeStyleComponent disabled = ui_macos26_control_style(
        ui,
        options.disabledColor,
        options.disabledOutlineColor,
        options.disabledOutlineWidth,
        1.0f);

    entt::entity outer = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(diameter, diameter, ui.scale()),
        options.enabled ? enabledIdle : disabled,
        Renderer2DPrimitive::eEllipse);
    ui.set_layer(outer, layer + 1, order);
    ui.attach_aligned(
        outer,
        handle.root,
        UiAlignment::eMiddleLeft,
        scaled_offset(0.0f, 0.0f, ui.scale()),
        scaled_size(diameter, diameter, ui.scale()));

    handle.indicator = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(indicatorDiameter, indicatorDiameter, ui.scale()),
        make_solid_style(
            options.enabled ?
                options.indicatorColor :
                options.disabledIndicatorColor,
            "#00000000",
            0.0f,
            options.checked ? 1.0f : 0.0f),
        Renderer2DPrimitive::eEllipse);
    ui.set_layer(handle.indicator, layer + 2, order);
    ui.attach_aligned(
        handle.indicator,
        outer,
        UiAlignment::eCenter,
        glm::vec2(0.0f),
        scaled_size(indicatorDiameter, indicatorDiameter, ui.scale()));

    const TextStyleComponent enabledLabel =
        ui_macos26_text_style(options.labelColor);
    const TextStyleComponent disabledLabel =
        ui_macos26_text_style(options.disabledLabelColor);

    handle.label = ui.create_aligned_text(
        std::move(label),
        fontAtlas,
        handle.root,
        UiAlignment::eMiddleLeft,
        options.labelFontSize,
        options.enabled ? enabledLabel : disabledLabel,
        layer + 1,
        order + 1u,
        { diameter + 8.0f, 0.0f });

    UiRadioControlComponent control = {};
    control.checked = options.checked;
    control.outer = outer;
    control.indicator = handle.indicator;
    control.label = handle.label;
    control.groupId = options.groupId;
    if (options.singleSelect && control.groupId.empty())
    {
        control.groupId = "parent:" + std::to_string(static_cast<uint32_t>(entt::to_integral(parent)));
    }
    control.singleSelect = options.singleSelect;
    control.enabledIdle = enabledIdle;
    control.enabledHovered = enabledHovered;
    control.enabledPressed = enabledPressed;
    control.disabled = disabled;
    control.enabledIndicator = make_solid_style(
        options.indicatorColor,
        "#00000000",
        0.0f,
        options.checked ? 1.0f : 0.0f);
    control.disabledIndicator = make_solid_style(
        options.disabledIndicatorColor,
        "#00000000",
        0.0f,
        options.checked ? 1.0f : 0.0f);
    control.enabledLabel = enabledLabel;
    control.disabledLabel = disabledLabel;
    control.onChanged = std::move(options.onChanged);
    registry.emplace<UiRadioControlComponent>(handle.root, std::move(control));

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onPress = [scenePtr = &scene, entity = handle.root](
                         const PointerInputEvent&) {
        ui_update_radio_visual(*scenePtr, entity);
    };
    button.onRelease = [scenePtr = &scene, entity = handle.root](
                           const PointerInputEvent&) {
        ui_update_radio_visual(*scenePtr, entity);
    };
    button.onHoverChanged = [scenePtr = &scene, entity = handle.root](
                                bool) {
        ui_update_radio_visual(*scenePtr, entity);
    };
    button.onClick = [scenePtr = &scene, entity = handle.root](const PointerInputEvent& event) {
        entt::registry& capturedRegistry = scenePtr->registry();
        UiRadioControlComponent* captured = capturedRegistry.try_get<UiRadioControlComponent>(entity);
        if (!captured)
        {
            return;
        }
        if (captured->singleSelect && captured->checked)
        {
            return;
        }

        const bool nextChecked = captured->singleSelect ? true : !captured->checked;
        if (captured->singleSelect)
        {
            const std::string groupId = captured->groupId;
            auto view = capturedRegistry.view<UiRadioControlComponent>();
            view.each([&](entt::entity otherEntity, UiRadioControlComponent& other) {
                if (otherEntity == entity || !other.singleSelect || other.groupId != groupId || !other.checked)
                {
                    return;
                }

                auto callback = other.onChanged;
                ui_set_radio_checked(*scenePtr, otherEntity, false);
                if (callback)
                {
                    callback(false, event);
                }
            });
        }

        auto callback = captured->onChanged;
        ui_set_radio_checked(*scenePtr, entity, nextChecked);
        if (callback)
        {
            callback(nextChecked, event);
        }
    };
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, ui_transparent_button_visual());
    ui_update_radio_visual(scene, handle.root);
    return handle;
}

inline UiControlHandle ui_create_checkbox(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    std::string label,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiCheckboxOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        ui_clear_surface_style(0.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, 6.0f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(handle.root, parent, alignment, scaled_offset(offset.x, offset.y, ui.scale()), scaled_size(size.x, size.y, ui.scale()));

    const ShapeStyleComponent uncheckedStyle = ui_macos26_control_style(
        ui,
        options.uncheckedColor,
        options.outlineColor,
        1.0f,
        1.0f);
    const ShapeStyleComponent checkedStyle = ui_macos26_control_style(
        ui,
        options.checkedColor,
        options.checkedColor,
        1.0f,
        1.0f);

    handle.caret = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(16.0f, 16.0f, ui.scale()),
        options.checked ? checkedStyle : uncheckedStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.caret, 5.0f);
    ui.set_layer(handle.caret, layer + 1, order);
    ui.attach_aligned(handle.caret, handle.root, UiAlignment::eMiddleLeft, glm::vec2(0.0f), scaled_size(16.0f, 16.0f, ui.scale()));

    if (options.checkIcon.valid())
    {
        handle.indicator = ui.create_aligned_media(
            options.checkIcon,
            handle.caret,
            UiAlignment::eCenter,
            { 12.0f, 12.0f },
            layer + 2,
            order);
        ui_tint_icon_media(scene, handle.indicator, options.checkTint);
    }
    else
    {
        handle.indicator = scene.create_shape(
            { 0.0f, 0.0f },
            scaled_size(8.0f, 8.0f, ui.scale()),
            make_solid_style(options.checkTint, "#00000000", 0.0f, options.checked ? 1.0f : 0.0f),
            Renderer2DPrimitive::eSquircle);
        ui.set_shape(handle.indicator, 3.0f);
        ui.set_layer(handle.indicator, layer + 2, order);
        ui.attach_aligned(handle.indicator, handle.caret, UiAlignment::eCenter, glm::vec2(0.0f), scaled_size(8.0f, 8.0f, ui.scale()));
    }

    if (RenderLayer2DComponent* indicatorLayer = registry.try_get<RenderLayer2DComponent>(handle.indicator))
    {
        indicatorLayer->visible = options.checked;
    }

    handle.label = ui.create_aligned_text(
        std::move(label),
        fontAtlas,
        handle.root,
        UiAlignment::eMiddleLeft,
        options.labelFontSize,
        ui_macos26_text_style(options.labelColor),
        layer + 1,
        order + 1u,
        { 24.0f, 0.0f });

    UiCheckboxControlComponent control = {};
    control.checked = options.checked;
    control.box = handle.caret;
    control.indicator = handle.indicator;
    control.uncheckedStyle = uncheckedStyle;
    control.checkedStyle = checkedStyle;
    control.onChanged = std::move(options.onChanged);
    registry.emplace<UiCheckboxControlComponent>(handle.root, std::move(control));

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onClick = [scenePtr = &scene, entity = handle.root](const PointerInputEvent& event) {
        UiCheckboxControlComponent* captured = scenePtr->registry().try_get<UiCheckboxControlComponent>(entity);
        if (!captured)
        {
            return;
        }

        const bool nextChecked = !captured->checked;
        auto callback = captured->onChanged;
        ui_set_checkbox_checked(*scenePtr, entity, nextChecked);
        if (callback)
        {
            callback(nextChecked, event);
        }
    };
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, ui_transparent_button_visual());
    return handle;
}


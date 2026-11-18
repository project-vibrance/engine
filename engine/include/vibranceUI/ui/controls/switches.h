// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>

struct UiSwitchControlComponent
{
    bool checked = false;
    entt::entity knob = entt::null;
    glm::vec2 offOffset { 0.0f };
    glm::vec2 onOffset { 0.0f };
    glm::vec2 currentOffset { 0.0f };
    glm::vec2 knobBaseSize { 0.0f };
    glm::vec2 dragStartOffset { 0.0f };
    glm::vec2 dragPressPoint { 0.0f };
    bool dragging = false;
    bool dragMoved = false;
    bool suppressClick = false;
    bool liquidThumbEnabled = false;
    float liquidThumbScale = 1.0f;
    float liquidThumbVelocity = 0.0f;
    UiLiquidInteractionState liquidInteraction;
    double lastThumbUpdateSeconds = 0.0;
    glm::vec2 animationStartOffset { 0.0f };
    glm::vec2 animationTargetOffset { 0.0f };
    double animationStartSeconds = 0.0;
    float animationDurationSeconds = 0.18f;
    bool animateKnob = true;
    bool animating = false;
    bool animationPendingStart = false;
    ShapeStyleComponent offIdle;
    ShapeStyleComponent offHovered;
    ShapeStyleComponent offPressed;
    ShapeStyleComponent onIdle;
    ShapeStyleComponent onHovered;
    ShapeStyleComponent onPressed;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

struct UiSwitchOptions
{
    bool checked = false;
    bool enabled = true;
    float minimumWidthRatio = 2.25f;
    float knobInset = 3.0f;
    float knobWidthRatio = 1.75f;
    float knobEdgeSoftness = 0.5f;
    bool knobSdfEdges = true;
    float animationDurationSeconds = 0.18f;
    bool animateKnob = true;
    std::string offColor = "#E2E2E4FF";
    std::string offHoveredColor = "#ECECEEFF";
    std::string offPressedColor = "#D6D6D9FF";
    std::string offOutlineColor = "#00000014";
    std::string onColor = "#007AFFFF";
    std::string onHoveredColor = "#1689FFFF";
    std::string onPressedColor = "#006DDEFF";
    std::string onOutlineColor = "#0068D8FF";
    std::string knobColor = "#FFFFFFFF";
    std::string knobOutlineColor = "#00000000";
    float trackOutlineWidth = 0.8f;
    float knobOutlineWidth = 0.0f;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

inline UiSwitchOptions ui_switch_options_from_theme(const UiTheme& theme, bool checked = false)
{

    UiSwitchOptions options = {};
    options.checked = checked;
    options.offColor = theme.surfaceMuted;
    options.offHoveredColor = theme.surfaceElevated;
    options.offPressedColor = theme.settingsPanel.cardSurface;
    options.offOutlineColor = theme.settingsPanel.cardOutline;
    options.onColor = theme.accent;
    options.onHoveredColor = theme.accentHover;
    options.onPressedColor = theme.accentPressed;
    options.onOutlineColor = theme.accentPressed;
    return options;
}

inline glm::vec2 ui_switch_target_offset(const UiSwitchControlComponent& control)
{
    return control.checked ? control.onOffset : control.offOffset;
}

inline void ui_apply_switch_knob_offset(
    Renderer2DScene& scene,
    UiSwitchControlComponent& control,
    glm::vec2 offset)
{
    entt::registry& registry = scene.registry();
    if (control.knob == entt::null || !registry.valid(control.knob))
    {
        control.currentOffset = offset;
        return;
    }

    if (Layout2DComponent* layout = registry.try_get<Layout2DComponent>(control.knob))
    {
        layout->anchorMin = ui_alignment_anchor(UiAlignment::eMiddleLeft);
        layout->anchorMax = layout->anchorMin;
        layout->pivot = ui_alignment_pivot(UiAlignment::eMiddleLeft);
        layout->offset = offset;
        if (control.liquidThumbEnabled)
            if (const auto* shape = registry.try_get<ShapeComponent>(control.knob))
                layout->offset.x -= (shape->size.x - control.knobBaseSize.x) * 0.5f;
    }
    control.currentOffset = offset;
    scene.mark_dirty(control.knob);
}

inline void ui_start_switch_knob_animation(Renderer2DScene& scene, entt::entity entity)
{
    entt::registry& registry = scene.registry();
    UiSwitchControlComponent* control = registry.try_get<UiSwitchControlComponent>(entity);
    if (!control)
    {
        return;
    }

    glm::vec2 startOffset = control->currentOffset;
    if (!control->liquidThumbEnabled && control->knob != entt::null && registry.valid(control->knob))
    {
        if (const Layout2DComponent* layout = registry.try_get<Layout2DComponent>(control->knob))
        {
            startOffset = layout->offset;
        }
    }

    const glm::vec2 targetOffset = ui_switch_target_offset(*control);
    if (!control->animateKnob || control->animationDurationSeconds <= 0.001f)
    {
        control->animating = false;
        control->animationPendingStart = false;
        ui_apply_switch_knob_offset(scene, *control, targetOffset);
        scene.mark_dirty(entity);
        return;
    }

    control->animationStartOffset = startOffset;
    control->animationTargetOffset = targetOffset;
    control->animating = true;
    control->animationPendingStart = true;
    scene.activate_dynamic(entity, control->animationDurationSeconds + 0.08f);
    if (control->knob != entt::null && registry.valid(control->knob))
    {
        scene.activate_dynamic(control->knob, control->animationDurationSeconds + 0.08f);
        scene.mark_dirty(control->knob);
    }
    scene.mark_dirty(entity);
}

inline void ui_update_switch_geometry(Renderer2DScene& scene, entt::entity entity)
{
    auto& registry = scene.registry();
    auto* captured = registry.try_get<UiSwitchControlComponent>(entity);
    if (!captured) return;
    auto& control = *captured;
    if (!registry.valid(control.knob) || control.knobBaseSize.x <= 0.0f || control.knobBaseSize.y <= 0.0f) return;
    const float heightScale = std::clamp(1.0f + std::max(control.liquidThumbScale - 1.0f, 0.0f) * 3.4f, 1.0f, 1.9f);
    const glm::vec2 size = ui_expanded_capsule_size(control.knobBaseSize, control.liquidThumbScale, heightScale);
    auto* shape = registry.try_get<ShapeComponent>(control.knob);
    if (shape && shape->size != size)
    {
        shape->size = size;
        shape->set_corner_radius(std::min(size.x, size.y) * 0.5f);
        if (auto* layout = registry.try_get<Layout2DComponent>(control.knob)) layout->size = size;
        ui_apply_switch_knob_offset(scene, control, control.currentOffset);
        ui_mark_direct_manipulation_dirty(scene, control.knob);
    }
    const auto* gesture = registry.try_get<StretchDynamicsComponent>(entity);
    const glm::vec2 deformation { control.liquidThumbEnabled && gesture ? gesture->scale.x : 1.0f, 1.0f };
    auto* visual = registry.try_get<InteractiveVisual2DComponent>(control.knob);
    if (visual || deformation != glm::vec2(1.0f))
    {
        if (!visual) visual = &registry.emplace<InteractiveVisual2DComponent>(control.knob);
        const auto rect = ui_entity_base_framebuffer_rect(registry, entity);
        const glm::vec2 centre { rect.x + control.currentOffset.x + control.knobBaseSize.x * .5f, rect.y + rect.w * .5f };
        if (visual->scale != deformation || visual->scaleOrigin != centre)
        {
            visual->scale = deformation;
            visual->scaleOrigin = centre;
            visual->inheritToChildren = false;
            ui_mark_direct_manipulation_dirty(scene, control.knob);
        }
    }
}

inline void ui_update_switch_visual(Renderer2DScene& scene, entt::entity entity)
{
    entt::registry& registry = scene.registry();
    UiSwitchControlComponent* control = registry.try_get<UiSwitchControlComponent>(entity);
    ButtonVisualComponent* visual = registry.try_get<ButtonVisualComponent>(entity);
    if (!control || !visual)
    {
        return;
    }

    visual->idle = control->checked ? control->onIdle : control->offIdle;
    visual->hovered = control->checked ? control->onHovered : control->offHovered;
    visual->pressed = control->checked ? control->onPressed : control->offPressed;
    ui_update_button_visual(scene, entity);

    if (control->knob != entt::null && registry.valid(control->knob))
    {
        if (!control->animating && !control->dragging)
        {
            ui_apply_switch_knob_offset(scene, *control, ui_switch_target_offset(*control));
        }
        scene.mark_dirty(control->knob);
    }
}

inline void ui_update_switch_animations(Renderer2DScene& scene, double currentTimeSeconds)
{
    entt::registry& registry = scene.registry();
    auto view = registry.view<UiSwitchControlComponent>();
    view.each([&](entt::entity entity, UiSwitchControlComponent& control) {
        if (control.dragging)
        {
            const auto* button = registry.try_get<ButtonInputComponent>(entity);
            if (!button || !button->enabled || !button->leftPressed)
            {
                control.dragging = false;
                ui_start_switch_knob_animation(scene, entity);
            }
        }
        const double thumbDelta = control.lastThumbUpdateSeconds > 0.0 ?
            currentTimeSeconds - control.lastThumbUpdateSeconds : 0.0;
        control.lastThumbUpdateSeconds = currentTimeSeconds;
        const float previousScale = control.liquidThumbScale;
        if (control.liquidThumbEnabled)
        {
            ui_update_liquid_interaction(control.liquidInteraction, control.dragging, control.dragMoved, thumbDelta);
            control.liquidThumbScale = 1.0f + 0.25f * control.liquidInteraction.formation;
            control.liquidThumbVelocity = 0.25f * control.liquidInteraction.velocity;
        }
        else
        {
            control.liquidThumbScale = 1.0f;
            control.liquidThumbVelocity = 0.0f;
        }
        ui_update_switch_geometry(scene, entity);
        if (previousScale != control.liquidThumbScale)
            ui_mark_direct_manipulation_dirty(scene, entity);
        if (!control.animating)
        {
            return;
        }

        if (control.animationPendingStart)
        {
            control.animationStartSeconds = currentTimeSeconds;
            control.animationPendingStart = false;
        }

        const float duration = std::max(control.animationDurationSeconds, 0.001f);
        const float progress = static_cast<float>(
            std::clamp((currentTimeSeconds - control.animationStartSeconds) / static_cast<double>(duration), 0.0, 1.0));
        const float eased = ui_control_ease_out(progress);
        const glm::vec2 offset = glm::mix(control.animationStartOffset, control.animationTargetOffset, eased);
        ui_apply_switch_knob_offset(scene, control, offset);

        if (progress >= 1.0f)
        {
            control.animating = false;
            control.animationPendingStart = false;
            ui_apply_switch_knob_offset(scene, control, ui_switch_target_offset(control));
        }
        else
        {
            scene.activate_dynamic(entity, 1.0f / 30.0f);
            if (control.knob != entt::null && registry.valid(control.knob))
            {
                scene.activate_dynamic(control.knob, 1.0f / 30.0f);
            }
        }
    });
}

inline void ui_finish_switch_drag(Renderer2DScene& scene, entt::entity entity, glm::vec2 point)
{
    auto& registry = scene.registry();
    auto* control = registry.try_get<UiSwitchControlComponent>(entity);
    if (!control || !control->dragging) return;
    const float displacement = point.x - control->dragPressPoint.x;
    control->dragMoved = control->dragMoved || std::abs(displacement) > 2.0f;
    ui_apply_switch_knob_offset(scene, *control, {
        std::clamp(control->dragStartOffset.x + displacement, control->offOffset.x, control->onOffset.x),
        control->currentOffset.y });
    control->dragging = false;
    control->suppressClick = control->dragMoved;
    if (control->dragMoved)
    {
        const bool checked = control->currentOffset.x >= (control->offOffset.x + control->onOffset.x) * 0.5f;
        const bool changed = checked != control->checked;
        control->checked = checked;
        auto callback = control->onChanged;
        ui_start_switch_knob_animation(scene, entity);
        ui_update_switch_visual(scene, entity);
        if (changed && callback) callback(checked, PointerInputEvent { entity, point, PointerButton::eLeft });
    }
    else ui_start_switch_knob_animation(scene, entity);
}

inline void ui_update_switch_drag(Renderer2DScene& scene, UiInputState& state,
    bool leftPressed, bool hasPoint, glm::vec2 point)
{
    auto& registry = scene.registry();
    const auto entity = state.pressedInputEntity;
    auto* control = registry.try_get<UiSwitchControlComponent>(entity);
    if (!control || !hasPoint) return;
    const auto* button = registry.try_get<ButtonInputComponent>(entity);
    if (!button || !button->enabled) return;
    if (!leftPressed)
    {
        ui_finish_switch_drag(scene, entity, point);
        return;
    }
    if (!control->dragging)
    {
        control->dragging = true;
        control->dragMoved = false;
        control->suppressClick = false;
        control->dragStartOffset = control->currentOffset;
        control->dragPressPoint = point;
        control->animating = false;
    }


    const float displacement = point.x - control->dragPressPoint.x;
    control->dragMoved = control->dragMoved || std::abs(displacement) > 2.0f;
    ui_apply_switch_knob_offset(scene, *control, {
        std::clamp(control->dragStartOffset.x + displacement, control->offOffset.x, control->onOffset.x),
        control->currentOffset.y });
}

inline UiControlHandle ui_create_switch(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiSwitchOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    const glm::vec2 trackSize {
        std::max(size.x, size.y * std::max(options.minimumWidthRatio, 1.0f)),
        size.y
    };
    const float knobInset = std::clamp(options.knobInset, 1.0f, std::max(trackSize.y * 0.45f, 1.0f));
    const float knobHeight = std::max(trackSize.y - knobInset * 2.0f, 2.0f);
    const float knobWidth = std::clamp(
        knobHeight * std::max(options.knobWidthRatio, 1.0f),
        knobHeight,
        std::max(trackSize.x - knobInset * 2.0f, knobHeight));
    UiControlHandle handle = {};
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(trackSize.x, trackSize.y, ui.scale()),
        ui_macos26_control_style(ui, options.offColor, options.offOutlineColor, options.trackOutlineWidth, 1.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, trackSize.y * 0.5f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(handle.root, parent, alignment, scaled_offset(offset.x, offset.y, ui.scale()), scaled_size(trackSize.x, trackSize.y, ui.scale()));

    handle.knob = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(knobWidth, knobHeight, ui.scale()),
        ui_macos26_control_style(ui, options.knobColor, options.knobOutlineColor, options.knobOutlineWidth, 1.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    if (ShapeStyleComponent* knobStyle = registry.try_get<ShapeStyleComponent>(handle.knob))
    {
        knobStyle->edgeSoftness = scaled_scalar(options.knobEdgeSoftness, ui.scale());
    }
    ui.set_shape(handle.knob, knobHeight * 0.5f, options.knobSdfEdges);
    ui.set_layer(handle.knob, layer + 1, order);
    ui.attach_aligned(handle.knob, handle.root, UiAlignment::eMiddleLeft, scaled_offset(knobInset, 0.0f, ui.scale()), scaled_size(knobWidth, knobHeight, ui.scale()));

    auto& knobShadow = ui.add_shadow(handle.knob, { 0.0f, 0.0f }, 8.0f, 0.12f);
    knobShadow.spread = 0.0f;
    knobShadow.outsideOnly = true;

    UiSwitchControlComponent control = {};
    control.checked = options.checked;
    control.knob = handle.knob;
    control.offOffset = scaled_offset(knobInset, 0.0f, ui.scale());
    control.onOffset = scaled_offset(trackSize.x - knobInset - knobWidth, 0.0f, ui.scale());
    control.currentOffset = control.checked ? control.onOffset : control.offOffset;
    control.knobBaseSize = scaled_size(knobWidth, knobHeight, ui.scale());
    control.animationDurationSeconds = std::max(options.animationDurationSeconds, 0.0f);
    control.animateKnob = options.animateKnob;
    control.offIdle = ui_macos26_control_style(ui, options.offColor, options.offOutlineColor, options.trackOutlineWidth, 1.0f);
    control.offHovered = ui_macos26_control_style(ui, options.offHoveredColor, options.offOutlineColor, options.trackOutlineWidth, 1.0f);
    control.offPressed = ui_macos26_control_style(ui, options.offPressedColor, options.offOutlineColor, options.trackOutlineWidth, 1.0f);
    control.onIdle = ui_macos26_control_style(ui, options.onColor, options.onOutlineColor, options.trackOutlineWidth, 1.0f);
    control.onHovered = ui_macos26_control_style(ui, options.onHoveredColor, options.onOutlineColor, options.trackOutlineWidth, 1.0f);
    control.onPressed = ui_macos26_control_style(ui, options.onPressedColor, options.onOutlineColor, options.trackOutlineWidth, 1.0f);
    control.onChanged = std::move(options.onChanged);
    registry.emplace<UiSwitchControlComponent>(handle.root, std::move(control));

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onPress = [scenePtr = &scene, entity = handle.root](const PointerInputEvent& event) {
        if (event.button != PointerButton::eLeft) return;
        auto& captured = scenePtr->registry().get<UiSwitchControlComponent>(entity);
        captured.dragging = true;
        captured.liquidInteraction.heldSeconds = 0.0;
        captured.dragMoved = false;
        captured.suppressClick = false;
        captured.animating = false;
        captured.animationPendingStart = false;
        captured.dragStartOffset = captured.currentOffset;
        captured.dragPressPoint = event.framebufferPosition;
    };
    button.onRelease = [scenePtr = &scene, entity = handle.root](const PointerInputEvent& event) {
        if (event.button == PointerButton::eLeft)
            ui_finish_switch_drag(*scenePtr, entity, event.framebufferPosition);
    };
    button.onClick = [scenePtr = &scene, entity = handle.root](const PointerInputEvent& event) {
        entt::registry& capturedRegistry = scenePtr->registry();
        UiSwitchControlComponent* captured = capturedRegistry.try_get<UiSwitchControlComponent>(entity);
        if (!captured)
        {
            return;
        }
        if (captured->suppressClick)
        {
            captured->suppressClick = false;
            return;
        }
        captured->checked = !captured->checked;
        const bool checked = captured->checked;
        auto callback = captured->onChanged;
        ui_start_switch_knob_animation(*scenePtr, entity);
        ui_update_switch_visual(*scenePtr, entity);
        if (callback)
        {
            callback(checked, event);
        }
    };
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, ui_macos26_button_visual(ui, false));
    StretchDynamicsOptions gesture {};
    gesture.axes = eStretchDynamicsHorizontal;
    gesture.stretchSelf = false;
    gesture.stretchDescendants = false;
    gesture.consumeClickAfterDrag = true;
    gesture.dragResponse = 0.10f;
    gesture.returnDampingFraction = 0.78f;
    gesture.onChanged = [scenePtr = &scene, entity = handle.root](const StretchDynamicsEvent& event) {
        auto& captured = scenePtr->registry().get<UiSwitchControlComponent>(entity);
        if (!captured.dragging) return;
        captured.dragMoved = captured.dragMoved || std::abs(event.displacement.x) > 2.0f;
        const float offset = std::clamp(captured.dragStartOffset.x + event.displacement.x,
            captured.offOffset.x, captured.onOffset.x);
        ui_apply_switch_knob_offset(*scenePtr, captured, { offset, captured.currentOffset.y });
    };
    gesture.onRelease = [scenePtr = &scene, entity = handle.root](const StretchDynamicsEvent& event) {
        ui_finish_switch_drag(*scenePtr, entity, event.framebufferPosition);
    };
    ui_enable_stretch_dynamics(scene, handle.root, std::move(gesture));
    ui_update_switch_visual(scene, handle.root);
    return handle;
}


// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/ui/input.h>
#include <vibranceUI/ui/visuals.h>
#include <vibranceUI/ui/text_input_interactions.h>

enum class UiInputAction
{
    ePress,
    eRelease,
    eRepeat
};

enum class UiCursorKind
{
    eDefault,
    eText,
    ePointer,
    eUnavailable,
    eResizeNwse,
    eResizeNesw,
    eResizeEw,
    eResizeNs
};

struct UiKeyMap
{
    int escape = 0;
    int backspace = 0;
    int deleteKey = 0;
    int enter = 0;
    int paste = 0;
    int clearLine = 0;
    int left = 0, right = 0, up = 0, down = 0;
    int home = 0, end = 0, pageUp = 0, pageDown = 0;
    int selectAll = 0, copy = 0, cut = 0, undo = 0, redo = 0;
    int insert = 0, tab = 0, killLine = 0;
    bool macOsBindings = false;
};

struct UiPointerButtonInput
{

    bool hasPoint = false;
    glm::vec2 point { 0.0f };
    PointerButton button = PointerButton::eOther;
    int platformButton = 0;
    UiInputAction action = UiInputAction::ePress;
    InputModifiers modifiers {};
    double timeSeconds = 0.0;
    unsigned clickCount = 0u;
    double multiClickInterval = 0.5;
};

struct UiScrollInput
{

    bool hasPoint = false;
    glm::vec2 point { 0.0f };
    double offsetX = 0.0;
    double offsetY = 0.0;
    InputModifiers modifiers {};
};

struct UiKeyInput
{
    int key = 0;
    UiInputAction action = UiInputAction::ePress;
    InputModifiers modifiers {};
    UiKeyMap keys {};
    std::string clipboardText;
    std::function<void(const std::string&)> writeClipboard;
    double timeSeconds = 0.0;
};

struct UiDropInput
{
    bool hasPoint = false;
    glm::vec2 point { 0.0f };
    std::vector<std::filesystem::path> paths;
};

struct UiHoverResult
{

    entt::entity textOwner = entt::null;
    entt::entity buttonOwner = entt::null;
    entt::entity sliderOwner = entt::null;
    entt::entity scrollBarOwner = entt::null;
    uint32_t resizeEdges = ePanelResizeNone;
    UiCursorKind cursor = UiCursorKind::eDefault;
};

inline UiCursorKind ui_cursor_kind_for_resize_edges(uint32_t resizeEdges)
{
    const bool left = (resizeEdges & ePanelResizeLeft) != 0u;
    const bool right = (resizeEdges & ePanelResizeRight) != 0u;
    const bool top = (resizeEdges & ePanelResizeTop) != 0u;
    const bool bottom = (resizeEdges & ePanelResizeBottom) != 0u;

    if ((left && top) || (right && bottom))
    {
        return UiCursorKind::eResizeNwse;
    }
    if ((right && top) || (left && bottom))
    {
        return UiCursorKind::eResizeNesw;
    }
    if (left || right)
    {
        return UiCursorKind::eResizeEw;
    }
    if (top || bottom)
    {
        return UiCursorKind::eResizeNs;
    }
    return UiCursorKind::eDefault;
}

inline UiCursorKind ui_cursor_kind_for_owner(
    const entt::registry& registry,
    entt::entity owner,
    UiCursorKind enabledCursor)
{

    if (owner == entt::null || !registry.valid(owner))
    {
        return UiCursorKind::eDefault;
    }

    const DisabledVisualComponent* disabledVisual = registry.try_get<DisabledVisualComponent>(owner);
    bool disabled = false;
    if (const TextInputComponent* text = registry.try_get<TextInputComponent>(owner))
    {
        disabled = !text->enabled;
    }
    else if (const ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(owner))
    {
        disabled = !button->enabled;
    }
    else if (const SliderInputComponent* slider = registry.try_get<SliderInputComponent>(owner))
    {
        disabled = !slider->enabled;
    }
    else if (const ScrollBarInputComponent* scrollBar = registry.try_get<ScrollBarInputComponent>(owner))
    {
        disabled = !scrollBar->enabled;
    }

    if (disabled)
    {
        return disabledVisual && disabledVisual->useUnavailableCursor ?
            UiCursorKind::eUnavailable :
            UiCursorKind::eDefault;
    }

    return enabledCursor;
}

inline UiCursorKind ui_cursor_kind_for_hover(
    const entt::registry& registry,
    entt::entity textOwner,
    entt::entity buttonOwner,
    entt::entity sliderOwner,
    entt::entity scrollBarOwner,
    uint32_t resizeEdges)
{
    const UiCursorKind resizeCursor = ui_cursor_kind_for_resize_edges(resizeEdges);
    if (resizeCursor != UiCursorKind::eDefault)
    {
        return resizeCursor;
    }
    if (textOwner != entt::null)
    {
        return ui_cursor_kind_for_owner(registry, textOwner, UiCursorKind::eText);
    }
    if (buttonOwner != entt::null)
    {
        return ui_cursor_kind_for_owner(registry, buttonOwner, UiCursorKind::ePointer);
    }
    if (sliderOwner != entt::null)
    {
        return ui_cursor_kind_for_owner(registry, sliderOwner, UiCursorKind::ePointer);
    }
    if (scrollBarOwner != entt::null)
    {
        return ui_cursor_kind_for_owner(registry, scrollBarOwner, UiCursorKind::ePointer);
    }
    return UiCursorKind::eDefault;
}

inline bool ui_input_blocks_panel_drag(Renderer2DScene& scene, const UiInputState& state, glm::vec2 point)
{

    if (state.pressedInputEntity != entt::null || state.pointerInputCapture != entt::null)
    {
        return true;
    }

    uint32_t resizeEdges = ePanelResizeNone;
    if (ui_resizable_panel_at(scene, point, resizeEdges) != entt::null)
    {
        return true;
    }
    return ui_input_owner_at(scene, point) != entt::null;
}

inline void ui_focus_text_input(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state,
    entt::entity entity)
{
    entt::registry& registry = scene.registry();
    if (state.focusedTextInput == entity && registry.valid(entity)) return;
    state.activeTextSelection = entt::null;
    if (state.focusedTextInput != entt::null && registry.valid(state.focusedTextInput))
    {
        scene.activate_dynamic(state.focusedTextInput, 0.4f);
        if (TextInputComponent* oldInput = registry.try_get<TextInputComponent>(state.focusedTextInput))
        {
            oldInput->focused = false;
            ui_update_text_input_visual(scene, fontAtlas, state.focusedTextInput);
        }
    }

    state.focusedTextInput = entt::null;
    if (entity == entt::null || !registry.valid(entity))
    {
        return;
    }

    TextInputComponent* input = registry.try_get<TextInputComponent>(entity);
    if (!input || !input->enabled)
    {
        return;
    }

    input->focused = true;
    state.focusedTextInput = entity;
    scene.activate_dynamic(entity, 0.4f);
    ui_update_text_input_visual(scene, fontAtlas, entity);
}

inline void ui_append_text_input(Renderer2DScene& scene, const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state, unsigned int codepoint, double timeSeconds = 0.0)
{
    const auto entity = state.focusedTextInput;
    auto* input = scene.registry().try_get<TextInputComponent>(entity);
    if (!input || !input->enabled || input->readOnly || codepoint < 32u || codepoint == 127u) return;
    const auto encoded = ui_text_encode_codepoint(codepoint);
    if (encoded.empty()) return;
    const bool changed = ui_text_replace_selection(*input, encoded, TextInputEditKind::eTyping, timeSeconds);
    ui_refresh_text_input(scene, fontAtlas, entity, changed);
}

inline void ui_emit_slider_change_if_needed(
    SliderInputComponent& slider,
    entt::entity entity,
    glm::vec2 point,
    bool changed)
{
    if (!changed || !slider.onChanged)
    {
        return;
    }

    SliderInputEvent event {
        entity,
        point,
        slider.value,
        ui_slider_normalized_value(slider)
    };
    slider.onChanged(event);
}

inline void ui_emit_slider_commit(
    SliderInputComponent& slider,
    entt::entity entity,
    glm::vec2 point)
{
    if (!slider.onCommitted)
    {
        return;
    }
    slider.onCommitted({
        entity,
        point,
        slider.value,
        ui_slider_normalized_value(slider)
    });
}

inline bool ui_scrollbar_point_hits_thumb(
    const entt::registry& registry,
    const ScrollBarInputComponent& scrollBar,
    glm::vec2 point)
{
    return scrollBar.thumb != entt::null &&
        registry.valid(scrollBar.thumb) &&
        ui_point_in_rect(ui_entity_framebuffer_rect(registry, scrollBar.thumb), point);
}

inline bool ui_apply_scrollbar_point(
    Renderer2DScene& scene,
    entt::entity scrollBarEntity,
    glm::vec2 point,
    float grabOffsetY,
    InputModifiers modifiers = {})
{
    entt::registry& registry = scene.registry();
    ScrollBarInputComponent* scrollBar = registry.try_get<ScrollBarInputComponent>(scrollBarEntity);
    if (!scrollBar || !scrollBar->enabled || scrollBar->scrollTarget == entt::null ||
        !registry.valid(scrollBar->scrollTarget))
    {
        return false;
    }

    ScrollInputComponent* scroll = registry.try_get<ScrollInputComponent>(scrollBar->scrollTarget);
    if (!scroll || !scroll->enabled)
    {
        return false;
    }

    const glm::vec4 trackRect = ui_entity_framebuffer_rect(registry, scrollBarEntity);
    const glm::vec4 thumbRect =
        scrollBar->thumb != entt::null && registry.valid(scrollBar->thumb) ?
            ui_entity_framebuffer_rect(registry, scrollBar->thumb) :
            glm::vec4(trackRect.x, trackRect.y, trackRect.z, 0.0f);
    const float scrollRange = std::max(scroll->maxOffset - scroll->minOffset, 0.0f);
    const float thumbTravel = std::max(trackRect.w - thumbRect.w, 1.0f);
    if (trackRect.w <= 1.0f || scrollRange <= 0.5f)
    {
        return false;
    }

    const float thumbTop = std::clamp(point.y - trackRect.y - grabOffsetY, 0.0f, thumbTravel);
    const float normalized = thumbTop / thumbTravel;
    const float previousOffset = scroll->offset;
    float nextOffset = std::clamp(
        scroll->minOffset + normalized * scrollRange,
        scroll->minOffset,
        scroll->maxOffset);



    if (nextOffset > scroll->minOffset && nextOffset < scroll->maxOffset)
    {
        nextOffset = std::clamp(
            std::round(nextOffset),
            scroll->minOffset,
            scroll->maxOffset);
    }
    scroll->offset = nextOffset;
    if (std::abs(scroll->offset - previousOffset) <= 1e-4f)
    {
        scrollBar->wakeRequested = true;
        return false;
    }

    scrollBar->wakeRequested = true;
    if (scroll->onScroll)
    {
        ScrollInputEvent event { scrollBar->scrollTarget, point, 0.0, 0.0, modifiers };
        scroll->onScroll(event);
    }
    return true;
}

inline void ui_wake_scrollbars_for_scroll_target(Renderer2DScene& scene, entt::entity scrollTarget)
{
    if (scrollTarget == entt::null)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    auto view = registry.view<ScrollBarInputComponent>();
    view.each([&](entt::entity entity, ScrollBarInputComponent& scrollBar) {
        if (scrollBar.scrollTarget != scrollTarget)
        {
            return;
        }
        scrollBar.wakeRequested = true;
        if (scrollBar.thumb != entt::null && registry.valid(scrollBar.thumb))
        {
            ui_mark_direct_manipulation_dirty(
                scene,
                scrollBar.thumb,
                scrollBar.idleDelaySeconds + scrollBar.fadeDurationSeconds + 0.1f);
        }
    });
}

inline void ui_update_scrollbar_fade(Renderer2DScene& scene, double currentTimeSeconds)
{
    entt::registry& registry = scene.registry();
    auto view = registry.view<ScrollBarInputComponent>();
    view.each([&](entt::entity entity, ScrollBarInputComponent& scrollBar) {
        if (scrollBar.thumb == entt::null || !registry.valid(scrollBar.thumb))
        {
            return;
        }

        bool scrollable = false;
        if (scrollBar.scrollTarget != entt::null && registry.valid(scrollBar.scrollTarget))
        {
            if (const ScrollInputComponent* scroll = registry.try_get<ScrollInputComponent>(scrollBar.scrollTarget))
            {
                scrollable = scroll->enabled && (scroll->maxOffset - scroll->minOffset) > 0.5f;
            }
        }

        if (!scrollable)
        {
            if (ShapeStyleComponent* style = registry.try_get<ShapeStyleComponent>(scrollBar.thumb))
            {
                if (std::abs(style->opacity - scrollBar.hiddenOpacity) > 0.001f)
                {
                    style->opacity = scrollBar.hiddenOpacity;
                    ui_mark_direct_manipulation_dirty(scene, scrollBar.thumb, 1.0f / 15.0f);
                }
            }
            scrollBar.wakeRequested = false;
            return;
        }

        if (!scrollBar.fadeWhenIdle)
        {
            if (ShapeStyleComponent* style = registry.try_get<ShapeStyleComponent>(scrollBar.thumb))
            {
                if (std::abs(style->opacity - scrollBar.visibleOpacity) > 0.001f)
                {
                    style->opacity = scrollBar.visibleOpacity;
                    ui_mark_direct_manipulation_dirty(scene, scrollBar.thumb, 1.0f / 15.0f);
                }
            }
            return;
        }

        const bool heldVisible = scrollBar.hovered || scrollBar.dragging;
        if (scrollBar.wakeRequested)
        {
            scrollBar.lastActiveSeconds = currentTimeSeconds;
            scrollBar.wakeRequested = false;
        }

        const double elapsed = heldVisible ?
            0.0 :
            std::max(currentTimeSeconds - scrollBar.lastActiveSeconds, 0.0);
        float opacity = scrollBar.visibleOpacity;
        if (!heldVisible && elapsed > static_cast<double>(scrollBar.idleDelaySeconds))
        {
            const float fadeT = std::clamp(
                static_cast<float>((elapsed - static_cast<double>(scrollBar.idleDelaySeconds)) /
                    static_cast<double>(std::max(scrollBar.fadeDurationSeconds, 0.001f))),
                0.0f,
                1.0f);
            opacity = glm::mix(scrollBar.visibleOpacity, scrollBar.hiddenOpacity, fadeT);
        }

        if (ShapeStyleComponent* style = registry.try_get<ShapeStyleComponent>(scrollBar.thumb))
        {
            if (std::abs(style->opacity - opacity) > 0.001f)
            {
                style->opacity = opacity;
                ui_mark_direct_manipulation_dirty(scene, scrollBar.thumb, 1.0f / 15.0f);
            }
        }
    });
}

inline StretchDynamicsEvent ui_stretch_dynamics_event(
    entt::entity entity,
    glm::vec2 point,
    const StretchDynamicsComponent& dynamics)
{
    return {
        entity,
        point,
        dynamics.displacement,
        dynamics.scale,
        dynamics.inwardProgress,
        dynamics.inwardProgress >= 1.0f
    };
}

inline void ui_apply_stretch_dynamics_visual(
    Renderer2DScene& scene,
    entt::entity entity,
    StretchDynamicsComponent& dynamics)
{
    entt::registry& registry = scene.registry();
    if (!ui_stretch_dynamics_allowed(registry, entity, dynamics.options))
    {
        ui_reset_stretch_dynamics_visual(scene, entity);
        return;
    }
    InteractiveVisual2DComponent& visual =
        registry.get_or_emplace<InteractiveVisual2DComponent>(entity);
    visual.enabled = dynamics.options.enabled;
    visual.affectSelf = dynamics.options.stretchSelf;
    visual.inheritToChildren = dynamics.options.stretchDescendants;
    const bool liquidMotion = ui_stretch_uses_liquid_motion(registry, entity, dynamics.options);
    visual.blurSelf = !liquidMotion && dynamics.options.inwardBlurSelf;
    visual.scale = glm::max(dynamics.scale, glm::vec2(0.001f));
    visual.offset = dynamics.offset;
    if (dynamics.options.constrainToParent)
    {
        const auto* parent = registry.try_get<Parent2DComponent>(entity);
        if (parent && registry.valid(parent->parent))
        {
            const auto bounds = ui_entity_base_framebuffer_rect(registry, parent->parent);
            const auto own = ui_entity_base_framebuffer_rect(registry, entity);
            for (int axis = 0; axis < 2; ++axis)
            {
                const float origin = dynamics.scaleOrigin[axis];
                const float nearDistance = origin - own[axis];
                const float farDistance = own[axis] + own[axis + 2] - origin;
                if (nearDistance > 0.001f)
                    visual.scale[axis] = std::min(visual.scale[axis], std::max(0.001f, (origin + visual.offset[axis] - bounds[axis]) / nearDistance));
                if (farDistance > 0.001f)
                    visual.scale[axis] = std::min(visual.scale[axis], std::max(0.001f, (bounds[axis] + bounds[axis + 2] - origin - visual.offset[axis]) / farDistance));
                const float minimum = origin + (own[axis] - origin) * visual.scale[axis];
                const float maximum = minimum + own[axis + 2] * visual.scale[axis];
                if (maximum - minimum <= bounds[axis + 2])
                    visual.offset[axis] = std::clamp(visual.offset[axis],
                        bounds[axis] - minimum, bounds[axis] + bounds[axis + 2] - maximum);
            }
        }
    }
    visual.scaleOrigin = dynamics.scaleOrigin;
    visual.blurRadius = std::max(dynamics.blurRadius, 0.0f);
    visual.opacity = 1.0f;
    if (liquidMotion && dynamics.contact > 0.0001f)
    {
        const auto rect = ui_entity_visual_framebuffer_rect(registry, entity);
        const glm::vec2 size = glm::max(glm::vec2(rect.z, rect.w), glm::vec2(1.0f));
        const glm::vec2 point = dynamics.pressPoint + dynamics.displacement;
        const glm::vec2 nearest = glm::clamp(point, glm::vec2(rect), glm::vec2(rect) + size);
        const float diameter = std::min(size.x, size.y);
        const float proximity = std::exp(-glm::length(point - nearest) / (diameter * 0.65f));
        registry.emplace_or_replace<LiquidGlassContact2DComponent>(entity,
            LiquidGlassContact2DComponent { (point - glm::vec2(rect)) / size,
                diameter * 2.4f, std::clamp(dynamics.contact, 0.0f, 1.0f) * proximity * 0.16f });
    }
    else registry.remove<LiquidGlassContact2DComponent>(entity);
    ui_mark_direct_manipulation_dirty(scene, entity, 0.08f);
}

inline bool ui_begin_stretch_dynamics(
    Renderer2DScene& scene,
    UiInputState& state,
    entt::entity entity,
    glm::vec2 point)
{
    entt::registry& registry = scene.registry();
    StretchDynamicsComponent* dynamics =
        registry.try_get<StretchDynamicsComponent>(entity);
    if (!dynamics || !dynamics->options.enabled ||
        (!ui_stretch_dynamics_allowed(registry, entity, dynamics->options) && !ui_stretch_dynamics_has_action(dynamics->options)))
    {
        return false;
    }

    const bool liquidMotion = ui_stretch_uses_liquid_motion(registry, entity, dynamics->options);
    const bool retainMotion = (liquidMotion || dynamics->options.dragResponse > 0.0f) && (dynamics->dragging || dynamics->settling);
    dynamics->dragging = true;
    dynamics->settling = false;
    dynamics->dragMoved = false;
    dynamics->pressPoint = point;
    dynamics->baseRect = ui_entity_base_framebuffer_rect(registry, entity);
    const glm::vec2 baseCenter {
        dynamics->baseRect.x + dynamics->baseRect.z * 0.5f,
        dynamics->baseRect.y + dynamics->baseRect.w * 0.5f
    };
    dynamics->grabSide = {
        point.x < baseCenter.x ? -1.0f : 1.0f,
        point.y < baseCenter.y ? -1.0f : 1.0f
    };
    const auto applyAnchor = [](float automaticSide, StretchDynamicsAnchor anchor) {
        if (anchor == StretchDynamicsAnchor::eMinimum)
        {
            return 1.0f;
        }
        if (anchor == StretchDynamicsAnchor::eMaximum)
        {
            return -1.0f;
        }
        return automaticSide;
    };
    dynamics->grabSide.x = applyAnchor(
        dynamics->grabSide.x,
        dynamics->options.horizontalAnchor);
    dynamics->grabSide.y = applyAnchor(
        dynamics->grabSide.y,
        dynamics->options.verticalAnchor);
    dynamics->outwardAxes = eStretchDynamicsNone;
    dynamics->displacement = glm::vec2(0.0f);
    dynamics->targetScale = glm::vec2(std::max(dynamics->options.grabScale, 1.0f));
    if (!liquidMotion && dynamics->options.dragResponse <= 0.0f)
        dynamics->scale = dynamics->targetScale;
    if (!retainMotion) dynamics->scaleVelocity = glm::vec2(0.0f);
    dynamics->targetScaleOrigin = {
        dynamics->baseRect.x + dynamics->baseRect.z * 0.5f,
        dynamics->baseRect.y + dynamics->baseRect.w * 0.5f
    };
    if (!dynamics->options.liquidMotion)
    {


        if ((dynamics->options.axes & eStretchDynamicsHorizontal) != 0u)
            dynamics->targetScaleOrigin.x = dynamics->baseRect.x +
                (dynamics->grabSide.x < 0.0f ? dynamics->baseRect.z : 0.0f);
        if ((dynamics->options.axes & eStretchDynamicsVertical) != 0u)
            dynamics->targetScaleOrigin.y = dynamics->baseRect.y +
                (dynamics->grabSide.y < 0.0f ? dynamics->baseRect.w : 0.0f);
    }
    if (liquidMotion) ui_target_liquid_stretch(*dynamics);
    if (!retainMotion)
    {
        dynamics->scaleOrigin = dynamics->targetScaleOrigin;
        dynamics->scaleOriginVelocity = glm::vec2(0.0f);
    }
    if (!retainMotion)
    {
        dynamics->offset = dynamics->offsetVelocity = {};
        dynamics->contact = dynamics->contactVelocity = 0.0f;
        dynamics->blurRadius = dynamics->blurVelocity = 0.0f;
    }
    dynamics->inwardProgress = 0.0f;
    dynamics->lastUpdateSeconds = 0.0;
    state.activeStretchDynamics = entity;
    ui_apply_stretch_dynamics_visual(scene, entity, *dynamics);
    return true;
}

inline bool ui_update_stretch_dynamics_drag(
    Renderer2DScene& scene,
    UiInputState& state,
    bool leftButtonPressed,
    bool hasPoint,
    glm::vec2 point);

inline bool ui_release_stretch_dynamics(
    Renderer2DScene& scene,
    UiInputState& state,
    bool hasPoint,
    glm::vec2 point)
{
    const entt::entity entity = state.activeStretchDynamics;
    if (entity == entt::null)
    {
        return false;
    }

    entt::registry& registry = scene.registry();
    if (!registry.valid(entity))
    {
        state.activeStretchDynamics = entt::null;
        return false;
    }

    const auto* captured = registry.try_get<StretchDynamicsComponent>(entity);
    if (hasPoint && captured && captured->options.enabled &&
        (ui_stretch_dynamics_allowed(registry, entity, captured->options) || ui_stretch_dynamics_has_action(captured->options)))
    {


        ui_update_stretch_dynamics_drag(
            scene,
            state,
            true,
            true,
            point);
    }
    state.activeStretchDynamics = entt::null;

    StretchDynamicsComponent* dynamics =
        registry.try_get<StretchDynamicsComponent>(entity);
    if (!dynamics)
    {
        return false;
    }
    dynamics->dragging = false;
    dynamics->settling = true;
    const StretchDynamicsEvent event = ui_stretch_dynamics_event(
        entity,
        hasPoint ? point : dynamics->pressPoint + dynamics->displacement,
        *dynamics);
    const bool inwardReleaseArmed = event.inwardReleaseArmed;
    const bool consumeClick = dynamics->options.consumeClickAfterDrag && dynamics->dragMoved;
    const auto onRelease = dynamics->options.onRelease;
    const auto onInwardRelease = dynamics->options.onInwardRelease;
    const float returnActiveSeconds =
        std::max(dynamics->options.returnResponse * 4.0f, 0.2f);
    if (onRelease)
    {
        onRelease(event);
    }
    if (inwardReleaseArmed && onInwardRelease)
    {
        onInwardRelease(event);
    }
    ui_mark_moving_entity_dirty(
        scene,
        entity,
        returnActiveSeconds);
    return inwardReleaseArmed || consumeClick;
}

inline bool ui_update_stretch_dynamics_drag(
    Renderer2DScene& scene,
    UiInputState& state,
    bool leftButtonPressed,
    bool hasPoint,
    glm::vec2 point)
{
    const entt::entity entity = state.activeStretchDynamics;
    if (entity == entt::null)
    {
        return false;
    }

    entt::registry& registry = scene.registry();
    StretchDynamicsComponent* dynamics = registry.valid(entity) ?
        registry.try_get<StretchDynamicsComponent>(entity) :
        nullptr;
    if (!dynamics)
    {
        state.activeStretchDynamics = entt::null;
        return false;
    }
    if (!ui_stretch_dynamics_allowed(registry, entity, dynamics->options) &&
        (!dynamics->options.enabled || !ui_stretch_dynamics_has_action(dynamics->options)))
    {
        ui_release_stretch_dynamics(scene, state, hasPoint, point);
        ui_reset_stretch_dynamics_visual(scene, entity);
        return false;
    }
    if (!leftButtonPressed)
    {
        ui_release_stretch_dynamics(scene, state, hasPoint, point);
        return false;
    }
    if (!hasPoint)
    {
        return true;
    }

    dynamics->displacement = point - dynamics->pressPoint;
    const StretchDynamicsOptions& options = dynamics->options;
    const float deadZone = std::max(options.dragDeadZone, 0.0f);
    dynamics->dragMoved = dynamics->dragMoved || glm::dot(dynamics->displacement, dynamics->displacement) > deadZone * deadZone;
    const auto updateDirectionAwareSide = [&options, dynamics](
        std::size_t axis,
        uint32_t axisFlag,
        StretchDynamicsAnchor anchor) {
        if (!options.directionAware ||
            anchor != StretchDynamicsAnchor::eAutomatic ||
            (options.axes & axisFlag) == 0u)
        {
            return;
        }

        const float displacement = dynamics->displacement[axis];
        if (std::abs(displacement) <= std::max(options.dragDeadZone, 0.0f))
        {
            return;
        }

        if (displacement * dynamics->grabSide[axis] > 0.0f)
        {
            dynamics->outwardAxes |= axisFlag;
        }
        if ((dynamics->outwardAxes & axisFlag) != 0u)
        {
            dynamics->grabSide[axis] = displacement < 0.0f ? -1.0f : 1.0f;
        }
    };
    updateDirectionAwareSide(
        0u,
        eStretchDynamicsHorizontal,
        options.horizontalAnchor);
    updateDirectionAwareSide(
        1u,
        eStretchDynamicsVertical,
        options.verticalAnchor);
    dynamics->targetScale = glm::vec2(1.0f);
    if ((options.axes & eStretchDynamicsHorizontal) != 0u)
    {
        dynamics->targetScale.x = ui_stretch_scale_for_axis(
            dynamics->displacement.x,
            dynamics->grabSide.x,
            dynamics->baseRect.z,
            options.dragResistance,
            options.maximumStretch,
            options.maximumCompression,
            options.dragDeadZone,
            options.elasticEdges);
    }
    if ((options.axes & eStretchDynamicsVertical) != 0u)
    {
        dynamics->targetScale.y = ui_stretch_scale_for_axis(
            dynamics->displacement.y,
            dynamics->grabSide.y,
            dynamics->baseRect.w,
            options.dragResistance,
            options.maximumStretch,
            options.maximumCompression,
            options.dragDeadZone,
            options.elasticEdges);
    }

    dynamics->targetScale *= std::max(options.grabScale, 1.0f);
    dynamics->targetScaleOrigin = {
        (options.axes & eStretchDynamicsHorizontal) != 0u ?
            dynamics->baseRect.x +
                (dynamics->grabSide.x < 0.0f ? dynamics->baseRect.z : 0.0f) :
            dynamics->baseRect.x + dynamics->baseRect.z * 0.5f,
        (options.axes & eStretchDynamicsVertical) != 0u ?
            dynamics->baseRect.y +
                (dynamics->grabSide.y < 0.0f ? dynamics->baseRect.w : 0.0f) :
            dynamics->baseRect.y + dynamics->baseRect.w * 0.5f
    };
    const bool liquidMotion = ui_stretch_uses_liquid_motion(registry, entity, options);
    if (liquidMotion) ui_target_liquid_stretch(*dynamics);

    dynamics->inwardProgress = 0.0f;
    const float inwardDirectionLength = glm::length(options.inwardDirection);
    if (options.inwardReleaseDistance > 0.0f &&
        inwardDirectionLength > 0.0001f)
    {
        const glm::vec2 inwardDirection =
            options.inwardDirection / inwardDirectionLength;
        const float inwardDistance = std::max(
            glm::dot(dynamics->displacement, inwardDirection),
            0.0f);
        dynamics->inwardProgress = std::clamp(
            inwardDistance / options.inwardReleaseDistance,
            0.0f,
            1.0f);
    }
    const float blurProgress = dynamics->inwardProgress *
        dynamics->inwardProgress *
        (3.0f - 2.0f * dynamics->inwardProgress);
    if (!liquidMotion)
        dynamics->blurRadius = std::max(options.inwardBlurRadius, 0.0f) * blurProgress;
    if (!liquidMotion && options.dragResponse <= 0.0f)
    {
        dynamics->scale = dynamics->targetScale;
        dynamics->scaleOrigin = dynamics->targetScaleOrigin;
        dynamics->scaleVelocity = glm::vec2(0.0f);
    }
    if (!liquidMotion) dynamics->blurVelocity = 0.0f;
    ui_apply_stretch_dynamics_visual(scene, entity, *dynamics);

    if (options.onChanged)
    {
        options.onChanged(ui_stretch_dynamics_event(entity, point, *dynamics));
    }
    return true;
}

inline float ui_stretch_dynamics_spring_step(
    float value,
    float& velocity,
    float target,
    double deltaSeconds,
    float response,
    float dampingFraction)
{
    constexpr float twoPi = 6.28318530718f;
    ui_damped_spring_step(value, velocity, target, deltaSeconds,
        twoPi / std::max(response, 0.001f), std::max(dampingFraction, 0.0f));
    return value;
}

inline void ui_update_stretch_dynamics_settling(
    Renderer2DScene& scene,
    UiInputState& state,
    double currentTimeSeconds)
{
    entt::registry& registry = scene.registry();
    entt::entity pendingRelease = entt::null;
    glm::vec2 releasePoint {};
    auto view = registry.view<StretchDynamicsComponent>();
    view.each([&](entt::entity entity, StretchDynamicsComponent& dynamics) {
        if (!ui_stretch_dynamics_allowed(registry, entity, dynamics.options))
        {
            if (dynamics.options.enabled && dynamics.dragging && state.activeStretchDynamics == entity &&
                ui_stretch_dynamics_has_action(dynamics.options))
            {
                ui_reset_stretch_dynamics_visual(scene, entity);
                return;
            }
            if (state.activeStretchDynamics == entity)
            {
                pendingRelease = entity;
                releasePoint = dynamics.pressPoint + dynamics.displacement;
            }
            else dynamics.dragging = false;
            ui_reset_stretch_dynamics_visual(scene, entity);
            return;
        }
        if (dynamics.dragging && state.activeStretchDynamics != entity)
        {
            dynamics.dragging = false;
            dynamics.settling = true;
        }
        if (dynamics.dragging)
        {
            const double delta = dynamics.lastUpdateSeconds > 0.0 ?
                std::max(currentTimeSeconds - dynamics.lastUpdateSeconds, 0.0) : 0.0;
            dynamics.lastUpdateSeconds = currentTimeSeconds;
            const bool liquidMotion = ui_stretch_uses_liquid_motion(registry, entity, dynamics.options);
            if (liquidMotion || dynamics.options.dragResponse > 0.0f)
            {
                const auto previousScale = dynamics.scale;
                const auto previousOrigin = dynamics.scaleOrigin;
                const auto previousOffset = dynamics.offset;
                const float previousContact = dynamics.contact;
                const float previousBlur = dynamics.blurRadius;
                const float response = dynamics.options.dragResponse > 0.0f ? dynamics.options.dragResponse : 0.18f;
                for (int axis = 0; axis < 2; ++axis)
                {
                    dynamics.scale[axis] = ui_stretch_dynamics_spring_step(
                        dynamics.scale[axis], dynamics.scaleVelocity[axis], dynamics.targetScale[axis],
                        delta, response, 0.78f);
                    dynamics.scaleOrigin[axis] = ui_stretch_dynamics_spring_step(
                        dynamics.scaleOrigin[axis], dynamics.scaleOriginVelocity[axis], dynamics.targetScaleOrigin[axis],
                        delta, response, 1.0f);
                    dynamics.offset[axis] = ui_stretch_dynamics_spring_step(
                        dynamics.offset[axis], dynamics.offsetVelocity[axis], dynamics.targetOffset[axis],
                        delta, response, 0.82f);
                }
                if (liquidMotion)
                {
                    ui_damped_spring_step(dynamics.contact, dynamics.contactVelocity, 1.0f, delta, 38.0f, 1.0f);
                    const float shortSide = std::min(dynamics.baseRect.z, dynamics.baseRect.w);
                    const float aspect = shortSide / std::max({ dynamics.baseRect.z, dynamics.baseRect.w, 1.0f });
                    const float contactBlur = std::min(shortSide * 0.035f, 3.0f) * aspect * aspect * dynamics.contact;
                    const float progress = dynamics.inwardProgress;
                    const float targetBlur = std::max(contactBlur, dynamics.options.inwardBlurRadius * progress * progress * (3.0f - 2.0f * progress));
                    ui_damped_spring_step(dynamics.blurRadius, dynamics.blurVelocity, targetBlur, delta, 38.0f, 1.0f);
                }
                if (previousScale != dynamics.scale || previousOrigin != dynamics.scaleOrigin ||
                    previousOffset != dynamics.offset || previousContact != dynamics.contact || previousBlur != dynamics.blurRadius)
                    ui_apply_stretch_dynamics_visual(scene, entity, dynamics);
            }
            return;
        }
        if (!dynamics.settling)
        {
            return;
        }

        if (dynamics.lastUpdateSeconds <= 0.0)
        {
            dynamics.lastUpdateSeconds = currentTimeSeconds;
            return;
        }
        const double deltaSeconds = std::max(
            currentTimeSeconds - dynamics.lastUpdateSeconds,
            0.0);
        dynamics.lastUpdateSeconds = currentTimeSeconds;
        for (int axis = 0; axis < 2; ++axis)
        {
            dynamics.scale[axis] = ui_stretch_dynamics_spring_step(
                dynamics.scale[axis],
                dynamics.scaleVelocity[axis],
                1.0f,
                deltaSeconds,
                dynamics.options.returnResponse,
                dynamics.options.returnDampingFraction);
            dynamics.offset[axis] = ui_stretch_dynamics_spring_step(
                dynamics.offset[axis], dynamics.offsetVelocity[axis], 0.0f,
                deltaSeconds, dynamics.options.returnResponse, dynamics.options.returnDampingFraction);
        }
        ui_damped_spring_step(dynamics.contact, dynamics.contactVelocity, 0.0f, deltaSeconds, 42.0f, 1.0f);
        dynamics.blurRadius = ui_stretch_dynamics_spring_step(
            dynamics.blurRadius,
            dynamics.blurVelocity,
            0.0f,
            deltaSeconds,
            dynamics.options.returnResponse,
            dynamics.options.returnDampingFraction);
        dynamics.inwardProgress = 0.0f;

        const bool settled =
            glm::length(dynamics.scale - glm::vec2(1.0f)) < 0.0008f &&
            glm::length(dynamics.scaleVelocity) < 0.0008f &&
            glm::length(dynamics.offset) < 0.001f && glm::length(dynamics.offsetVelocity) < 0.001f &&
            dynamics.contact == 0.0f && dynamics.contactVelocity == 0.0f &&
            std::abs(dynamics.blurRadius) < 0.0008f &&
            std::abs(dynamics.blurVelocity) < 0.0008f;
        if (settled)
        {
            dynamics.scale = glm::vec2(1.0f);
            dynamics.scaleVelocity = glm::vec2(0.0f);
            dynamics.blurRadius = 0.0f;
            dynamics.blurVelocity = 0.0f;
            dynamics.offset = dynamics.offsetVelocity = {};
            dynamics.settling = false;
            dynamics.lastUpdateSeconds = 0.0;
        }
        ui_apply_stretch_dynamics_visual(scene, entity, dynamics);
    });
    if (pendingRelease != entt::null && state.activeStretchDynamics == pendingRelease)
    {
        ui_release_stretch_dynamics(scene, state, true, releasePoint);
        ui_reset_stretch_dynamics_visual(scene, pendingRelease);
    }
}

inline bool ui_dispatch_outside_pointer_press(Renderer2DScene& scene, entt::entity hit,
    const PointerInputEvent& event)
{
    auto& registry = scene.registry();
    std::vector<std::pair<entt::entity, std::function<void(const PointerInputEvent&)>>> pending;
    for (const auto entity : registry.view<OutsidePointerInputComponent>())
    {
        const auto& input = registry.get<OutsidePointerInputComponent>(entity);
        if (!input.enabled || !input.onPress || !ui_entity_visible_by_hierarchy(registry, entity)) continue;
        bool inside = false;
        auto current = hit;
        for (std::uint32_t depth = 0u; depth < 64u && registry.valid(current); ++depth)
        {
            if (current == entity) { inside = true; break; }
            const auto* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent || parent->parent == current) break;
            current = parent->parent;
        }
        if (!inside) pending.emplace_back(entity, input.onPress);
    }
    bool delivered = false;
    for (auto& [entity, callback] : pending)
        if (const auto* input = registry.try_get<OutsidePointerInputComponent>(entity);
            input && input->enabled && ui_entity_visible_by_hierarchy(registry, entity))
        {
            auto outsideEvent = event;
            outsideEvent.target = entity;
            callback(outsideEvent);
            delivered = true;
        }
    return delivered;
}

inline void ui_handle_window_focus(Renderer2DScene& scene, UiInputState& state, bool focused)
{
    const bool lostFocus = state.hasWindowFocusSample && state.windowFocused && !focused;
    state.hasWindowFocusSample = true;
    state.windowFocused = focused;
    if (!lostFocus) return;
    if (state.pointerInputCapture == state.activeTextSelection) state.pointerInputCapture = entt::null;
    state.activeTextSelection = entt::null;
    auto& registry = scene.registry();
    std::vector<std::pair<entt::entity, std::function<void()>>> pending;
    for (const auto entity : registry.view<OutsidePointerInputComponent>())
    {
        const auto& input = registry.get<OutsidePointerInputComponent>(entity);
        if (input.enabled && input.onFocusLost && ui_entity_visible_by_hierarchy(registry, entity))
            pending.emplace_back(entity, input.onFocusLost);
    }
    for (auto& [entity, callback] : pending)
        if (const auto* input = registry.try_get<OutsidePointerInputComponent>(entity);
            input && input->enabled && ui_entity_visible_by_hierarchy(registry, entity))
            callback();
}

inline void ui_handle_pointer_button(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state,
    const UiPointerButtonInput& input)
{
    if (input.action != UiInputAction::ePress && input.action != UiInputAction::eRelease)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    entt::entity hit = input.hasPoint ? scene.entity_at(input.point) : entt::null;
    if (input.action == UiInputAction::ePress && ui_dispatch_outside_pointer_press(scene, hit,
        PointerInputEvent { entt::null, input.point, input.button, input.platformButton, input.modifiers }))
        hit = input.hasPoint ? scene.entity_at(input.point) : entt::null;

    const entt::entity buttonOwner = ui_component_owner<ButtonInputComponent>(registry, hit);
    const entt::entity textOwner = ui_component_owner<TextInputComponent>(registry, hit);
    const entt::entity scrollOwner = ui_component_owner<ScrollInputComponent>(registry, hit);
    const entt::entity scrollBarOwner = ui_component_owner<ScrollBarInputComponent>(registry, hit);
    const entt::entity sliderOwner = ui_component_owner<SliderInputComponent>(registry, hit);
    const entt::entity stretchOwner = ui_component_owner<StretchDynamicsComponent>(registry, hit);
    const entt::entity stretchBlockerOwner =
        ui_component_owner<StretchDynamicsBlockerComponent>(registry, hit);
    const StretchDynamicsBlockerComponent* stretchBlocker =
        stretchBlockerOwner != entt::null ?
            registry.try_get<StretchDynamicsBlockerComponent>(stretchBlockerOwner) :
            nullptr;
    bool stretchConsumedRelease = false;
    uint32_t resizeEdges = ePanelResizeNone;
    entt::entity resizeOwner = entt::null;

    if (input.button == PointerButton::eLeft &&
        input.action == UiInputAction::eRelease)
    {
        ui_update_text_selection_drag(scene, fontAtlas, state, true, input.hasPoint, input.point);
        ui_update_text_selection_drag(scene, fontAtlas, state, false, input.hasPoint, input.point);
        stretchConsumedRelease = ui_release_stretch_dynamics(
            scene,
            state,
            input.hasPoint,
            input.point);
    }

    if (input.button == PointerButton::eLeft &&
        input.action == UiInputAction::eRelease &&
        state.resizingPanel != entt::null)
    {
        if (registry.valid(state.resizingPanel))
        {
            if (ResizablePanelComponent* resize = registry.try_get<ResizablePanelComponent>(state.resizingPanel))
            {
                resize->activeEdges = ePanelResizeNone;
            }
        }
        state.resizingPanel = entt::null;
        state.activeResizeEdges = ePanelResizeNone;
        state.pointerInputCapture = entt::null;
        return;
    }

    if (input.button == PointerButton::eLeft)
    {
        if (input.action == UiInputAction::ePress && input.hasPoint)
        {

            state.pointerInputCapture = ui_input_owner_from_hit(registry, hit);
        }
        else if (input.action == UiInputAction::eRelease)
        {
            state.pointerInputCapture = entt::null;
        }
    }

    if (input.action == UiInputAction::ePress && input.button == PointerButton::eLeft)
    {

        const bool alreadyFocused = state.focusedTextInput == textOwner;
        ui_focus_text_input(scene, fontAtlas, state, textOwner);
        if (input.hasPoint && textOwner != entt::null && registry.get<TextInputComponent>(textOwner).enabled)
        {
            const bool consecutive = state.lastTextClickEntity == textOwner && input.timeSeconds > 0.0 &&
                state.lastTextClickSeconds >= 0.0 && input.timeSeconds >= state.lastTextClickSeconds &&
                input.timeSeconds - state.lastTextClickSeconds <= input.multiClickInterval &&
                glm::length(input.point - state.lastTextClickPoint) <= 5.0f;
            state.textClickCount = input.clickCount ? input.clickCount : consecutive ? state.textClickCount % 3u + 1u : 1u;
            state.lastTextClickEntity = textOwner;
            state.lastTextClickSeconds = input.timeSeconds;
            state.lastTextClickPoint = input.point;
            ui_begin_text_selection(scene, fontAtlas, state, textOwner, input.point,
                input.modifiers.shift && alreadyFocused, state.textClickCount);
        }
        if (input.hasPoint && stretchOwner != entt::null &&
            sliderOwner == entt::null &&
            (scrollOwner == entt::null || buttonOwner == stretchOwner || textOwner == stretchOwner) &&
            scrollBarOwner == entt::null &&
            (!stretchBlocker || !stretchBlocker->enabled) &&
            (textOwner == entt::null ||
                registry.get<StretchDynamicsComponent>(stretchOwner).options.allowTextInputDrag))
        {
            ui_begin_stretch_dynamics(
                scene,
                state,
                stretchOwner,
                input.point);
        }
    }

    if (input.action == UiInputAction::ePress)
    {
        if (buttonOwner != entt::null)
        {

            ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(buttonOwner);
            if (button && button->enabled)
            {
                if (input.button == PointerButton::eLeft)
                {
                    button->leftPressed = true;
                }
                else if (input.button == PointerButton::eRight)
                {
                    button->rightPressed = true;
                }
                state.pressedInputEntity = buttonOwner;
                PointerInputEvent event { buttonOwner, input.point, input.button, input.platformButton, input.modifiers };
                if (button->onPress)
                {
                    button->onPress(event);
                }
                ui_update_button_visual(scene, buttonOwner);
                return;
            }
        }

        if (scrollBarOwner != entt::null)
        {

            ScrollBarInputComponent* scrollBar = registry.try_get<ScrollBarInputComponent>(scrollBarOwner);
            if (scrollBar && scrollBar->enabled && input.button == PointerButton::eLeft)
            {
                const glm::vec4 thumbRect =
                    scrollBar->thumb != entt::null && registry.valid(scrollBar->thumb) ?
                        ui_entity_framebuffer_rect(registry, scrollBar->thumb) :
                        glm::vec4(input.point.x, input.point.y, 0.0f, 0.0f);
                scrollBar->dragGrabOffsetY = ui_scrollbar_point_hits_thumb(registry, *scrollBar, input.point) ?
                    std::clamp(input.point.y - thumbRect.y, 0.0f, std::max(thumbRect.w, 0.0f)) :
                    thumbRect.w * 0.5f;
                scrollBar->dragging = true;
                state.activeScrollBar = scrollBarOwner;
                state.pointerInputCapture = scrollBarOwner;
                ui_apply_scrollbar_point(
                    scene,
                    scrollBarOwner,
                    input.point,
                    scrollBar->dragGrabOffsetY,
                    input.modifiers);
                return;
            }
        }

        if (sliderOwner != entt::null)
        {

            SliderInputComponent* slider = registry.try_get<SliderInputComponent>(sliderOwner);
            if (slider && slider->enabled && input.button == PointerButton::eLeft)
            {
                slider->dragging = true;
                slider->liquidPressPoint = input.point;
                slider->liquidDragMoved = false;
                slider->liquidDrag.tracking = false;
                slider->liquidInteraction.heldSeconds = 0.0;
                state.activeSlider = sliderOwner;
                state.pointerInputCapture = sliderOwner;
                const bool changed = ui_set_slider_value_from_point(scene, sliderOwner, input.point);
                ui_emit_slider_change_if_needed(*slider, sliderOwner, input.point, changed);
                if (changed)
                {
                    ui_mark_moving_entity_dirty(scene, sliderOwner, 0.25f);
                    ui_update_slider_visual(scene, fontAtlas, sliderOwner);
                }
                return;
            }
        }

        if (input.button == PointerButton::eLeft && input.hasPoint)
        {
            resizeOwner = ui_resizable_panel_at(scene, input.point, resizeEdges);
            if (resizeOwner != entt::null)
            {
                ui_focus_text_input(scene, fontAtlas, state, entt::null);
                state.resizingPanel = resizeOwner;
                state.draggedPanel = entt::null;
                state.activeResizeEdges = resizeEdges;
                state.pointerInputCapture = resizeOwner;
                state.resizeStartPoint = input.point;

                const glm::vec4 rect = ui_entity_framebuffer_rect(registry, resizeOwner);
                state.resizeStartMin = { rect.x, rect.y };
                state.resizeStartSize = { rect.z, rect.w };
                if (const Layout2DComponent* layout = registry.try_get<Layout2DComponent>(resizeOwner))
                {
                    state.resizeStartLayoutOffset = layout->offset;
                }
                else
                {
                    state.resizeStartLayoutOffset = glm::vec2(0.0f);
                }
                if (ResizablePanelComponent* resize = registry.try_get<ResizablePanelComponent>(resizeOwner))
                {
                    resize->activeEdges = resizeEdges;
                }
                return;
            }
        }

        return;
    }

    const entt::entity releasedEntity = state.pressedInputEntity;

    ButtonInputComponent* button = (releasedEntity != entt::null && registry.valid(releasedEntity)) ?
        registry.try_get<ButtonInputComponent>(releasedEntity) :
        nullptr;
    if (!button)
    {
        state.pressedInputEntity = entt::null;
        if (input.button == PointerButton::eLeft &&
            state.activeScrollBar != entt::null &&
            registry.valid(state.activeScrollBar))
        {
            ScrollBarInputComponent* scrollBar = registry.try_get<ScrollBarInputComponent>(state.activeScrollBar);
            if (scrollBar)
            {
                if (input.hasPoint)
                {
                    ui_apply_scrollbar_point(
                        scene,
                        state.activeScrollBar,
                        input.point,
                        scrollBar->dragGrabOffsetY,
                        input.modifiers);
                }
                scrollBar->dragging = false;
                scrollBar->dragGrabOffsetY = 0.0f;
            }
            state.activeScrollBar = entt::null;
            state.pointerInputCapture = entt::null;
        }

        if (input.button == PointerButton::eLeft &&
            state.activeSlider != entt::null &&
            registry.valid(state.activeSlider))
        {
            SliderInputComponent* slider = registry.try_get<SliderInputComponent>(state.activeSlider);
            if (slider)
            {
                slider->dragging = false;
                if (input.hasPoint)
                {
                    const bool changed = ui_set_slider_value_from_point(scene, state.activeSlider, input.point);
                    ui_emit_slider_change_if_needed(*slider, state.activeSlider, input.point, changed);
                    if (changed)
                    {
                        ui_mark_moving_entity_dirty(scene, state.activeSlider, 0.25f);
                        ui_update_slider_visual(scene, fontAtlas, state.activeSlider);
                    }
                }
                ui_emit_slider_commit(
                    *slider,
                    state.activeSlider,
                    input.hasPoint ? input.point : glm::vec2(0.0f));
                state.activeSlider = entt::null;
                state.pointerInputCapture = entt::null;
            }
        }
        return;
    }

    if (input.button == PointerButton::eLeft)
    {
        if (!button->leftPressed)
        {
            return;
        }
        button->leftPressed = false;
    }
    else if (input.button == PointerButton::eRight)
    {
        if (!button->rightPressed)
        {
            return;
        }
        button->rightPressed = false;
    }
    if (!button->leftPressed && !button->rightPressed)
    {
        state.pressedInputEntity = entt::null;
    }

    PointerInputEvent event { releasedEntity, input.point, input.button, input.platformButton, input.modifiers };
    if (button->onRelease)
    {
        button->onRelease(event);
    }

    if (releasedEntity == buttonOwner && !stretchConsumedRelease)
    {
        if (input.button == PointerButton::eRight && button->onRightClick)
        {
            button->onRightClick(event);
        }
        else if (input.button == PointerButton::eLeft && button->onClick)
        {
            button->onClick(event);
        }
    }
    ui_update_button_visual(scene, releasedEntity);
}

inline bool ui_recover_pointer_button_release(
    Renderer2DScene& scene,
    UiInputState& state,
    bool leftButtonPressed,
    bool rightButtonPressed,
    glm::vec2 point)
{
    const entt::entity entity = state.pressedInputEntity;
    if (entity == entt::null)
    {



        if (!leftButtonPressed && state.pointerInputCapture != entt::null)
        {
            state.activeTextSelection = entt::null;
            state.pointerInputCapture = entt::null;
            return true;
        }
        return false;
    }

    entt::registry& registry = scene.registry();
    ButtonInputComponent* button = registry.valid(entity) ?
        registry.try_get<ButtonInputComponent>(entity) : nullptr;
    if (!button)
    {
        state.pressedInputEntity = entt::null;
        if (state.pointerInputCapture == entity)
        {
            state.pointerInputCapture = entt::null;
        }
        return true;
    }

    const bool lostLeftRelease = button->leftPressed && !leftButtonPressed;
    const bool lostRightRelease = button->rightPressed && !rightButtonPressed;
    if (!lostLeftRelease && !lostRightRelease)
    {
        return false;
    }




    const auto onRelease = button->onRelease;
    if (lostLeftRelease)
    {
        button->leftPressed = false;
        if (state.pointerInputCapture == entity)
        {
            state.pointerInputCapture = entt::null;
        }
    }
    if (lostRightRelease)
    {
        button->rightPressed = false;
    }
    if (!button->leftPressed && !button->rightPressed)
    {
        state.pressedInputEntity = entt::null;
    }

    if (lostLeftRelease)
    {
        ui_release_stretch_dynamics(scene, state, false, point);
    }
    for (const auto released : { PointerButton::eLeft, PointerButton::eRight })
    {
        if ((released == PointerButton::eLeft ? lostLeftRelease : lostRightRelease) &&
            onRelease && registry.valid(entity) &&
            registry.all_of<ButtonInputComponent>(entity))
        {
            onRelease(PointerInputEvent { entity, point, released });
        }
    }


    if (!registry.valid(entity) || !registry.all_of<ButtonInputComponent>(entity))
    {
        if (state.pressedInputEntity == entity)
        {
            state.pressedInputEntity = entt::null;
        }
        if (state.pointerInputCapture == entity)
        {
            state.pointerInputCapture = entt::null;
        }
    }
    else
    {
        ui_update_button_visual(scene, entity);
    }
    return true;
}

inline void ui_handle_scroll(Renderer2DScene& scene, const UiScrollInput& input)
{

    if (!input.hasPoint)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    const entt::entity hit = scene.entity_at(input.point);
    const entt::entity scrollOwner = ui_component_owner<ScrollInputComponent>(registry, hit);
    ScrollInputComponent* scroll = registry.try_get<ScrollInputComponent>(scrollOwner);
    if (!scroll || !scroll->enabled)
    {
        return;
    }

    const float previousOffset = scroll->offset;
    scroll->offset = std::clamp(
        scroll->offset - static_cast<float>(input.offsetY) * scroll->step,
        scroll->minOffset,
        scroll->maxOffset);
    if (std::abs(scroll->offset - previousOffset) <= 1e-4f)
    {
        return;
    }

    ScrollInputEvent event { scrollOwner, input.point, input.offsetX, input.offsetY, input.modifiers };
    if (scroll->onScroll)
    {
        scroll->onScroll(event);
    }
    ui_wake_scrollbars_for_scroll_target(scene, scrollOwner);
}

inline bool ui_handle_key(Renderer2DScene& scene, const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state, const UiKeyInput& event)
{
    if (event.action != UiInputAction::ePress && event.action != UiInputAction::eRepeat) return false;
    auto& registry = scene.registry();
    const auto entity = state.focusedTextInput;
    auto* input = registry.try_get<TextInputComponent>(entity);
    const auto matches = [&](int key) { return key != 0 && event.key == key; };
    const bool press = event.action == UiInputAction::ePress;
    if (input && input->enabled)
    {
        ui_text_input_sync(*input);
        const auto& keys = event.keys;

        if (!keys.macOsBindings && event.modifiers.control && event.modifiers.alt) return false;
        const bool command = keys.macOsBindings && event.modifiers.super;
        const bool primary = event.modifiers.control || command;
        const bool word = event.modifiers.control || (keys.macOsBindings && event.modifiers.alt);
        const bool extend = event.modifiers.shift;
        bool handled = true, changed = false, vertical = false;
        if (matches(keys.escape))
        {
            if (press) ui_focus_text_input(scene, fontAtlas, state, entt::null);
            return true;
        }
        if (primary && matches(keys.selectAll))
        {
            input->selectionAnchorByte = 0u;
            ui_text_move_caret(*input, input->value.size(), true);
        }
        else if ((primary && (matches(keys.copy) || matches(keys.cut))) ||
            (event.modifiers.control && matches(keys.insert)) || (extend && !primary && matches(keys.deleteKey)))
        {
            const auto selection = ui_text_selection(*input);
            const bool cut = matches(keys.cut) || (extend && matches(keys.deleteKey));
            if (press && selection.start != selection.end)
            {
                if (event.writeClipboard) event.writeClipboard(input->value.substr(selection.start, selection.end - selection.start));
                input = registry.try_get<TextInputComponent>(entity);
                if (!input) return true;
                if (cut) changed = ui_text_erase(*input, true, false, false, event.timeSeconds);
            }
            else if (selection.start == selection.end && extend && matches(keys.deleteKey))
                changed = ui_text_erase(*input, true, false, false, event.timeSeconds);
        }
        else if ((primary && matches(keys.paste)) || (extend && matches(keys.insert)))
        {
            if (press && !event.clipboardText.empty()) changed = ui_text_replace_selection(*input,
                event.clipboardText, TextInputEditKind::eReplace, event.timeSeconds);
        }
        else if ((primary && (matches(keys.undo) || matches(keys.redo))) ||
            (!keys.macOsBindings && event.modifiers.alt && matches(keys.backspace)))
        {
            if (press) changed = ui_text_undo(*input, matches(keys.redo) || extend);
        }
        else if (matches(keys.backspace) || matches(keys.deleteKey))
            changed = ui_text_erase(*input, matches(keys.deleteKey), word, command, event.timeSeconds);
        else if (primary && (matches(keys.clearLine) || matches(keys.killLine)))
            changed = ui_text_erase(*input, matches(keys.killLine), false, true, event.timeSeconds);
        else if (matches(keys.left) || matches(keys.right))
        {
            const bool right = matches(keys.right);
            const auto* text = registry.try_get<TextComponent>(input->textEntity);
            const bool rtl = fontAtlas.layout_text(input->value, text ? text->fontSize : 14.0f).rightToLeft;
            const bool forward = right != rtl;
            const auto selection = ui_text_selection(*input);
            std::size_t position = input->caretByte;
            if (!extend && selection.start != selection.end && !word && !command)
                position = forward ? selection.end : selection.start;
            else if (command) position = forward ? ui_text_line_end(*input, position) : ui_text_line_start(*input, position);
            else if (word) position = forward ? ui_text_next_word(*input, position) : ui_text_previous_word(*input, position);
            else position = forward ? ui_text_next_character(*input, position) : ui_text_previous_character(*input, position);
            ui_text_move_caret(*input, position, extend);
        }
        else if (matches(keys.home) || matches(keys.end))
        {
            const bool end = matches(keys.end);
            ui_text_move_caret(*input, primary ? (end ? input->value.size() : 0u) :
                end ? ui_text_line_end(*input, input->caretByte) : ui_text_line_start(*input, input->caretByte), extend);
        }
        else if (matches(keys.up) || matches(keys.down) || matches(keys.pageUp) || matches(keys.pageDown))
        {
            const bool down = matches(keys.down) || matches(keys.pageDown);
            std::size_t position;
            if (command || !input->multiline || (primary && (matches(keys.pageUp) || matches(keys.pageDown))))
                position = down ? input->value.size() : 0u;
            else if (event.modifiers.control)
            {
                const auto start = ui_text_line_start(*input, input->caretByte);
                if (down)
                {
                    const auto end = ui_text_line_end(*input, input->caretByte);
                    position = end < input->value.size() ? end + 1u : input->value.size();
                }
                else position = input->caretByte > start ? start : start ? ui_text_line_start(*input, start - 1u) : 0u;
            }
            else
            {
                vertical = true;
                int rows = 1;
                if (matches(keys.pageUp) || matches(keys.pageDown))
                {
                    const auto* shape = registry.try_get<ShapeComponent>(entity);
                    const auto* text = registry.try_get<TextComponent>(input->textEntity);
                    if (shape) rows = std::max(1, static_cast<int>(shape->size.y / ((text ? text->fontSize : 14.0f) * 1.25f)));
                }
                position = ui_text_input_vertical_target(fontAtlas, registry, *input, down ? rows : -rows);
            }
            ui_text_move_caret(*input, position, extend);
        }
        else if (matches(keys.enter))
        {
            if (input->multiline && !primary)
                changed = ui_text_replace_selection(*input, "\n", TextInputEditKind::eTyping, event.timeSeconds);
            else if (press && input->onSubmitted)
            {
                const auto callback = input->onSubmitted;
                const auto value = input->value;
                callback(value);
            }
        }
        else if (matches(keys.insert) && !event.modifiers.any())
        {
            if (press && !input->readOnly) input->overwrite = !input->overwrite;
        }
        else if (matches(keys.tab) && !primary && !event.modifiers.alt)
        {
            if (!press) return true;
            std::vector<entt::entity> fields;
            for (const auto candidate : registry.view<TextInputComponent>())
                if (registry.get<TextInputComponent>(candidate).enabled && ui_entity_visible_by_hierarchy(registry, candidate)) fields.push_back(candidate);
            std::sort(fields.begin(), fields.end(), [&](entt::entity a, entt::entity b) {
                const auto left = ui_entity_base_framebuffer_rect(registry, a), right = ui_entity_base_framebuffer_rect(registry, b);
                if (std::round(left.y) != std::round(right.y)) return std::round(left.y) < std::round(right.y);
                if (left.x != right.x) return ui_layout_is_rtl(registry, a) ? left.x > right.x : left.x < right.x;
                return entt::to_integral(a) < entt::to_integral(b);
            });
            if (!fields.empty())
            {
                const auto found = std::find(fields.begin(), fields.end(), entity);
                const auto index = found == fields.end() ? 0u : static_cast<std::size_t>(found - fields.begin());
                const auto next = fields[(index + (extend ? fields.size() - 1u : 1u)) % fields.size()];
                ui_focus_text_input(scene, fontAtlas, state, next);
                auto& focused = registry.get<TextInputComponent>(next);
                ui_text_input_sync(focused);
                focused.selectionAnchorByte = 0u;
                ui_text_move_caret(focused, focused.value.size(), true);
                ui_refresh_text_input(scene, fontAtlas, next, false);
            }
            return true;
        }
        else handled = false;
        if (handled)
        {
            if (auto* current = registry.try_get<TextInputComponent>(entity); current && !vertical)
                current->preferredCaretX = std::numeric_limits<float>::quiet_NaN();
            ui_refresh_text_input(scene, fontAtlas, entity, changed);
            return true;
        }

        if (!primary && !event.modifiers.alt && !event.modifiers.super) return false;
    }
    else if (entity != entt::null) state.focusedTextInput = entt::null;
    if (press)
    {
        std::vector<std::function<void()>> callbacks;
        for (const auto candidate : registry.view<KeyboardShortcutComponent>())
        {
            const auto& shortcut = registry.get<KeyboardShortcutComponent>(candidate);
            if (ui_shortcut_matches(shortcut, event.key, event.modifiers) && shortcut.onTriggered)
                callbacks.push_back(shortcut.onTriggered);
        }
        for (const auto& callback : callbacks) callback();
        return !callbacks.empty();
    }
    return false;
}

inline void ui_handle_drop(
    Renderer2DScene& scene,
    const UiDropInput& input)
{
    if (!input.hasPoint || input.paths.empty())
    {
        return;
    }

    entt::registry& registry = scene.registry();
    const entt::entity hit = scene.entity_at(input.point);
    const entt::entity dropOwner = ui_component_owner<DropTargetComponent>(registry, hit);
    DropTargetComponent* dropTarget = registry.try_get<DropTargetComponent>(dropOwner);
    if (!dropTarget || !dropTarget->enabled)
    {
        return;
    }

    dropTarget->lastDroppedPaths = input.paths;
    FileDropInputEvent event { dropOwner, input.point, dropTarget->lastDroppedPaths };
    if (dropTarget->onDrop)
    {
        dropTarget->onDrop(event);
    }
    ui_update_drop_target_visual(scene, dropOwner);
    scene.activate_dynamic(dropOwner, 0.5f);
}

inline void ui_update_hover_regions(Renderer2DScene& scene, bool hasPoint,
    glm::vec2 point, double currentTimeSeconds)
{
    auto& registry = scene.registry();
    for (const auto entity : registry.view<UiHoverRegionComponent>())
    {
        auto& region = registry.get<UiHoverRegionComponent>(entity);
        const bool hovered = region.enabled && hasPoint &&
            ui_entity_visible_by_hierarchy(registry, entity) &&
            ui_point_in_rect(ui_entity_framebuffer_rect(registry, entity), point);
        if (hovered == region.hovered) continue;
        region.hovered = hovered;
        if (region.onHoverChanged) region.onHoverChanged(hovered, currentTimeSeconds);
    }
}

inline UiHoverResult ui_update_input_hover_from_hit(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state,
    bool hasPoint,
    glm::vec2 point,
    entt::entity resolvedHit)
{
    entt::registry& registry = scene.registry();
    const entt::entity hit = hasPoint ? resolvedHit : entt::null;
    entt::entity buttonOwner = ui_component_owner<ButtonInputComponent>(registry, hit);
    entt::entity textOwner = ui_component_owner<TextInputComponent>(registry, hit);
    entt::entity dropOwner = ui_component_owner<DropTargetComponent>(registry, hit);
    entt::entity scrollOwner = ui_component_owner<ScrollInputComponent>(registry, hit);
    entt::entity scrollBarOwner = ui_component_owner<ScrollBarInputComponent>(registry, hit);
    entt::entity sliderOwner = ui_component_owner<SliderInputComponent>(registry, hit);

    auto hovered_owner_contains_point = [&](entt::entity entity) {
        return hasPoint &&
            entity != entt::null &&
            registry.valid(entity) &&
            ui_entity_visible_by_hierarchy(registry, entity) &&
            ui_point_in_rect(ui_entity_framebuffer_rect(registry, entity), point);
    };

    const bool resolvedInputOwner =
        buttonOwner != entt::null ||
        textOwner != entt::null ||
        dropOwner != entt::null ||
        scrollBarOwner != entt::null ||
        sliderOwner != entt::null;
    auto clear_other_hover_owners = [&](
        bool keepButton,
        bool keepSlider,
        bool keepScrollBar,
        bool keepDrop) {
        if (!keepButton)
        {
            buttonOwner = entt::null;
        }
        if (!keepSlider)
        {
            sliderOwner = entt::null;
        }
        if (!keepScrollBar)
        {
            scrollBarOwner = entt::null;
        }
        if (!keepDrop)
        {
            dropOwner = entt::null;
        }
        textOwner = entt::null;
    };




    if (!resolvedInputOwner)
    {
        if (hovered_owner_contains_point(state.hoveredButton))
        {
            buttonOwner = state.hoveredButton;
            clear_other_hover_owners(true, false, false, false);
        }
        else if (hovered_owner_contains_point(state.hoveredSlider))
        {
            sliderOwner = state.hoveredSlider;
            clear_other_hover_owners(false, true, false, false);
        }
        else if (hovered_owner_contains_point(state.hoveredScrollBar))
        {
            scrollBarOwner = state.hoveredScrollBar;
            clear_other_hover_owners(false, false, true, false);
        }
        else if (hovered_owner_contains_point(state.hoveredDropTarget))
        {
            dropOwner = state.hoveredDropTarget;
            clear_other_hover_owners(false, false, false, true);
        }
        else
        {
            buttonOwner = entt::null;
            sliderOwner = entt::null;
            scrollBarOwner = entt::null;
            dropOwner = entt::null;
        }
    }

    uint32_t resizeEdges = state.activeResizeEdges;
    const bool frontInputOwner =
        buttonOwner != entt::null ||
        textOwner != entt::null ||
        sliderOwner != entt::null ||
        scrollBarOwner != entt::null ||
        dropOwner != entt::null;
    const entt::entity resizeOwner = state.resizingPanel != entt::null ?
        state.resizingPanel :
        (hasPoint && !frontInputOwner) ? ui_resizable_panel_at_from_hit(scene, hit, point, resizeEdges) : entt::null;

    auto update_hovered_button = [&](entt::entity entity, bool hovered) {
        if (entity == entt::null || !registry.valid(entity))
        {
            return;
        }
        if (ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(entity);
            button && button->hovered != hovered)
        {
            button->hovered = hovered;
            if (button->onHoverChanged)
            {
                button->onHoverChanged(hovered);
            }
            ui_update_button_visual(scene, entity);
        }
    };

    auto update_hovered_drop = [&](entt::entity entity, bool hovered) {
        if (entity == entt::null || !registry.valid(entity))
        {
            return;
        }
        if (DropTargetComponent* dropTarget = registry.try_get<DropTargetComponent>(entity);
            dropTarget && dropTarget->hovered != hovered)
        {
            dropTarget->hovered = hovered;
            ui_update_drop_target_visual(scene, entity);
        }
    };

    auto update_hovered_scroll = [&](entt::entity entity, bool hovered) {
        if (entity == entt::null || !registry.valid(entity))
        {
            return;
        }
        if (ScrollInputComponent* scroll = registry.try_get<ScrollInputComponent>(entity))
        {
            scroll->hovered = hovered;
        }
    };

    auto update_hovered_scrollbar = [&](entt::entity entity, bool hovered) {
        if (entity == entt::null || !registry.valid(entity))
        {
            return;
        }
        if (ScrollBarInputComponent* scrollBar = registry.try_get<ScrollBarInputComponent>(entity))
        {
            if (scrollBar->hovered != hovered)
            {
                scrollBar->hovered = hovered;
                scrollBar->wakeRequested = true;
            }
        }
    };

    auto update_hovered_slider = [&](entt::entity entity, bool hovered) {
        if (entity == entt::null || !registry.valid(entity))
        {
            return;
        }
        if (SliderInputComponent* slider = registry.try_get<SliderInputComponent>(entity))
        {
            slider->hovered = hovered;
            ui_update_slider_visual(scene, fontAtlas, entity);
        }
    };

    auto update_hovered_resize = [&](entt::entity entity, bool hovered, uint32_t edges) {
        if (entity == entt::null || !registry.valid(entity))
        {
            return;
        }
        if (ResizablePanelComponent* resize = registry.try_get<ResizablePanelComponent>(entity))
        {
            resize->hovered = hovered;
            resize->activeEdges = hovered ? edges : ePanelResizeNone;
        }
    };

    if (buttonOwner != state.hoveredButton)
    {
        ui_clear_button_cursor_shadow(scene, state.hoveredButton);
        update_hovered_button(state.hoveredButton, false);
        state.hoveredButton = buttonOwner;
        update_hovered_button(state.hoveredButton, true);
    }
    if (state.hoveredButton != entt::null && hasPoint)
    {
        ui_update_button_cursor_shadow(scene, state.hoveredButton, point);
    }

    if (sliderOwner != state.hoveredSlider)
    {
        update_hovered_slider(state.hoveredSlider, false);
        state.hoveredSlider = sliderOwner;
        update_hovered_slider(state.hoveredSlider, true);
    }

    if (dropOwner != state.hoveredDropTarget)
    {
        update_hovered_drop(state.hoveredDropTarget, false);
        state.hoveredDropTarget = dropOwner;
        update_hovered_drop(state.hoveredDropTarget, true);
    }

    if (scrollOwner != state.hoveredScrollTarget)
    {
        update_hovered_scroll(state.hoveredScrollTarget, false);
        state.hoveredScrollTarget = scrollOwner;
        update_hovered_scroll(state.hoveredScrollTarget, true);
    }

    if (scrollBarOwner != state.hoveredScrollBar)
    {
        update_hovered_scrollbar(state.hoveredScrollBar, false);
        state.hoveredScrollBar = scrollBarOwner;
        update_hovered_scrollbar(state.hoveredScrollBar, true);
    }

    if (resizeOwner != state.hoveredResizePanel)
    {
        update_hovered_resize(state.hoveredResizePanel, false, ePanelResizeNone);
        state.hoveredResizePanel = resizeOwner;
    }
    update_hovered_resize(state.hoveredResizePanel, state.hoveredResizePanel != entt::null, resizeEdges);

    return {
        textOwner,
        buttonOwner,
        sliderOwner,
        scrollBarOwner,
        resizeEdges,
        ui_cursor_kind_for_hover(registry, textOwner, buttonOwner, sliderOwner, scrollBarOwner, resizeEdges)
    };
}

inline UiHoverResult ui_update_input_hover(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state,
    bool hasPoint,
    glm::vec2 point)
{
    return ui_update_input_hover_from_hit(scene, fontAtlas, state, hasPoint, point,
        hasPoint ? scene.entity_at(point) : entt::null);
}

inline bool ui_update_slider_drag(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state,
    bool leftButtonPressed,
    bool hasPoint,
    glm::vec2 point)
{
    if (state.activeSlider == entt::null)
    {
        return false;
    }

    entt::registry& registry = scene.registry();
    if (!registry.valid(state.activeSlider))
    {
        state.activeSlider = entt::null;
        return false;
    }

    SliderInputComponent* slider = registry.try_get<SliderInputComponent>(state.activeSlider);
    if (!slider || !slider->dragging)
    {
        return false;
    }

    if (!leftButtonPressed)
    {
        ui_emit_slider_commit(
            *slider,
            state.activeSlider,
            hasPoint ? point : glm::vec2(0.0f));
        slider->dragging = false;
        state.activeSlider = entt::null;
        state.pointerInputCapture = entt::null;
        return false;
    }

    if (!hasPoint)
    {
        return true;
    }

    slider->liquidDragMoved = slider->liquidDragMoved || glm::length(point - slider->liquidPressPoint) > 2.0f;
    const bool changed = ui_set_slider_value_from_point(scene, state.activeSlider, point);
    if (!changed)
    {
        return true;
    }

    ui_mark_moving_entity_dirty(scene, state.activeSlider, 0.25f);
    ui_emit_slider_change_if_needed(*slider, state.activeSlider, point, changed);
    ui_update_slider_visual(scene, fontAtlas, state.activeSlider);
    return true;
}

inline bool ui_update_scrollbar_drag(
    Renderer2DScene& scene,
    UiInputState& state,
    bool leftButtonPressed,
    bool hasPoint,
    glm::vec2 point)
{
    if (state.activeScrollBar == entt::null)
    {
        return false;
    }

    entt::registry& registry = scene.registry();
    if (!registry.valid(state.activeScrollBar))
    {
        state.activeScrollBar = entt::null;
        state.pointerInputCapture = entt::null;
        return false;
    }

    ScrollBarInputComponent* scrollBar = registry.try_get<ScrollBarInputComponent>(state.activeScrollBar);
    if (!scrollBar || !scrollBar->dragging)
    {
        state.activeScrollBar = entt::null;
        state.pointerInputCapture = entt::null;
        return false;
    }

    if (!leftButtonPressed)
    {
        scrollBar->dragging = false;
        scrollBar->dragGrabOffsetY = 0.0f;
        state.activeScrollBar = entt::null;
        state.pointerInputCapture = entt::null;
        return false;
    }

    if (!hasPoint)
    {
        return true;
    }

    ui_apply_scrollbar_point(scene, state.activeScrollBar, point, scrollBar->dragGrabOffsetY);
    return true;
}

inline bool ui_update_panel_resize(
    Renderer2DScene& scene,
    UiInputState& state,
    bool leftButtonPressed,
    bool hasPoint,
    glm::vec2 point,
    glm::vec2 bounds)
{
    if (state.resizingPanel == entt::null)
    {
        return false;
    }

    entt::registry& registry = scene.registry();
    if (!leftButtonPressed || !registry.valid(state.resizingPanel))
    {
        if (state.resizingPanel != entt::null && registry.valid(state.resizingPanel))
        {
            if (ResizablePanelComponent* resize = registry.try_get<ResizablePanelComponent>(state.resizingPanel))
            {
                resize->activeEdges = ePanelResizeNone;
            }
        }
        state.resizingPanel = entt::null;
        state.activeResizeEdges = ePanelResizeNone;
        state.pointerInputCapture = entt::null;
        return false;
    }

    ResizablePanelComponent* resize = registry.try_get<ResizablePanelComponent>(state.resizingPanel);
    Transform2DComponent* transform = registry.try_get<Transform2DComponent>(state.resizingPanel);
    if (!resize || !resize->enabled || !transform || state.activeResizeEdges == ePanelResizeNone)
    {
        state.resizingPanel = entt::null;
        state.activeResizeEdges = ePanelResizeNone;
        state.pointerInputCapture = entt::null;
        return false;
    }

    if (!hasPoint || bounds.x <= 0.0f || bounds.y <= 0.0f)
    {
        return true;
    }

    const glm::vec2 delta = point - state.resizeStartPoint;
    glm::vec2 newMin = state.resizeStartMin;
    glm::vec2 newMax = state.resizeStartMin + state.resizeStartSize;
    if ((state.activeResizeEdges & ePanelResizeLeft) != 0u)
    {
        newMin.x += delta.x;
    }
    if ((state.activeResizeEdges & ePanelResizeRight) != 0u)
    {
        newMax.x += delta.x;
    }
    if ((state.activeResizeEdges & ePanelResizeTop) != 0u)
    {
        newMin.y += delta.y;
    }
    if ((state.activeResizeEdges & ePanelResizeBottom) != 0u)
    {
        newMax.y += delta.y;
    }

    const auto clamp_axis = [](float& minValue, float& maxValue, float minSize, float maxSize, float boundsMax, bool resizeMin) {
        minSize = std::max(minSize, 1.0f);
        maxSize = std::max(maxSize, minSize);
        if (resizeMin)
        {
            minValue = std::clamp(minValue, 0.0f, std::max(maxValue - minSize, 0.0f));
            const float size = maxValue - minValue;
            if (size > maxSize)
            {
                minValue = maxValue - maxSize;
            }
            if (minValue < 0.0f)
            {
                minValue = 0.0f;
            }
        }
        else
        {
            maxValue = std::clamp(maxValue, minValue + minSize, boundsMax);
            const float size = maxValue - minValue;
            if (size > maxSize)
            {
                maxValue = minValue + maxSize;
            }
            if (maxValue > boundsMax)
            {
                maxValue = boundsMax;
            }
        }
    };

    const bool resizeLeft = (state.activeResizeEdges & ePanelResizeLeft) != 0u;
    const bool resizeRight = (state.activeResizeEdges & ePanelResizeRight) != 0u;
    const bool resizeTop = (state.activeResizeEdges & ePanelResizeTop) != 0u;
    const bool resizeBottom = (state.activeResizeEdges & ePanelResizeBottom) != 0u;
    if (resizeLeft || resizeRight)
    {
        clamp_axis(newMin.x, newMax.x, resize->minSize.x, resize->maxSize.x, bounds.x, resizeLeft);
    }
    if (resizeTop || resizeBottom)
    {
        clamp_axis(newMin.y, newMax.y, resize->minSize.y, resize->maxSize.y, bounds.y, resizeTop);
    }

    const glm::vec2 newSize = glm::max(newMax - newMin, glm::vec2(1.0f));
    if (!ui_has_meaningful_delta(newMin - state.resizeStartMin, 0.001f) &&
        !ui_has_meaningful_delta(newSize - state.resizeStartSize, 0.001f))
    {
        return true;
    }

    const glm::vec2 safeScale {
        std::abs(transform->scale.x) > 0.0001f ? transform->scale.x : 1.0f,
        std::abs(transform->scale.y) > 0.0001f ? transform->scale.y : 1.0f
    };
    if (Layout2DComponent* layout = registry.try_get<Layout2DComponent>(state.resizingPanel))
    {
        layout->offset = state.resizeStartLayoutOffset + (newMin - state.resizeStartMin) / safeScale;
        layout->size = newSize / safeScale;
        ui_set_entity_framebuffer_size(registry, state.resizingPanel, newSize);
    }
    else
    {
        ui_set_entity_framebuffer_size(registry, state.resizingPanel, newSize);
        transform->position = newMin + transform->origin * newSize;
    }

    resize->activeEdges = state.activeResizeEdges;
    ui_mark_moving_entity_dirty(scene, state.resizingPanel);
    return true;
}

inline bool ui_update_panel_drag(
    Renderer2DScene& scene,
    UiInputState& state,
    bool leftButtonPressed,
    bool hasPoint,
    glm::vec2 point,
    glm::vec2 bounds)
{
    if (state.resizingPanel != entt::null)
    {
        return false;
    }

    if (!leftButtonPressed)
    {
        state.draggedPanel = entt::null;
        return false;
    }

    if (!hasPoint || bounds.x <= 0.0f || bounds.y <= 0.0f)
    {
        state.draggedPanel = entt::null;
        return false;
    }

    entt::registry& registry = scene.registry();
    if (state.draggedPanel == entt::null && ui_input_blocks_panel_drag(scene, state, point))
    {
        return false;
    }

    if (state.draggedPanel == entt::null)
    {
        state.draggedPanel = scene.draggable_parent_at(point);
        state.lastPanelDragPoint = point;
        if (state.draggedPanel == entt::null)
        {
            return false;
        }
        ui_bring_panel_to_front(scene, state.draggedPanel);
    }

    if (!registry.valid(state.draggedPanel))
    {
        state.draggedPanel = entt::null;
        return false;
    }


    glm::vec2 delta = point - state.lastPanelDragPoint;
    if (!ui_has_meaningful_delta(delta))
    {
        return false;
    }

    delta = ui_clamp_drag_delta_to_bounds(registry, state.draggedPanel, delta, bounds);
    if (!ui_has_meaningful_delta(delta))
    {

        state.lastPanelDragPoint = point;
        return false;
    }
    state.lastPanelDragPoint = point;

    if (Layout2DComponent* layout = registry.try_get<Layout2DComponent>(state.draggedPanel))
    {
        const Transform2DComponent* transform = registry.try_get<Transform2DComponent>(state.draggedPanel);
        const glm::vec2 scale = transform ? glm::max(glm::abs(transform->scale), glm::vec2(0.0001f)) : glm::vec2(1.0f);
        layout->offset += delta / scale;
    }
    else if (Transform2DComponent* transform = registry.try_get<Transform2DComponent>(state.draggedPanel))
    {
        transform->position += delta;
    }
    else
    {
        state.draggedPanel = entt::null;
        return false;
    }

    ui_mark_moving_entity_dirty(scene, state.draggedPanel);
    return true;
}

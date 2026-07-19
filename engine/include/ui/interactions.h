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
};

struct UiPointerButtonInput
{
    // Input adapters fill this from whatever platform backend is active
    bool hasPoint = false;
    glm::vec2 point { 0.0f };
    PointerButton button = PointerButton::eOther;
    int platformButton = 0;
    UiInputAction action = UiInputAction::ePress;
    InputModifiers modifiers {};
};

struct UiScrollInput
{
    // Scroll input is routed through the same front-most hit-test path as clicks
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
};

struct UiDropInput
{
    bool hasPoint = false;
    glm::vec2 point { 0.0f };
    std::vector<std::filesystem::path> paths;
};

struct UiHoverResult
{
    // Highest-front matching owner for each interaction family under the pointer
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
    // Disabled components may either use the default cursor or an unavailable cursor
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
    // Any active control owns the pointer before panel dragging can begin
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
    if (state.focusedTextInput != entt::null && registry.valid(state.focusedTextInput))
    {
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
    ui_update_text_input_visual(scene, fontAtlas, entity);
    scene.activate_dynamic(entity, 0.4f);
}

inline void ui_append_text_input(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state,
    unsigned int codepoint)
{
    if (state.focusedTextInput == entt::null)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    TextInputComponent* input = registry.try_get<TextInputComponent>(state.focusedTextInput);
    if (!input || !input->enabled)
    {
        state.focusedTextInput = entt::null;
        return;
    }

    if (!ui_append_codepoint(input->value, codepoint, input->maxBytes))
    {
        return;
    }

    if (input->onChanged)
    {
        input->onChanged(input->value);
    }
    ui_update_text_input_visual(scene, fontAtlas, state.focusedTextInput);
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
    scroll->offset = std::clamp(
        scroll->minOffset + normalized * scrollRange,
        scroll->minOffset,
        scroll->maxOffset);
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
    scene.activate_dynamic(scrollBarEntity, 0.25f);
    scene.mark_dirty(scrollBarEntity);
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
        scene.activate_dynamic(entity, scrollBar.idleDelaySeconds + scrollBar.fadeDurationSeconds + 0.1f);
        if (scrollBar.thumb != entt::null && registry.valid(scrollBar.thumb))
        {
            scene.activate_dynamic(scrollBar.thumb, scrollBar.idleDelaySeconds + scrollBar.fadeDurationSeconds + 0.1f);
        }
        scene.mark_dirty(entity);
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
                    ui_mark_moving_entity_dirty(scene, scrollBar.thumb, 1.0f / 15.0f);
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
                    ui_mark_moving_entity_dirty(scene, scrollBar.thumb, 1.0f / 15.0f);
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
                ui_mark_moving_entity_dirty(scene, entity, 1.0f / 15.0f);
                ui_mark_moving_entity_dirty(scene, scrollBar.thumb, 1.0f / 15.0f);
            }
        }
    });
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
    const entt::entity hit = input.hasPoint ? scene.entity_at(input.point) : entt::null;
    // Resolve all possible owners from the same hit so priority stays explicit below
    const entt::entity buttonOwner = ui_component_owner<ButtonInputComponent>(registry, hit);
    const entt::entity textOwner = ui_component_owner<TextInputComponent>(registry, hit);
    const entt::entity scrollBarOwner = ui_component_owner<ScrollBarInputComponent>(registry, hit);
    const entt::entity sliderOwner = ui_component_owner<SliderInputComponent>(registry, hit);
    uint32_t resizeEdges = ePanelResizeNone;
    entt::entity resizeOwner = entt::null;

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
            // Capture begins on press so drags can continue after leaving the original bounds
            state.pointerInputCapture = ui_input_owner_from_hit(registry, hit);
        }
        else if (input.action == UiInputAction::eRelease)
        {
            state.pointerInputCapture = entt::null;
        }
    }

    if (input.action == UiInputAction::ePress && input.button == PointerButton::eLeft)
    {
        // Text focus follows left-click ownership before any drag-only controls react
        ui_focus_text_input(scene, fontAtlas, state, textOwner);
    }

    if (input.action == UiInputAction::ePress)
    {
        if (buttonOwner != entt::null)
        {
            // Buttons take priority over sliders and scrollbars when their hit regions overlap
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
            // Scrollbars support both direct thumb dragging and track-click jumping
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
            // Sliders commit a value immediately on press, then continue through drag capture
            SliderInputComponent* slider = registry.try_get<SliderInputComponent>(sliderOwner);
            if (slider && slider->enabled && input.button == PointerButton::eLeft)
            {
                slider->dragging = true;
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
    state.pressedInputEntity = entt::null;
    // Release only clicks the same logical owner that received the press
    ButtonInputComponent* button = (releasedEntity != entt::null && registry.valid(releasedEntity)) ?
        registry.try_get<ButtonInputComponent>(releasedEntity) :
        nullptr;
    if (!button)
    {
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
                state.activeSlider = entt::null;
                state.pointerInputCapture = entt::null;
            }
        }
        return;
    }

    if (input.button == PointerButton::eLeft)
    {
        button->leftPressed = false;
    }
    else if (input.button == PointerButton::eRight)
    {
        button->rightPressed = false;
    }

    PointerInputEvent event { releasedEntity, input.point, input.button, input.platformButton, input.modifiers };
    if (button->onRelease)
    {
        button->onRelease(event);
    }

    if (releasedEntity == buttonOwner)
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

inline void ui_handle_scroll(Renderer2DScene& scene, const UiScrollInput& input)
{
    // Scroll always targets the front-most scroll owner under the pointer
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

inline void ui_handle_key(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state,
    const UiKeyInput& keyInput)
{
    if (keyInput.action != UiInputAction::ePress && keyInput.action != UiInputAction::eRepeat)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    const bool isPress = keyInput.action == UiInputAction::ePress;
    if (isPress)
    {
        auto shortcutView = registry.view<KeyboardShortcutComponent>();
        shortcutView.each([&](entt::entity, KeyboardShortcutComponent& shortcut) {
            if (ui_shortcut_matches(shortcut, keyInput.key, keyInput.modifiers) && shortcut.onTriggered)
            {
                shortcut.onTriggered();
            }
        });
    }

    if (state.focusedTextInput == entt::null || !registry.valid(state.focusedTextInput))
    {
        state.focusedTextInput = entt::null;
        return;
    }

    TextInputComponent* input = registry.try_get<TextInputComponent>(state.focusedTextInput);
    if (!input || !input->enabled)
    {
        state.focusedTextInput = entt::null;
        return;
    }

    bool changed = false;
    if (keyInput.keys.escape != 0 && keyInput.key == keyInput.keys.escape && isPress)
    {
        ui_focus_text_input(scene, fontAtlas, state, entt::null);
        return;
    }
    if (keyInput.keys.backspace != 0 && keyInput.key == keyInput.keys.backspace)
    {
        changed = ui_erase_previous_codepoint(input->value);
    }
    else if (keyInput.keys.deleteKey != 0 && keyInput.key == keyInput.keys.deleteKey)
    {
        changed = ui_erase_previous_codepoint(input->value);
    }
    else if (keyInput.keys.enter != 0 &&
        keyInput.key == keyInput.keys.enter &&
        input->multiline &&
        input->value.size() < input->maxBytes)
    {
        input->value.push_back('\n');
        changed = true;
    }
    else if (isPress &&
        keyInput.modifiers.control &&
        keyInput.keys.paste != 0 &&
        keyInput.key == keyInput.keys.paste)
    {
        for (char c : keyInput.clipboardText)
        {
            if (input->value.size() >= input->maxBytes)
            {
                break;
            }

            const unsigned char byte = static_cast<unsigned char>(c);
            if (byte >= 32u && byte != 127u)
            {
                input->value.push_back(static_cast<char>(byte < 128u ? byte : '?'));
                changed = true;
            }
        }
    }
    else if (isPress &&
        keyInput.modifiers.control &&
        keyInput.keys.clearLine != 0 &&
        keyInput.key == keyInput.keys.clearLine)
    {
        changed = !input->value.empty();
        input->value.clear();
    }

    if (!changed)
    {
        return;
    }

    if (input->onChanged)
    {
        input->onChanged(input->value);
    }
    ui_update_text_input_visual(scene, fontAtlas, state.focusedTextInput);
    scene.activate_dynamic(state.focusedTextInput, 0.35f);
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

inline UiHoverResult ui_update_input_hover(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    UiInputState& state,
    bool hasPoint,
    glm::vec2 point)
{
    entt::registry& registry = scene.registry();
    const entt::entity hit = hasPoint ? scene.entity_at(point) : entt::null;
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
    else if (!resolvedInputOwner)
    {
        buttonOwner = entt::null;
        sliderOwner = entt::null;
        scrollBarOwner = entt::null;
        dropOwner = entt::null;
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
        slider->dragging = false;
        state.activeSlider = entt::null;
        state.pointerInputCapture = entt::null;
        return false;
    }

    if (!hasPoint)
    {
        return true;
    }

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

    // Accumulate subpixel motion so slow drags still move once the delta is visible
    glm::vec2 delta = point - state.lastPanelDragPoint;
    if (!ui_has_meaningful_delta(delta))
    {
        return false;
    }

    delta = ui_clamp_drag_delta_to_bounds(registry, state.draggedPanel, delta, bounds);
    if (!ui_has_meaningful_delta(delta))
    {
        // Keep the pointer anchor current when bounds clipping consumes the move
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

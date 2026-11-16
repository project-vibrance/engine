// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(__APPLE__)
#include <vibranceUI/ui/interactions.h>
#include <optional>



inline std::optional<UiCursorKind> macos_captured_cursor_kind(
    const entt::registry& registry, const UiInputState& state)
{
    const auto valid = [&](entt::entity entity) {
        return entity != entt::null && registry.valid(entity);
    };
    if (valid(state.resizingPanel))
        return ui_cursor_kind_for_resize_edges(state.activeResizeEdges);
    for (const auto owner : {state.pointerInputCapture, state.activeSlider,
             state.activeScrollBar, state.pressedInputEntity, state.activeStretchDynamics})
    {
        if (!valid(owner)) continue;
        if (registry.all_of<TextInputComponent>(owner))
            return ui_cursor_kind_for_owner(registry, owner, UiCursorKind::eText);
        if (registry.any_of<ButtonInputComponent, SliderInputComponent, ScrollBarInputComponent>(owner))
            return ui_cursor_kind_for_owner(registry, owner, UiCursorKind::ePointer);
        if (owner == state.activeStretchDynamics) return UiCursorKind::ePointer;
    }
    if (valid(state.draggedPanel)) return UiCursorKind::ePointer;
    return std::nullopt;
}
#endif

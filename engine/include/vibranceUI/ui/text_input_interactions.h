// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/visuals.h>

struct UiTextPointerHit
{
    std::size_t caret = 0u;
    std::size_t character = 0u;
};

inline void ui_refresh_text_input(Renderer2DScene& scene, const Renderer2DFontAtlas& atlas,
    entt::entity entity, bool changed)
{
    auto& registry = scene.registry();
    auto* input = registry.try_get<TextInputComponent>(entity);
    if (!input) return;
    scene.activate_dynamic(entity, 0.4f);
    if (changed && input->onChanged)
    {
        const auto callback = input->onChanged;
        const auto value = input->value;
        callback(value);
    }
    if (registry.valid(entity) && registry.all_of<TextInputComponent>(entity))
        ui_update_text_input_visual(scene, atlas, entity);
}

inline UiTextPointerHit ui_text_input_pointer_hit(Renderer2DScene& scene,
    const Renderer2DFontAtlas& atlas, entt::entity entity, const TextInputComponent& input, glm::vec2 point)
{
    auto& registry = scene.registry();
    const auto* text = registry.try_get<TextComponent>(input.textEntity);
    if (!text || input.value.empty()) return {};
    const auto base = ui_entity_base_framebuffer_rect(registry, input.textEntity);
    const auto visible = ui_entity_visual_framebuffer_rect(registry, input.textEntity);
    const auto scale = glm::vec2(base.z > 0.0f && visible.z > 0.0f ? base.z / visible.z : 1.0f,
        base.w > 0.0f && visible.w > 0.0f ? base.w / visible.w : 1.0f);
    const auto local = (point - glm::vec2(visible)) * scale;
    const auto geometry = ui_text_input_geometry(atlas,
        std::string_view(input.value).substr(input.displayStartByte, input.displayEndByte - input.displayStartByte),
        text->fontSize, input.displayStartByte);
    const float firstCentre = geometry.baseline - text->fontSize * 0.35f;
    const auto row = static_cast<std::size_t>(std::clamp(std::round((local.y - firstCentre) /
        std::max(geometry.lineHeight, 1.0f)), 0.0f, static_cast<float>(geometry.lines.size() - 1u)));
    const auto& line = geometry.lines[row];
    UiTextPointerHit result { ui_text_input_nearest_stop(line, local.x), line.end };
    for (std::size_t index = 0u; index + 1u < line.stops.size(); ++index)
    {
        const auto& a = line.stops[index];
        const auto& b = line.stops[index + 1u];
        if (local.x >= std::min(a.x, b.x) && local.x <= std::max(a.x, b.x))
        { result.character = a.byte; break; }
    }
    if (result.character == line.end && local.x < 0.0f) result.character = line.start;
    (void)entity;
    return result;
}

inline void ui_begin_text_selection(Renderer2DScene& scene, const Renderer2DFontAtlas& atlas,
    UiInputState& state, entt::entity entity, glm::vec2 point, bool extend, unsigned clicks)
{
    auto* input = scene.registry().try_get<TextInputComponent>(entity);
    if (!input || !input->enabled) return;
    ui_text_input_sync(*input);
    const auto hit = ui_text_input_pointer_hit(scene, atlas, entity, *input, point);
    state.textDragGranularity = extend ? 1u : std::min(clicks, 3u);
    if (!extend && clicks >= 3u)
    {
        input->selectionAnchorByte = 0u;
        ui_text_move_caret(*input, input->value.size(), true);
    }
    else if (!extend && clicks == 2u)
    {
        const auto word = ui_text_word_at(*input, hit.character);
        input->selectionAnchorByte = word.start;
        ui_text_move_caret(*input, word.end, true);
    }
    else ui_text_move_caret(*input, hit.caret, extend);
    const auto selection = ui_text_selection(*input);
    state.textDragStartByte = selection.start;
    state.textDragEndByte = selection.end;
    state.activeTextSelection = entity;
    state.pointerInputCapture = entity;
    input->preferredCaretX = std::numeric_limits<float>::quiet_NaN();
    ui_refresh_text_input(scene, atlas, entity, false);
}

inline bool ui_update_text_selection_drag(Renderer2DScene& scene, const Renderer2DFontAtlas& atlas,
    UiInputState& state, bool leftPressed, bool hasPoint, glm::vec2 point)
{
    const auto entity = state.activeTextSelection;
    if (entity == entt::null) return false;
    auto* input = scene.registry().try_get<TextInputComponent>(entity);
    if (!leftPressed || !input || !input->enabled || !input->focused)
    {
        state.activeTextSelection = entt::null;
        if (state.pointerInputCapture == entity) state.pointerInputCapture = entt::null;
        return false;
    }
    if (!hasPoint || state.textDragGranularity >= 3u) return true;
    ui_text_input_sync(*input);
    auto hit = ui_text_input_pointer_hit(scene, atlas, entity, *input, point);
    if (!input->multiline)
    {
        const auto bounds = ui_entity_visual_framebuffer_rect(scene.registry(), entity);
        if (point.x < bounds.x && input->displayStartByte)
            hit.caret = ui_text_previous_character(*input, input->displayStartByte);
        else if (point.x > bounds.x + bounds.z && input->displayEndByte < input->value.size())
            hit.caret = ui_text_next_character(*input, input->displayEndByte);
    }
    if (state.textDragGranularity == 2u)
    {
        const auto word = ui_text_word_at(*input, hit.character);
        const bool backwards = hit.caret < state.textDragStartByte;
        input->selectionAnchorByte = backwards ? state.textDragEndByte : state.textDragStartByte;
        hit.caret = backwards ? word.start : word.end;
    }
    if (hit.caret != input->caretByte)
    {
        ui_text_move_caret(*input, hit.caret, true);
        ui_refresh_text_input(scene, atlas, entity, false);
    }
    return true;
}

inline std::size_t ui_text_input_vertical_target(const Renderer2DFontAtlas& atlas,
    const entt::registry& registry, TextInputComponent& input, int rows)
{
    const auto* text = registry.try_get<TextComponent>(input.textEntity);
    const auto geometry = ui_text_input_geometry(atlas, input.value, text ? text->fontSize : 14.0f);
    const auto current = ui_text_input_line_index(geometry, input.caretByte);
    if (!std::isfinite(input.preferredCaretX))
        input.preferredCaretX = ui_text_input_stop_x(geometry.lines[current], input.caretByte);
    const auto target = static_cast<std::size_t>(std::clamp(static_cast<int>(current) + rows,
        0, static_cast<int>(geometry.lines.size()) - 1));
    if (target == current && rows < 0) return geometry.lines.front().start;
    if (target == current && rows > 0) return input.value.size();
    return ui_text_input_nearest_stop(geometry.lines[target], input.preferredCaretX);
}

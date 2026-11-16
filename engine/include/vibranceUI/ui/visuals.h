// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>
#include <string>
#include <string_view>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/ui/input.h>
#include <vibranceUI/ui/text.h>
#include <vibranceUI/ui/text_input_layout.h>

inline std::string ui_clip_label_tail(const std::string& value, std::size_t maxCharacters)
{
    if (value.size() <= maxCharacters || maxCharacters <= 3u)
    {
        return value;
    }

    return "..." + value.substr(value.size() - (maxCharacters - 3u));
}

inline bool ui_utf8_continuation_byte(char value)
{
    return (static_cast<unsigned char>(value) & 0xC0u) == 0x80u;
}

inline std::size_t ui_previous_utf8_boundary(const std::string& value, std::size_t offset)
{

    offset = std::min(offset, value.size());
    if (offset == 0u)
    {
        return 0u;
    }

    --offset;
    while (offset > 0u && ui_utf8_continuation_byte(value[offset]))
    {
        --offset;
    }
    return offset;
}

inline std::size_t ui_next_utf8_boundary(const std::string& value, std::size_t offset)
{
    offset = std::min(offset, value.size());
    if (offset >= value.size())
    {
        return value.size();
    }

    ++offset;
    while (offset < value.size() && ui_utf8_continuation_byte(value[offset]))
    {
        ++offset;
    }
    return offset;
}

inline float ui_text_width(
    const Renderer2DFontAtlas& fontAtlas,
    std::string_view value,
    float fontSize)
{
    if (value.empty())
    {
        return 0.0f;
    }
    return std::max(fontAtlas.layout_text(value, fontSize).bounds.x, 0.0f);
}

inline float ui_text_input_available_width(
    const entt::registry& registry,
    entt::entity entity,
    const TextInputComponent& input)
{
    const ShapeComponent* shape = registry.try_get<ShapeComponent>(entity);
    if (!shape)
    {
        return 0.0f;
    }

    float leftInset = std::max(input.textPaddingLeft, 0.0f);
    if (leftInset == 0.0f && input.textEntity != entt::null && registry.valid(input.textEntity))
    {
        if (const Layout2DComponent* layout = registry.try_get<Layout2DComponent>(input.textEntity))
        {
            leftInset = std::max(layout->offset.x, 0.0f);
        }
    }
    return std::max(shape->size.x - leftInset - std::max(input.textPaddingRight, 0.0f), 0.0f);
}

inline std::string ui_clip_text_head_to_width(
    const Renderer2DFontAtlas& fontAtlas,
    const std::string& value,
    float fontSize,
    float availableWidth)
{
    if (value.empty() || availableWidth <= 0.0f || ui_text_width(fontAtlas, value, fontSize) <= availableWidth)
    {
        return value;
    }

    std::size_t end = 0u;
    std::size_t next = ui_next_utf8_boundary(value, end);
    while (next > end && next <= value.size())
    {
        const std::string_view candidate(value.data(), next);
        if (ui_text_width(fontAtlas, candidate, fontSize) > availableWidth)
        {
            break;
        }
        end = next;
        if (next == value.size())
        {
            break;
        }
        next = ui_next_utf8_boundary(value, next);
    }
    return value.substr(0u, end);
}

inline std::string ui_clip_text_tail_to_width(
    const Renderer2DFontAtlas& fontAtlas,
    const std::string& value,
    float fontSize,
    float availableWidth,
    std::size_t& displayStartByte)
{

    displayStartByte = 0u;
    if (value.empty() || availableWidth <= 0.0f || ui_text_width(fontAtlas, value, fontSize) <= availableWidth)
    {
        return value;
    }

    std::size_t bestStart = value.size();
    std::size_t start = value.size();
    while (start > 0u)
    {
        const std::size_t previous = ui_previous_utf8_boundary(value, start);
        const std::string_view candidate(value.data() + previous, value.size() - previous);
        if (ui_text_width(fontAtlas, candidate, fontSize) > availableWidth)
        {
            break;
        }
        bestStart = previous;
        start = previous;
    }

    if (bestStart == value.size())
    {
        bestStart = ui_previous_utf8_boundary(value, value.size());
    }
    displayStartByte = bestStart;
    return value.substr(bestStart);
}

inline void ui_set_text_input_caret_visible(
    Renderer2DScene& scene,
    entt::entity caret,
    bool visible)
{
    if (caret == entt::null)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    if (!registry.valid(caret))
    {
        return;
    }

    if (RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(caret))
    {
        if (layer->visible == visible)
        {
            return;
        }
        layer->visible = visible;
        scene.mark_dirty(caret);
    }
}

inline void ui_update_text_input_caret_layout(
    Renderer2DScene& scene, const Renderer2DFontAtlas& fontAtlas, entt::entity entity,
    const TextInputComponent& input, float visibleValueWidth)
{
    auto& registry = scene.registry();
    if (!registry.valid(input.caretEntity)) return;
    const auto* text = registry.try_get<TextComponent>(input.textEntity);
    const auto* placement = registry.try_get<Layout2DComponent>(input.textEntity);
    if (!text || !placement) return;
    const auto geometry = ui_text_input_geometry(fontAtlas,
        std::string_view(input.value).substr(input.displayStartByte, input.displayEndByte - input.displayStartByte),
        text->fontSize, input.displayStartByte);
    const auto row = ui_text_input_line_index(geometry, input.caretByte);
    const auto& line = geometry.lines[row];
    float x = ui_text_input_stop_x(line, input.caretByte);
    if (!fontAtlas.loaded() && geometry.lines.size() == 1u && line.width > 0.0f)
        x *= visibleValueWidth / line.width;
    const float width = std::max(input.caretWidth, 1.0f);
    if (line.rightToLeft) x -= width;
    const float height = std::max(input.caretHeight, text->fontSize * 0.8f);
    const float textWidth = fontAtlas.loaded() ? text->bounds.x : visibleValueWidth;
    const float authoredX = ui_layout_is_rtl(registry, entity) ? textWidth - x - width : x;
    const float verticalOffset = input.value.empty() && !input.multiline ? 0.0f :
        -geometry.height * placement->pivot.y + geometry.baseline - text->fontSize * 0.35f + row * geometry.lineHeight;
    if (auto* layout = registry.try_get<Layout2DComponent>(input.caretEntity))
    {
        layout->anchorMin = layout->anchorMax = placement->anchorMin;
        layout->pivot = { 0.0f, 0.5f };
        layout->offset = placement->offset + glm::vec2(authoredX, verticalOffset);
    }
    auto& shape = registry.get<ShapeComponent>(input.caretEntity);
    shape.size = { width, height };
    scene.activate_dynamic(input.caretEntity, 1.1f);
    scene.mark_dirty(input.caretEntity);
}

inline void ui_update_text_input_selection(Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas, entt::entity entity, TextInputComponent& input)
{
    auto& registry = scene.registry();
    const auto selected = ui_text_selection(input);
    const auto* text = registry.try_get<TextComponent>(input.textEntity);
    const auto* placement = registry.try_get<Layout2DComponent>(input.textEntity);
    const auto* textLayer = registry.try_get<RenderLayer2DComponent>(input.textEntity);
    std::size_t used = 0u;
    if (text && placement && textLayer && input.enabled && input.focused && selected.start != selected.end)
    {
        const auto geometry = ui_text_input_geometry(fontAtlas,
            std::string_view(input.value).substr(input.displayStartByte, input.displayEndByte - input.displayStartByte),
            text->fontSize, input.displayStartByte);
        const auto* visual = registry.try_get<TextInputVisualComponent>(entity);
        const auto paint = visual ? visual->selectionStyle : TextInputVisualComponent {}.selectionStyle;
        const auto textPlacement = *placement;
        const auto layer = *textLayer;
        for (std::size_t row = 0u; row < geometry.lines.size(); ++row)
        {
            const auto& line = geometry.lines[row];
            const auto first = std::max(selected.start, line.start);
            const auto last = std::min(selected.end, line.end);
            const bool newline = selected.end > line.end && selected.start <= line.end && line.end < input.value.size();
            if (first > last || (first == last && !newline)) continue;
            const float a = ui_text_input_stop_x(line, first), b = ui_text_input_stop_x(line, last);
            const float width = std::max(std::abs(b - a) + (newline ? text->fontSize * 0.35f : 0.0f), 1.0f);
            const float height = std::max(input.caretHeight, text->fontSize * 1.05f);
            if (used == input.selectionEntities.size())
            {
                const auto highlight = scene.create_shape({}, { width, height }, paint, Renderer2DPrimitive::eRectangle);
                registry.emplace<Parent2DComponent>(highlight, Parent2DComponent { entity });
                registry.emplace<InputTransparent2DComponent>(highlight);
                registry.emplace<Layout2DComponent>(highlight);
                input.selectionEntities.push_back(highlight);
            }
            const auto highlight = input.selectionEntities[used++];
            registry.emplace_or_replace<RenderLayer2DComponent>(highlight, layer);
            registry.get<ShapeStyleComponent>(highlight) = paint;
            registry.get<ShapeComponent>(highlight).size = { width, height };
            auto& layout = registry.get<Layout2DComponent>(highlight);
            layout.anchorMin = layout.anchorMax = textPlacement.anchorMin;
            layout.pivot = { 0.0f, 0.5f };
            layout.size = { width, height };
            const float x = ui_layout_is_rtl(registry, entity) ? text->bounds.x - std::max(a, b) - (newline ? text->fontSize * 0.35f : 0.0f) : std::min(a, b);
            layout.offset = textPlacement.offset + glm::vec2(x,
                -geometry.height * textPlacement.pivot.y + geometry.baseline - text->fontSize * 0.35f + row * geometry.lineHeight);
            scene.activate_dynamic(highlight, 1.1f);
            scene.mark_dirty(highlight);
        }
    }
    for (std::size_t slot = used; slot < input.selectionEntities.size(); ++slot)
        if (auto* layer = registry.try_get<RenderLayer2DComponent>(input.selectionEntities[slot]); layer && layer->visible)
        { layer->visible = false; scene.mark_dirty(input.selectionEntities[slot]); }
}

inline void ui_update_text_input_visual(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity entity,
    std::size_t maxDisplayCharacters = 28u)
{

    if (entity == entt::null)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    TextInputComponent* input = registry.try_get<TextInputComponent>(entity);
    if (!input)
    {
        return;
    }

    ui_text_input_sync(*input);
    const bool hasValue = !input->value.empty();
    const TextInputVisualComponent* visual = registry.try_get<TextInputVisualComponent>(entity);
    (void)maxDisplayCharacters;
    const TextComponent* text = (input->textEntity != entt::null && registry.valid(input->textEntity)) ?
        registry.try_get<TextComponent>(input->textEntity) :
        nullptr;
    const float fontSize = text ? text->fontSize : 14.0f;
    const float availableWidth = ui_text_input_available_width(registry, entity, *input);
    std::string displayText;
    if (hasValue && !input->multiline)
    {
        auto start = std::min(input->displayStartByte, input->caretByte);
        const auto boundary = std::upper_bound(input->graphemeBoundaries.begin(), input->graphemeBoundaries.end(), start);
        start = boundary == input->graphemeBoundaries.begin() ? 0u : *std::prev(boundary);
        while (start < input->caretByte && availableWidth > 0.0f &&
            ui_text_input_advance(fontAtlas, std::string_view(input->value).substr(start, input->caretByte - start), fontSize) > std::max(availableWidth - input->caretWidth, 1.0f))
            start = ui_text_next_character(*input, start);
        auto end = start;
        while (end < input->value.size())
        {
            const auto next = ui_text_next_character(*input, end);
            if (next > input->caretByte && availableWidth > 0.0f &&
                ui_text_input_advance(fontAtlas, std::string_view(input->value).substr(start, next - start), fontSize) > availableWidth) break;
            end = next;
        }

        while (start > 0u && availableWidth > 0.0f)
        {
            const auto previous = ui_text_previous_character(*input, start);
            const float caretSpace = input->caretByte == end ? input->caretWidth : 0.0f;
            if (ui_text_input_advance(fontAtlas, std::string_view(input->value).substr(previous, end - previous),
                fontSize) + caretSpace > availableWidth) break;
            start = previous;
        }
        input->displayStartByte = start;
        input->displayEndByte = end;
        displayText = input->value.substr(start, end - start);
    }
    else
    {
        input->displayStartByte = 0u;
        input->displayEndByte = input->value.size();
        displayText = hasValue ? input->value : availableWidth > 0.0f ?
            ui_clip_text_head_to_width(fontAtlas, input->placeholder, fontSize, availableWidth) : input->placeholder;
    }
    set_text_entity(
        scene,
        fontAtlas,
        input->textEntity,
        displayText);

    if (input->textAlignment && input->textPaddingLeft > 0.0f)
        if (auto* layout = registry.try_get<Layout2DComponent>(input->textEntity))
        {
            const auto* displayed = registry.try_get<TextComponent>(input->textEntity);
            const float width = displayed ? displayed->bounds.x : ui_text_width(fontAtlas, displayText, fontSize);
            const float remaining = std::max(availableWidth - width, 0.0f);
            const bool reversed = fontAtlas.layout_text(displayText, fontSize).rightToLeft != ui_layout_is_rtl(registry, entity);
            const float end = reversed ? 0.0f : 1.0f;
            const float factor = *input->textAlignment == TextHorizontalAlignment2D::eCenter ? .5f :
                *input->textAlignment == TextHorizontalAlignment2D::eEnd ? end : 1.0f - end;
            const float offset = input->textPaddingLeft + remaining * factor;
            if (std::abs(layout->offset.x - offset) > .001f)
            {
                layout->offset.x = offset;
                scene.mark_dirty(input->textEntity);
            }
        }

    if (input->multiline)
        if (auto* placement = registry.try_get<Layout2DComponent>(input->textEntity))
        {
            const auto geometry = ui_text_input_geometry(fontAtlas, input->value, fontSize);
            const auto line = ui_text_input_line_index(geometry, input->caretByte);
            const float lineTop = line * geometry.lineHeight;
            const auto* shape = registry.try_get<ShapeComponent>(entity);
            const float height = shape ? std::max({ shape->size.y - input->textPaddingTop * 2.0f, input->caretHeight, geometry.lineHeight }) : geometry.height;
            input->verticalScroll = std::clamp(input->verticalScroll,
                std::max(lineTop + geometry.lineHeight - height, 0.0f), std::max(lineTop, 0.0f));
            placement->anchorMin.y = placement->anchorMax.y = placement->pivot.y = 0.0f;
            placement->offset.y = input->textPaddingTop - input->verticalScroll;
        }
    const float visibleValueWidth = hasValue ? ui_text_input_advance(fontAtlas, displayText, fontSize) : 0.0f;
    ui_update_text_input_caret_layout(scene, fontAtlas, entity, *input, visibleValueWidth);
    ui_update_text_input_selection(scene, fontAtlas, entity, *input);

    if (visual)
    {
        if (ShapeStyleComponent* style = registry.try_get<ShapeStyleComponent>(entity))
        {
            if (!input->enabled && visual->hasDisabled)
            {
                *style = visual->disabled;
            }
            else if (input->focused)
            {
                *style = visual->focused;
            }
            else
            {
                *style = visual->idle;
            }
        }

        if (input->textEntity != entt::null && registry.valid(input->textEntity))
        {
            if (TextStyleComponent* textStyle = registry.try_get<TextStyleComponent>(input->textEntity))
            {
                *textStyle = hasValue ? visual->valueText : visual->placeholderText;
            }
            scene.mark_dirty(input->textEntity);
        }
        if (input->caretEntity != entt::null && registry.valid(input->caretEntity))
        {
            if (ShapeStyleComponent* caretStyle = registry.try_get<ShapeStyleComponent>(input->caretEntity))
            {
                *caretStyle = visual->caretStyle;
            }
            ui_set_text_input_caret_visible(scene, input->caretEntity, input->enabled && input->focused && input->caretByte == input->selectionAnchorByte);
        }
        scene.mark_dirty(entity);
        return;
    }

    if (ShapeStyleComponent* style = registry.try_get<ShapeStyleComponent>(entity))
    {
        style->outlineColor = input->focused ? renderer2d_hex_color("#5CEBDBE6") : renderer2d_hex_color("#FFFFFF33");
        style->outlineWidth = input->focused ? std::max(style->outlineWidth, 2.0f) : std::max(style->outlineWidth, 1.0f);
        style->color0 = input->focused ? renderer2d_hex_color("#0B1518F2") : renderer2d_hex_color("#090D11E8");
        style->color1 = style->color0;
    }

    if (input->textEntity != entt::null && registry.valid(input->textEntity))
    {
        if (TextStyleComponent* textStyle = registry.try_get<TextStyleComponent>(input->textEntity))
        {
            textStyle->set_color(hasValue ? renderer2d_hex_color("#F7FFFCEB") : renderer2d_hex_color("#9BB0B4A8"));
        }
        scene.mark_dirty(input->textEntity);
    }
    ui_set_text_input_caret_visible(scene, input->caretEntity, input->enabled && input->focused && input->caretByte == input->selectionAnchorByte);
    scene.mark_dirty(entity);
}

inline void ui_update_text_input_carets(
    Renderer2DScene& scene,
    UiInputState& state,
    double currentTimeSeconds)
{
    entt::registry& registry = scene.registry();
    auto view = registry.view<TextInputComponent>();
    view.each([&](entt::entity entity, TextInputComponent& input) {
        const bool focused = entity == state.focusedTextInput && input.enabled && input.focused;
        bool visible = false;
        if (focused && input.caretByte == input.selectionAnchorByte)
        {
            if (input.caretWakeRequested) { input.caretBlinkEpoch = currentTimeSeconds; input.caretWakeRequested = false; }
            const float period = std::max(input.caretBlinkPeriodSeconds, 0.1f);
            visible = std::fmod(std::max(currentTimeSeconds - input.caretBlinkEpoch, 0.0), static_cast<double>(period)) < static_cast<double>(period) * 0.5;
        }
        ui_set_text_input_caret_visible(scene, input.caretEntity, visible);
    });
}

inline void ui_update_button_visual(Renderer2DScene& scene, entt::entity entity)
{
    if (entity == entt::null)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(entity);
    ShapeStyleComponent* style = registry.try_get<ShapeStyleComponent>(entity);
    if (!button || !style)
    {
        return;
    }

    if (const ButtonVisualComponent* visual = registry.try_get<ButtonVisualComponent>(entity))
    {
        if (!button->enabled && visual->hasDisabled)
        {
            *style = visual->disabled;
        }
        else if (button->leftPressed || button->rightPressed)
        {
            *style = visual->pressed;
        }
        else if (button->hovered)
        {
            *style = visual->hovered;
        }
        else
        {
            *style = visual->idle;
        }
        scene.mark_dirty(entity);
        return;
    }

    DisabledVisualComponent* disabledVisual = registry.try_get<DisabledVisualComponent>(entity);
    if (!button->enabled)
    {
        if (disabledVisual && disabledVisual->dimWhenDisabled)
        {
            style->set_gradient(
                renderer2d_hex_color("#071518A8"),
                renderer2d_hex_color("#0B1A1EA8"),
                { 0.0f, 0.0f },
                { 1.0f, 1.0f });
            style->outlineColor = renderer2d_hex_color("#FFFFFF40");
        }
        else
        {
            style->set_gradient(
                renderer2d_hex_color("#0D171CEB"),
                renderer2d_hex_color("#16202AEB"),
                { 0.0f, 0.0f },
                { 1.0f, 1.0f });
            style->outlineColor = renderer2d_hex_color("#FFFFFF40");
        }
        scene.mark_dirty(entity);
        return;
    }

    if (button->leftPressed || button->rightPressed)
    {
        style->set_gradient(
            renderer2d_hex_color("#25BFAAFA"),
            renderer2d_hex_color("#FFB333F2"),
            { 0.0f, 0.0f },
            { 1.0f, 1.0f });
        style->outlineColor = renderer2d_hex_color("#FFFFFFA8");
    }
    else if (button->hovered)
    {
        style->set_gradient(
            renderer2d_hex_color("#143038F2"),
            renderer2d_hex_color("#213642F2"),
            { 0.0f, 0.0f },
            { 1.0f, 1.0f });
        style->outlineColor = renderer2d_hex_color("#5CEBDBA8");
    }
    else
    {
        style->set_gradient(
            renderer2d_hex_color("#0D171CEB"),
            renderer2d_hex_color("#16202AEB"),
            { 0.0f, 0.0f },
            { 1.0f, 1.0f });
        style->outlineColor = renderer2d_hex_color("#FFFFFF40");
    }
    scene.mark_dirty(entity);
}

inline void ui_update_button_cursor_shadow(Renderer2DScene& scene, entt::entity entity, glm::vec2 point)
{
    if (entity == entt::null)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(entity);
    ShapeStyleComponent* style = registry.try_get<ShapeStyleComponent>(entity);
    const ShapeComponent* shape = registry.try_get<ShapeComponent>(entity);
    const Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
    if (!button || !style || !shape || !transform || !button->hovered ||
        registry.all_of<ButtonVisualComponent>(entity))
    {
        return;
    }

    const glm::vec2 size = glm::max(shape->size * transform->scale, glm::vec2(1.0f));
    const glm::vec2 minPosition = transform->position - transform->origin * size;
    const glm::vec2 uv = glm::clamp((point - minPosition) / size, glm::vec2(0.0f), glm::vec2(1.0f));
    const float radius = std::clamp(78.0f / std::max(size.x, size.y), 0.28f, 0.72f);
    const bool pressed = button->leftPressed || button->rightPressed;
    const glm::vec4 centerColor = pressed ?
        renderer2d_hex_color("#FFE1A366") :
        renderer2d_hex_color("#F2FFFC2E");
    const glm::vec4 edgeColor = pressed ?
        renderer2d_hex_color("#16685FEB") :
        renderer2d_hex_color("#10212AF0");
    const glm::vec4 outlineColor = pressed ?
        renderer2d_hex_color("#FFFFFFB8") :
        renderer2d_hex_color("#5CEBDBA8");
    const glm::vec2 gradientEnd =
        uv + glm::vec2(std::max(radius, 0.0001f), 0.0f);

    const bool changed =
        style->fill != Renderer2DFill::eRadialGradient ||
        glm::length(style->color0 - centerColor) > 0.0001f ||
        glm::length(style->color1 - edgeColor) > 0.0001f ||
        glm::length(style->outlineColor - outlineColor) > 0.0001f ||
        glm::length(style->gradientStart - uv) > 0.0001f ||
        glm::length(style->gradientEnd - gradientEnd) > 0.0001f;
    if (!changed)
    {
        return;
    }

    style->set_radial_gradient(centerColor, edgeColor, uv, radius);
    style->outlineColor = outlineColor;
    scene.mark_dirty(entity);
}

inline void ui_clear_button_cursor_shadow(Renderer2DScene& scene, entt::entity entity)
{
    ui_update_button_visual(scene, entity);
}

inline void ui_update_drop_target_visual(Renderer2DScene& scene, entt::entity entity)
{
    if (entity == entt::null)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    DropTargetComponent* dropTarget = registry.try_get<DropTargetComponent>(entity);
    ShapeStyleComponent* style = registry.try_get<ShapeStyleComponent>(entity);
    if (!dropTarget || !style)
    {
        return;
    }

    style->outlineColor = dropTarget->hovered ? renderer2d_hex_color("#FFCB4DB8") : renderer2d_hex_color("#FFFFFF3D");
    style->color0 = dropTarget->hovered ? renderer2d_hex_color("#191715F2") : renderer2d_hex_color("#0B1116E8");
    style->color1 = dropTarget->hovered ? renderer2d_hex_color("#1F1A12F2") : renderer2d_hex_color("#111820E8");
    scene.mark_dirty(entity);
}

inline void ui_update_slider_visual_impl(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas* fontAtlas,
    entt::entity entity)
{
    if (entity == entt::null)
    {
        return;
    }

    entt::registry& registry = scene.registry();
    SliderInputComponent* slider = registry.try_get<SliderInputComponent>(entity);
    if (!slider)
    {
        return;
    }

    DisabledVisualComponent* disabledVisual = registry.try_get<DisabledVisualComponent>(entity);
    const bool dimWhen = disabledVisual ? disabledVisual->dimWhenDisabled : slider->dimWhenDisabled;
    const float normalized = ui_slider_visual_normalized_value(*slider);
    const float expansion = std::clamp(slider->hoverExpansion, 0.0f, 1.0f);
    const float desiredTrackHeight = glm::mix(
        slider->trackHeight,
        slider->hoveredTrackHeight,
        expansion);
    glm::vec2 desiredThumbSize = glm::mix(
        slider->thumbSize,
        slider->hoveredThumbSize,
        expansion);
    if (slider->liquidThumbEnabled)
    {
        const float formation = std::clamp((slider->liquidThumbScale - 1.0f) * 4.0f, 0.0f, 1.12f);
        const float stretch = std::clamp(slider->liquidDrag.stretch, 0.0f, 1.0f);
        const float width = glm::mix(slider->liquidThumbScale, 1.32f, stretch * std::min(formation, 1.0f));
        const float height = 1.0f + formation * (0.45f - 0.06f * stretch);
        desiredThumbSize = ui_expanded_capsule_size(slider->thumbSize, slider->liquidThumbScale, height);
        desiredThumbSize.x += slider->thumbSize.x * (width - slider->liquidThumbScale);
    }

    const auto resize_shape = [&](entt::entity target, glm::vec2 desiredSize)
    {
        if (target == entt::null || !registry.valid(target))
        {
            return;
        }
        bool changed = false;
        if (ShapeComponent* shape = registry.try_get<ShapeComponent>(target))
        {
            if (glm::length(shape->size - desiredSize) > 0.001f)
            {
                shape->size = desiredSize;
                shape->set_corner_radius(
                    std::min(desiredSize.x, desiredSize.y) * 0.5f);
                changed = true;
            }
        }
        if (Layout2DComponent* layout = registry.try_get<Layout2DComponent>(target))
        {
            if (glm::length(layout->size - desiredSize) > 0.001f)
            {
                layout->size = desiredSize;
                changed = true;
            }
        }
        if (changed)
        {
            scene.mark_dirty(target);
        }
    };

    if (ShapeComponent* track = registry.try_get<ShapeComponent>(entity))
    {


        if (auto* layout = registry.try_get<Layout2DComponent>(entity))
            layout->offset.y -= (desiredTrackHeight - track->size.y) * (0.5f - layout->pivot.y);
        resize_shape(entity, { track->size.x, desiredTrackHeight });
    }
    if (slider->maskEntity != entt::null && registry.valid(slider->maskEntity))
    {
        if (ShapeComponent* mask = registry.try_get<ShapeComponent>(slider->maskEntity))
        {
            resize_shape(slider->maskEntity, { mask->size.x, desiredTrackHeight });
        }
    }

    if (fontAtlas && slider->labelEntity != entt::null && registry.valid(slider->labelEntity))
    {
        if (TextComponent* text = registry.try_get<TextComponent>(slider->labelEntity))
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << slider->value;
            text->text = ss.str();
            text->bounds = glm::vec2(0.0f);
            text->glyphs.clear();
            apply_font_layout(*fontAtlas, registry, slider->labelEntity);
            if (TextStyleComponent* textStyle = registry.try_get<TextStyleComponent>(slider->labelEntity))
            {
                if (dimWhen)
                {
                    textStyle->set_color(slider->enabled ? renderer2d_hex_color("#F7FFFCEB") : renderer2d_hex_color("#9BB0B4A8"));
                }
                else
                {
                    textStyle->set_color(renderer2d_hex_color("#F7FFFCEB"));
                }
            }
            scene.mark_dirty(slider->labelEntity);
        }
    }

    const glm::vec4 rect = ui_entity_framebuffer_rect(registry, entity);
    const glm::vec2 minPos = glm::vec2(rect.x, rect.y);
    const glm::vec2 size = glm::vec2(rect.z, rect.w);

    if (slider->fillEntity != entt::null && registry.valid(slider->fillEntity))
    {
        ShapeComponent* fillShape = registry.try_get<ShapeComponent>(slider->fillEntity);
        Layout2DComponent* fillLayout = registry.try_get<Layout2DComponent>(slider->fillEntity);
        if (ShapeComponent* trackShape = registry.try_get<ShapeComponent>(entity))
        {
            const auto* thumbPaint = registry.try_get<ShapeStyleComponent>(slider->thumbEntity);
            const auto* thumbGlass = registry.try_get<LiquidGlassComponent>(slider->thumbEntity);
            const bool visibleThumb = thumbPaint &&
                thumbPaint->opacity > 0.001f &&
                (std::max(thumbPaint->color0.a, thumbPaint->color1.a) > 0.001f ||
                 (thumbGlass && thumbGlass->material == GlassMaterial::eLiquid));


            const float desiredWidth = visibleThumb ?
                ui_slider_thumb_position(trackShape->size.x, ui_slider_thumb_reserved_width(*slider), normalized) :
                trackShape->size.x * normalized;
            if (fillShape)
            {
                fillShape->size.x = desiredWidth;
                fillShape->size.y = desiredTrackHeight;
                fillShape->set_corner_radius(desiredTrackHeight * 0.5f);
            }
            if (fillLayout)
            {
                fillLayout->size.x = desiredWidth;
                fillLayout->size.y = desiredTrackHeight;
            }
            if (ShapeStyleComponent* fillStyle = registry.try_get<ShapeStyleComponent>(slider->fillEntity))
            {
                fillStyle->opacity = (!dimWhen || slider->enabled) ? 1.0f : 0.45f;
            }
            scene.mark_dirty(slider->fillEntity);
        }
    }

    if (slider->thumbEntity != entt::null && registry.valid(slider->thumbEntity))
    {
        desiredThumbSize.x = std::min(desiredThumbSize.x, size.x);
        const float thumbPosition = ui_slider_thumb_position(size.x,
            ui_slider_thumb_reserved_width(*slider), normalized);
        resize_shape(slider->thumbEntity, desiredThumbSize);
        if (Layout2DComponent* thumbLayout = registry.try_get<Layout2DComponent>(slider->thumbEntity))
        {
            thumbLayout->offset.x = thumbPosition;
            thumbLayout->offset.y = 0.0f;
            scene.mark_dirty(slider->thumbEntity);
        }
        else if (Transform2DComponent* thumbTransform = registry.try_get<Transform2DComponent>(slider->thumbEntity))
        {
            thumbTransform->position.x = minPos.x + thumbPosition;
            thumbTransform->position.y = minPos.y + size.y * 0.5f;
            scene.mark_dirty(slider->thumbEntity);
        }
    }

    scene.mark_dirty(entity);
}

inline void ui_update_slider_visual(Renderer2DScene& scene, const Renderer2DFontAtlas& fontAtlas, entt::entity entity)
{
    ui_update_slider_visual_impl(scene, &fontAtlas, entity);
}

inline void ui_update_slider_geometry(Renderer2DScene& scene, entt::entity entity)
{
    ui_update_slider_visual_impl(scene, nullptr, entity);
}

inline void ui_update_slider_smoothing(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    double currentTimeSeconds)
{
    entt::registry& registry = scene.registry();
    auto view = registry.view<SliderInputComponent>();
    for (entt::entity entity : view)
    {
        SliderInputComponent& slider = view.get<SliderInputComponent>(entity);
        if (slider.lastVisualUpdateSeconds <= 0.0)
        {
            slider.lastVisualUpdateSeconds = currentTimeSeconds;
            slider.visualValue = slider.value;
            ui_update_slider_visual(scene, fontAtlas, entity);
            continue;
        }

        const double rawDelta = currentTimeSeconds - slider.lastVisualUpdateSeconds;
        slider.lastVisualUpdateSeconds = currentTimeSeconds;
        const float deltaSeconds = static_cast<float>(std::clamp(rawDelta, 0.0, 1.0 / 20.0));
        if (deltaSeconds <= 0.0f)
        {
            continue;
        }

        const float target = std::clamp(
            slider.value,
            slider.minValue,
            slider.maxValue);
        const float previous = slider.visualValue;
        if (slider.smoothScrubbing)
        {
            const float rate = std::max(slider.visualSmoothingRate, 0.0f);
            const float alpha = rate <= 0.0f ?
                1.0f : 1.0f - std::exp(-rate * deltaSeconds);
            slider.visualValue = std::clamp(
                previous + (target - previous) * alpha,
                slider.minValue,
                slider.maxValue);
        }
        else
        {
            slider.visualValue = target;
        }

        if (std::abs(slider.visualValue - target) < 0.0005f)
        {
            slider.visualValue = target;
        }

        const float previousExpansion = slider.hoverExpansion;
        const float previousThumbScale = slider.liquidThumbScale;
        const float previousStretch = slider.liquidDrag.stretch;
        if (slider.liquidThumbEnabled)
        {


            ui_update_liquid_interaction(slider.liquidInteraction, slider.enabled && slider.dragging,
                slider.liquidDragMoved, deltaSeconds);
            slider.liquidThumbScale = 1.0f + 0.25f * slider.liquidInteraction.formation;
            slider.liquidThumbVelocity = 0.25f * slider.liquidInteraction.velocity;
            const auto* shape = registry.try_get<ShapeComponent>(entity);
            const float travel = shape ? shape->size.x / std::max(slider.thumbSize.y, 1.0f) : 0.0f;
            const float position = ui_slider_normalized_value(slider) * travel;
            ui_update_liquid_drag(slider.liquidDrag, slider.enabled && slider.dragging, position, deltaSeconds);
        }
        const float expansionTarget = slider.liquidThumbEnabled && (slider.hovered || slider.dragging) ? 1.0f : 0.0f;
        const float expansionRate = std::max(slider.hoverExpansionRate, 0.0f);
        const float expansionAlpha = expansionRate <= 0.0f ?
            1.0f : 1.0f - std::exp(-expansionRate * deltaSeconds);
        slider.hoverExpansion +=
            (expansionTarget - slider.hoverExpansion) * expansionAlpha;
        if (std::abs(slider.hoverExpansion - expansionTarget) < 0.001f)
        {
            slider.hoverExpansion = expansionTarget;
        }

        if (std::abs(slider.visualValue - previous) > 0.00001f ||
            std::abs(slider.hoverExpansion - previousExpansion) > 0.0001f ||
            std::abs(slider.liquidThumbScale - previousThumbScale) > 0.00001f ||
            std::abs(slider.liquidDrag.stretch - previousStretch) > 0.00001f)
        {
            ui_mark_moving_entity_dirty(scene, entity, 1.0f / 30.0f);
            if (slider.fillEntity != entt::null && registry.valid(slider.fillEntity))
            {
                ui_mark_moving_entity_dirty(scene, slider.fillEntity, 1.0f / 30.0f);
            }
            if (slider.thumbEntity != entt::null && registry.valid(slider.thumbEntity))
            {
                ui_mark_moving_entity_dirty(scene, slider.thumbEntity, 1.0f / 30.0f);
            }
            ui_update_slider_visual(scene, fontAtlas, entity);
        }
    }
}

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
    // Move by UTF-8 codepoint boundaries so caret clipping never splits a glyph
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

    float leftInset = 0.0f;
    if (input.textEntity != entt::null && registry.valid(input.textEntity))
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
    // Text inputs show the tail of long values so the active typing point remains visible
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
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity entity,
    const TextInputComponent& input,
    float visibleValueWidth)
{
    entt::registry& registry = scene.registry();
    if (input.caretEntity == entt::null || !registry.valid(input.caretEntity))
    {
        return;
    }

    glm::vec2 baseOffset(0.0f);
    if (input.textEntity != entt::null && registry.valid(input.textEntity))
    {
        if (const Layout2DComponent* textLayout = registry.try_get<Layout2DComponent>(input.textEntity))
        {
            baseOffset = textLayout->offset;
        }
    }

    const float availableWidth = ui_text_input_available_width(registry, entity, input);
    const float caretX = std::min(std::max(visibleValueWidth + 1.0f, 0.0f), std::max(availableWidth, 0.0f));
    if (Layout2DComponent* caretLayout = registry.try_get<Layout2DComponent>(input.caretEntity))
    {
        caretLayout->anchorMin = { 0.0f, 0.5f };
        caretLayout->anchorMax = caretLayout->anchorMin;
        caretLayout->pivot = { 0.0f, 0.5f };
        caretLayout->offset = { baseOffset.x + caretX, baseOffset.y };
    }

    if (ShapeComponent* caretShape = registry.try_get<ShapeComponent>(input.caretEntity))
    {
        const TextComponent* text = (input.textEntity != entt::null && registry.valid(input.textEntity)) ?
            registry.try_get<TextComponent>(input.textEntity) :
            nullptr;
        const float fontSize = text ? text->fontSize : 14.0f;
        caretShape->size = {
            std::max(input.caretWidth, 1.0f),
            std::max(input.caretHeight, fontSize * 0.8f)
        };
    }
    scene.activate_dynamic(input.caretEntity, 1.1f);
    scene.mark_dirty(input.caretEntity);
    (void)fontAtlas;
}

inline void ui_update_text_input_visual(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity entity,
    std::size_t maxDisplayCharacters = 28u)
{
    // Sync text, placeholder, caret layout, and field style from input state
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

    const bool hasValue = !input->value.empty();
    const TextInputVisualComponent* visual = registry.try_get<TextInputVisualComponent>(entity);
    const std::size_t displayCharacters = visual ?
        std::max(visual->maxDisplayCharacters, std::size_t { 4u }) :
        maxDisplayCharacters;
    const TextComponent* text = (input->textEntity != entt::null && registry.valid(input->textEntity)) ?
        registry.try_get<TextComponent>(input->textEntity) :
        nullptr;
    const float fontSize = text ? text->fontSize : 14.0f;
    const float availableWidth = ui_text_input_available_width(registry, entity, *input);
    std::size_t displayStartByte = 0u;
    std::string displayText;
    if (hasValue)
    {
        displayText = availableWidth > 0.0f ?
            ui_clip_text_tail_to_width(fontAtlas, input->value, fontSize, availableWidth, displayStartByte) :
            ui_clip_label_tail(input->value, displayCharacters);
    }
    else
    {
        displayText = availableWidth > 0.0f ?
            ui_clip_text_head_to_width(fontAtlas, input->placeholder, fontSize, availableWidth) :
            input->placeholder;
    }
    input->displayStartByte = displayStartByte;
    set_text_entity(
        scene,
        fontAtlas,
        input->textEntity,
        displayText);

    const float visibleValueWidth = hasValue ? ui_text_width(fontAtlas, displayText, fontSize) : 0.0f;
    ui_update_text_input_caret_layout(scene, fontAtlas, entity, *input, visibleValueWidth);

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
            ui_set_text_input_caret_visible(scene, input->caretEntity, input->enabled && input->focused);
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
    ui_set_text_input_caret_visible(scene, input->caretEntity, input->enabled && input->focused);
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
        if (focused)
        {
            const float period = std::max(input.caretBlinkPeriodSeconds, 0.1f);
            visible = std::fmod(currentTimeSeconds, static_cast<double>(period)) < static_cast<double>(period) * 0.5;
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

inline void ui_update_slider_visual(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
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
    const glm::vec2 desiredThumbSize = glm::mix(
        slider->thumbSize,
        slider->hoveredThumbSize,
        expansion);

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
        resize_shape(entity, { track->size.x, desiredTrackHeight });
    }
    if (slider->maskEntity != entt::null && registry.valid(slider->maskEntity))
    {
        if (ShapeComponent* mask = registry.try_get<ShapeComponent>(slider->maskEntity))
        {
            resize_shape(slider->maskEntity, { mask->size.x, desiredTrackHeight });
        }
    }

    if (slider->labelEntity != entt::null && registry.valid(slider->labelEntity))
    {
        if (TextComponent* text = registry.try_get<TextComponent>(slider->labelEntity))
        {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(2) << slider->value;
            text->text = ss.str();
            text->bounds = glm::vec2(0.0f);
            text->glyphs.clear();
            apply_font_layout(fontAtlas, registry, slider->labelEntity);
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
            const float desiredWidth = trackShape->size.x * normalized;
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
        resize_shape(slider->thumbEntity, desiredThumbSize);
        if (Layout2DComponent* thumbLayout = registry.try_get<Layout2DComponent>(slider->thumbEntity))
        {
            thumbLayout->offset.x = size.x * normalized;
            thumbLayout->offset.y = 0.0f;
            scene.mark_dirty(slider->thumbEntity);
        }
        else if (Transform2DComponent* thumbTransform = registry.try_get<Transform2DComponent>(slider->thumbEntity))
        {
            thumbTransform->position.x = minPos.x + size.x * normalized;
            thumbTransform->position.y = minPos.y + size.y * 0.5f;
            scene.mark_dirty(slider->thumbEntity);
        }
    }

    scene.mark_dirty(entity);
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
        const float expansionTarget = slider.hovered || slider.dragging ? 1.0f : 0.0f;
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
            std::abs(slider.hoverExpansion - previousExpansion) > 0.0001f)
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

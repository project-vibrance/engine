#pragma once

#include <string>
#include <utility>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vibranceUI/localisation/localisation.h>
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/renderer/renderer2d_components.h>

inline void apply_font_layout(const Renderer2DFontAtlas& fontAtlas, entt::registry& registry, entt::entity entity)
{
    TextComponent& text = registry.get<TextComponent>(entity);
    TextLayout2DComponent* textLayout =
        registry.try_get<TextLayout2DComponent>(entity);
    Renderer2DTextLayout layout = textLayout ?
        fontAtlas.layout_text(text.text, text.fontSize, textLayout->options) :
        fontAtlas.layout_text(text.text, text.fontSize);
    text.atlasId = fontAtlas.atlas_id();
    text.msdfPixelRange = fontAtlas.pixel_range();
    text.bounds = layout.bounds;
    text.glyphs = std::move(layout.glyphs);
    if (const auto* alignment = registry.try_get<TextCapHeightAlignment2DComponent>(entity))
    {
        if (auto* placement = registry.try_get<Layout2DComponent>(entity))
        {
            const float capHeight = fontAtlas.loaded() ?
                fontAtlas.layout_text("H", text.fontSize).bounds.y : text.fontSize * 0.72f;
            placement->pivot.y = 0.0f;
            placement->offset.y = alignment->centreOffsetY + capHeight * 0.5f - layout.baseline;
        }
    }
    if (textLayout)
    {
        textLayout->lineCount = layout.lineCount;
        textLayout->resolvedLineHeight = layout.lineHeight;
    }
}

inline void ui_align_text_cap_height(Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas, entt::entity entity, float centreOffsetY = 0.0f)
{
    auto& registry = scene.registry();
    if (!registry.valid(entity) || !registry.all_of<TextComponent, Layout2DComponent>(entity)) return;
    registry.emplace_or_replace<TextCapHeightAlignment2DComponent>(entity, centreOffsetY);
    apply_font_layout(fontAtlas, registry, entity);
    scene.mark_dirty(entity);
}

inline bool set_text_entity(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity entity,
    const std::string& textValue)
{
    if (entity == entt::null)
    {
        return false;
    }

    entt::registry& registry = scene.registry();
    TextComponent* text = registry.try_get<TextComponent>(entity);
    if (!text)
    {
        return false;
    }

    text->text = textValue;
    text->bounds = glm::vec2(0.0f);
    text->glyphs.clear();
    apply_font_layout(fontAtlas, registry, entity);
    scene.mark_dirty(entity);
    return true;
}

inline bool set_text_entity(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    const Localisation& localisation,
    entt::entity entity,
    const Text& textValue)
{
    if (entity == entt::null)
    {
        return false;
    }

    entt::registry& registry = scene.registry();
    if (!registry.valid(entity) || !registry.all_of<TextComponent>(entity))
    {
        return false;
    }

    if (LocalisedTextComponent* localised = registry.try_get<LocalisedTextComponent>(entity))
    {
        localised->value = textValue;
    }
    else
    {
        registry.emplace<LocalisedTextComponent>(entity, textValue);
    }

    return set_text_entity(scene, fontAtlas, entity, localisation.resolve(textValue));
}

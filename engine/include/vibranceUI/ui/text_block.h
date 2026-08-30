#pragma once

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>

#include <entt/entt.hpp>
#include <glm/glm.hpp>

#include <vibranceUI/localisation/localisation.h>
#include <vibranceUI/ui/builder.h>

struct UiTextBlockOptions
{
    // width and all spacing values are logical pixels. width <= 0 keeps the
    // text intrinsic and disables automatic wrapping.
    float width = 0.0f;
    float fontSize = 16.0f;
    TextWrapMode2D wrapMode = TextWrapMode2D::eWord;
    TextHorizontalAlignment2D textAlignment =
        TextHorizontalAlignment2D::eStart;
    float lineHeightMultiplier = 1.0f;
    float lineSpacing = 0.0f;
    float paragraphSpacing = 0.0f;
    float characterSpacing = 0.0f;
    float wordSpacing = 0.0f;
    TextStyleComponent style {};

    // Placement controls the text block as a whole; textAlignment positions
    // individual lines inside width.
    UiAlignment placement = UiAlignment::eTopLeft;
    glm::vec2 offset { 0.0f };
    int32_t layer = 0;
    uint32_t order = 0u;
    bool dynamicCache = false;
};

struct UiTextBlockHandle
{
    entt::entity entity = entt::null;
    glm::vec2 logicalSize { 0.0f };
};

inline TextLayout2DOptions ui_text_block_renderer_options(
    const UiBuilder& ui,
    const UiTextBlockOptions& options)
{
    const LayoutScale& scale = ui.scale();
    TextLayout2DOptions layout = {};
    layout.maximumWidth = options.width > 0.0f ?
        scaled_scalar(options.width, scale) : 0.0f;
    layout.wrapMode = options.width > 0.0f ?
        options.wrapMode : TextWrapMode2D::eNone;
    layout.horizontalAlignment = options.textAlignment;
    layout.lineHeightMultiplier = options.lineHeightMultiplier;
    layout.lineSpacing = scaled_scalar(options.lineSpacing, scale);
    layout.paragraphSpacing = scaled_scalar(
        options.paragraphSpacing,
        scale);
    layout.characterSpacing = scaled_scalar(
        options.characterSpacing,
        scale);
    layout.wordSpacing = scaled_scalar(options.wordSpacing, scale);
    return layout;
}

inline UiTextBlockHandle ui_create_text_block(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    std::string value,
    UiTextBlockOptions options = {})
{
    UiTextBlockHandle handle = {};
    handle.entity = ui.scene().create_text(
        std::move(value),
        glm::vec2(0.0f),
        scaled_scalar(options.fontSize, ui.scale()),
        options.style);
    if (handle.entity == entt::null)
    {
        return handle;
    }

    TextLayout2DComponent textLayout = {};
    textLayout.options = ui_text_block_renderer_options(ui, options);
    ui.registry().emplace_or_replace<TextLayout2DComponent>(
        handle.entity,
        std::move(textLayout));
    apply_font_layout(fontAtlas, ui.registry(), handle.entity);
    ui.set_layer(handle.entity, options.layer, options.order);
    ui.attach_aligned(
        handle.entity,
        parent,
        options.placement,
        scaled_offset(options.offset.x, options.offset.y, ui.scale()));
    if (options.dynamicCache)
    {
        ui.set_dynamic_cache(handle.entity);
    }

    if (const TextComponent* text =
            ui.registry().try_get<TextComponent>(handle.entity))
    {
        const glm::vec2 safeScale = glm::max(
            ui.scale().factor,
            glm::vec2(0.0001f));
        handle.logicalSize = text->bounds / safeScale;
    }
    return handle;
}

inline UiTextBlockHandle ui_create_text_block(
    UiBuilder& ui,
    const Localisation& localisation,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    const Text& value,
    UiTextBlockOptions options = {})
{
    UiTextBlockHandle handle = ui_create_text_block(
        ui,
        fontAtlas,
        parent,
        localisation.resolve(value),
        std::move(options));
    if (handle.entity != entt::null)
    {
        ui.registry().emplace_or_replace<LocalisedTextComponent>(
            handle.entity,
            value);
    }
    return handle;
}

inline bool ui_set_text_block_layout(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    UiTextBlockHandle& handle,
    const UiTextBlockOptions& options)
{
    if (handle.entity == entt::null ||
        !ui.registry().valid(handle.entity) ||
        !ui.registry().all_of<TextComponent>(handle.entity))
    {
        return false;
    }

    TextComponent& text = ui.registry().get<TextComponent>(handle.entity);
    text.fontSize = scaled_scalar(options.fontSize, ui.scale());
    ui.registry().emplace_or_replace<TextStyleComponent>(
        handle.entity,
        options.style);
    TextLayout2DComponent& layout =
        ui.registry().get_or_emplace<TextLayout2DComponent>(handle.entity);
    layout.options = ui_text_block_renderer_options(ui, options);
    apply_font_layout(fontAtlas, ui.registry(), handle.entity);

    const glm::vec2 safeScale = glm::max(
        ui.scale().factor,
        glm::vec2(0.0001f));
    handle.logicalSize = text.bounds / safeScale;
    ui.scene().mark_dirty(handle.entity);
    return true;
}

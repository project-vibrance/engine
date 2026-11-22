// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/renderer/media2d.h>
#include <vibranceUI/renderer/present_mode.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/ui/builder.h>
#include <vibranceUI/ui/content_sizing.h>
#include <vibranceUI/ui/glass.h>
#include <vibranceUI/ui/input.h>
#include <vibranceUI/ui/resources.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/surfaces.h>
#include <vibranceUI/ui/visuals.h>

struct UiControlAppearance
{
    static constexpr Renderer2DPrimitive buttonPrimitive = Renderer2DPrimitive::eSquircle;
#if defined(__APPLE__)
    static constexpr float buttonCornerRadius = 8.0f;
#else
    static constexpr float buttonCornerRadius = 18.0f;
#endif
    static constexpr float buttonSquircleAmount = 0.74f;
    static constexpr float buttonSquirclePower = 4.7f;
    static constexpr float buttonFontSize = 13.5f;
    static constexpr float buttonFontWeight = 460.0f;
    static constexpr float menuRowHeight = 28.0f;
    static constexpr glm::vec2 sliderThumbSize { 24.0f, 18.0f };
};

struct UiControlHandle
{

    entt::entity root = entt::null;
    entt::entity label = entt::null;
    entt::entity caret = entt::null;
    entt::entity leadingIcon = entt::null;
    entt::entity trailingIcon = entt::null;
    entt::entity knob = entt::null;
    entt::entity indicator = entt::null;
    entt::entity menu = entt::null;
};

inline std::string ui_truncate_text_with_ellipsis(std::string_view text, std::size_t maxCharacters)
{

    if (maxCharacters == 0u)
    {
        return std::string(text);
    }

    std::size_t characters = 0u;
    std::size_t byteEnd = 0u;
    for (std::size_t i = 0u; i < text.size() && characters < maxCharacters;)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t width = 1u;
        if ((c & 0x80u) == 0u)
        {
            width = 1u;
        }
        else if ((c & 0xE0u) == 0xC0u && i + 1u < text.size())
        {
            width = 2u;
        }
        else if ((c & 0xF0u) == 0xE0u && i + 2u < text.size())
        {
            width = 3u;
        }
        else if ((c & 0xF8u) == 0xF0u && i + 3u < text.size())
        {
            width = 4u;
        }

        byteEnd = i + width;
        i += width;
        ++characters;
    }

    if (byteEnd >= text.size())
    {
        return std::string(text);
    }

    std::string result(text.substr(0u, byteEnd));
    result += "...";
    return result;
}

inline bool ui_string_ends_with_ellipsis(const std::string& value)
{
    return value.size() >= 3u && value.compare(value.size() - 3u, 3u, "...") == 0;
}

inline std::string ui_truncate_text_to_width_with_ellipsis(
    const Renderer2DFontAtlas& fontAtlas,
    std::string_view text,
    float fontSize,
    float availableWidth,
    std::size_t maxCharacters = 0u)
{

    if (availableWidth <= 0.0f)
    {
        return {};
    }

    std::string clipped = ui_truncate_text_with_ellipsis(text, maxCharacters);
    if (clipped.empty() || ui_text_width(fontAtlas, clipped, fontSize) <= availableWidth)
    {
        return clipped;
    }

    constexpr std::string_view ellipsis = "...";
    if (ui_text_width(fontAtlas, ellipsis, fontSize) > availableWidth)
    {
        return {};
    }

    if (ui_string_ends_with_ellipsis(clipped))
    {
        clipped.resize(clipped.size() - 3u);
    }

    std::size_t bestEnd = 0u;
    std::size_t next = ui_next_utf8_boundary(clipped, 0u);
    while (next > bestEnd && next <= clipped.size())
    {
        std::string candidate = clipped.substr(0u, next);
        candidate += ellipsis;
        if (ui_text_width(fontAtlas, candidate, fontSize) > availableWidth)
        {
            break;
        }

        bestEnd = next;
        if (next == clipped.size())
        {
            break;
        }
        next = ui_next_utf8_boundary(clipped, next);
    }

    if (bestEnd == 0u)
    {
        return std::string(ellipsis);
    }

    std::string result = clipped.substr(0u, bestEnd);
    result += ellipsis;
    return result;
}

inline TextStyleComponent ui_macos26_text_style(std::string_view color = "#1D1D1FFF")
{
    TextStyleComponent style = {};
    style.set_color(color);
    style.shadowColor = glm::vec4(0.0f);
    style.effectColor = glm::vec4(0.0f);
    return style;
}

inline ShapeStyleComponent ui_macos26_control_style(
    UiBuilder& ui,
    std::string_view fill,
    std::string_view outline = "#00000018",
    float outlineWidth = 0.8f,
    float opacity = 1.0f)
{
    return make_solid_style(fill, outline, scaled_scalar(outlineWidth, ui.scale()), opacity);
}

inline ShapeStyleComponent ui_macos26_frosted_control_style(
    UiBuilder& ui,
    std::string_view topColor,
    std::string_view bottomColor,
    std::string_view outline = "#00000018",
    float outlineWidth = 0.8f,
    float opacity = 1.0f,
    float backdropBlurRadius = 16.0f,
    uint32_t backdropBlurPasses = 2u,
    float backdropBlurOpacity = 0.85f)
{
    return make_frosted_panel_style(
        topColor,
        bottomColor,
        outline,
        scaled_scalar(outlineWidth, ui.scale()),
        opacity,
        scaled_scalar(backdropBlurRadius, ui.scale()),
        backdropBlurPasses,
        backdropBlurOpacity);
}

inline ButtonVisualComponent ui_macos26_button_visual(UiBuilder& ui, bool primary = false)
{
    static const UiTheme theme;
    ButtonVisualComponent visual = {};
    visual.idle = ui_macos26_control_style(ui, primary ? theme.accent : theme.surfaceMuted, "#00000000", 0.0f);
    visual.hovered = visual.idle;
    visual.pressed = ui_macos26_control_style(ui,
        primary ? theme.accentPressed : theme.settingsPanel.cardSurface, "#00000000", 0.0f);
    visual.disabled = ui_macos26_control_style(ui, theme.surfaceMuted, "#00000000", 0.0f, 0.5f);
    visual.hasDisabled = true;
    return visual;
}

inline ButtonVisualComponent ui_fixed_colour_button_visual(const ShapeStyleComponent& style)
{


    return { style, style, style, style, true };
}

inline ButtonVisualComponent ui_transparent_button_visual()
{

    ButtonVisualComponent visual = {};
    visual.idle = ui_clear_surface_style(0.0f);
    visual.hovered = ui_clear_surface_style(0.0f);
    visual.pressed = ui_clear_surface_style(0.0f);
    visual.disabled = ui_clear_surface_style(0.0f);
    visual.hasDisabled = true;
    return visual;
}

inline void ui_tint_icon_media(Renderer2DScene& scene, entt::entity entity, std::string_view color)
{
    ui_set_media_mask_tint(scene, entity, color);
}

inline float ui_control_ease_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}


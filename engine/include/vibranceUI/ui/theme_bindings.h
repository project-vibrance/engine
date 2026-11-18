// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/liquid_controls.h>
#include <vibranceUI/ui/surface_paint.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <span>

namespace ui_theme_colours
{
using Palette = std::span<const glm::vec4>;
struct RoleOrder
{
    std::span<const int> surface;
    std::span<const int> outline;
    std::span<const int> text;
    std::span<const int> icon;
};

struct Binding
{
    int role = -1;
    bool preserveAlpha = false;
};

struct Bindings
{
    std::array<Binding, 53> fields;
};

inline bool same(glm::vec4 a, glm::vec4 b, bool ignoreAlpha = false)
{
    return std::abs(a.r - b.r) < 0.0001f && std::abs(a.g - b.g) < 0.0001f &&
        std::abs(a.b - b.b) < 0.0001f && (ignoreAlpha || std::abs(a.a - b.a) < 0.0001f);
}

inline bool recolour(glm::vec4& colour, Binding& binding, const Palette& previous,
    const Palette& next, std::span<const int> roles, bool matchRgb = false)
{
    if ((binding.role < 0 || std::size_t(binding.role) >= previous.size() || std::size_t(binding.role) >= next.size()) && colour.a <= 0.0001f) return false;
    if (binding.role < 0 || std::size_t(binding.role) >= previous.size() || std::size_t(binding.role) >= next.size() || !same(colour, previous[binding.role], binding.preserveAlpha))
    {
        binding = {};
        for (const auto role : roles)
            if (role >= 0 && std::size_t(role) < previous.size() && std::size_t(role) < next.size() && same(colour, previous[role])) { binding.role = role; break; }
        if (binding.role < 0 && matchRgb)
            for (const auto role : roles)
                if (role >= 0 && std::size_t(role) < previous.size() && std::size_t(role) < next.size() && same(colour, previous[role], true))
                {
                    binding.role = role;
                    binding.preserveAlpha = true;
                    break;
                }
    }
    if (binding.role < 0) return false;
    auto replacement = next[binding.role];
    if (binding.preserveAlpha) replacement.a = colour.a;
    if (same(colour, replacement)) return false;
    colour = replacement;
    return true;
}

inline bool recolour_paint(ShapeStyleComponent& paint, Bindings& bindings, std::size_t offset,
    const Palette& previous, const Palette& next, const RoleOrder& order)
{
    const bool top = recolour(paint.color0, bindings.fields[offset], previous, next, order.surface, true);
    const bool bottom = recolour(paint.color1, bindings.fields[offset + 1u], previous, next, order.surface, true);
    const bool outline = recolour(paint.outlineColor, bindings.fields[offset + 2u], previous, next, order.outline);
    return top || bottom || outline;
}

inline bool recolour_button(ButtonVisualComponent& visual, Bindings& bindings, std::size_t offset,
    const Palette& previous, const Palette& next, const RoleOrder& order)
{
    const bool idle = recolour_paint(visual.idle, bindings, offset, previous, next, order);
    const bool hover = recolour_paint(visual.hovered, bindings, offset + 3u, previous, next, order);
    const bool pressed = recolour_paint(visual.pressed, bindings, offset + 6u, previous, next, order);
    const bool disabled = recolour_paint(visual.disabled, bindings, offset + 9u, previous, next, order);
    return idle || hover || pressed || disabled;
}
}

template <typename ColourBindings = ui_theme_colours::Bindings>
inline void ui_recolour_scene(Renderer2DScene& scene, ui_theme_colours::Palette previous,
    ui_theme_colours::Palette next, const ui_theme_colours::RoleOrder& order, const std::function<bool(entt::entity)>& include = {}, bool bindOnly = false)
{
    using namespace ui_theme_colours;
    if (previous.size() != next.size()) return;
    if (std::equal(previous.begin(), previous.end(), next.begin()) && !bindOnly) return;
    auto& registry = scene.registry();
    for (const auto entity : registry.view<ShapeStyleComponent>())
    {
        if (include && !include(entity)) continue;
        auto& bindings = registry.get_or_emplace<ColourBindings>(entity);
        auto& paint = registry.get<ShapeStyleComponent>(entity);
        bool changed = false;
        if (!registry.any_of<UiLiquidControlPaint>(entity))
            changed = recolour_paint(paint, bindings, 0u, previous, next, order);
        if (auto* visual = registry.try_get<ButtonVisualComponent>(entity))
            changed = recolour_button(*visual, bindings, 3u, previous, next, order) || changed;
        if (auto* saved = registry.try_get<UiSurfaceMaterialPaint>(entity))
        {
            bool authoredChanged = recolour_paint(saved->authored, bindings, 19u, previous, next, order);
            if (saved->buttonVisual)
                authoredChanged = recolour_button(*saved->buttonVisual, bindings, 22u, previous, next, order) || authoredChanged;
            if (authoredChanged) saved->applied.reset();
            changed = changed || authoredChanged;
        }
        if (auto* saved = registry.try_get<UiLiquidControlPaint>(entity))
        {
            changed = recolour_paint(saved->shape, bindings, 34u, previous, next, order) || changed;
            if (saved->buttonVisual)
                changed = recolour_button(*saved->buttonVisual, bindings, 37u, previous, next, order) || changed;
        }
        if (auto* saved = registry.try_get<UiLiquidThumbPaint>(entity))
            changed = recolour_paint(saved->shape, bindings, 49u, previous, next, order) || changed;
        if (changed)
        {
            const float opacity = paint.opacity;
            if (registry.any_of<ButtonVisualComponent>(entity))
            {
                ui_update_button_visual(scene, entity);
                paint.opacity = opacity;
            }
            if (registry.any_of<LiquidGlassStrokeSurfaceComponent>(entity))
            {
                if (const auto* saved = registry.try_get<UiSurfaceMaterialPaint>(entity))
                    ui_set_liquid_stroke_surface(scene, entity, saved->authored.color0, saved->authored.color1);
                else ui_set_liquid_stroke_surface(scene, entity, paint.color0, paint.color1);
            }
            scene.mark_dirty(entity);
        }
    }
    for (const auto entity : registry.view<TextStyleComponent>())
    {
        if (include && !include(entity)) continue;
        auto& bindings = registry.get_or_emplace<ColourBindings>(entity);
        auto& paint = registry.get<TextStyleComponent>(entity);
        const bool top = recolour(paint.color0, bindings.fields[15u], previous, next, order.text);
        const bool bottom = recolour(paint.color1, bindings.fields[16u], previous, next, order.text);
        const bool effect = recolour(paint.effectColor, bindings.fields[52u], previous, next, order.text);
        if (top || bottom || effect) scene.mark_dirty(entity);
    }
    for (const auto entity : registry.view<Media2DComponent>())
    {
        auto& media = registry.get<Media2DComponent>(entity);
        if (!media.tintAsMask) continue;
        if (include && !include(entity)) continue;
        auto& bindings = registry.get_or_emplace<ColourBindings>(entity);
        const bool start = recolour(media.tint, bindings.fields[17u], previous, next, order.icon, true);
        const bool end = recolour(media.tintEnd, bindings.fields[18u], previous, next, order.icon, true);
        if (start || end) scene.mark_dirty(entity);
    }
}

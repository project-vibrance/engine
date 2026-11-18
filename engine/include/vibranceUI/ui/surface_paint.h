// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/glass.h>
#include <optional>

struct UiSurfaceMaterialPaint
{
    ShapeStyleComponent authored;
    std::optional<ButtonVisualComponent> buttonVisual;
    std::optional<UiGlassOptions> applied;
};


inline bool ui_glass_options_equal(const UiGlassOptions& a, const UiGlassOptions& b)
{
    const auto sameColour = [](const SystemBackdropColor& x, const SystemBackdropColor& y) {
        return x.red == y.red && x.green == y.green && x.blue == y.blue && x.alpha == y.alpha;
    };
    return a.material == b.material && a.systemProvider == b.systemProvider &&
        a.systemBlurRadius == b.systemBlurRadius && a.systemSaturation == b.systemSaturation &&
        a.systemVerticalStart == b.systemVerticalStart && sameColour(a.systemTint, b.systemTint) &&
        a.liquidBackend == b.liquidBackend && a.sceneBackdrop == b.sceneBackdrop &&
        a.liquidRefraction == b.liquidRefraction && a.liquidBlur == b.liquidBlur &&
        a.liquidRim == b.liquidRim && a.liquidDispersion == b.liquidDispersion &&
        a.liquidRimAnimated == b.liquidRimAnimated &&
        sameColour(a.liquidTint, b.liquidTint);
}

inline ShapeStyleComponent ui_surface_material_paint(ShapeStyleComponent paint,
    GlassMaterial material, float topAlpha = 0.06f, float bottomAlpha = 0.045f)
{
    paint.backdropBlurRadius = 0.0f;
    paint.backdropBlurPasses = 1u;
    paint.backdropBlurOpacity = 0.0f;
    if (material == GlassMaterial::eLiquid)
    {
        paint.color0.a = std::min(paint.color0.a, topAlpha);
        paint.color1.a = std::min(paint.color1.a, bottomAlpha);
        paint.outlineWidth = 0.55f;
        paint.outlineColor = { 1.0f, 1.0f, 1.0f, 0.16f };
#if defined(_WIN32)
        paint.outlineWidth = 0.0f;
#endif
    }
    else if (material == GlassMaterial::eOff)
    {
        paint.color0.a = paint.color1.a = 1.0f;
    }
    return paint;
}

inline void ui_apply_surface_material(UiBuilder& ui, entt::entity entity,
    const UiGlassOptions& options)
{
    auto& registry = ui.registry();
    if (!registry.valid(entity) || !registry.all_of<ShapeStyleComponent>(entity)) return;
    auto* saved = registry.try_get<UiSurfaceMaterialPaint>(entity);
    if (!saved)
    {
        UiSurfaceMaterialPaint original { registry.get<ShapeStyleComponent>(entity) };
        if (const auto* visual = registry.try_get<ButtonVisualComponent>(entity))
            original.buttonVisual = *visual;
        saved = &registry.emplace<UiSurfaceMaterialPaint>(entity, std::move(original));
    }
    if (saved->applied && ui_glass_options_equal(*saved->applied, options)) return;
    if (saved->applied && options.material == GlassMaterial::eLiquid)
    {
        auto previous = *saved->applied;
        previous.liquidTint = options.liquidTint;
        if (ui_glass_options_equal(previous, options))
            if (auto* backdrop = registry.try_get<SystemBackdropComponent>(entity);
                backdrop && backdrop->region.material == GlassMaterial::eLiquid)
            {
                backdrop->region.liquidTint = options.liquidTint;
                saved->applied = options;
                ui.scene().mark_dirty(entity);
                return;
            }
    }
    auto& paint = registry.get<ShapeStyleComponent>(entity);
    const float opacity = paint.opacity;
    const auto materialPaint = [&](ShapeStyleComponent source, float top = 0.06f, float bottom = 0.045f) {
        auto result = ui_surface_material_paint(source, options.material, top, bottom);
        if (options.material == GlassMaterial::eLiquid && options.sceneBackdrop)
        {
            const glm::vec3 tint { options.liquidTint.red, options.liquidTint.green, options.liquidTint.blue };
            const auto applyTint = [&](glm::vec4 colour) {
                const float coverage = std::clamp(options.liquidTint.alpha, 0.0f, 1.0f) * (1.0f - colour.a);
                const float alpha = colour.a + coverage;
                return alpha > 0.0f ? glm::vec4((glm::vec3(colour) * colour.a + tint * coverage) / alpha, alpha) : colour;
            };
            result.color0 = applyTint(result.color0);
            result.color1 = applyTint(result.color1);
        }
        return result;
    };
    paint = materialPaint(saved->authored);
    paint.opacity = opacity;
    if (saved->buttonVisual)
    {
        auto visual = *saved->buttonVisual;
        visual.idle = materialPaint(visual.idle);
        visual.hovered = materialPaint(visual.hovered, 0.10f, 0.08f);
        visual.pressed = materialPaint(visual.pressed, 0.035f, 0.035f);
        visual.disabled = materialPaint(visual.disabled);
        registry.emplace_or_replace<ButtonVisualComponent>(entity, std::move(visual));
        ui_update_button_visual(ui.scene(), entity);
        registry.get<ShapeStyleComponent>(entity).opacity = opacity;
    }
    if (options.material == GlassMaterial::eLiquid)
        ui_set_liquid_stroke_surface(ui.scene(), entity, saved->authored.color0, saved->authored.color1);
    ui_apply_glass_material(ui, entity, options);
    if (auto* liquid = registry.try_get<LiquidGlassComponent>(entity))
        liquid->sampleOwnPaint = options.sceneBackdrop && options.liquidTint.alpha >= 0.5f;
    registry.remove<BlurComponent>(entity);
    saved->applied = options;
    ui.scene().mark_dirty(entity);
}

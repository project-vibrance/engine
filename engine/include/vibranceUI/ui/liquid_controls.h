// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <optional>
#include <vector>
#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/glass.h>



enum class UiLiquidControlAppearance { eAutomatic, eLight, eDark };

struct UiLiquidControlSurface {
    bool desktopBackdrop = false;
    bool cutoutParentPaint = false;
    bool preserveColour = false;
    bool alwaysEnabled = false;
    UiLiquidControlAppearance appearance = UiLiquidControlAppearance::eAutomatic;
};
struct UiLiquidControlParentContour {
    ShapeComponent original;
    ShapeComponent glass;
};
struct UiLiquidControlPaint {
    ShapeStyleComponent shape;
    std::optional<TextInputVisualComponent> textVisual;
    std::optional<ButtonVisualComponent> buttonVisual;
    bool nativeHitRegionAdded = false;
    std::optional<ShadowComponent> shadow;
    entt::entity cutoutParent = entt::null;
    std::optional<ShapeCutout2DComponent> parentCutout;
};

inline bool ui_liquid_control_light_surface(const ShapeStyleComponent& paint,
    UiLiquidControlAppearance appearance = UiLiquidControlAppearance::eAutomatic)
{
    if (appearance != UiLiquidControlAppearance::eAutomatic)
        return appearance == UiLiquidControlAppearance::eLight;
    const glm::vec3 colour(paint.color0);
    const float luminance = glm::dot(colour, glm::vec3(0.2126f, 0.7152f, 0.0722f));
    const float chroma = std::max({ colour.r, colour.g, colour.b }) -
        std::min({ colour.r, colour.g, colour.b });
    return luminance > 0.72f && chroma < 0.16f && paint.color0.a >= 0.25f;
}

inline ShapeStyleComponent ui_liquid_control_paint(ShapeStyleComponent paint,
    bool preserveColour = false, float rim = 0.6f,
    UiLiquidControlAppearance appearance = UiLiquidControlAppearance::eAutomatic,
    LiquidGlassBackend backend = default_liquid_glass_backend())
{
    if (backend == LiquidGlassBackend::eMacOSNative)
        paint.set_color(glm::vec4(0.0f));
    else
    {
        const glm::vec3 authoredColour(paint.color0);
        if (ui_liquid_control_light_surface(paint, appearance))
        {
            paint.fill = Renderer2DFill::eLinearGradient;
            paint.color0 = glm::vec4(glm::min(glm::mix(glm::vec3(paint.color0), glm::vec3(1.0f), 0.65f), glm::vec3(0.98f)),
                std::clamp(paint.color0.a, 0.55f, 0.78f));
            paint.color1 = glm::vec4(glm::min(glm::mix(glm::vec3(paint.color1), glm::vec3(1.0f), 0.45f), glm::vec3(0.97f)),
                std::clamp(paint.color1.a, 0.48f, 0.72f));
            paint.gradientStart = { 0.0f, 0.0f };
            paint.gradientEnd = { 0.0f, 1.0f };
        }
        else paint.set_color(glm::vec4(1.0f, 1.0f, 1.0f, 0.015f));
        if (preserveColour)
        {
            const float strength = std::clamp(rim, 0.0f, 1.0f);
            paint.fill = Renderer2DFill::eLinearGradient;
            paint.color0 = glm::vec4(authoredColour * (0.98f - 0.06f * strength), 0.98f);
            paint.color1 = glm::vec4(glm::mix(authoredColour, glm::vec3(1.0f), 0.015f + 0.025f * strength), 0.98f);
            paint.gradientStart = { 0.0f, 0.05f };
            paint.gradientEnd = { 0.0f, 0.95f };
            paint.outlineColor = glm::vec4(0.0f);
        }
    }
    paint.outlineWidth = 0.0f;
    paint.backdropBlurRadius = 0.0f;
    paint.backdropBlurClipToInheritedMask = true;
    return paint;
}
struct UiLiquidThumbPaint { ShapeStyleComponent shape; bool clear = false; };

inline void ui_set_liquid_button_visual(Renderer2DScene& scene, entt::entity entity,
    const ButtonVisualComponent& authored)
{
    auto& registry = scene.registry();
    if (!registry.valid(entity)) return;
    auto visual = authored;
    if (auto* saved = registry.try_get<UiLiquidControlPaint>(entity))
    {


        saved->buttonVisual = authored;
        saved->shape = authored.idle;
        const auto* surface = registry.try_get<UiLiquidControlSurface>(entity);
        const auto* glass = registry.try_get<LiquidGlassComponent>(entity);
        if (surface && glass && glass->material == GlassMaterial::eLiquid)
        {
            visual.idle = ui_liquid_control_paint(authored.idle, surface->preserveColour, glass->rim, surface->appearance, glass->backend);
            visual.hovered = ui_liquid_control_paint(authored.hovered, surface->preserveColour, glass->rim, surface->appearance, glass->backend);
            visual.pressed = ui_liquid_control_paint(authored.pressed, surface->preserveColour, glass->rim, surface->appearance, glass->backend);
        }
    }
    registry.emplace_or_replace<ButtonVisualComponent>(entity, visual);
    ui_update_button_visual(scene, entity);
}

inline void ui_set_liquid_thumb_resting_style(Renderer2DScene& scene, entt::entity entity,
    ShapeStyleComponent paint)
{
    auto& registry = scene.registry();
    auto* style = registry.try_get<ShapeStyleComponent>(entity);
    if (!style) return;
    if (auto* thumb = registry.try_get<UiLiquidThumbPaint>(entity)) thumb->shape = paint;
    if (auto* original = registry.try_get<UiLiquidControlPaint>(entity)) original->shape = paint;
    if (registry.any_of<LiquidGlassComponent>(entity)) return;
    if (style->color0 != paint.color0 || style->color1 != paint.color1 ||
        style->fill != paint.fill || style->outlineColor != paint.outlineColor || style->outlineWidth != paint.outlineWidth ||
        style->gradientStart != paint.gradientStart || style->gradientEnd != paint.gradientEnd ||
        style->opacity != paint.opacity || style->edgeSoftness != paint.edgeSoftness ||
        style->backdropBlurRadius != paint.backdropBlurRadius || style->backdropBlurPasses != paint.backdropBlurPasses ||
        style->backdropBlurOpacity != paint.backdropBlurOpacity ||
        style->backdropBlurFollowsFillAlpha != paint.backdropBlurFollowsFillAlpha ||
        style->backdropBlurClipToInheritedMask != paint.backdropBlurClipToInheritedMask ||
        style->backdropBlurReplaceSource != paint.backdropBlurReplaceSource ||
        style->backdropBlurDynamicContentOnly != paint.backdropBlurDynamicContentOnly ||
        style->backdropBlurClampToClip != paint.backdropBlurClampToClip)
    {
        *style = paint;
        scene.mark_dirty(entity);
    }
}


struct UiLiquidAutomaticStretch
{
    std::optional<InteractiveVisual2DComponent> authored;
};



inline void ui_update_liquid_glass_optics(Renderer2DScene& scene, const UiGlassOptions& options)
{
    auto& registry = scene.registry();
    for (const auto entity : registry.view<LiquidGlassComponent>())
    {
        auto& glass = registry.get<LiquidGlassComponent>(entity);
        if (glass.material != GlassMaterial::eLiquid) continue;
        const auto* surface = registry.try_get<UiLiquidControlSurface>(entity);
        const auto* thumb = registry.try_get<UiLiquidThumbPaint>(entity);
        const float refraction = options.liquidRefraction * ((thumb && thumb->clear) ? 0.2f : 1.0f);
        const float blur = (surface && surface->preserveColour) || (thumb && thumb->clear) ? 0.0f : options.liquidBlur;
        if (glass.refraction == refraction && glass.blur == blur &&
            glass.rim == options.liquidRim && glass.dispersion == options.liquidDispersion &&
            glass.rimAnimated == options.liquidRimAnimated) continue;
        glass.refraction = refraction;
        glass.blur = blur;
        glass.rim = options.liquidRim;
        glass.dispersion = options.liquidDispersion;
        glass.rimAnimated = options.liquidRimAnimated;
        scene.mark_dirty(entity);
    }
    for (const auto entity : registry.view<SystemBackdropComponent>())
    {
        auto& region = registry.get<SystemBackdropComponent>(entity).region;
        if (region.material != GlassMaterial::eLiquid) continue;
        const auto* surface = registry.try_get<UiLiquidControlSurface>(entity);
        const auto* thumb = registry.try_get<UiLiquidThumbPaint>(entity);
        const float refraction = options.liquidRefraction * ((thumb && thumb->clear) ? 0.2f : 1.0f);
        const float blur = (surface && surface->preserveColour) || (thumb && thumb->clear) ? 0.0f : options.liquidBlur;
        if (region.liquidRefraction == refraction && region.liquidBlur == blur &&
            region.liquidRim == options.liquidRim && region.liquidDispersion == options.liquidDispersion &&
            region.liquidRimAnimated == options.liquidRimAnimated) continue;
        region.liquidRefraction = refraction;
        region.liquidBlur = blur;
        region.liquidRim = options.liquidRim;
        region.liquidDispersion = options.liquidDispersion;
        region.liquidRimAnimated = options.liquidRimAnimated;
        scene.mark_dirty(entity);
    }
}

inline void ui_update_liquid_controls(Renderer2DScene& scene, bool enabled,
    const UiGlassOptions& options, bool desktopBackdrop = false,
    entt::entity onlySurface = entt::null, bool updateAuthoredSurfaces = true,
    bool allowAlwaysEnabled = true)
{
    auto& registry = scene.registry();
    if (onlySurface == entt::null && updateAuthoredSurfaces) ui_update_liquid_glass_optics(scene, options);
    const auto resetSlider = [&](entt::entity entity, SliderInputComponent& slider) {
        const bool changed = slider.liquidThumbEnabled || slider.liquidThumbScale != 1.0f || slider.hoverExpansion != 0.0f;
        slider.liquidThumbEnabled = false;
        slider.liquidThumbScale = 1.0f;
        slider.liquidThumbVelocity = 0.0f;
        slider.liquidInteraction = {};
        slider.liquidDrag = {};
        slider.hoverExpansion = 0.0f;
        if (changed) ui_update_slider_geometry(scene, entity);
    };
    const auto restoreParentPaint = [&](entt::entity entity, UiLiquidControlPaint& saved) {
        if (registry.valid(saved.cutoutParent))
        {
            const auto* cutout = registry.try_get<ShapeCutout2DComponent>(saved.cutoutParent);
            if (cutout && cutout->source == entity && !cutout->affectBackdrop)
            {
                if (saved.parentCutout)
                    registry.emplace_or_replace<ShapeCutout2DComponent>(saved.cutoutParent, *saved.parentCutout);
                else registry.remove<ShapeCutout2DComponent>(saved.cutoutParent);
                if (const auto* contour = registry.try_get<UiLiquidControlParentContour>(saved.cutoutParent))
                {
                    auto& shape = registry.get<ShapeComponent>(saved.cutoutParent);
                    const auto size = shape.size;
                    shape = contour->original;
                    shape.size = size;
                }
                scene.mark_dirty(saved.cutoutParent);
            }
        }
        saved.cutoutParent = entt::null;
        saved.parentCutout.reset();
    };
    const auto remove = [&](entt::entity entity) {
        if (auto* saved = registry.try_get<UiLiquidControlPaint>(entity))
        {
            auto& style = registry.get<ShapeStyleComponent>(entity);
            const float opacity = style.opacity;
            style = saved->shape;
            if (saved->textVisual)
            {
                registry.emplace_or_replace<TextInputVisualComponent>(entity, *saved->textVisual);
                if (const auto* input = registry.try_get<TextInputComponent>(entity))
                    style = !input->enabled && saved->textVisual->hasDisabled ? saved->textVisual->disabled :
                        input->focused ? saved->textVisual->focused : saved->textVisual->idle;
            }
            style.opacity = opacity;
            if (saved->buttonVisual)
                registry.emplace_or_replace<ButtonVisualComponent>(entity, *saved->buttonVisual);
            if (saved->nativeHitRegionAdded) registry.remove<HitRegion2DComponent>(entity);
            if (saved->shadow) registry.emplace_or_replace<ShadowComponent>(entity, *saved->shadow);
            else registry.remove<ShadowComponent>(entity);
            restoreParentPaint(entity, *saved);
            if (const auto* automatic = registry.try_get<UiLiquidAutomaticStretch>(entity))
            {
                const auto authored = automatic->authored;
                ui_disable_stretch_dynamics(scene, entity);
                if (authored) registry.emplace_or_replace<InteractiveVisual2DComponent>(entity, *authored);
                registry.remove<UiLiquidAutomaticStretch>(entity);
            }
            registry.remove<LiquidGlassComponent, SystemBackdropComponent, UiLiquidControlPaint>(entity);
            scene.mark_dirty(entity);
        }
    };
    for (const auto entity : registry.view<UiLiquidControlSurface, StretchDynamicsComponent>())
        if ((onlySurface == entt::null || onlySurface == entity) &&
            registry.get<UiLiquidControlSurface>(entity).preserveColour)
            registry.get<StretchDynamicsComponent>(entity).options.enabled =
                enabled || (allowAlwaysEnabled && registry.get<UiLiquidControlSurface>(entity).alwaysEnabled);
    if (!enabled && onlySurface == entt::null)
    {
        std::vector<entt::entity> restore;
        for (auto entity : registry.view<UiLiquidControlPaint>())
        {
            const auto* surface = registry.try_get<UiLiquidControlSurface>(entity);
            if (!surface || !allowAlwaysEnabled || !surface->alwaysEnabled) restore.push_back(entity);
        }
        for (auto entity : restore) remove(entity);
        restore.clear();
        for (auto entity : registry.view<UiLiquidThumbPaint>()) restore.push_back(entity);
        for (auto entity : restore)
        {
            auto& style = registry.get<ShapeStyleComponent>(entity);
            const float opacity = style.opacity;
            style = registry.get<UiLiquidThumbPaint>(entity).shape;
            style.opacity = opacity;
            registry.remove<UiLiquidThumbPaint>(entity);
            scene.mark_dirty(entity);
        }
        for (auto entity : registry.view<SliderInputComponent>())
        {
            auto& slider = registry.get<SliderInputComponent>(entity);
            resetSlider(entity, slider);
        }
        for (auto entity : registry.view<UiSwitchControlComponent>())
        {
            auto& toggle = registry.get<UiSwitchControlComponent>(entity);
            const bool changed = toggle.liquidThumbEnabled || toggle.liquidThumbScale != 1.0f;
            toggle.liquidThumbEnabled = false;
            toggle.liquidThumbScale = 1.0f;
            toggle.liquidThumbVelocity = 0.0f;
            toggle.liquidInteraction = {};
            if (changed)
            {
                ui_update_switch_geometry(scene, entity);
                ui_update_switch_visual(scene, entity);
            }
        }
    }
    const auto apply = [&](entt::entity entity) {
        if (!registry.valid(entity) || !registry.all_of<ShapeStyleComponent, ShapeComponent>(entity)) return;
        auto* saved = registry.try_get<UiLiquidControlPaint>(entity);
        if (!saved && registry.any_of<SystemBackdropComponent, LiquidGlassComponent>(entity)) return;
        auto& style = registry.get<ShapeStyleComponent>(entity);
        const auto* surface = registry.try_get<UiLiquidControlSurface>(entity);
        const auto* thumb = registry.try_get<UiLiquidThumbPaint>(entity);
        const bool clearThumb = thumb && thumb->clear;
        const float refraction = options.liquidRefraction * (clearThumb ? 0.2f : 1.0f);
        const auto appearance = surface ? surface->appearance : UiLiquidControlAppearance::eAutomatic;
        const float blur = (surface && surface->preserveColour) || clearThumb ? 0.0f : options.liquidBlur;
        if (!saved)
        {
            UiLiquidControlPaint original {};
            original.shape = style;
            if (const auto* shadow = registry.try_get<ShadowComponent>(entity)) original.shadow = *shadow;
            if (surface)
                if (const auto* visual = registry.try_get<TextInputVisualComponent>(entity))
                    original.textVisual = *visual;
            if (const auto* visual = registry.try_get<ButtonVisualComponent>(entity))
                original.buttonVisual = *visual;
            saved = &registry.emplace<UiLiquidControlPaint>(entity, std::move(original));
            if (saved->textVisual)
            {
                auto& visual = registry.get<TextInputVisualComponent>(entity);
                visual.idle = ui_liquid_control_paint(visual.idle, false, options.liquidRim, appearance, options.liquidBackend);
                visual.focused = ui_liquid_control_paint(visual.focused, false, options.liquidRim, appearance, options.liquidBackend);
                visual.disabled = ui_liquid_control_paint(visual.disabled, false, options.liquidRim, appearance, options.liquidBackend);
            }
            scene.mark_dirty(entity);
        }
#if !defined(__APPLE__)
        if (surface && !registry.any_of<ShadowComponent>(entity))
        {
            ShadowComponent shadow {};
            shadow.set_color("#000000FF");
            const auto size = registry.get<ShapeComponent>(entity).size;
            const float shortSide = std::min(size.x, size.y);
            shadow.blurRadius = std::clamp(shortSide * 0.18f, 1.5f, 6.0f);
            shadow.offset = { 0.0f, std::min(shortSide * 0.03f, 1.5f) };
            shadow.spread = 0.0f;
            shadow.opacity = 0.06f;
            shadow.outsideOnly = true;
            registry.emplace<ShadowComponent>(entity, shadow);
            scene.mark_dirty(entity);
        }
#endif
        ShapeStyleComponent desired = saved->shape;
        if (surface)
        {
            if (const auto* input = registry.try_get<TextInputComponent>(entity); input && saved->textVisual)
            {
                const auto& visual = *saved->textVisual;
                desired = !input->enabled && visual.hasDisabled ? visual.disabled :
                    input->focused ? visual.focused : visual.idle;
            }
            desired = ui_liquid_control_paint(desired, surface->preserveColour, options.liquidRim, appearance, options.liquidBackend);
        }
        else
        {
            desired.color0.a = desired.color1.a = 0.04f;
            if (clearThumb) desired.set_color(glm::vec4(0.0f));
            desired.outlineWidth = 0.0f;
            desired.backdropBlurRadius = 0.0f;
            desired.backdropBlurClipToInheritedMask = true;
        }


        bool native = false;
#if defined(_WIN32)
        native = !clearThumb && (desktopBackdrop || (surface && surface->desktopBackdrop));
#elif defined(__APPLE__)
        native = options.liquidBackend == LiquidGlassBackend::eMacOSNative;
        (void)desktopBackdrop;
        if (native && !registry.any_of<HitRegion2DComponent>(entity))
        {
            registry.emplace<HitRegion2DComponent>(entity);
            saved->nativeHitRegionAdded = true;
        }
#else
        (void)desktopBackdrop;
#endif
#if !defined(__APPLE__)
        if (native && surface && surface->cutoutParentPaint)
        {
            const auto* parent = registry.try_get<Parent2DComponent>(entity);
            if (parent && registry.valid(parent->parent))
            {
                if (saved->cutoutParent == entt::null)
                {
                    saved->cutoutParent = parent->parent;
                    if (const auto* cutout = registry.try_get<ShapeCutout2DComponent>(parent->parent))
                        saved->parentCutout = *cutout;
                }
                const auto* cutout = registry.try_get<ShapeCutout2DComponent>(parent->parent);
                if (!cutout || cutout->source != entity || cutout->affectBackdrop)
                {
                    if (const auto* contour = registry.try_get<UiLiquidControlParentContour>(parent->parent))
                    {
                        auto& shape = registry.get<ShapeComponent>(parent->parent);
                        const auto size = shape.size;
                        shape = contour->glass;
                        shape.size = size;
                    }
                    registry.emplace_or_replace<ShapeCutout2DComponent>(parent->parent,
                        ShapeCutout2DComponent { entity, false });
                    scene.mark_dirty(parent->parent);
                }
                if (const auto* dynamics = registry.try_get<StretchDynamicsComponent>(entity);
                    dynamics && (dynamics->dragging || dynamics->settling))
                    scene.mark_dirty(parent->parent);
            }
        }
#endif
        if (!native)
        {
            restoreParentPaint(entity, *saved);
            if (registry.remove<SystemBackdropComponent>(entity)) scene.mark_dirty(entity);
        }
        if (!native && !clearThumb && options.liquidTint.alpha > 0.0f)
        {
            const glm::vec3 tintColour { options.liquidTint.red,
                options.liquidTint.green, options.liquidTint.blue };
            const auto tint = [&](glm::vec4 colour) {
                const float tintAlpha = std::clamp(options.liquidTint.alpha, 0.0f, 1.0f);
                const bool preserveColour = surface && surface->preserveColour;
                const float coverage = preserveColour ? tintAlpha * (1.0f - colour.a) : tintAlpha;
                const float retained = colour.a * (preserveColour ? 1.0f : 1.0f - tintAlpha);
                const float alpha = retained + coverage;
                return glm::vec4((glm::vec3(colour) * retained + tintColour * coverage) / alpha, alpha);
            };
            desired.color0 = tint(desired.color0);
            desired.color1 = tint(desired.color1);
        }
#if defined(__APPLE__)


        if (native)
        {
            desired.set_color(glm::vec4(0.0f));
            desired.outlineWidth = 0.0f;
            if (registry.remove<ShadowComponent>(entity)) scene.mark_dirty(entity);
        }
#endif
        desired.opacity = style.opacity;
        if (saved->buttonVisual && surface)
        {
            auto& visual = registry.get<ButtonVisualComponent>(entity);
            visual.idle = ui_liquid_control_paint(saved->buttonVisual->idle, surface->preserveColour, options.liquidRim, appearance, options.liquidBackend);
            visual.hovered = ui_liquid_control_paint(saved->buttonVisual->hovered, surface->preserveColour, options.liquidRim, appearance, options.liquidBackend);
            visual.pressed = ui_liquid_control_paint(saved->buttonVisual->pressed, surface->preserveColour, options.liquidRim, appearance, options.liquidBackend);
            visual.disabled = saved->buttonVisual->disabled;
        }
        if (style.color0 != desired.color0 || style.color1 != desired.color1 ||
            style.fill != desired.fill || style.gradientStart != desired.gradientStart ||
            style.gradientEnd != desired.gradientEnd || style.outlineWidth != desired.outlineWidth ||
            style.outlineColor != desired.outlineColor ||
            style.backdropBlurRadius != 0.0f)
        {
            style = desired;
            scene.mark_dirty(entity);
        }
        auto* liquid = registry.try_get<LiquidGlassComponent>(entity);
#if defined(_WIN32)
        ui_set_liquid_stroke_surface(scene, entity, saved->shape.color0, saved->shape.color1);
#endif
        const auto backend = native ?
#if defined(__APPLE__)
            LiquidGlassBackend::eMacOSNative :
#else
            LiquidGlassBackend::eWindowsCompositionShader :
#endif
            LiquidGlassBackend::eEngineRenderer;
        if (!liquid || liquid->backend != backend || liquid->sceneBackdrop != !native ||
            liquid->refraction != refraction || liquid->blur != blur ||
            liquid->rim != options.liquidRim || liquid->dispersion != options.liquidDispersion ||
            liquid->rimAnimated != options.liquidRimAnimated)
        {
            registry.emplace_or_replace<LiquidGlassComponent>(entity, LiquidGlassComponent {
                GlassMaterial::eLiquid, backend, !native,
                refraction, blur, options.liquidRim, options.liquidDispersion,
                options.liquidRimAnimated });
            scene.mark_dirty(entity);
        }


        auto& appliedGlass = registry.get<LiquidGlassComponent>(entity);
        const bool sampleOwnPaint = !native && surface &&
            (surface->preserveColour || ui_liquid_control_light_surface(saved->shape, appearance) || options.liquidTint.alpha >= 0.5f);
        if (appliedGlass.sampleOwnPaint != sampleOwnPaint || appliedGlass.preserveBackdropColour != clearThumb)
        {
            appliedGlass.sampleOwnPaint = sampleOwnPaint;
            appliedGlass.preserveBackdropColour = clearThumb;
            scene.mark_dirty(entity);
        }
        if (native)
        {
            auto* backdrop = registry.try_get<SystemBackdropComponent>(entity);
            if (!backdrop)
            {
                SystemBackdropRegion region {};
                region.material = GlassMaterial::eLiquid;
                region.liquidBackend = backend;
                registry.emplace<SystemBackdropComponent>(entity, SystemBackdropComponent { region });
                backdrop = registry.try_get<SystemBackdropComponent>(entity);
            }
            auto& region = backdrop->region;
            region.clipRawBackdropToShape = true;
            region.liquidRefraction = refraction;
            region.liquidBlur = blur;
            region.liquidRim = options.liquidRim;
            region.liquidRimAnimated = options.liquidRimAnimated;
            region.liquidDispersion = options.liquidDispersion;
            auto tint = options.liquidTint;
            if (clearThumb) tint.alpha = 0.0f;
#if defined(__APPLE__)
            if (surface && surface->preserveColour)
                tint = { saved->shape.color0.r, saved->shape.color0.g,
                    saved->shape.color0.b, 0.9f };
#endif
            if (region.liquidTint.red != tint.red || region.liquidTint.green != tint.green ||
                region.liquidTint.blue != tint.blue || region.liquidTint.alpha != tint.alpha)
                scene.mark_dirty(entity);
            region.liquidTint = tint;
        }
        if (surface && registry.any_of<ButtonInputComponent>(entity) &&
            !registry.any_of<StretchDynamicsComponent>(entity))
        {
            UiLiquidAutomaticStretch automatic;
            if (const auto* visual = registry.try_get<InteractiveVisual2DComponent>(entity)) automatic.authored = *visual;
            StretchDynamicsOptions gesture;
            gesture.consumeClickAfterDrag = true;
            gesture.returnResponse = 0.24f;
            gesture.returnDampingFraction = 0.64f;
            ui_enable_stretch_dynamics(scene, entity, gesture);
            registry.emplace<UiLiquidAutomaticStretch>(entity, automatic);
        }
    };
    if (onlySurface != entt::null)
    {
        const auto* surface = registry.try_get<UiLiquidControlSurface>(onlySurface);
        if (enabled || (allowAlwaysEnabled && surface && surface->alwaysEnabled)) apply(onlySurface);
        else remove(onlySurface);
        ui_reset_non_liquid_stretch_dynamics(scene);
        return;
    }
    for (auto entity : registry.view<UiLiquidControlSurface>())
        if (enabled || (allowAlwaysEnabled && registry.get<UiLiquidControlSurface>(entity).alwaysEnabled))
            apply(entity);
    ui_reset_non_liquid_stretch_dynamics(scene);
    if (!enabled) return;
    const auto thumb = [&](entt::entity entity, float formation, bool clear = false) {
        if (!registry.valid(entity) || !registry.all_of<ShapeStyleComponent>(entity)) return;
        if (!registry.any_of<UiLiquidThumbPaint>(entity))
            registry.emplace<UiLiquidThumbPaint>(entity,
                UiLiquidThumbPaint { registry.get<ShapeStyleComponent>(entity), clear });
        registry.get<UiLiquidThumbPaint>(entity).clear = clear;
        if (formation > 0.001f)
        {
            apply(entity);
            const float progress = std::clamp(formation, 0.0f, 1.0f);
            auto& paint = registry.get<ShapeStyleComponent>(entity);
            const auto& resting = registry.get<UiLiquidThumbPaint>(entity).shape;
            paint.color0 = glm::mix(resting.color0, paint.color0, progress);
            paint.color1 = glm::mix(resting.color1, paint.color1, progress);
            paint.outlineColor = resting.outlineColor;
            paint.outlineWidth = resting.outlineWidth * (1.0f - progress);
            if (auto* liquid = registry.try_get<LiquidGlassComponent>(entity))
            {
                liquid->refraction *= progress;
                liquid->rim *= progress;
                liquid->dispersion *= progress;
            }
            if (auto* backdrop = registry.try_get<SystemBackdropComponent>(entity))
            {
                backdrop->region.liquidRefraction *= progress;
                backdrop->region.liquidRim *= progress;
                backdrop->region.liquidDispersion *= progress;
            }
        }
        else
        {
            remove(entity);
            auto& style = registry.get<ShapeStyleComponent>(entity);
            const auto& resting = registry.get<UiLiquidThumbPaint>(entity).shape;
            if (style.color0 != resting.color0 || style.color1 != resting.color1 ||
                style.outlineWidth != resting.outlineWidth || style.outlineColor != resting.outlineColor)
            {
                const float opacity = style.opacity;
                style = resting;
                style.opacity = opacity;
                scene.mark_dirty(entity);
            }
        }
    };
    for (auto entity : registry.view<SliderInputComponent>())
    {
        auto& slider = registry.get<SliderInputComponent>(entity);


        const auto* saved = registry.try_get<UiLiquidThumbPaint>(slider.thumbEntity);
        const auto* normal = saved ? &saved->shape : registry.try_get<ShapeStyleComponent>(slider.thumbEntity);
        const bool visibleThumb = normal && (normal->color0.a > 0.001f || normal->color1.a > 0.001f ||
            (normal->outlineWidth > 0.0f && normal->outlineColor.a > 0.001f));
        if (visibleThumb)
        {
            slider.liquidThumbEnabled = true;
            thumb(slider.thumbEntity, slider.enabled ? slider.liquidInteraction.formation : 0.0f, true);
        }
        else resetSlider(entity, slider);
    }
    for (auto entity : registry.view<UiSwitchControlComponent>())
    {
        auto& toggle = registry.get<UiSwitchControlComponent>(entity);
        toggle.liquidThumbEnabled = true;
        thumb(toggle.knob, toggle.liquidInteraction.formation, true);
    }
    for (auto entity : registry.view<ScrollBarInputComponent>())
    {
        const auto& bar = registry.get<ScrollBarInputComponent>(entity);
        thumb(bar.thumb, bar.enabled && bar.dragging);
    }
}

inline void ui_apply_liquid_button(UiBuilder& ui, entt::entity button)
{
    if (!ui.registry().valid(button) ||
        !ui.registry().all_of<ButtonInputComponent, ShapeComponent>(button)) return;
    ui_clear_glass_material(ui, button);
    ui.registry().emplace_or_replace<UiLiquidControlSurface>(button,
        UiLiquidControlSurface { .preserveColour = true, .alwaysEnabled = true });

    ui_update_liquid_controls(ui.scene(), true, ui_liquid_glass_options(), false, button);
}

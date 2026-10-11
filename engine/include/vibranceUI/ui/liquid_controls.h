#pragma once

#include <optional>
#include <vector>
#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/glass.h>

// Explicit shared surfaces, such as a navigation cluster, own one lens.
// Individual button paints and pointer viewports retain their normal styles.
struct UiLiquidControlSurface {
    bool desktopBackdrop = false;
    bool cutoutParentPaint = false;
    bool preserveColour = false;
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

inline ShapeStyleComponent ui_liquid_control_paint(ShapeStyleComponent paint)
{
    // Shared controls use the same clear, uniform lens paint as a grabbed
    // thumb. Their background has no dark cap or metallic gradient.
#if defined(__APPLE__)
    paint.set_color(glm::vec4(0.0f));
#else
    paint.set_color(glm::vec4(1.0f, 1.0f, 1.0f, 0.04f));
#endif
    paint.outlineWidth = 0.0f;
    paint.backdropBlurRadius = 0.0f;
    paint.backdropBlurClipToInheritedMask = true;
    return paint;
}
struct UiLiquidThumbPaint { ShapeStyleComponent shape; };

inline void ui_update_liquid_controls(Renderer2DScene& scene, bool enabled,
    const UiGlassOptions& options, bool desktopBackdrop = false)
{
    auto& registry = scene.registry();
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
            registry.remove<LiquidGlassComponent, SystemBackdropComponent, UiLiquidControlPaint>(entity);
            scene.mark_dirty(entity);
        }
    };
    for (const auto entity : registry.view<UiLiquidControlSurface, StretchDynamicsComponent>())
        if (registry.get<UiLiquidControlSurface>(entity).preserveColour)
            registry.get<StretchDynamicsComponent>(entity).options.enabled = enabled;
    if (!enabled)
    {
        std::vector<entt::entity> restore;
        for (auto entity : registry.view<UiLiquidControlPaint>()) restore.push_back(entity);
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
            slider.liquidThumbEnabled = false;
            slider.liquidThumbScale = 1.0f;
            slider.liquidThumbVelocity = 0.0f;
        }
        for (auto entity : registry.view<UiSwitchControlComponent>())
            registry.get<UiSwitchControlComponent>(entity).liquidThumbEnabled = false;
        return;
    }
    const auto apply = [&](entt::entity entity) {
        if (!registry.valid(entity) || !registry.all_of<ShapeStyleComponent, ShapeComponent>(entity)) return;
        auto* saved = registry.try_get<UiLiquidControlPaint>(entity);
        if (!saved && registry.any_of<SystemBackdropComponent, LiquidGlassComponent>(entity)) return;
        auto& style = registry.get<ShapeStyleComponent>(entity);
        const auto* surface = registry.try_get<UiLiquidControlSurface>(entity);
        if (!saved)
        {
            UiLiquidControlPaint original {};
            original.shape = style;
            if (const auto* shadow = registry.try_get<ShadowComponent>(entity)) original.shadow = *shadow;
            if (surface)
                if (const auto* visual = registry.try_get<TextInputVisualComponent>(entity))
                    original.textVisual = *visual;
#if defined(__APPLE__)
            if (const auto* visual = registry.try_get<ButtonVisualComponent>(entity))
                original.buttonVisual = *visual;
#endif
            saved = &registry.emplace<UiLiquidControlPaint>(entity, std::move(original));
            if (saved->textVisual)
            {
                auto& visual = registry.get<TextInputVisualComponent>(entity);
                visual.idle = ui_liquid_control_paint(visual.idle);
                visual.focused = ui_liquid_control_paint(visual.focused);
                visual.disabled = ui_liquid_control_paint(visual.disabled);
            }
#if defined(__APPLE__)
            if (saved->buttonVisual)
            {
                auto& visual = registry.get<ButtonVisualComponent>(entity);
                visual.idle = ui_liquid_control_paint(visual.idle);
                visual.hovered = ui_liquid_control_paint(visual.hovered);
                visual.pressed = ui_liquid_control_paint(visual.pressed);
                visual.disabled = ui_liquid_control_paint(visual.disabled);
            }
#endif
            scene.mark_dirty(entity);
        }
#if !defined(__APPLE__)
        if (surface && !registry.any_of<ShadowComponent>(entity))
        {
            ShadowComponent shadow {};
            shadow.set_color("#000000FF");
            shadow.blurRadius = 8.0f;
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
                const auto& visual = registry.get<TextInputVisualComponent>(entity);
                desired = !input->enabled && visual.hasDisabled ? visual.disabled :
                    input->focused ? visual.focused : visual.idle;
            }
            desired = ui_liquid_control_paint(desired);
            if (surface->preserveColour)
            {
                // A coloured circular lens needs a curved highlight even
                // when the panel underneath it is a uniform solid colour.
                const glm::vec3 colour(saved->shape.color0);
                const float rim = std::clamp(options.liquidRim, 0.0f, 1.0f);
                desired.fill = Renderer2DFill::eLinearGradient;
                desired.color0 = glm::vec4(glm::mix(colour, glm::vec3(1.0f), 0.06f + 0.16f * rim), 0.84f);
                desired.color1 = glm::vec4(colour * (0.96f - 0.16f * rim), 0.92f);
                desired.gradientStart = { 0.0f, 0.05f };
                desired.gradientEnd = { 0.0f, 0.95f };
                desired.outlineColor = glm::vec4(colour * 0.55f, 0.30f * rim);
                desired.outlineWidth = 0.8f * rim;
            }
        }
        else
        {
            desired.color0.a = desired.color1.a = 0.04f;
            desired.outlineWidth = 0.0f;
            desired.backdropBlurRadius = 0.0f;
            desired.backdropBlurClipToInheritedMask = true;
        }
        // Portable controls tint the sampled scene through their own paint;
        // native controls pass the tint to the compositor's optical shader.
        bool native = false;
#if defined(_WIN32)
        native = desktopBackdrop || (surface && surface->desktopBackdrop);
#elif defined(__APPLE__)
        native = true;
        (void)desktopBackdrop;
        if (!registry.any_of<HitRegion2DComponent>(entity))
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
        if (!native && options.liquidTint.alpha > 0.0f)
        {
            const glm::vec3 tintColour { options.liquidTint.red,
                options.liquidTint.green, options.liquidTint.blue };
            const auto tint = [&](glm::vec4 colour) {
                const float coverage = std::clamp(options.liquidTint.alpha, 0.0f, 1.0f) * (1.0f - colour.a);
                const float alpha = colour.a + coverage;
                return glm::vec4((glm::vec3(colour) * colour.a + tintColour * coverage) / alpha, alpha);
            };
            desired.color0 = tint(desired.color0);
            desired.color1 = tint(desired.color1);
        }
#if defined(__APPLE__)
        // AppKit supplies the lens, tint and edge. Keep only foreground labels
        // and icons in Metal rather than painting a second optical surface.
        desired.set_color(glm::vec4(0.0f));
        desired.outlineWidth = 0.0f;
        if (registry.remove<ShadowComponent>(entity)) scene.mark_dirty(entity);
#endif
        desired.opacity = style.opacity;
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
        const auto backend = native ?
#if defined(__APPLE__)
            LiquidGlassBackend::eMacOSNative :
#else
            LiquidGlassBackend::eWindowsCompositionShader :
#endif
            LiquidGlassBackend::eEngineRenderer;
        if (!liquid || liquid->backend != backend || liquid->sceneBackdrop != !native ||
            liquid->refraction != options.liquidRefraction || liquid->blur != options.liquidBlur ||
            liquid->rim != options.liquidRim || liquid->dispersion != options.liquidDispersion)
        {
            registry.emplace_or_replace<LiquidGlassComponent>(entity, LiquidGlassComponent {
                GlassMaterial::eLiquid, backend, !native,
                options.liquidRefraction, options.liquidBlur, options.liquidRim, options.liquidDispersion });
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
            region.liquidRefraction = options.liquidRefraction;
            region.liquidBlur = options.liquidBlur;
            region.liquidRim = options.liquidRim;
            region.liquidDispersion = options.liquidDispersion;
            auto tint = options.liquidTint;
#if defined(__APPLE__)
            if (surface && surface->preserveColour)
                tint = { saved->shape.color0.r, saved->shape.color0.g,
                    saved->shape.color0.b, 0.9f };
            if (region.liquidTint.red != tint.red || region.liquidTint.green != tint.green ||
                region.liquidTint.blue != tint.blue || region.liquidTint.alpha != tint.alpha)
                scene.mark_dirty(entity);
#endif
            region.liquidTint = tint;
        }
    };
    for (auto entity : registry.view<UiLiquidControlSurface>()) apply(entity);
    const auto thumb = [&](entt::entity entity, bool dragging) {
        if (!registry.valid(entity) || !registry.all_of<ShapeStyleComponent>(entity)) return;
        if (!registry.any_of<UiLiquidThumbPaint>(entity))
            registry.emplace<UiLiquidThumbPaint>(entity,
                UiLiquidThumbPaint { registry.get<ShapeStyleComponent>(entity) });
        if (dragging) apply(entity);
        else
        {
            remove(entity);
            auto& style = registry.get<ShapeStyleComponent>(entity);
            if (style.color0 != glm::vec4(1.0f) || style.outlineWidth != 0.0f)
            {
                style.set_color(glm::vec4(1.0f));
                style.outlineWidth = 0.0f;
                scene.mark_dirty(entity);
            }
        }
    };
    for (auto entity : registry.view<SliderInputComponent>())
    {
        auto& slider = registry.get<SliderInputComponent>(entity);
        // Media seeking deliberately authors an invisible thumb. Preserve
        // that choice rather than turning its pointer geometry white.
        const auto* saved = registry.try_get<UiLiquidThumbPaint>(slider.thumbEntity);
        const auto* normal = saved ? &saved->shape : registry.try_get<ShapeStyleComponent>(slider.thumbEntity);
        const bool visibleThumb = normal && (normal->color0.a > 0.001f || normal->color1.a > 0.001f ||
            (normal->outlineWidth > 0.0f && normal->outlineColor.a > 0.001f));
        slider.liquidThumbEnabled = visibleThumb;
        if (visibleThumb) thumb(slider.thumbEntity, slider.enabled && slider.dragging);
    }
    for (auto entity : registry.view<UiSwitchControlComponent>())
    {
        auto& toggle = registry.get<UiSwitchControlComponent>(entity);
        toggle.liquidThumbEnabled = true;
        thumb(toggle.knob, toggle.dragging);
    }
    for (auto entity : registry.view<ScrollBarInputComponent>())
    {
        const auto& bar = registry.get<ScrollBarInputComponent>(entity);
        thumb(bar.thumb, bar.enabled && bar.dragging);
    }
}

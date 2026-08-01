#pragma once

#include <algorithm>

#include <entt/entt.hpp>

#include <vibranceUI/graphics/backdrop.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/ui/builder.h>

// One material request that can be reused by stock controls and custom
// renderer entities. Platform and renderer ownership stay hidden here.
struct UiGlassOptions
{
    GlassMaterial material = GlassMaterial::eOff;

    SystemBackdropProvider systemProvider = SystemBackdropProvider::eEngine;
    float systemBlurRadius = 12.0f;
    float systemSaturation = 1.0f;
    float systemVerticalStart = 0.0f;
    SystemBackdropColor systemTint { 1.0f, 1.0f, 1.0f, 0.06f };

    LiquidGlassBackend liquidBackend = LiquidGlassBackend::eEngineRenderer;
};

inline UiGlassOptions ui_system_glass_options(
    float blurRadius = 12.0f,
    float saturation = 1.0f,
    SystemBackdropColor tint = { 1.0f, 1.0f, 1.0f, 0.06f },
    SystemBackdropProvider provider = SystemBackdropProvider::eEngine)
{
    UiGlassOptions options = {};
    options.material = GlassMaterial::eSystemGlass;
    options.systemProvider = provider;
    options.systemBlurRadius = std::max(blurRadius, 0.0f);
    options.systemSaturation = std::max(saturation, 0.0f);
    options.systemTint = tint;
    return options;
}

inline UiGlassOptions ui_liquid_glass_options()
{
    UiGlassOptions options = {};
    options.material = GlassMaterial::eLiquid;
    options.liquidBackend = default_liquid_glass_backend();
    return options;
}

inline void ui_apply_glass_material(
    UiBuilder& ui,
    entt::entity entity,
    const UiGlassOptions& options)
{
    entt::registry& registry = ui.registry();
    if (entity == entt::null || !registry.valid(entity))
    {
        return;
    }

    // A shape has one glass owner. Changing material cannot leave a stale
    // platform visual or renderer request behind.
    registry.remove<SystemBackdropComponent>(entity);
    registry.remove<LiquidGlassComponent>(entity);

    if (options.material == GlassMaterial::eSystemGlass)
    {
        SystemBackdropRegion region = {};
        region.material = GlassMaterial::eSystemGlass;
        region.provider = options.systemProvider;
        region.deriveShapeFromEntity = true;
        region.blurRadius = std::max(options.systemBlurRadius, 0.0f);
        region.saturation = std::max(options.systemSaturation, 0.0f);
        region.verticalStart = std::clamp(options.systemVerticalStart, 0.0f, 0.98f);
        region.tint = options.systemTint;
        registry.emplace_or_replace<SystemBackdropComponent>(
            entity,
            SystemBackdropComponent { region });
        return;
    }

    if (options.material == GlassMaterial::eLiquid)
    {
        registry.emplace_or_replace<LiquidGlassComponent>(
            entity,
            LiquidGlassComponent {
                GlassMaterial::eLiquid,
                options.liquidBackend
            });
    }
}

inline void ui_clear_glass_material(UiBuilder& ui, entt::entity entity)
{
    ui_apply_glass_material(ui, entity, UiGlassOptions {});
}

#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <string>
#include <vector>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vibranceUI/ui/builder.h>
#include <vibranceUI/ui/styles.h>

inline ShapeStyleComponent ui_clear_surface_style(float alpha = 0.0f)
{
    // Transparent surfaces are still useful as layout parents, masks, and hit areas
    return make_solid_style(
        glm::vec4(1.0f, 1.0f, 1.0f, alpha),
        glm::vec4(0.0f),
        0.0f,
        1.0f);
}

struct UiSurfaceBlockOptions
{
    // Common block options shared by cards, fixed panes, and utility surfaces
    ShapeStyleComponent style {};
    Renderer2DPrimitive primitive = Renderer2DPrimitive::eSquircle;
    float cornerRadius = 12.0f;
    float squircleAmount = 0.72f;
    float squirclePower = 4.6f;
    int32_t layer = 0;
    uint32_t order = 0u;
    bool alwaysOnTop = false;
    bool dynamicCache = false;
    bool clipChildren = false;
};

inline entt::entity ui_create_surface_block(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    const UiSurfaceBlockOptions& options)
{
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return entt::null;
    }

    entt::entity block = ui.scene().create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        options.style,
        options.primitive);
    ui.set_layer(block, options.layer, options.order, options.alwaysOnTop);
    ui.set_shape(block, options.cornerRadius);
    if (options.primitive == Renderer2DPrimitive::eSquircle)
    {
        ui.set_shape_squircle(block, options.squircleAmount, options.squirclePower);
    }
    ui.attach_aligned(
        block,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaled_size(size.x, size.y, ui.scale()));
    if (options.clipChildren)
    {
        ui.scene().enable_mask(block, false);
    }
    if (options.dynamicCache)
    {
        ui.set_dynamic_cache(block);
    }
    return block;
}

inline entt::entity ui_create_fixed_block(
    UiBuilder& ui,
    entt::entity parent,
    glm::vec2 offset,
    glm::vec2 size,
    const ShapeStyleComponent& style,
    float cornerRadius,
    int32_t layer,
    uint32_t order,
    Renderer2DPrimitive primitive = Renderer2DPrimitive::eSquircle)
{
    UiSurfaceBlockOptions options = {};
    options.style = style;
    options.primitive = primitive;
    options.cornerRadius = cornerRadius;
    options.layer = layer;
    options.order = order;
    return ui_create_surface_block(ui, parent, UiAlignment::eTopLeft, offset, size, options);
}

inline entt::entity ui_create_card(
    UiBuilder& ui,
    entt::entity parent,
    float y,
    float height,
    const ShapeStyleComponent& style,
    float cornerRadius,
    int32_t layer,
    uint32_t order)
{
    // Cards stretch to the full parent width while keeping a fixed logical height
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return entt::null;
    }

    entt::entity block = ui.scene().create_shape(
        { 0.0f, 0.0f },
        glm::vec2(1.0f),
        style,
        Renderer2DPrimitive::eSquircle);
    ui.set_layer(block, layer, order);
    ui.set_shape(block, cornerRadius);
    ui.set_shape_squircle(block, 0.7f, 4.8f);
    ui.attach_stretch(
        block,
        parent,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        glm::vec4(0.0f),
        scaled_offset(0.0f, y, ui.scale()),
        scaled_size(0.0f, height, ui.scale()));
    return block;
}

struct UiSeparatorOptions
{
    std::string color = "rgba(0, 0, 0, 0.08)";
    glm::vec2 inset { 14.0f, 14.0f };
    float thickness = 1.0f;
};

inline entt::entity ui_create_separator(
    UiBuilder& ui,
    entt::entity parent,
    float y,
    int32_t layer,
    uint32_t order,
    const UiSeparatorOptions& options = {})
{
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return entt::null;
    }

    entt::entity separator = ui.scene().create_shape(
        { 0.0f, 0.0f },
        glm::vec2(1.0f),
        make_solid_style(options.color, "rgba(0, 0, 0, 0)", 0.0f, 1.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(separator, layer, order);
    ui.attach_stretch(
        separator,
        parent,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        scaled_edges(options.inset.x, 0.0f, options.inset.y, 0.0f, ui.scale()),
        scaled_offset(0.0f, y, ui.scale()),
        scaled_size(0.0f, options.thickness, ui.scale()));
    return separator;
}

inline entt::entity ui_create_dot(
    UiBuilder& ui,
    entt::entity parent,
    glm::vec2 center,
    float radius,
    std::string_view color,
    int32_t layer,
    uint32_t order,
    std::string_view outline = "rgba(0, 0, 0, 0)",
    float outlineWidth = 0.0f)
{
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return entt::null;
    }

    const float diameter = radius * 2.0f;
    entt::entity dot = ui.scene().create_shape(
        { 0.0f, 0.0f },
        scaled_size(diameter, diameter, ui.scale()),
        make_solid_style(color, outline, scaled_scalar(outlineWidth, ui.scale()), 1.0f),
        Renderer2DPrimitive::eEllipse);
    ui.set_layer(dot, layer, order);
    ui.attach_aligned(
        dot,
        parent,
        UiAlignment::eTopLeft,
        scaled_offset(center.x - radius, center.y - radius, ui.scale()),
        scaled_size(diameter, diameter, ui.scale()));
    return dot;
}

enum class UiPanelBackgroundCornerMode
{
    eNone,
    eAuto,
    eAll,
    eLeft,
    eRight,
    eTop,
    eBottom,
    eCustom
};

struct UiPanelBackgroundLayer
{
    // Normalised panel rect: x, y, width, height
    glm::vec4 rect { 0.0f, 0.0f, 1.0f, 1.0f };
    ShapeStyleComponent style {};
    Renderer2DPrimitive primitive = Renderer2DPrimitive::eRoundedRectangle;
    UiPanelBackgroundCornerMode corners = UiPanelBackgroundCornerMode::eAuto;
    float cornerRadius = 0.0f;
    glm::vec4 customCornerRadii { 0.0f };
    glm::vec4 margin { 0.0f };
    glm::vec2 fixedSize { 0.0f };
    glm::vec2 pivot { 0.0f };
    glm::vec2 offset { 0.0f };
    int32_t layerOffset = 1;
    uint32_t order = 0u;
    bool dynamicCache = false;
    bool clipChildren = true;
    bool sdfEdges = true;

    static UiPanelBackgroundLayer fill(const ShapeStyleComponent& style, float radius = 0.0f)
    {
        UiPanelBackgroundLayer layer = {};
        layer.style = style;
        layer.cornerRadius = radius;
        return layer;
    }

    static UiPanelBackgroundLayer left(const ShapeStyleComponent& style, float fraction, float radius = 0.0f)
    {
        UiPanelBackgroundLayer layer = fill(style, radius);
        layer.rect = { 0.0f, 0.0f, std::clamp(fraction, 0.0f, 1.0f), 1.0f };
        return layer;
    }

    static UiPanelBackgroundLayer fixed_left(const ShapeStyleComponent& style, float width, float radius = 0.0f)
    {
        // Fixed columns keep their logical width while other columns absorb resize
        UiPanelBackgroundLayer layer = fill(style, radius);
        layer.rect = { 0.0f, 0.0f, 0.0f, 1.0f };
        layer.fixedSize.x = std::max(width, 0.0f);
        return layer;
    }

    static UiPanelBackgroundLayer fill_after_left(const ShapeStyleComponent& style, float leftWidth, float radius = 0.0f)
    {
        UiPanelBackgroundLayer layer = fill(style, radius);
        layer.margin.x = std::max(leftWidth, 0.0f);
        return layer;
    }

    static UiPanelBackgroundLayer fill_before_right(const ShapeStyleComponent& style, float rightWidth, float radius = 0.0f)
    {
        UiPanelBackgroundLayer layer = fill(style, radius);
        layer.margin.z = std::max(rightWidth, 0.0f);
        return layer;
    }

    static UiPanelBackgroundLayer right(const ShapeStyleComponent& style, float fraction, float radius = 0.0f)
    {
        const float width = std::clamp(fraction, 0.0f, 1.0f);
        UiPanelBackgroundLayer layer = fill(style, radius);
        layer.rect = { 1.0f - width, 0.0f, width, 1.0f };
        return layer;
    }

    static UiPanelBackgroundLayer fixed_right(const ShapeStyleComponent& style, float width, float radius = 0.0f)
    {
        UiPanelBackgroundLayer layer = fill(style, radius);
        layer.rect = { 1.0f, 0.0f, 0.0f, 1.0f };
        layer.fixedSize.x = std::max(width, 0.0f);
        layer.pivot.x = 1.0f;
        return layer;
    }

    static UiPanelBackgroundLayer top(const ShapeStyleComponent& style, float fraction, float radius = 0.0f)
    {
        UiPanelBackgroundLayer layer = fill(style, radius);
        layer.rect = { 0.0f, 0.0f, 1.0f, std::clamp(fraction, 0.0f, 1.0f) };
        return layer;
    }

    static UiPanelBackgroundLayer bottom(const ShapeStyleComponent& style, float fraction, float radius = 0.0f)
    {
        const float height = std::clamp(fraction, 0.0f, 1.0f);
        UiPanelBackgroundLayer layer = fill(style, radius);
        layer.rect = { 0.0f, 1.0f - height, 1.0f, height };
        return layer;
    }

    static UiPanelBackgroundLayer custom_rect(
        const ShapeStyleComponent& style,
        glm::vec4 normalizedRect,
        float radius = 0.0f)
    {
        UiPanelBackgroundLayer layer = fill(style, radius);
        layer.rect = normalizedRect;
        return layer;
    }
};

enum class UiPanelBackgroundAxis
{
    eHorizontal,
    eVertical
};

struct UiPanelBackgroundSegment
{
    GridTrack2D track { GridTrack2D::fraction(1.0f) };
    UiPanelBackgroundLayer layer {};

    static UiPanelBackgroundSegment pixels(
        const ShapeStyleComponent& style,
        float pixels,
        float radius = 0.0f)
    {
        return { GridTrack2D::pixels(std::max(pixels, 0.0f)), UiPanelBackgroundLayer::fill(style, radius) };
    }

    static UiPanelBackgroundSegment fraction(
        const ShapeStyleComponent& style,
        float fraction,
        float radius = 0.0f)
    {
        return { GridTrack2D::fraction(std::max(fraction, 0.0f)), UiPanelBackgroundLayer::fill(style, radius) };
    }
};

inline std::vector<UiPanelBackgroundLayer> ui_panel_background_segments(
    UiPanelBackgroundAxis axis,
    const std::vector<UiPanelBackgroundSegment>& segments)
{
    float fixedTotal = 0.0f;
    float fractionTotal = 0.0f;
    for (const UiPanelBackgroundSegment& segment : segments)
    {
        const float value = std::max(segment.track.value, 0.0f);
        if (segment.track.unit == GridTrackUnit2D::ePixels)
        {
            fixedTotal += value;
        }
        else
        {
            fractionTotal += value;
        }
    }

    std::vector<UiPanelBackgroundLayer> layers;
    layers.reserve(segments.size());

    float fixedBefore = 0.0f;
    float fractionBefore = 0.0f;
    const bool horizontal = axis == UiPanelBackgroundAxis::eHorizontal;
    constexpr float kFractionEpsilon = 0.0001f;

    for (const UiPanelBackgroundSegment& segment : segments)
    {
        UiPanelBackgroundLayer layer = segment.layer;
        const float value = std::max(segment.track.value, 0.0f);
        if (segment.track.unit == GridTrackUnit2D::ePixels)
        {
            const float fractionAnchor = fractionTotal > kFractionEpsilon ?
                fractionBefore / fractionTotal :
                0.0f;
            const float pixelOffset = fixedBefore - fixedTotal * fractionAnchor;
            if (horizontal)
            {
                layer.rect = { fractionAnchor, 0.0f, 0.0f, 1.0f };
                layer.fixedSize.x = value;
                layer.offset.x += pixelOffset;
            }
            else
            {
                layer.rect = { 0.0f, fractionAnchor, 1.0f, 0.0f };
                layer.fixedSize.y = value;
                layer.offset.y += pixelOffset;
            }
            fixedBefore += value;
        }
        else
        {
            const float nextFraction = fractionBefore + value;
            const float startFraction = fractionTotal > kFractionEpsilon ?
                fractionBefore / fractionTotal :
                0.0f;
            const float endFraction = fractionTotal > kFractionEpsilon ?
                nextFraction / fractionTotal :
                startFraction;
            const float startOffset = fixedBefore - fixedTotal * startFraction;
            const float endMargin = fixedTotal * endFraction - fixedBefore;
            if (horizontal)
            {
                layer.rect = { startFraction, 0.0f, std::max(endFraction - startFraction, 0.0f), 1.0f };
                layer.margin.x += startOffset;
                layer.margin.z += endMargin;
            }
            else
            {
                layer.rect = { 0.0f, startFraction, 1.0f, std::max(endFraction - startFraction, 0.0f) };
                layer.margin.y += startOffset;
                layer.margin.w += endMargin;
            }
            fractionBefore = nextFraction;
        }

        layers.push_back(layer);
    }

    return layers;
}

inline std::vector<UiPanelBackgroundLayer> ui_panel_background_segments(
    UiPanelBackgroundAxis axis,
    std::initializer_list<UiPanelBackgroundSegment> segments)
{
    return ui_panel_background_segments(
        axis,
        std::vector<UiPanelBackgroundSegment>(segments.begin(), segments.end()));
}

inline std::vector<UiPanelBackgroundLayer> ui_panel_background_columns(
    std::initializer_list<UiPanelBackgroundSegment> segments)
{
    return ui_panel_background_segments(UiPanelBackgroundAxis::eHorizontal, segments);
}

inline std::vector<UiPanelBackgroundLayer> ui_panel_background_rows(
    std::initializer_list<UiPanelBackgroundSegment> segments)
{
    return ui_panel_background_segments(UiPanelBackgroundAxis::eVertical, segments);
}

inline glm::vec4 ui_panel_background_auto_corners(glm::vec4 rect, float radius)
{
    constexpr float edgeEpsilon = 0.0001f;
    const bool touchesLeft = rect.x <= edgeEpsilon;
    const bool touchesTop = rect.y <= edgeEpsilon;
    const bool touchesRight = rect.x + rect.z >= 1.0f - edgeEpsilon;
    const bool touchesBottom = rect.y + rect.w >= 1.0f - edgeEpsilon;
    return {
        touchesLeft && touchesTop ? radius : 0.0f,
        touchesRight && touchesTop ? radius : 0.0f,
        touchesRight && touchesBottom ? radius : 0.0f,
        touchesLeft && touchesBottom ? radius : 0.0f
    };
}

inline glm::vec4 ui_panel_background_corners(const UiPanelBackgroundLayer& layer)
{
    const float radius = std::max(layer.cornerRadius, 0.0f);
    switch (layer.corners)
    {
    case UiPanelBackgroundCornerMode::eAll:
        return glm::vec4(radius);
    case UiPanelBackgroundCornerMode::eLeft:
        return { radius, 0.0f, 0.0f, radius };
    case UiPanelBackgroundCornerMode::eRight:
        return { 0.0f, radius, radius, 0.0f };
    case UiPanelBackgroundCornerMode::eTop:
        return { radius, radius, 0.0f, 0.0f };
    case UiPanelBackgroundCornerMode::eBottom:
        return { 0.0f, 0.0f, radius, radius };
    case UiPanelBackgroundCornerMode::eCustom:
        return layer.customCornerRadii;
    case UiPanelBackgroundCornerMode::eNone:
        return glm::vec4(0.0f);
    case UiPanelBackgroundCornerMode::eAuto:
    default:
        return ui_panel_background_auto_corners(layer.rect, radius);
    }
}

inline entt::entity ui_create_panel_background_layer(
    UiBuilder& ui,
    entt::entity parent,
    const UiPanelBackgroundLayer& layer,
    int32_t baseLayer,
    uint32_t baseOrder,
    bool alwaysOnTop = false)
{
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return entt::null;
    }

    const glm::vec4 clamped {
        std::clamp(layer.rect.x, 0.0f, 1.0f),
        std::clamp(layer.rect.y, 0.0f, 1.0f),
        std::clamp(layer.rect.z, 0.0f, 1.0f),
        std::clamp(layer.rect.w, 0.0f, 1.0f)
    };
    const glm::vec2 anchorMin { clamped.x, clamped.y };
    const glm::vec2 fixedSize {
        layer.fixedSize.x > 0.0f ? layer.fixedSize.x * ui.scale().factor.x : 0.0f,
        layer.fixedSize.y > 0.0f ? layer.fixedSize.y * ui.scale().factor.y : 0.0f
    };
    const glm::vec2 anchorMax {
        fixedSize.x > 0.0f ? anchorMin.x : std::clamp(clamped.x + clamped.z, 0.0f, 1.0f),
        fixedSize.y > 0.0f ? anchorMin.y : std::clamp(clamped.y + clamped.w, 0.0f, 1.0f)
    };
    const bool validX = fixedSize.x > 0.0f || anchorMax.x > anchorMin.x;
    const bool validY = fixedSize.y > 0.0f || anchorMax.y > anchorMin.y;
    if (!validX || !validY)
    {
        return entt::null;
    }

    entt::entity entity = ui.scene().create_shape(
        { 0.0f, 0.0f },
        glm::vec2(1.0f),
        layer.style,
        layer.primitive);
    ui.set_layer(entity, baseLayer + layer.layerOffset, baseOrder + layer.order, alwaysOnTop);
    ui.attach_stretch(
        entity,
        parent,
        anchorMin,
        anchorMax,
        layer.pivot,
        scaled_edges(layer.margin.x, layer.margin.y, layer.margin.z, layer.margin.w, ui.scale()),
        scaled_offset(layer.offset.x, layer.offset.y, ui.scale()),
        fixedSize);

    const glm::vec4 corners = ui_panel_background_corners(layer);
    ui.set_shape_corners(entity, corners.x, corners.y, corners.z, corners.w, layer.sdfEdges);
    if (layer.clipChildren)
    {
        ui.scene().enable_mask(entity, false);
    }
    if (layer.dynamicCache)
    {
        ui.set_dynamic_cache(entity);
    }
    return entity;
}

inline std::vector<entt::entity> ui_create_panel_background_layers(
    UiBuilder& ui,
    entt::entity parent,
    const std::vector<UiPanelBackgroundLayer>& layers,
    int32_t baseLayer,
    uint32_t baseOrder,
    bool alwaysOnTop = false)
{
    std::vector<entt::entity> entities;
    entities.reserve(layers.size());
    for (const UiPanelBackgroundLayer& layer : layers)
    {
        entt::entity entity = ui_create_panel_background_layer(ui, parent, layer, baseLayer, baseOrder, alwaysOnTop);
        if (entity != entt::null)
        {
            entities.push_back(entity);
        }
    }
    return entities;
}

inline std::vector<entt::entity> ui_create_panel_background_layers(
    UiBuilder& ui,
    entt::entity parent,
    std::initializer_list<UiPanelBackgroundLayer> layers,
    int32_t baseLayer,
    uint32_t baseOrder,
    bool alwaysOnTop = false)
{
    return ui_create_panel_background_layers(
        ui,
        parent,
        std::vector<UiPanelBackgroundLayer>(layers.begin(), layers.end()),
        baseLayer,
        baseOrder,
        alwaysOnTop);
}

enum class UiScrollFadeEdge
{
    eTop,
    eBottom
};

struct UiScrollEdgeFadeOptions
{
    float topHeight = 42.0f;
    float bottomHeight = 42.0f;
    float topSolidHeight = 0.0f;
    float bottomSolidHeight = 0.0f;
    std::string blurColor = "rgba(255, 255, 255, 0.48)";
    std::string transparentColor = "rgba(255, 255, 255, 0)";
    std::string outlineColor = "rgba(0, 0, 0, 0)";
    float outlineWidth = 0.0f;
    float opacity = 1.0f;
    float backdropBlurRadius = 18.0f;
    uint32_t backdropBlurPasses = 2u;
    float backdropBlurOpacity = 0.85f;
    int32_t layer = 5;
    uint32_t order = 0u;
    bool alwaysOnTop = false;
    bool dynamicCache = false;
    bool sdfEdges = false;
    bool drawTint = true;
    bool backdropBlurFollowsFillAlpha = true;
    bool backdropBlurClipToInheritedMask = true;
};

inline entt::entity ui_create_scroll_edge_fade(
    UiBuilder& ui,
    entt::entity parent,
    UiScrollFadeEdge edge,
    const UiScrollEdgeFadeOptions& options)
{
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return entt::null;
    }

    const bool top = edge == UiScrollFadeEdge::eTop;
    const float height = std::max(top ? options.topHeight : options.bottomHeight, 0.0f);
    if (height <= 0.0f)
    {
        return entt::null;
    }

    ShapeStyleComponent style = top ?
        make_frosted_panel_style(
            options.blurColor,
            options.transparentColor,
            options.outlineColor,
            scaled_scalar(options.outlineWidth, ui.scale()),
            options.opacity,
            scaled_scalar(options.backdropBlurRadius, ui.scale()),
            options.backdropBlurPasses,
            options.backdropBlurOpacity) :
        make_frosted_panel_style(
            options.transparentColor,
            options.blurColor,
            options.outlineColor,
            scaled_scalar(options.outlineWidth, ui.scale()),
            options.opacity,
            scaled_scalar(options.backdropBlurRadius, ui.scale()),
            options.backdropBlurPasses,
            options.backdropBlurOpacity);
    if (!options.sdfEdges)
    {
        style.edgeSoftness = 0.0f;
    }
    const float solidHeight = std::max(top ? options.topSolidHeight : options.bottomSolidHeight, 0.0f);
    const float solidT = height > 0.0f ? std::clamp(solidHeight / height, 0.0f, 0.95f) : 0.0f;
    if (top)
    {
        style.gradientStart = { 0.0f, solidT };
        style.gradientEnd = { 0.0f, 1.0f };
    }
    else
    {
        style.gradientStart = { 0.0f, 0.0f };
        style.gradientEnd = { 0.0f, 1.0f - solidT };
    }
    style.backdropBlurFollowsFillAlpha = options.backdropBlurFollowsFillAlpha;
    style.backdropBlurClipToInheritedMask = options.backdropBlurClipToInheritedMask;
    if (!options.drawTint)
    {
        style.opacity = 0.0f;
    }

    entt::entity fade = ui.scene().create_shape(
        { 0.0f, 0.0f },
        glm::vec2(1.0f),
        style,
        Renderer2DPrimitive::eRectangle);
    if (ShapeComponent* shape = ui.registry().try_get<ShapeComponent>(fade))
    {
        shape->sdfEdges = options.sdfEdges;
    }
    ui.set_layer(fade, options.layer, options.order + (top ? 0u : 1u), options.alwaysOnTop);
    ui.attach_stretch(
        fade,
        parent,
        top ? glm::vec2(0.0f, 0.0f) : glm::vec2(0.0f, 1.0f),
        top ? glm::vec2(1.0f, 0.0f) : glm::vec2(1.0f, 1.0f),
        top ? glm::vec2(0.0f, 0.0f) : glm::vec2(0.0f, 1.0f),
        glm::vec4(0.0f),
        glm::vec2(0.0f),
        scaled_size(0.0f, height, ui.scale()));
    if (options.dynamicCache)
    {
        ui.set_dynamic_cache(fade);
    }
    return fade;
}

inline std::array<entt::entity, 2> ui_create_scroll_edge_fades(
    UiBuilder& ui,
    entt::entity parent,
    const UiScrollEdgeFadeOptions& options)
{
    return {
        ui_create_scroll_edge_fade(ui, parent, UiScrollFadeEdge::eTop, options),
        ui_create_scroll_edge_fade(ui, parent, UiScrollFadeEdge::eBottom, options)
    };
}

struct UiScrollBarOptions
{
    float width = 4.0f;
    float hitWidth = 12.0f;
    float right = 6.0f;
    float top = 8.0f;
    float bottom = 8.0f;
    float minThumbHeight = 34.0f;
    std::string trackColor = "rgba(0, 0, 0, 0)";
    std::string thumbColor = "rgba(0, 0, 0, 0.28)";
    std::string thumbOutlineColor = "rgba(255, 255, 255, 0.22)";
    float thumbOutlineWidth = 0.0f;
    float visibleOpacity = 1.0f;
    float hiddenOpacity = 0.0f;
    float idleDelaySeconds = 0.85f;
    float fadeDurationSeconds = 0.22f;
    int32_t layer = 6;
    uint32_t order = 0u;
    bool alwaysOnTop = false;
    bool dynamicCache = true;
    bool interactive = true;
    bool fadeWhenIdle = true;
};

struct UiScrollBarHandle
{
    entt::entity track = entt::null;
    entt::entity thumb = entt::null;
    float top = 0.0f;
    float bottom = 0.0f;
    float minThumbHeight = 0.0f;
};

inline UiScrollBarHandle ui_create_scrollbar_indicator(
    UiBuilder& ui,
    entt::entity parent,
    const UiScrollBarOptions& options = {})
{
    UiScrollBarHandle handle = {};
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return handle;
    }

    const float width = std::max(scaled_scalar(options.width, ui.scale()), 1.0f);
    const float hitWidth = std::max(scaled_scalar(options.hitWidth, ui.scale()), width);
    const float radius = width * 0.5f;
    ShapeStyleComponent trackStyle = make_solid_style(options.trackColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f);
    if (std::max(trackStyle.color0.a, trackStyle.color1.a) <= 0.001f)
    {
        trackStyle.color0.a = 0.002f;
        trackStyle.color1.a = 0.002f;
    }
    trackStyle.edgeSoftness = 0.75f;
    ShapeStyleComponent thumbStyle = make_solid_style(
        options.thumbColor,
        options.thumbOutlineColor,
        scaled_scalar(options.thumbOutlineWidth, ui.scale()),
        1.0f);
    thumbStyle.edgeSoftness = 0.75f;

    handle.track = ui.scene().create_shape(
        { 0.0f, 0.0f },
        { hitWidth, hitWidth },
        trackStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.track, hitWidth * 0.5f);
    ui.set_layer(handle.track, options.layer, options.order, options.alwaysOnTop);
    ui.attach_stretch(
        handle.track,
        parent,
        { 1.0f, 0.0f },
        { 1.0f, 1.0f },
        { 1.0f, 0.0f },
        scaled_edges(0.0f, options.top, options.right, options.bottom, ui.scale()),
        glm::vec2(0.0f),
        { hitWidth, 0.0f });

    handle.thumb = ui.scene().create_shape(
        { 0.0f, 0.0f },
        { width, std::max(scaled_scalar(options.minThumbHeight, ui.scale()), width) },
        thumbStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.thumb, radius);
    ui.set_layer(handle.thumb, options.layer, options.order + 1u, options.alwaysOnTop);
    ui.attach_stretch(
        handle.thumb,
        handle.track,
        { 1.0f, 0.0f },
        { 1.0f, 0.0f },
        { 1.0f, 0.0f },
        glm::vec4(0.0f),
        glm::vec2(0.0f),
        { width, std::max(scaled_scalar(options.minThumbHeight, ui.scale()), width) });

    if (options.interactive)
    {
        ScrollBarInputComponent input = {};
        input.scrollTarget = parent;
        input.thumb = handle.thumb;
        input.fadeWhenIdle = options.fadeWhenIdle;
        input.visibleOpacity = std::clamp(options.visibleOpacity, 0.0f, 1.0f);
        input.hiddenOpacity = std::clamp(options.hiddenOpacity, 0.0f, input.visibleOpacity);
        input.idleDelaySeconds = std::max(options.idleDelaySeconds, 0.0f);
        input.fadeDurationSeconds = std::max(options.fadeDurationSeconds, 0.001f);
        input.wakeRequested = true;
        ui.registry().emplace<ScrollBarInputComponent>(handle.track, input);
    }

    handle.top = scaled_scalar(options.top, ui.scale());
    handle.bottom = scaled_scalar(options.bottom, ui.scale());
    handle.minThumbHeight = std::max(scaled_scalar(options.minThumbHeight, ui.scale()), width);

    return handle;
}

inline bool ui_set_scrollbar_visible(Renderer2DScene& scene, UiScrollBarHandle handle, bool visible)
{
    entt::registry& registry = scene.registry();
    bool changed = false;
    for (entt::entity entity : { handle.track, handle.thumb })
    {
        if (entity == entt::null || !registry.valid(entity))
        {
            continue;
        }
        if (RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(entity))
        {
            if (layer->visible == visible)
            {
                continue;
            }
            layer->visible = visible;
            scene.mark_dirty(entity);
            changed = true;
        }
    }
    return changed;
}

inline void ui_update_scrollbar_indicator(
    Renderer2DScene& scene,
    UiScrollBarHandle handle,
    float viewportHeight,
    float scrollOffset,
    float minOffset,
    float maxOffset)
{
    entt::registry& registry = scene.registry();
    if (handle.thumb == entt::null || !registry.valid(handle.thumb))
    {
        return;
    }

    const float scrollRange = std::max(maxOffset - minOffset, 0.0f);
    const float trackHeight = std::max(viewportHeight - handle.top - handle.bottom, 1.0f);
    if (scrollRange <= 0.5f || viewportHeight <= 1.0f)
    {
        ui_set_scrollbar_visible(scene, handle, false);
        if (ShapeStyleComponent* style = registry.try_get<ShapeStyleComponent>(handle.thumb))
        {
            if (style->opacity > 0.001f)
            {
                style->opacity = 0.0f;
                scene.mark_dirty(handle.thumb);
            }
        }
        return;
    }

    const bool visibilityChanged = ui_set_scrollbar_visible(scene, handle, true);
    const float contentHeight = viewportHeight + scrollRange;
    const float thumbHeight = std::clamp(
        trackHeight * (viewportHeight / std::max(contentHeight, 1.0f)),
        std::min(handle.minThumbHeight, trackHeight),
        trackHeight);
    const float normalized = std::clamp((scrollOffset - minOffset) / scrollRange, 0.0f, 1.0f);
    const float thumbY = (trackHeight - thumbHeight) * normalized;

    bool thumbChanged = false;
    if (Layout2DComponent* layout = registry.try_get<Layout2DComponent>(handle.thumb))
    {
        if (std::abs(layout->offset.y - thumbY) > 0.001f)
        {
            layout->offset.y = thumbY;
            thumbChanged = true;
        }
        if (std::abs(layout->size.y - thumbHeight) > 0.001f)
        {
            layout->size.y = thumbHeight;
            thumbChanged = true;
        }
    }
    if (ShapeComponent* shape = registry.try_get<ShapeComponent>(handle.thumb))
    {
        if (std::abs(shape->size.y - thumbHeight) > 0.001f)
        {
            shape->size.y = thumbHeight;
            thumbChanged = true;
        }
    }
    if (!visibilityChanged && !thumbChanged)
    {
        return;
    }
    if (thumbChanged)
    {
        ui_mark_moving_entity_dirty(scene, handle.thumb, 0.35f);
    }
    else if (visibilityChanged)
    {
        scene.mark_dirty(handle.thumb);
    }
}

struct UiScrollViewOptions
{
    // Scroll views combine a masked viewport, optional content, fades, and scrollbar
    glm::vec4 viewportMargin { 0.0f };
    glm::vec4 contentMargin { 0.0f };
    float viewportHeight = 0.0f;
    float contentHeight = 0.0f;
    float contentInitialOffset = 0.0f;
    float scrollStep = 48.0f;
    int32_t viewportLayer = 1;
    uint32_t viewportOrder = 0u;
    int32_t contentLayer = 1;
    uint32_t contentOrder = 1u;
    bool maskViewport = true;
    bool createContent = true;
    bool createScrollbar = false;
    bool createEdgeFades = false;
    bool edgeFadesAttachToViewport = false;
    bool cacheMovingContent = true;
    float movingContentActiveSeconds = 0.35f;
    entt::entity edgeFadeParent = entt::null;
    UiScrollBarOptions scrollbar {};
    UiScrollEdgeFadeOptions edgeFades {};
    std::function<void(const ScrollInputEvent&)> onScroll;
};

struct UiScrollViewHandle
{
    entt::entity viewport = entt::null;
    entt::entity content = entt::null;
    UiScrollBarHandle scrollbar {};
    float viewportHeight = 0.0f;
    float contentInitialOffset = 0.0f;
};

inline UiScrollViewHandle ui_create_scroll_view(
    UiBuilder& ui,
    entt::entity parent,
    const UiScrollViewOptions& options = {})
{
    // The viewport owns clipping while the content entity is the part that moves
    UiScrollViewHandle handle = {};
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return handle;
    }

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    handle.viewportHeight = std::max(scaled_scalar(options.viewportHeight, ui.scale()), 1.0f);
    handle.contentInitialOffset = scaled_scalar(options.contentInitialOffset, ui.scale());

    handle.viewport = scene.create_shape(
        { 0.0f, 0.0f },
        glm::vec2(1.0f),
        ui_clear_surface_style(0.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(handle.viewport, options.viewportLayer, options.viewportOrder);
    ui.attach_fill(handle.viewport, parent, options.viewportMargin);
    if (options.maskViewport)
    {
        scene.enable_mask(handle.viewport, false);
    }

    if (options.createEdgeFades)
    {
        const entt::entity fadeParent = options.edgeFadesAttachToViewport ?
            handle.viewport :
            (options.edgeFadeParent != entt::null ? options.edgeFadeParent : parent);
        ui_create_scroll_edge_fades(
            ui,
            fadeParent,
            options.edgeFades);
    }

    if (options.createContent)
    {
        handle.content = scene.create_shape(
            { 0.0f, 0.0f },
            glm::vec2(1.0f),
            ui_clear_surface_style(0.0f),
            Renderer2DPrimitive::eRectangle);
        ui.set_layer(handle.content, options.contentLayer, options.contentOrder);
        ui.attach_stretch(
            handle.content,
            handle.viewport,
            { 0.0f, 0.0f },
            { 1.0f, 0.0f },
            { 0.0f, 0.0f },
            scaled_edges(
                options.contentMargin.x,
                options.contentMargin.y,
                options.contentMargin.z,
                options.contentMargin.w,
                ui.scale()),
            scaled_offset(0.0f, options.contentInitialOffset, ui.scale()),
            scaled_size(0.0f, options.contentHeight, ui.scale()));
    }

    if (options.createScrollbar)
    {
        handle.scrollbar = ui_create_scrollbar_indicator(ui, handle.viewport, options.scrollbar);
    }

    ScrollInputComponent scroll = {};
    scroll.step = scaled_scalar(options.scrollStep, ui.scale());
    scroll.maxOffset = std::max(
        0.0f,
        handle.contentInitialOffset + scaled_scalar(options.contentHeight, ui.scale()) - handle.viewportHeight);
    ui_update_scrollbar_indicator(
        scene,
        handle.scrollbar,
        handle.viewportHeight,
        scroll.offset,
        scroll.minOffset,
        scroll.maxOffset);

    auto userOnScroll = options.onScroll;
    scroll.onScroll = [
        scenePtr = &scene,
        content = handle.content,
        scrollbar = handle.scrollbar,
        viewportHeight = handle.viewportHeight,
        initialOffset = handle.contentInitialOffset,
        cacheMovingContent = options.cacheMovingContent,
        movingContentActiveSeconds = options.movingContentActiveSeconds,
        userOnScroll](const ScrollInputEvent& event) {
        entt::registry& capturedRegistry = scenePtr->registry();
        const ScrollInputComponent* scrollInput = capturedRegistry.try_get<ScrollInputComponent>(event.target);
        if (!scrollInput)
        {
            return;
        }

        if (content != entt::null && capturedRegistry.valid(content))
        {
            if (Layout2DComponent* layout = capturedRegistry.try_get<Layout2DComponent>(content))
            {
                layout->offset.y = initialOffset - scrollInput->offset;
                if (cacheMovingContent)
                {
                    ui_mark_moving_entity_dirty(*scenePtr, content, movingContentActiveSeconds);
                }
                else
                {
                    scenePtr->mark_dirty(content);
                }
            }
        }
        ui_update_scrollbar_indicator(
            *scenePtr,
            scrollbar,
            viewportHeight,
            scrollInput->offset,
            scrollInput->minOffset,
            scrollInput->maxOffset);
        if (userOnScroll)
        {
            userOnScroll(event);
        }
    };
    registry.emplace<ScrollInputComponent>(handle.viewport, std::move(scroll));
    return handle;
}

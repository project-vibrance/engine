// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>

struct UiScrollViewportEntranceOptions
{


    bool enabled = true;
    float durationSeconds = 0.44f;
    float delaySeconds = 0.0f;
    float startScale = 1.06f;
    float startOpacity = 0.0f;
    DisplayTransitionCurve2D curve = DisplayTransitionCurve2D::eEaseOut;
};

struct UiScrollViewportBuildContext
{


    UiBuilder& ui;
    entt::entity content = entt::null;
    int32_t layer = 1;
    uint32_t order = 0u;
    glm::vec2 viewportSize { 0.0f };
};

struct UiScrollViewportOptions
{

    float contentHeight = 0.0f;
    glm::vec4 contentPadding { 10.0f, 8.0f, 18.0f, 8.0f };
    float scrollStep = 42.0f;
    bool enabled = true;
    bool showScrollbar = true;
    bool scrollbarFadesWhenIdle = true;
    bool showEdgeFades = false;
    float cornerRadius = 8.0f;
    std::string backgroundColor = "rgba(255, 255, 255, 0.96)";
    std::string backgroundBottomColor {};
    std::string outlineColor = "#0A84FFFF";
    float outlineWidth = 1.8f;
    float opacity = 1.0f;
    float backdropBlurRadius = 0.0f;
    uint32_t backdropBlurPasses = 1u;
    float backdropBlurOpacity = 0.85f;
    int32_t layer = 1;
    uint32_t order = 0u;
    UiScrollBarOptions scrollbar {};
    UiScrollEdgeFadeOptions edgeFades {};
    UiScrollViewportEntranceOptions entrance {};
    std::function<void(const UiScrollViewportBuildContext&)> buildContent;
    std::function<void(const ScrollInputEvent&)> onScroll;
};

struct UiScrollViewportHandle
{



    entt::entity root = entt::null;
    entt::entity viewport = entt::null;
    entt::entity border = entt::null;
    entt::entity content = entt::null;
    UiScrollBarHandle scrollbar {};
    UiScrollViewHandle scrollView {};
};

inline UiScrollBarOptions ui_scrollbar_options_from_theme(
    const UiTheme& theme,
    float right,
    float top,
    float bottom,
    uint32_t order)
{

    UiScrollBarOptions options = {};
    options.width = 4.0f;
    options.right = right;
    options.top = top;
    options.bottom = bottom;
    options.minThumbHeight = 42.0f;
    options.thumbColor = theme.settingsPanel.scrollbarThumb;
    options.thumbOutlineColor = theme.settingsPanel.scrollbarThumbOutline;
    options.layer = 7;
    options.order = order;
    return options;
}

inline UiScrollViewportHandle ui_create_scroll_viewport(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    UiScrollViewportOptions options = {})
{


    UiScrollViewportHandle handle = {};
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return handle;
    }

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    const glm::vec2 logicalSize = glm::max(size, glm::vec2(1.0f));
    const float contentHeight = options.contentHeight > 0.0f ?
        options.contentHeight : logicalSize.y;

    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(logicalSize.x, logicalSize.y, ui.scale()),
        ui_clear_surface_style(0.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(handle.root, options.layer, options.order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaled_size(logicalSize.x, logicalSize.y, ui.scale()));

    UiScrollViewOptions scroll = {};
    scroll.viewportHeight = logicalSize.y;
    scroll.contentHeight = std::max(contentHeight, 1.0f);
    scroll.contentMargin = options.contentPadding;
    scroll.scrollStep = std::max(options.scrollStep, 1.0f);
    scroll.viewportLayer = options.layer;
    scroll.viewportOrder = options.order + 1u;
    scroll.contentLayer = options.layer + 1;
    scroll.contentOrder = options.order;
    scroll.createScrollbar = options.showScrollbar;
    scroll.createEdgeFades = options.showEdgeFades;
    scroll.edgeFadesAttachToViewport = true;
    scroll.scrollbar = options.scrollbar;
    scroll.scrollbar.layer = options.layer + 2;
    scroll.scrollbar.order = options.order;
    scroll.scrollbar.fadeWhenIdle = options.scrollbarFadesWhenIdle;
    scroll.edgeFades = options.edgeFades;
    scroll.edgeFades.layer = options.layer + 2;
    scroll.edgeFades.order = options.order + 10u;
    scroll.onScroll = std::move(options.onScroll);

    handle.scrollView = ui_create_scroll_view(ui, handle.root, scroll);
    handle.viewport = handle.scrollView.viewport;
    handle.content = handle.scrollView.content;
    handle.scrollbar = handle.scrollView.scrollbar;
    if (handle.viewport == entt::null || !registry.valid(handle.viewport))
    {
        return handle;
    }

    const std::string bottomColor = options.backgroundBottomColor.empty() ?
        options.backgroundColor : options.backgroundBottomColor;
    ShapeStyleComponent surfaceStyle =
        options.backdropBlurRadius > 0.0f || !options.backgroundBottomColor.empty() ?
        ui_macos26_frosted_control_style(
            ui,
            options.backgroundColor,
            bottomColor,
            "rgba(0, 0, 0, 0)",
            0.0f,
            options.opacity,
            options.backdropBlurRadius,
            options.backdropBlurPasses,
            options.backdropBlurOpacity) :
        ui_macos26_control_style(
            ui,
            options.backgroundColor,
            "rgba(0, 0, 0, 0)",
            0.0f,
            options.opacity);
    surfaceStyle.edgeSoftness = scaled_scalar(0.7f, ui.scale());
    registry.emplace_or_replace<ShapeStyleComponent>(handle.viewport, surfaceStyle);
    ui.set_shape(handle.viewport, std::max(options.cornerRadius, 0.0f));
    if (ShapeComponent* shape = registry.try_get<ShapeComponent>(handle.viewport))
    {
        shape->primitive = Renderer2DPrimitive::eRoundedRectangle;
    }

    ShapeStyleComponent borderStyle = make_solid_style(
        "rgba(0, 0, 0, 0)",
        options.outlineColor,
        scaled_scalar(std::max(options.outlineWidth, 0.0f), ui.scale()),
        options.opacity);
    borderStyle.edgeSoftness = scaled_scalar(0.7f, ui.scale());
    handle.border = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(logicalSize.x, logicalSize.y, ui.scale()),
        borderStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.border, std::max(options.cornerRadius, 0.0f));
    ui.set_layer(handle.border, options.layer + 3, options.order);
    ui.attach_fill(handle.border, handle.root);

    registry.emplace<InputTransparent2DComponent>(handle.border);

    if (ScrollInputComponent* input = registry.try_get<ScrollInputComponent>(handle.viewport))
    {
        input->enabled = options.enabled;
    }

    if (options.buildContent && handle.content != entt::null && registry.valid(handle.content))
    {
        const UiScrollViewportBuildContext context {
            ui,
            handle.content,
            options.layer + 1,
            options.order,
            logicalSize
        };
        options.buildContent(context);
    }

    if (options.entrance.enabled && options.entrance.durationSeconds > 0.0f)
    {
        DisplayTransition2DComponent transition = {};
        transition.inheritToChildren = false;
        transition.durationSeconds = options.entrance.durationSeconds;
        transition.fromOpacity = std::clamp(options.entrance.startOpacity, 0.0f, 1.0f);
        transition.toOpacity = 1.0f;
        transition.fromScale = glm::vec2(std::max(options.entrance.startScale, 0.0f));
        transition.toScale = glm::vec2(1.0f);
        transition.curve = options.entrance.curve;
        transition.set_delay(options.entrance.delaySeconds);


        scene.play_display_transition(handle.border, transition, 0.0);
    }

    scene.mark_dirty(handle.viewport);
    scene.mark_dirty(handle.border);
    return handle;
}


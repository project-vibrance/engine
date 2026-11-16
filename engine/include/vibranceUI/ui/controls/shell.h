// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>
#include <vibranceUI/ui/controls/navigation.h>
#include <vibranceUI/ui/controls/search.h>

struct UiSettingsShellOptions
{

    UiNavClusterOptions nav {};
    UiSearchFieldOptions search {};
    UiScrollEdgeFadeOptions sidebarEdgeFades {};
    UiScrollEdgeFadeOptions rightEdgeFades {};
    UiScrollBarOptions sidebarScrollbar {};
    UiScrollBarOptions rightScrollbar {};
    float panelHeight = 1.0f;
    glm::vec2 navOffset { 10.0f, 12.0f };
    glm::vec2 navSize { 84.0f, 40.0f };
    int32_t navLayer = 2;
    uint32_t navOrder = 0u;
    int32_t searchLayer = 6;
    uint32_t searchOrder = 1u;
    float sidebarStaticTop = 72.0f;
    float sidebarSearchHeight = 30.0f;
    glm::vec2 sidebarSearchMargin { 22.0f, 14.0f };
    float sidebarViewportTop = 0.0f;
    float sidebarBottom = 0.0f;
    glm::vec2 sidebarContentMargin { 22.0f, 14.0f };
    float sidebarContentInitialOffset = 72.0f;
    float sidebarContentHeight = 840.0f;
    float sidebarScrollStep = 34.0f;
    int32_t sidebarViewportLayer = 1;
    uint32_t sidebarViewportOrder = 0u;
    int32_t sidebarContentLayer = 1;
    uint32_t sidebarContentOrder = 1u;
    bool sidebarFades = true;
    bool sidebarScrollbarEnabled = true;
    bool fixedChromeDynamicCache = false;
    glm::vec4 rightViewportMargin { 32.0f, 72.0f, 28.0f, 24.0f };
    float rightScrollStep = 44.0f;
    int32_t rightViewportLayer = 1;
    uint32_t rightViewportOrder = 0u;
    bool rightFades = true;
    bool rightScrollbarEnabled = true;

    std::function<void(const ScrollInputEvent&)> onRightScroll;
};

struct UiSettingsShellHandle
{

    UiControlHandle navCluster {};
    UiControlHandle search {};
    UiScrollViewHandle sidebar {};
    UiScrollViewHandle right {};
    entt::entity sidebarContent = entt::null;
    entt::entity rightViewport = entt::null;
    UiScrollBarHandle rightScrollbar {};
    float sidebarViewportHeight = 0.0f;
    float rightViewportHeight = 0.0f;
};

using UiSplitShellOptions = UiSettingsShellOptions;
using UiSplitShellHandle = UiSettingsShellHandle;

inline UiSettingsShellHandle ui_create_settings_shell(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity leftSection,
    entt::entity rightSection,
    UiSettingsShellOptions options = {})
{

    UiSettingsShellHandle handle = {};
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    if (leftSection == entt::null || rightSection == entt::null ||
        !registry.valid(leftSection) || !registry.valid(rightSection))
    {
        return handle;
    }

    handle.navCluster = ui_create_nav_cluster(
        ui,
        fontAtlas,
        rightSection,
        UiAlignment::eTopLeft,
        options.navOffset,
        options.navSize,
        options.navLayer,
        options.navOrder,
        options.nav);
    if (options.fixedChromeDynamicCache && handle.navCluster.root != entt::null && registry.valid(handle.navCluster.root))
    {
        ui.set_dynamic_cache(handle.navCluster.root);
    }

    handle.search = ui_create_search_field(
        ui,
        fontAtlas,
        leftSection,
        UiAlignment::eTopLeft,
        { options.sidebarSearchMargin.x, options.sidebarStaticTop },
        { 1.0f, options.sidebarSearchHeight },
        options.searchLayer,
        options.searchOrder,
        options.search);
    if (options.fixedChromeDynamicCache && handle.search.root != entt::null && registry.valid(handle.search.root))
    {
        ui.set_dynamic_cache(handle.search.root);
    }
    ui.attach_stretch(
        handle.search.root,
        leftSection,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        scaled_edges(
            options.sidebarSearchMargin.x,
            0.0f,
            options.sidebarSearchMargin.y,
            0.0f,
            ui.scale()),
        scaled_offset(0.0f, options.sidebarStaticTop, ui.scale()),
        scaled_size(0.0f, options.sidebarSearchHeight, ui.scale()));

    const float sidebarViewportHeight = std::max(
        1.0f,
        options.panelHeight - options.sidebarViewportTop - options.sidebarBottom);
    handle.sidebarViewportHeight = scaled_scalar(sidebarViewportHeight, ui.scale());
    UiScrollViewOptions sidebarScroll = {};
    sidebarScroll.viewportMargin = {
        0.0f,
        options.sidebarViewportTop,
        0.0f,
        options.sidebarBottom
    };
    sidebarScroll.contentMargin = {
        options.sidebarContentMargin.x,
        0.0f,
        options.sidebarContentMargin.y,
        0.0f
    };
    sidebarScroll.viewportHeight = sidebarViewportHeight;
    sidebarScroll.contentHeight = options.sidebarContentHeight;
    sidebarScroll.contentInitialOffset = options.sidebarContentInitialOffset;
    sidebarScroll.scrollStep = options.sidebarScrollStep;
    sidebarScroll.viewportLayer = options.sidebarViewportLayer;
    sidebarScroll.viewportOrder = options.sidebarViewportOrder;
    sidebarScroll.contentLayer = options.sidebarContentLayer;
    sidebarScroll.contentOrder = options.sidebarContentOrder;
    sidebarScroll.createScrollbar = options.sidebarScrollbarEnabled;
    sidebarScroll.createEdgeFades = options.sidebarFades;
    sidebarScroll.edgeFadeParent = leftSection;
    sidebarScroll.scrollbar = options.sidebarScrollbar;
    sidebarScroll.edgeFades = options.sidebarEdgeFades;
    if (options.fixedChromeDynamicCache)
    {
        sidebarScroll.edgeFades.dynamicCache = true;
    }
    handle.sidebar = ui_create_scroll_view(ui, leftSection, sidebarScroll);
    handle.sidebarContent = handle.sidebar.content;

    const float rightViewportHeight = std::max(
        1.0f,
        options.panelHeight - options.rightViewportMargin.y - options.rightViewportMargin.w);
    handle.rightViewportHeight = scaled_scalar(rightViewportHeight, ui.scale());

    UiScrollViewOptions rightScroll = {};
    rightScroll.viewportMargin = options.rightViewportMargin;
    rightScroll.viewportHeight = rightViewportHeight;
    rightScroll.scrollStep = options.rightScrollStep;
    rightScroll.viewportLayer = options.rightViewportLayer;
    rightScroll.viewportOrder = options.rightViewportOrder;
    rightScroll.createContent = false;
    rightScroll.createScrollbar = options.rightScrollbarEnabled;
    rightScroll.createEdgeFades = options.rightFades;
    rightScroll.edgeFadeParent = rightSection;
    rightScroll.scrollbar = options.rightScrollbar;
    rightScroll.edgeFades = options.rightEdgeFades;
    rightScroll.onScroll = std::move(options.onRightScroll);
    if (options.fixedChromeDynamicCache)
    {
        rightScroll.edgeFades.dynamicCache = true;
    }
    handle.right = ui_create_scroll_view(ui, rightSection, rightScroll);
    handle.rightViewport = handle.right.viewport;
    handle.rightScrollbar = handle.right.scrollbar;
    return handle;
}

inline UiSplitShellHandle ui_create_split_shell(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity leftSection,
    entt::entity rightSection,
    UiSplitShellOptions options = {})
{

    return ui_create_settings_shell(
        ui,
        fontAtlas,
        leftSection,
        rightSection,
        std::move(options));
}


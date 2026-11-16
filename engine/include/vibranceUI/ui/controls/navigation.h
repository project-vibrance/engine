// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>
#include <vibranceUI/ui/controls/navigation_history.h>

struct UiNavClusterOptions
{

    Media2DHandle backIcon {};
    Media2DHandle forwardIcon {};
    bool backEnabled = true;
    bool forwardEnabled = true;
    float cornerRadius = 0.0f;
    float iconSize = 16.0f;
    std::string backgroundColor = "rgba(255, 255, 255, 0.90)";
    std::string backgroundBottomColor {};
    float backgroundOpacity = 1.0f;
    std::string outlineColor = "rgba(0, 0, 0, 0.16)";
    float outlineWidth = 0.65f;
    float backdropBlurRadius = 0.0f;
    uint32_t backdropBlurPasses = 1u;
    float backdropBlurOpacity = 0.85f;
    UiGlassOptions glass {};
    std::string dividerColor = "rgba(0, 0, 0, 0.08)";
    std::string iconTint = "#55585EFF";
    std::string disabledIconTint = "#B1B4BAFF";
    std::function<void(const PointerInputEvent&)> onBack;
    std::function<void(const PointerInputEvent&)> onForward;
    bool useHistory = false;
    std::size_t initialPage = 0u;
    std::function<void(std::size_t, const PointerInputEvent&)> onNavigatePage;
};

struct UiSidebarNavItemHandle
{

    entt::entity row = entt::null;
    entt::entity icon = entt::null;
    entt::entity glyph = entt::null;
    entt::entity mediaIcon = entt::null;
    entt::entity label = entt::null;
};

struct UiSidebarRichButtonHandle
{

    entt::entity row = entt::null;
    entt::entity mediaFrame = entt::null;
    entt::entity media = entt::null;
    entt::entity avatar = entt::null;
    entt::entity avatarMedia = entt::null;
    entt::entity title = entt::null;
    entt::entity subtitle = entt::null;
};

struct UiSplitShellRuntimeState
{

    entt::entity titleText = entt::null;
    entt::entity navCluster = entt::null;
    UiSidebarRichButtonHandle featuredRow {};
    entt::entity rightViewport = entt::null;
    entt::entity rightContent = entt::null;
    UiScrollBarHandle rightScrollbar {};
    float rightViewportHeight = 0.0f;
    float rightContentInitialOffset = 0.0f;
    float pendingRightScrollOffset = -1.0f;
    std::size_t activeIndex = 0u;
    std::vector<UiSidebarNavItemHandle> sidebarItems;
};

struct UiSidebarNavItemOptions
{

    std::string glyph;
    Media2DHandle mediaIcon {};
    std::string iconColor = "#0A84FFFF";
    bool selected = false;
    float rowHeight = 36.0f;
    float rowCornerRadius = 9.0f;
    float iconSize = 26.0f;
    float iconCornerRadius = 7.0f;
    UiAlignment labelAlignment = UiAlignment::eMiddleLeft;
    glm::vec2 iconOffset { 8.0f, 0.0f };
    glm::vec2 labelOffset { 44.0f, 0.0f };
    float labelFontSize = 15.0f;
    std::size_t maxLabelCharacters = 0u;
    float glyphFontSize = 11.0f;
    glm::vec2 mediaIconSize { 16.0f, 16.0f };
    Media2DFit mediaIconFit = Media2DFit::eContain;
    bool mediaIconUsesContainer = true;
    bool showIconBackground = true;
    std::string mediaIconTint;
    std::string selectedMediaIconTint;
    std::string selectedRowColor = "rgba(0, 113, 227, 1)";
    std::string selectedHoveredRowColor = "rgba(0, 103, 207, 1)";
    std::string selectedPressedRowColor = "rgba(0, 92, 190, 1)";
    std::string hoveredRowColor = "rgba(255, 255, 255, 0.55)";
    std::string pressedRowColor = "rgba(230, 230, 232, 0.75)";
    std::string clearRowColor = "rgba(255, 255, 255, 0.002)";
    std::string selectedIconColor = "#111111FF";
    std::string iconOutlineColor = "rgba(255, 255, 255, 0.34)";
    float iconOutlineWidth = 0.6f;
    std::string selectedLabelColor = "#FFFFFFFF";
    std::string labelColor = "#2C2D30FF";
    float selectedLabelWeight = 590.0f;
    float labelWeight = 450.0f;
    std::function<void(const PointerInputEvent&)> onClick;
};

struct UiSidebarRichButtonOptions
{

    Media2DHandle media {};
    Media2DHandle avatar {};
    std::string title;
    std::string subtitle;
    bool selected = false;
    bool showMedia = true;
    float rowHeight = 56.0f;
    float rowCornerRadius = 12.0f;
    float mediaRadius = 0.0f;
    glm::vec2 mediaCenter { 28.0f, 28.0f };
    std::string mediaBackgroundColor {};
    std::string mediaOutlineColor {};
    Media2DFit mediaFit = Media2DFit::eCover;
    float avatarRadius = 23.0f;
    glm::vec2 avatarCenter { 28.0f, 28.0f };
    glm::vec2 textOffset { 70.0f, 10.0f };
    float titleFontSize = 16.0f;
    float subtitleFontSize = 13.0f;
    std::size_t maxTitleCharacters = 0u;
    std::size_t maxSubtitleCharacters = 0u;
    std::string selectedRowColor = "rgba(0, 113, 227, 1)";
    std::string selectedHoveredRowColor = "rgba(0, 103, 207, 1)";
    std::string selectedPressedRowColor = "rgba(0, 92, 190, 1)";
    std::string hoveredRowColor = "rgba(255, 255, 255, 0.55)";
    std::string pressedRowColor = "rgba(230, 230, 232, 0.75)";
    std::string clearRowColor = "rgba(255, 255, 255, 0.002)";
    std::string avatarBackgroundColor = "rgba(222, 166, 130, 1)";
    std::string avatarOutlineColor = "#FFFFFF96";
    std::string titleColor = "#2C2D30FF";
    std::string subtitleColor = "#6F7278FF";
    std::string selectedTitleColor = "#FFFFFFFF";
    std::string selectedSubtitleColor = "rgba(255, 255, 255, 0.82)";
    float titleWeight = 700.0f;
    float subtitleWeight = 430.0f;
    std::function<void(const PointerInputEvent&)> onClick;
};

inline void ui_apply_sidebar_nav_palette(const UiTheme& theme, UiSidebarNavItemOptions& options)
{

    options.selectedRowColor = theme.settingsPanel.sidebarRowSelected;
    options.selectedHoveredRowColor = theme.settingsPanel.sidebarRowSelectedHover;
    options.selectedPressedRowColor = theme.settingsPanel.sidebarRowSelectedPressed;
    options.hoveredRowColor = theme.settingsPanel.sidebarRowHover;
    options.pressedRowColor = theme.settingsPanel.sidebarRowPressed;
    options.labelColor = theme.settingsPanel.sidebarLabel;
    options.selectedLabelColor = theme.settingsPanel.sidebarSelectedLabel;
    if (options.mediaIcon.sourceType == Media2DSourceType::eSvg &&
        !options.mediaIconUsesContainer && !options.showIconBackground)
    {
        options.mediaIconTint = theme.icon;
        options.selectedMediaIconTint = theme.settingsPanel.sidebarSelectedLabel;
    }
}

inline void ui_apply_rich_sidebar_row_palette(const UiTheme& theme, UiSidebarRichButtonOptions& options)
{

    options.selectedRowColor = theme.settingsPanel.sidebarRowSelected;
    options.selectedHoveredRowColor = theme.settingsPanel.sidebarRowSelectedHover;
    options.selectedPressedRowColor = theme.settingsPanel.sidebarRowSelectedPressed;
    options.hoveredRowColor = theme.settingsPanel.sidebarRowHover;
    options.pressedRowColor = theme.settingsPanel.sidebarRowPressed;
    options.titleColor = theme.settingsPanel.sidebarLabel;
    options.subtitleColor = theme.settingsPanel.sidebarSubLabel;
    options.selectedTitleColor = theme.settingsPanel.sidebarSelectedLabel;
    options.selectedSubtitleColor = "rgba(255, 255, 255, 0.82)";
    options.mediaBackgroundColor = theme.settingsPanel.avatarBackground;
}

inline UiNavClusterOptions ui_nav_cluster_options_from_theme(
    const UiTheme& theme,
    Media2DHandle backIcon = {},
    Media2DHandle forwardIcon = {})
{

    UiNavClusterOptions options = {};
    options.backIcon = backIcon;
    options.forwardIcon = forwardIcon;
    options.iconSize = 19.0f;
    options.backgroundColor = theme.settingsPanel.navBackground;
    options.backgroundBottomColor = theme.settingsPanel.navBackgroundBottom;
    options.outlineColor = theme.settingsPanel.navOutline;
    options.outlineWidth = 0.65f;
    options.backdropBlurRadius = 18.0f;
    options.backdropBlurPasses = 2u;
    options.backdropBlurOpacity = 0.9f;
    options.dividerColor = theme.settingsPanel.navDivider;
    options.iconTint = theme.settingsPanel.navIcon;
    options.disabledIconTint = theme.settingsPanel.navIconDisabled;
    return options;
}

inline TextStyleComponent ui_sidebar_nav_label_style(
    const UiSidebarNavItemOptions& options,
    bool selected)
{
    TextStyleComponent style = ui_macos26_text_style(selected ? options.selectedLabelColor : options.labelColor);
    style.set_font_weight(selected ? options.selectedLabelWeight : options.labelWeight);
    return style;
}

inline ShapeStyleComponent ui_sidebar_nav_row_style(
    const UiSidebarNavItemOptions& options,
    bool selected)
{
    return selected ?
        make_solid_style(options.selectedRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f) :
        make_solid_style(options.clearRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f);
}

inline ButtonVisualComponent ui_sidebar_nav_visual(
    const UiSidebarNavItemOptions& options,
    bool selected)
{
    ButtonVisualComponent visual = {};
    visual.idle = ui_sidebar_nav_row_style(options, selected);
    visual.hovered = selected ?
        make_solid_style(options.selectedHoveredRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f) :
        make_solid_style(options.hoveredRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f);
    visual.pressed = selected ?
        make_solid_style(options.selectedPressedRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f) :
        make_solid_style(options.pressedRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f);
    return visual;
}

inline void ui_apply_sidebar_nav_item_state(
    Renderer2DScene& scene,
    const UiSidebarNavItemHandle& handle,
    bool selected,
    UiSidebarNavItemOptions options = {})
{
    entt::registry& registry = scene.registry();
    options.selected = selected;
    if (ShapeStyleComponent* rowStyle = registry.try_get<ShapeStyleComponent>(handle.row))
    {
        *rowStyle = ui_sidebar_nav_row_style(options, selected);
    }
    if (ButtonVisualComponent* visual = registry.try_get<ButtonVisualComponent>(handle.row))
    {
        *visual = ui_sidebar_nav_visual(options, selected);
    }
    if (handle.icon != entt::null && registry.valid(handle.icon))
    {
        if (ShapeStyleComponent* iconStyle = registry.try_get<ShapeStyleComponent>(handle.icon))
        {
            *iconStyle = make_solid_style(
                options.showIconBackground ? (selected ? options.selectedIconColor : options.iconColor) : "rgba(255, 255, 255, 0.002)",
                options.showIconBackground ? options.iconOutlineColor : "rgba(0, 0, 0, 0)",
                options.showIconBackground ? iconStyle->outlineWidth : 0.0f,
                1.0f);
        }
    }
    if (TextStyleComponent* labelStyle = registry.try_get<TextStyleComponent>(handle.label))
    {
        *labelStyle = ui_sidebar_nav_label_style(options, selected);
    }
    const auto& iconTint = selected && !options.selectedMediaIconTint.empty() ?
        options.selectedMediaIconTint : options.mediaIconTint;
    if (!iconTint.empty()) ui_set_media_mask_tint(scene, handle.mediaIcon, iconTint);
    ui_update_button_visual(scene, handle.row);
    if (handle.icon != entt::null && registry.valid(handle.icon))
    {
        scene.mark_dirty(handle.icon);
    }
    scene.mark_dirty(handle.label);
}

inline TextStyleComponent ui_sidebar_rich_button_text_style(
    std::string_view color,
    float weight)
{
    TextStyleComponent style = ui_macos26_text_style(color);
    style.set_font_weight(weight);
    return style;
}

inline void ui_apply_sidebar_rich_button_state(
    Renderer2DScene& scene,
    const UiSidebarRichButtonHandle& handle,
    bool selected,
    UiSidebarRichButtonOptions options = {})
{
    entt::registry& registry = scene.registry();
    options.selected = selected;
    UiSidebarNavItemOptions navOptions = {};
    navOptions.selectedRowColor = options.selectedRowColor;
    navOptions.selectedHoveredRowColor = options.selectedHoveredRowColor;
    navOptions.selectedPressedRowColor = options.selectedPressedRowColor;
    navOptions.hoveredRowColor = options.hoveredRowColor;
    navOptions.pressedRowColor = options.pressedRowColor;
    navOptions.clearRowColor = options.clearRowColor;

    if (ShapeStyleComponent* rowStyle = registry.try_get<ShapeStyleComponent>(handle.row))
    {
        *rowStyle = ui_sidebar_nav_row_style(navOptions, selected);
    }
    if (ButtonVisualComponent* visual = registry.try_get<ButtonVisualComponent>(handle.row))
    {
        *visual = ui_sidebar_nav_visual(navOptions, selected);
    }
    if (TextStyleComponent* titleStyle = registry.try_get<TextStyleComponent>(handle.title))
    {
        *titleStyle = ui_sidebar_rich_button_text_style(
            selected ? options.selectedTitleColor : options.titleColor,
            options.titleWeight);
        scene.mark_dirty(handle.title);
    }
    if (TextStyleComponent* subtitleStyle = registry.try_get<TextStyleComponent>(handle.subtitle))
    {
        *subtitleStyle = ui_sidebar_rich_button_text_style(
            selected ? options.selectedSubtitleColor : options.subtitleColor,
            options.subtitleWeight);
        scene.mark_dirty(handle.subtitle);
    }
    ui_update_button_visual(scene, handle.row);
    scene.mark_dirty(handle.row);
}

inline UiSidebarRichButtonHandle ui_create_sidebar_rich_button(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    float y,
    int32_t layer,
    uint32_t order,
    UiSidebarRichButtonOptions options = {})
{

    UiSidebarRichButtonHandle handle = {};
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return handle;
    }

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiSidebarNavItemOptions navOptions = {};
    navOptions.selectedRowColor = options.selectedRowColor;
    navOptions.selectedHoveredRowColor = options.selectedHoveredRowColor;
    navOptions.selectedPressedRowColor = options.selectedPressedRowColor;
    navOptions.hoveredRowColor = options.hoveredRowColor;
    navOptions.pressedRowColor = options.pressedRowColor;
    navOptions.clearRowColor = options.clearRowColor;

    handle.row = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(1.0f, options.rowHeight, ui.scale()),
        ui_sidebar_nav_row_style(navOptions, options.selected),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.row, options.rowCornerRadius);
    ui.set_layer(handle.row, layer, order);
    ui.attach_stretch(
        handle.row,
        parent,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        glm::vec4(0.0f),
        scaled_offset(0.0f, y, ui.scale()),
        scaled_size(0.0f, options.rowHeight, ui.scale()));

    ButtonInputComponent input = {};
    input.onClick = std::move(options.onClick);
    registry.emplace<ButtonInputComponent>(handle.row, std::move(input));
    registry.emplace<ButtonVisualComponent>(handle.row, ui_sidebar_nav_visual(navOptions, options.selected));

    const bool usesGenericMedia = options.media.valid();
    const Media2DHandle mediaHandle = usesGenericMedia ? options.media : options.avatar;
    const float mediaRadius = usesGenericMedia && options.mediaRadius > 0.0f ? options.mediaRadius : options.avatarRadius;
    const glm::vec2 mediaCenter = usesGenericMedia ? options.mediaCenter : options.avatarCenter;
    const std::string mediaBackground = usesGenericMedia && !options.mediaBackgroundColor.empty() ?
        options.mediaBackgroundColor :
        options.avatarBackgroundColor;
    const std::string mediaOutline = usesGenericMedia && !options.mediaOutlineColor.empty() ?
        options.mediaOutlineColor :
        options.avatarOutlineColor;

    if (options.showMedia)
    {
        handle.mediaFrame = ui_create_dot(
            ui,
            handle.row,
            mediaCenter,
            mediaRadius,
            mediaBackground,
            layer + 1,
            order,
            mediaOutline,
            1.0f);
        handle.avatar = handle.mediaFrame;
        scene.enable_mask(handle.mediaFrame, false);
        if (mediaHandle.valid())
        {
            handle.media = ui.create_aligned_media(
                mediaHandle,
                handle.mediaFrame,
                UiAlignment::eCenter,
                { mediaRadius * 2.0f, mediaRadius * 2.0f },
                layer + 2,
                order,
                glm::vec2(0.0f),
                options.mediaFit);
            handle.avatarMedia = handle.media;
        }
    }

    handle.title = ui.create_aligned_text(
        ui_truncate_text_with_ellipsis(options.title, options.maxTitleCharacters),
        fontAtlas,
        handle.row,
        UiAlignment::eTopLeft,
        options.titleFontSize,
        ui_sidebar_rich_button_text_style(
            options.selected ? options.selectedTitleColor : options.titleColor,
            options.titleWeight),
        layer + 1,
        order + 1u,
        options.textOffset);
    handle.subtitle = ui.create_aligned_text(
        ui_truncate_text_with_ellipsis(options.subtitle, options.maxSubtitleCharacters),
        fontAtlas,
        handle.row,
        UiAlignment::eTopLeft,
        options.subtitleFontSize,
        ui_sidebar_rich_button_text_style(
            options.selected ? options.selectedSubtitleColor : options.subtitleColor,
            options.subtitleWeight),
        layer + 1,
        order + 2u,
        { options.textOffset.x, options.textOffset.y + 22.0f });

    ui_update_button_visual(scene, handle.row);
    return handle;
}

inline UiSidebarNavItemHandle ui_create_sidebar_nav_item(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    float y,
    std::string label,
    int32_t layer,
    uint32_t order,
    UiSidebarNavItemOptions options = {})
{
    UiSidebarNavItemHandle handle = {};
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return handle;
    }

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    handle.row = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(1.0f, options.rowHeight, ui.scale()),
        ui_sidebar_nav_row_style(options, options.selected),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.row, options.rowCornerRadius);
    ui.set_layer(handle.row, layer, order);
    ui.attach_stretch(
        handle.row,
        parent,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        glm::vec4(0.0f),
        scaled_offset(0.0f, y, ui.scale()),
        scaled_size(0.0f, options.rowHeight, ui.scale()));

    ButtonInputComponent input = {};
    input.onClick = std::move(options.onClick);
    registry.emplace<ButtonInputComponent>(handle.row, std::move(input));
    registry.emplace<ButtonVisualComponent>(handle.row, ui_sidebar_nav_visual(options, options.selected));

    const bool directMediaIcon = options.mediaIcon.valid() && !options.mediaIconUsesContainer;
    if (!directMediaIcon)
    {
        handle.icon = scene.create_shape(
            { 0.0f, 0.0f },
            scaled_size(options.iconSize, options.iconSize, ui.scale()),
            make_solid_style(
                options.showIconBackground ? (options.selected ? options.selectedIconColor : options.iconColor) : "rgba(255, 255, 255, 0.002)",
                options.showIconBackground ? options.iconOutlineColor : "rgba(0, 0, 0, 0)",
                options.showIconBackground ? scaled_scalar(options.iconOutlineWidth, ui.scale()) : 0.0f,
                1.0f),
            Renderer2DPrimitive::eRoundedRectangle);
        ui.set_shape(handle.icon, options.iconCornerRadius);
        ui.set_layer(handle.icon, layer + 1, order);
        ui.attach_aligned(
            handle.icon,
            handle.row,
            UiAlignment::eMiddleLeft,
            scaled_offset(options.iconOffset.x, options.iconOffset.y, ui.scale()),
            scaled_size(options.iconSize, options.iconSize, ui.scale()));
    }

    if (options.mediaIcon.valid())
    {
        const glm::vec2 directOffset {
            options.iconOffset.x + (options.iconSize - options.mediaIconSize.x) * 0.5f,
            options.iconOffset.y
        };
        handle.mediaIcon = ui.create_aligned_media(
            options.mediaIcon,
            directMediaIcon ? handle.row : handle.icon,
            directMediaIcon ? UiAlignment::eMiddleLeft : UiAlignment::eCenter,
            options.mediaIconSize,
            layer + 2,
            order,
            directMediaIcon ? directOffset : glm::vec2(0.0f),
            options.mediaIconFit);
        const auto& iconTint = options.selected && !options.selectedMediaIconTint.empty() ?
            options.selectedMediaIconTint : options.mediaIconTint;
        if (!iconTint.empty()) ui_set_media_mask_tint(scene, handle.mediaIcon, iconTint);
    }
    else if (!options.glyph.empty())
    {
        TextStyleComponent glyphStyle = ui_macos26_text_style("#FFFFFFFF");
        glyphStyle.set_font_weight(700.0f);
        handle.glyph = ui.create_aligned_text(
            options.glyph,
            fontAtlas,
            handle.icon,
            UiAlignment::eCenter,
            options.glyphFontSize,
            glyphStyle,
            layer + 2,
            order);
    }

    handle.label = ui.create_aligned_text(
        ui_truncate_text_with_ellipsis(label, options.maxLabelCharacters),
        fontAtlas,
        handle.row,
        options.labelAlignment,
        options.labelFontSize,
        ui_sidebar_nav_label_style(options, options.selected),
        layer + 2,
        order + 1u,
        options.labelOffset);
    ui_update_button_visual(scene, handle.row);
    return handle;
}

inline UiControlHandle ui_create_nav_cluster(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiNavClusterOptions options = {})
{
    (void)fontAtlas;
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    const glm::vec2 safeSize {
        std::max(size.x, 2.0f),
        std::max(size.y, 2.0f)
    };
    const float halfWidth = safeSize.x * 0.5f;

    const float buttonDiameter = std::max(2.0f, std::min(halfWidth, safeSize.y) - 8.0f);
    const float cornerRadius = options.cornerRadius > 0.0f ? options.cornerRadius : safeSize.y * 0.5f;
    const std::string backgroundBottom = options.backgroundBottomColor.empty() ?
        options.backgroundColor :
        options.backgroundBottomColor;
    const ShapeStyleComponent rootStyle =
        options.glass.material == GlassMaterial::eOff &&
        options.backdropBlurRadius > 0.0f ?
        ui_macos26_frosted_control_style(
            ui,
            options.backgroundColor,
            backgroundBottom,
            options.outlineColor,
            options.outlineWidth,
            options.backgroundOpacity,
            options.backdropBlurRadius,
            options.backdropBlurPasses,
            options.backdropBlurOpacity) :
        ui_macos26_control_style(
            ui,
            options.backgroundColor,
            options.outlineColor,
            options.outlineWidth,
            options.backgroundOpacity);

    UiControlHandle handle = {};
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(safeSize.x, safeSize.y, ui.scale()),
        rootStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, cornerRadius);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaled_size(safeSize.x, safeSize.y, ui.scale()));
    ui_apply_glass_material(ui, handle.root, options.glass);
    scene.enable_mask(handle.root, false);
    const auto iconColour = renderer2d_hex_color(options.iconTint);
    const bool lightIcons = iconColour.r * 0.2126f + iconColour.g * 0.7152f +
        iconColour.b * 0.0722f > 0.5f;
    auto make_button_visual = [&ui, lightIcons]() {
        ButtonVisualComponent visual = {};
        visual.idle = ui_macos26_control_style(ui, "#FFFFFF00", "#00000000", 0.0f, 1.0f);
        visual.hovered = ui_macos26_control_style(ui, lightIcons ? "#FFFFFF14" : "#0000000A", "#00000000", 0.0f, 1.0f);
        visual.pressed = ui_macos26_control_style(ui, lightIcons ? "#FFFFFF26" : "#00000018", "#00000000", 0.0f, 1.0f);
        visual.disabled = ui_macos26_control_style(ui, "#FFFFFF00", "#00000000", 0.0f, 1.0f);
        visual.hasDisabled = true;
        return visual;
    };

    handle.label = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(buttonDiameter, buttonDiameter, ui.scale()),
        ui_macos26_control_style(ui, "#FFFFFF00", "#00000000", 0.0f, 1.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.label, buttonDiameter * 0.5f);
    ui.set_layer(handle.label, layer + 1, order);
    ui.attach_aligned(
        handle.label,
        handle.root,
        UiAlignment::eTopLeft,
        scaled_offset((halfWidth - buttonDiameter) * 0.5f,
            (safeSize.y - buttonDiameter) * 0.5f, ui.scale()),
        scaled_size(buttonDiameter, buttonDiameter, ui.scale()));

    handle.caret = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(buttonDiameter, buttonDiameter, ui.scale()),
        ui_macos26_control_style(ui, "#FFFFFF00", "#00000000", 0.0f, 1.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.caret, buttonDiameter * 0.5f);
    ui.set_layer(handle.caret, layer + 1, order + 1u);
    ui.attach_aligned(
        handle.caret,
        handle.root,
        UiAlignment::eTopLeft,
        scaled_offset(halfWidth + (halfWidth - buttonDiameter) * 0.5f,
            (safeSize.y - buttonDiameter) * 0.5f, ui.scale()),
        scaled_size(buttonDiameter, buttonDiameter, ui.scale()));

    ButtonInputComponent backButton = {};
    backButton.enabled = options.backEnabled;
    backButton.onClick = std::move(options.onBack);
    registry.emplace<ButtonInputComponent>(handle.label, std::move(backButton));
    registry.emplace<ButtonVisualComponent>(handle.label, make_button_visual());

    ButtonInputComponent forwardButton = {};
    forwardButton.enabled = options.forwardEnabled;
    forwardButton.onClick = std::move(options.onForward);
    registry.emplace<ButtonInputComponent>(handle.caret, std::move(forwardButton));
    registry.emplace<ButtonVisualComponent>(handle.caret, make_button_visual());

    handle.indicator = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(1.0f, std::max(safeSize.y - 20.0f, 1.0f), ui.scale()),
        make_solid_style(options.dividerColor, "#00000000", 0.0f, 1.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(handle.indicator, layer + 2, order);
    ui.attach_aligned(
        handle.indicator,
        handle.root,
        UiAlignment::eCenter,
        glm::vec2(0.0f),
        scaled_size(1.0f, std::max(safeSize.y - 20.0f, 1.0f), ui.scale()));


    const auto updateDivider = [scenePtr = &scene, back = handle.label,
        forward = handle.caret, divider = handle.indicator](bool) {
        auto& capturedRegistry = scenePtr->registry();
        if (!capturedRegistry.valid(divider)) return;
        const auto isHovered = [&](entt::entity button) {
            const auto* input = capturedRegistry.try_get<ButtonInputComponent>(button);
            return input && input->enabled && input->hovered;
        };
        auto& paint = capturedRegistry.get<ShapeStyleComponent>(divider);
        const float opacity = isHovered(back) || isHovered(forward) ? 0.0f : 1.0f;
        if (paint.opacity != opacity)
        {
            paint.opacity = opacity;
            scenePtr->mark_dirty(divider);
        }
    };
    registry.get<ButtonInputComponent>(handle.label).onHoverChanged = updateDivider;
    registry.get<ButtonInputComponent>(handle.caret).onHoverChanged = updateDivider;

    if (options.backIcon.valid())
    {
        handle.leadingIcon = ui.create_aligned_media(
            options.backIcon,
            handle.label,
            UiAlignment::eCenter,
            { options.iconSize, options.iconSize },
            layer + 3,
            order);
        ui_tint_icon_media(
            scene,
            handle.leadingIcon,
            options.backEnabled ? options.iconTint : options.disabledIconTint);
        registry.emplace<UiDirectionalIconComponent>(handle.leadingIcon);
    }

    if (options.forwardIcon.valid())
    {
        handle.trailingIcon = ui.create_aligned_media(
            options.forwardIcon,
            handle.caret,
            UiAlignment::eCenter,
            { options.iconSize, options.iconSize },
            layer + 3,
            order + 1u);
        ui_tint_icon_media(
            scene,
            handle.trailingIcon,
            options.forwardEnabled ? options.iconTint : options.disabledIconTint);
        registry.emplace<UiDirectionalIconComponent>(handle.trailingIcon);
    }

    if (options.useHistory)
    {
        UiNavigationHistoryComponent history = {};
        history.pages.push_back(options.initialPage);
        history.cursor = 0u;
        history.backButton = handle.label;
        history.forwardButton = handle.caret;
        history.backIcon = handle.leadingIcon;
        history.forwardIcon = handle.trailingIcon;
        history.iconTint = options.iconTint;
        history.disabledIconTint = options.disabledIconTint;
        history.onNavigate = std::move(options.onNavigatePage);
        registry.emplace<UiNavigationHistoryComponent>(handle.root, std::move(history));

        if (ButtonInputComponent* back = registry.try_get<ButtonInputComponent>(handle.label))
        {
            back->onClick = [scenePtr = &scene, root = handle.root](const PointerInputEvent& event) {
                ui_navigation_go_back(*scenePtr, root, event);
            };
        }
        if (ButtonInputComponent* forward = registry.try_get<ButtonInputComponent>(handle.caret))
        {
            forward->onClick = [scenePtr = &scene, root = handle.root](const PointerInputEvent& event) {
                ui_navigation_go_forward(*scenePtr, root, event);
            };
        }
        ui_update_navigation_history_visual(scene, handle.root);
    }

    ui_update_button_visual(scene, handle.label);
    ui_update_button_visual(scene, handle.caret);
    return handle;
}


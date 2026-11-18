// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>
#include <vibranceUI/ui/controls/navigation_history.h>

struct UiButtonOptions
{

    Media2DHandle leadingIcon {};
    Media2DHandle trailingIcon {};
    glm::vec4 padding { 12.0f, 4.0f, 12.0f, 4.0f };


    bool fitContentWidth = false;
    bool truncateLabel = false;
    Renderer2DPrimitive primitive = Renderer2DPrimitive::eRoundedRectangle;
    float squircleAmount = 0.0f;
    float squirclePower = 4.0f;
    float cornerRadius = 10.0f;
    float fontSize = 13.0f;
    float fontWeight = 400.0f;
    float iconSize = 14.0f;
    float leadingIconSize = 0.0f;
    float trailingIconSize = 0.0f;
    float iconTextGap = 6.0f;
    bool primary = false;
    bool enabled = true;
    bool tintLeadingIcon = true;
    bool tintTrailingIcon = true;
    std::string textColor {};
    std::string iconTint {};
    std::string backgroundColor {};
    std::string hoveredBackgroundColor {};
    std::string pressedBackgroundColor {};
    std::string disabledBackgroundColor {};
    std::string outlineColor {};
    std::string hoveredOutlineColor {};
    std::string pressedOutlineColor {};
    std::string disabledOutlineColor {};
    float outlineWidth = 0.8f;
    float hoveredOutlineWidth = 0.9f;
    float pressedOutlineWidth = 1.0f;
    float disabledOutlineWidth = 0.6f;
    UiGlassOptions glass {};



    std::optional<StretchDynamicsOptions> stretchDynamics {};
    std::function<void(const PointerInputEvent&)> onClick;
};

inline UiButtonOptions ui_button_options_from_theme(
    const UiTheme& theme,
    bool primary = false)
{
    UiButtonOptions options;
    options.primary = primary;

    options.cornerRadius = 1000.0f;
    options.textColor = primary ? "#FFFFFFFF" : theme.text;
    options.iconTint = primary ? "#FFFFFFFF" : theme.icon;
    options.backgroundColor = primary ? theme.accent : theme.surfaceMuted;
    options.hoveredBackgroundColor = primary ? theme.accentHover : theme.surfaceElevated;
    options.pressedBackgroundColor = primary ? theme.accentPressed : theme.surface;
    options.disabledBackgroundColor = theme.surfaceMuted;
    options.outlineColor = primary ? "#FFFFFF38" : theme.outline;
    options.hoveredOutlineColor = primary ? "#FFFFFF55" : theme.outlineStrong;
    options.pressedOutlineColor = primary ? "#00000018" : theme.outlineStrong;
    options.disabledOutlineColor = theme.outline;
    return options;
}

struct UiSubmenuRowOptions
{

    UiButtonOptions button {};
    float horizontalInset = 8.0f;
    float height = 42.0f;
};

enum class UiNavigationDirection : uint8_t
{
    eBack,
    eForward
};

struct UiIconButtonOptions
{

    Media2DHandle icon {};
    bool enabled = true;
    float cornerRadius = 0.0f;
    float iconSize = 16.0f;
    std::string backgroundColor = "rgba(255, 255, 255, 0.90)";
    std::string backgroundBottomColor {};
    std::string hoveredBackgroundColor = "rgba(255, 255, 255, 1)";
    std::string pressedBackgroundColor = "rgba(233, 233, 235, 0.96)";
    std::string disabledBackgroundColor = "rgba(242, 242, 242, 0.50)";
    std::string outlineColor = "#00000018";
    float outlineWidth = 0.8f;
    float backdropBlurRadius = 0.0f;
    uint32_t backdropBlurPasses = 1u;
    float backdropBlurOpacity = 0.85f;
    UiGlassOptions glass {};
    std::optional<StretchDynamicsOptions> stretchDynamics {};
    std::string iconTint = "#55585EFF";
    std::string disabledIconTint = "#B1B4BAFF";
    std::function<void(const PointerInputEvent&)> onClick;

    bool useHistory = false;
    UiNavigationDirection historyDirection = UiNavigationDirection::eBack;
    std::size_t initialPage = 0u;
    std::function<void(std::size_t, const PointerInputEvent&)> onNavigatePage;
};

inline ButtonVisualComponent ui_button_visual_from_options(
    UiBuilder& ui,
    const UiButtonOptions& options)
{
    ButtonVisualComponent visual = ui_macos26_button_visual(ui, options.primary);
    const auto style = [&ui](const std::string& fill, const std::string& outline, float outlineWidth, const ShapeStyleComponent& fallback) {
        if (fill.empty() && outline.empty())
        {
            return fallback;
        }
        return make_solid_style(
            fill.empty() ? fallback.color0 : renderer2d_hex_color(fill),
            outline.empty() ? fallback.outlineColor : renderer2d_hex_color(outline, glm::vec4(0.0f)),
            scaled_scalar(outlineWidth, ui.scale()),
            fallback.opacity);
    };

    visual.idle = style(options.backgroundColor, options.outlineColor, options.outlineWidth, visual.idle);
    visual.hovered = style(options.hoveredBackgroundColor, options.hoveredOutlineColor, options.hoveredOutlineWidth, visual.hovered);
    visual.pressed = style(options.pressedBackgroundColor, options.pressedOutlineColor, options.pressedOutlineWidth, visual.pressed);
    visual.disabled = style(options.disabledBackgroundColor, options.disabledOutlineColor, options.disabledOutlineWidth, visual.disabled);
    visual.hasDisabled = true;
    return visual;
}

inline ShapeStyleComponent ui_icon_button_style(
    UiBuilder& ui,
    const UiIconButtonOptions& options,
    std::string_view fill)
{
    const std::string bottom = options.backgroundBottomColor.empty() ?
        std::string(fill) :
        options.backgroundBottomColor;
    if (options.glass.material == GlassMaterial::eOff &&
        options.backdropBlurRadius > 0.0f)
    {
        return ui_macos26_frosted_control_style(
            ui,
            fill,
            bottom,
            options.outlineColor,
            options.outlineWidth,
            1.0f,
            options.backdropBlurRadius,
            options.backdropBlurPasses,
            options.backdropBlurOpacity);
    }
    return ui_macos26_control_style(ui, fill, options.outlineColor, options.outlineWidth, 1.0f);
}

inline UiControlHandle ui_create_text_button(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    std::string label,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiButtonOptions options = {})
{

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    ButtonVisualComponent visual = ui_button_visual_from_options(ui, options);
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        options.enabled ? visual.idle : visual.disabled,
        options.primitive);
    ui.set_shape(handle.root, options.cornerRadius);
    ui.set_shape_squircle(handle.root, options.squircleAmount, options.squirclePower);
    ui.set_layer(handle.root, layer, order);
    ui.set_padding(handle.root, options.padding.x, options.padding.y, options.padding.z, options.padding.w);
    ui_apply_glass_material(ui, handle.root, options.glass);

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onClick = std::move(options.onClick);
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, visual);



    registry.emplace<HitRegion2DComponent>(handle.root);
    if (options.stretchDynamics)
    {
        ui_enable_stretch_dynamics(
            scene,
            handle.root,
            std::move(*options.stretchDynamics));
    }

    const std::string textColor = !options.textColor.empty() ? options.textColor : (options.primary ? "#FFFFFFFF" : "#1D1D1FFF");
    const std::string iconTint = !options.iconTint.empty() ? options.iconTint : textColor;
    const float iconGap = std::max(options.iconTextGap, 0.0f);
    const float leadingIconSize = options.leadingIconSize > 0.0f ?
        options.leadingIconSize : options.iconSize;
    const float trailingIconSize = options.trailingIconSize > 0.0f ?
        options.trailingIconSize : options.iconSize;
    glm::vec2 labelOffset { 0.0f };
    UiAlignment labelAlignment = UiAlignment::eCenter;
    if (options.leadingIcon.valid())
    {
        handle.leadingIcon = ui.create_aligned_media(
            options.leadingIcon,
            handle.root,
            UiAlignment::eMiddleLeft,
            { leadingIconSize, leadingIconSize },
            layer + 1,
            order,
            { options.padding.x, 0.0f });
        if (options.tintLeadingIcon)
        {
            ui_tint_icon_media(scene, handle.leadingIcon, iconTint);
        }
        labelAlignment = UiAlignment::eMiddleLeft;
        labelOffset.x = options.padding.x + leadingIconSize + iconGap;
    }
    if (options.trailingIcon.valid())
    {
        handle.trailingIcon = ui.create_aligned_media(
            options.trailingIcon,
            handle.root,
            UiAlignment::eMiddleRight,
            { trailingIconSize, trailingIconSize },
            layer + 1,
            order + 1u,
            { -options.padding.z, 0.0f });
        if (options.tintTrailingIcon)
        {
            ui_tint_icon_media(scene, handle.trailingIcon, iconTint);
        }
        if (labelAlignment == UiAlignment::eCenter)
        {
            labelOffset.x = -((trailingIconSize + iconGap) * 0.5f);
        }
    }

    if (options.truncateLabel && !options.fitContentWidth)
    {
        float available = size.x - options.padding.x - options.padding.z;
        if (options.leadingIcon.valid()) available -= leadingIconSize + iconGap;
        if (options.trailingIcon.valid()) available -= trailingIconSize + iconGap;
        label = ui_truncate_text_to_width_with_ellipsis(fontAtlas, label,
            scaled_scalar(options.fontSize, ui.scale()), scaled_scalar(std::max(available, 0.0f), ui.scale()));
    }
    auto labelStyle = ui_macos26_text_style(textColor);
    labelStyle.set_font_weight(options.fontWeight);
    handle.label = ui.create_aligned_text(
        std::move(label),
        fontAtlas,
        handle.root,
        labelAlignment,
        options.fontSize,
        labelStyle,
        layer + 1,
        order + 2u,
        labelOffset);
    if (options.fitContentWidth)
    {
        auto padding = options.padding;
        if (options.leadingIcon.valid()) padding.x += leadingIconSize + iconGap;
        if (options.trailingIcon.valid()) padding.z += trailingIconSize + iconGap;
        ui_fit_content_size(ui, handle.root, handle.label, size, padding);
    }
    ui_update_button_visual(scene, handle.root);
    return handle;
}

inline UiControlHandle ui_create_icon_button(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiIconButtonOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    const float cornerRadius = options.cornerRadius > 0.0f ?
        options.cornerRadius :
        std::min(size.x, size.y) * 0.5f;

    ButtonVisualComponent visual = {};
    visual.idle = ui_icon_button_style(ui, options, options.backgroundColor);
    visual.hovered = ui_icon_button_style(ui, options, options.hoveredBackgroundColor);
    visual.pressed = ui_icon_button_style(ui, options, options.pressedBackgroundColor);
    visual.disabled = ui_icon_button_style(ui, options, options.disabledBackgroundColor);
    visual.hasDisabled = true;

    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        options.enabled ? visual.idle : visual.disabled,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, cornerRadius);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaled_size(size.x, size.y, ui.scale()));
    ui_apply_glass_material(ui, handle.root, options.glass);

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onClick = std::move(options.onClick);
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, visual);
    if (options.stretchDynamics)
    {
        ui_enable_stretch_dynamics(
            scene,
            handle.root,
            std::move(*options.stretchDynamics));
    }

    if (options.icon.valid())
    {
        handle.leadingIcon = ui.create_aligned_media(
            options.icon,
            handle.root,
            UiAlignment::eCenter,
            { options.iconSize, options.iconSize },
            layer + 1,
            order);
        ui_tint_icon_media(
            scene,
            handle.leadingIcon,
            options.enabled ? options.iconTint : options.disabledIconTint);
    }

    if (options.useHistory)
    {
        if (registry.valid(handle.leadingIcon))
            registry.emplace<UiDirectionalIconComponent>(handle.leadingIcon);
        UiNavigationHistoryComponent history = {};
        history.pages.push_back(options.initialPage);
        history.cursor = 0u;
        history.iconTint = options.iconTint;
        history.disabledIconTint = options.disabledIconTint;
        history.onNavigate = std::move(options.onNavigatePage);
        if (options.historyDirection == UiNavigationDirection::eBack)
        {
            history.backButton = handle.root;
            history.backIcon = handle.leadingIcon;
        }
        else
        {
            history.forwardButton = handle.root;
            history.forwardIcon = handle.leadingIcon;
        }
        registry.emplace<UiNavigationHistoryComponent>(handle.root, std::move(history));

        if (ButtonInputComponent* input = registry.try_get<ButtonInputComponent>(handle.root))
        {
            const UiNavigationDirection direction = options.historyDirection;
            input->onClick = [scenePtr = &scene, root = handle.root, direction](const PointerInputEvent& event) {
                if (direction == UiNavigationDirection::eBack)
                {
                    ui_navigation_go_back(*scenePtr, root, event);
                }
                else
                {
                    ui_navigation_go_forward(*scenePtr, root, event);
                }
            };
        }
        ui_update_navigation_history_visual(scene, handle.root);
    }
    ui_update_button_visual(scene, handle.root);
    return handle;
}

inline UiControlHandle ui_create_aligned_text_button(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    std::string label,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiButtonOptions options = {})
{
    UiControlHandle handle = ui_create_text_button(
        ui,
        fontAtlas,
        std::move(label),
        size,
        layer,
        order,
        std::move(options));
    ui.attach_aligned(handle.root, parent, alignment, scaled_offset(offset.x, offset.y, ui.scale()), scaled_size(size.x, size.y, ui.scale()));
    return handle;
}

inline UiControlHandle ui_create_submenu_row(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    float y,
    std::string label,
    int32_t layer,
    uint32_t order,
    UiSubmenuRowOptions options = {})
{
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return {};
    }

    const float height = std::max(options.height, 1.0f);
    const float horizontalInset = std::max(options.horizontalInset, 0.0f);
    UiControlHandle handle = ui_create_text_button(
        ui,
        fontAtlas,
        std::move(label),
        { 1.0f, height },
        layer,
        order,
        std::move(options.button));
    if (ui.registry().valid(handle.trailingIcon))
        ui.registry().emplace<UiDirectionalIconComponent>(handle.trailingIcon);
    ui.attach_stretch(
        handle.root,
        parent,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        scaled_edges(
            horizontalInset,
            0.0f,
            horizontalInset,
            0.0f,
            ui.scale()),
        scaled_offset(0.0f, y, ui.scale()),
        scaled_size(0.0f, height, ui.scale()));
    return handle;
}


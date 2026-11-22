// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>

struct UiThemePreviewHandle
{
    entt::entity root = entt::null;
    entt::entity label = entt::null;
    entt::entity image = entt::null;
};

enum class UiChoicePreviewKind { eTheme, eDisplay };

struct UiChoicePreviewAppearance
{
    static constexpr float scale = 0.8f * 0.8f;
    static constexpr glm::vec2 imageSize = glm::vec2(136.0f, 88.0f) * scale;
    static constexpr float columnWidth = 168.0f * scale;
    static constexpr float rowHeight = 144.0f * scale;
};

struct UiThemePreviewOptions
{

    UiTheme theme {};
    std::string label;
    bool selected = false;
    bool dark = false;
    glm::vec2 size = UiChoicePreviewAppearance::imageSize;
    glm::vec2 labelOffset { 0.0f, 100.0f * UiChoicePreviewAppearance::scale };
    float cornerRadius = UiChoicePreviewAppearance::imageSize.y * 0.14f;
    float labelFontSize = 14.0f;
    float selectedOutlineWidth = 4.0f * UiChoicePreviewAppearance::scale;
    float outlineWidth = 0.7f * UiChoicePreviewAppearance::scale;
    int32_t layer = 3;
    uint32_t order = 0u;
    Media2DHandle artwork {};
    UiChoicePreviewKind kind = UiChoicePreviewKind::eTheme;
    bool automatic = false;
    UiAlignment labelAlignment = UiAlignment::eTopCenter;
    float selectionInset = 6.0f * UiChoicePreviewAppearance::scale;
};

inline UiThemePreviewHandle ui_create_theme_preview(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    glm::vec2 offset,
    const UiThemePreviewOptions& options)
{
    UiThemePreviewHandle handle = {};
    handle.root = ui_create_fixed_block(
        ui, parent, offset, { options.size.x, std::max(options.size.y * 1.5f,
            options.labelOffset.y + options.labelFontSize) }, ui_clear_surface_style(),
        0.0f, options.layer, options.order);
    ui.registry().emplace<ButtonVisualComponent>(handle.root, ui_transparent_button_visual());
    if (options.selected)
    {
        const auto outline = ui_create_fixed_block(ui, handle.root, glm::vec2(-options.selectionInset),
            options.size + glm::vec2(2.0f * options.selectionInset),
            make_solid_style("#00000000", options.theme.accent,
                scaled_scalar(options.selectedOutlineWidth, ui.scale())),
            options.cornerRadius + options.selectionInset, options.layer, options.order);
        ui.set_shape_squircle(outline, 1.0f, 4.0f);
    }
    handle.image = ui_create_fixed_block(ui, handle.root, {}, options.size,
        make_solid_style(options.dark ? "#0B1125FF" : "#F7FBFFFF", "#00000020",
            scaled_scalar(options.outlineWidth, ui.scale())),
        options.cornerRadius, options.layer + 1, options.order);
    ui.set_shape_squircle(handle.image, 1.0f, 4.0f);
    ui.scene().enable_mask(handle.image, false);
    const glm::vec2 artworkScale = options.size / glm::vec2(136.0f, 88.0f);
    const auto point = [artworkScale](float x, float y) { return glm::vec2(x, y) * artworkScale; };
    const auto radius = [artworkScale](float value) { return value * artworkScale.y; };
    if (options.artwork.valid())
        ui.create_aligned_media(options.artwork, handle.image, UiAlignment::eCentre,
            options.size, options.layer + 2, options.order, {}, Media2DFit::eCover);
    else if (options.kind == UiChoicePreviewKind::eTheme)
    {
        const auto paint = [&](entt::entity pane, bool dark) {
            ui_create_fixed_block(ui, pane, {}, options.size,
                make_gradient_style(dark ? "#563BDAFF" : "#B3DCEEFF", dark ? "#061B61FF" : "#1357C1FF"),
                0.0f, options.layer + 2, options.order);
            ui_create_fixed_block(ui, pane, point(0, 34), point(136, 20),
                make_gradient_style(dark ? "#5169CFFF" : "#A4DDF4FF", dark ? "#071440FF" : "#1260B6FF"),
                0.0f, options.layer + 3, options.order);
            const auto back = ui_create_fixed_block(ui, pane, point(4, 14), point(76, 29),
                make_solid_style(dark ? "#182862FF" : "#DCE9EDFF", "#00000050", radius(0.7f)),
                radius(5.0f), options.layer + 4, options.order);
            ui_create_fixed_block(ui, back, point(4, 5), point(68, 11),
                make_solid_style("#347DF3FF"), radius(2.0f), options.layer + 5, options.order);
            const auto front = ui_create_fixed_block(ui, pane, point(options.automatic ? 10.0f : 34.0f, 48),
                point(110, 50), make_solid_style(dark ? "#080A0EFF" : "#F7F8F9FF", "#00000040", radius(0.8f)),
                radius(12.0f), options.layer + 6, options.order);
            ui_create_dot(ui, front, point(18, 20), radius(5.5f), "#F4766EFF", options.layer + 7, options.order);
            ui_create_dot(ui, front, point(38, 20), radius(5.5f), "#F5CC44FF", options.layer + 7, options.order + 1u);
            ui_create_dot(ui, front, point(58, 20), radius(5.5f), "#58C54BFF", options.layer + 7, options.order + 2u);
        };
        if (options.automatic)
            for (unsigned half = 0; half < 2; ++half)
            {
                const auto pane = ui_create_fixed_block(ui, handle.image, { options.size.x * 0.5f * half, 0 },
                    { options.size.x * 0.5f, options.size.y }, ui_clear_surface_style(), 0.0f, options.layer + 2, options.order);
                ui.registry().emplace<Mask2DComponent>(pane);
                paint(pane, half != 0);
            }
        else paint(handle.image, options.dark);
    }
    else
    {
        ui_create_fixed_block(ui, handle.image, point(3, 4), point(130, 45),
            make_gradient_style(options.dark ? "#183267FF" : "#94D8F7FF", options.dark ? "#061122FF" : "#D9F3FFFF"),
            radius(1.5f), options.layer + 2, options.order);
        ui_create_fixed_block(ui, handle.image, point(7, 7), point(40, 12),
            make_solid_style(options.dark ? "#007AFFFF" : "#7ED5F8FF"), radius(1.0f), options.layer + 3, options.order);
        for (unsigned dot = 0; dot < 3; ++dot)
            ui_create_dot(ui, handle.image, point(21.0f + 15.0f * dot, 69), radius(4.0f),
                dot == 0 ? "#FF635DFF" : dot == 1 ? "#FFCC00FF" : "#34C759FF", options.layer + 3, options.order + dot);
    }
    handle.label = ui.create_aligned_text(
        ui_truncate_text_to_width_with_ellipsis(fontAtlas, options.label,
            scaled_scalar(options.labelFontSize, ui.scale()), scaled_scalar(options.size.x, ui.scale())),
        fontAtlas, handle.root, options.labelAlignment, options.labelFontSize, options.theme.text_style(
            options.selected ? options.theme.settingsPanel.selectedPreviewLabel : options.theme.settingsPanel.previewLabel,
            options.selected ? 650.0f : 440.0f), options.layer + 2, options.order + 10u, options.labelOffset);
    return handle;
}


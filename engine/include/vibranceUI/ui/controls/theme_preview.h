// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>

struct UiThemePreviewHandle
{
    entt::entity root = entt::null;
    entt::entity label = entt::null;
};

struct UiThemePreviewOptions
{

    UiTheme theme {};
    std::string label;
    bool selected = false;
    bool dark = false;
    glm::vec2 size { 88.0f, 58.0f };
    glm::vec2 labelOffset { 22.0f, 63.0f };
    float cornerRadius = 9.0f;
    float labelFontSize = 13.0f;
    float selectedOutlineWidth = 2.6f;
    float outlineWidth = 0.8f;
    int32_t layer = 3;
    uint32_t order = 0u;
};

inline UiThemePreviewHandle ui_create_theme_preview(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    glm::vec2 offset,
    const UiThemePreviewOptions& options)
{
    UiThemePreviewHandle handle = {};
    ShapeStyleComponent frameStyle = make_solid_style(
        options.dark ? "#0E1324FF" : "#F8FBFFFF",
        options.selected ? options.theme.accent : "rgba(0, 0, 0, 0.12)",
        scaled_scalar(options.selected ? options.selectedOutlineWidth : options.outlineWidth, ui.scale()),
        1.0f);
    handle.root = ui_create_fixed_block(
        ui,
        parent,
        offset,
        options.size,
        frameStyle,
        options.cornerRadius,
        options.layer,
        options.order);

    ShapeStyleComponent stripStyle = make_gradient_style(
        options.dark ? "#1B3A8AFF" : "#7CC7FFFF",
        options.dark ? "#071023FF" : "#E7F7FFFF",
        "rgba(0, 0, 0, 0)",
        0.0f,
        1.0f);
    ui_create_fixed_block(
        ui,
        handle.root,
        { 5.0f, 7.0f },
        { std::max(1.0f, options.size.x - 10.0f), 24.0f },
        stripStyle,
        5.0f,
        options.layer + 1,
        options.order);
    ui_create_fixed_block(
        ui,
        handle.root,
        { 8.0f, 11.0f },
        { 44.0f, 11.0f },
        make_solid_style(
            options.dark ? "#006BFFFF" : "#85D4FFFF",
            "rgba(0, 0, 0, 0.12)",
            0.4f,
            1.0f),
        3.0f,
        options.layer + 2,
        options.order);
    ui_create_dot(ui, handle.root, { 28.0f, 42.0f }, 4.0f, "#FF635DFF", options.layer + 2, options.order);
    ui_create_dot(ui, handle.root, { 44.0f, 42.0f }, 4.0f, "#FFCC00FF", options.layer + 2, options.order + 1u);
    ui_create_dot(ui, handle.root, { 60.0f, 42.0f }, 4.0f, "#34C759FF", options.layer + 2, options.order + 2u);

    handle.label = ui.create_aligned_text(
        options.label,
        fontAtlas,
        parent,
        UiAlignment::eTopLeft,
        options.labelFontSize,
        options.theme.text_style(
            options.selected ? options.theme.settingsPanel.selectedPreviewLabel : options.theme.settingsPanel.previewLabel,
            options.selected ? 620.0f : 440.0f),
        options.layer + 1,
        options.order + 10u,
        offset + options.labelOffset);
    return handle;
}


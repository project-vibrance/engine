// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/glfw/panel_window.h>
#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/liquid_controls.h>
#include <vibranceUI/ui/resources.h>

inline UiDropdownOptions ui_panel_dropdown_options(const UiTheme& theme,
    Media2DHandle chevron = {}, Media2DHandle check = {})
{
    auto options = ui_dropdown_options_from_theme(theme, chevron, check);
    options.rowHeight = 28.0f;
    options.buttonHoveredBackgroundColor = options.buttonBackgroundColor;
    options.buttonHoveredOutlineColor = options.buttonOutlineColor;
    options.buttonHoveredOutlineWidth = options.buttonOutlineWidth;
    options.rowHoveredColor = options.backgroundColor;
    options.rowHoveredTextColor = options.rowTextColor;
    options.rowHoveredCheckTint = options.rowCheckTint;
    return options;
}

inline UiButtonOptions ui_panel_button_options(const UiTheme& theme, bool primary = false)
{
    const auto dropdown = ui_panel_dropdown_options(theme);
    UiButtonOptions options;
    options.primary = primary;
    options.primitive = dropdown.buttonPrimitive;
    options.cornerRadius = dropdown.buttonCornerRadius;
    options.squircleAmount = dropdown.buttonSquircleAmount;
    options.squirclePower = dropdown.buttonSquirclePower;
    options.fontSize = dropdown.buttonFontSize;
    options.fontWeight = dropdown.buttonFontWeight;
    options.truncateLabel = true;
    options.textColor = primary ? "#FFFFFFFF" : dropdown.buttonTextColor;
    options.iconTint = primary ? "#FFFFFFFF" : dropdown.buttonIconTint;
    options.backgroundColor = primary ? theme.accent : dropdown.buttonBackgroundColor;
    options.hoveredBackgroundColor = options.backgroundColor;
    options.pressedBackgroundColor = primary ? theme.accentPressed : dropdown.buttonPressedBackgroundColor;
    options.outlineColor = dropdown.buttonOutlineColor;
    options.hoveredOutlineColor = options.outlineColor;
    options.pressedOutlineColor = dropdown.buttonPressedOutlineColor;
    options.outlineWidth = dropdown.buttonOutlineWidth;
    options.hoveredOutlineWidth = options.outlineWidth;
    options.pressedOutlineWidth = dropdown.buttonPressedOutlineWidth;
    return options;
}

inline UiControlHandle ui_create_panel_text_button(UiBuilder& ui, const Renderer2DFontAtlas& font,
    entt::entity parent, UiAlignment alignment, glm::vec2 offset, std::string text,
    glm::vec2 size, int32_t layer, uint32_t order, UiButtonOptions options = {})
{
    const UiDropdownOptions dropdown;
    options.primitive = dropdown.buttonPrimitive;
    options.cornerRadius = dropdown.buttonCornerRadius;
    options.squircleAmount = dropdown.buttonSquircleAmount;
    options.squirclePower = dropdown.buttonSquirclePower;
    options.fontWeight = dropdown.buttonFontWeight;
    options.truncateLabel = true;
    return ui_create_aligned_text_button(ui, font, parent, alignment, offset,
        std::move(text), size, layer, order, std::move(options));
}

inline void ui_panel_transitions(GlfwPanelWindowTemplateOptions& options)
{
#if defined(__APPLE__)
    options.openingTransition.reset();
    options.closingTransition.reset();
#else
    DisplayTransition2DComponent transition;
    transition.durationSeconds = .28f;
    transition.fromOpacity = 0.0f;
    transition.toOpacity = 1.0f;
    transition.fromScale = glm::vec2(.86f);
    transition.toScale = glm::vec2(1.0f);
    transition.set_interactive_spring(.18f, 1.0f);
    options.openingTransition = transition;
    transition.fromOpacity = 1.0f;
    transition.toOpacity = 0.0f;
    transition.fromScale = glm::vec2(1.0f);
    transition.toScale = glm::vec2(.88f);
    options.closingTransition = transition;
#endif
}

inline UiSliderOptions ui_panel_slider_options(const UiTheme& theme, float value)
{
    auto options = ui_slider_options_from_theme(theme, value);
    options.thumbSize = { 24.0f, 18.0f };
    return options;
}


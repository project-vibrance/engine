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
    return ui_dropdown_options_from_theme(theme, chevron, check);
}

inline UiButtonOptions ui_panel_button_options(const UiTheme& theme, bool primary = false)
{
    return ui_button_options_from_theme(theme, primary);
}

inline UiControlHandle ui_create_panel_text_button(UiBuilder& ui, const Renderer2DFontAtlas& font,
    entt::entity parent, UiAlignment alignment, glm::vec2 offset, std::string text,
    glm::vec2 size, int32_t layer, uint32_t order, UiButtonOptions options = {})
{
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
    return ui_slider_options_from_theme(theme, value);
}


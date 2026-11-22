// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>
#include <vibranceUI/ui/controls/switches.h>

enum class UiSliderFillStyle : uint8_t
{
    eRoundedFill,
    eFlatFillClippedByTrack
};

struct UiSliderOptions
{

    float value = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float step = 0.0f;
    float snapPercentage = 0.0f;
    bool enabled = true;
    bool dimWhenDisabled = true;
    UiSliderFillStyle fillStyle = UiSliderFillStyle::eFlatFillClippedByTrack;
    float thumbDiameter = 0.0f;
    glm::vec2 thumbSize { 0.0f, 0.0f };
    bool insetThumb = false;

    float hoveredTrackHeight = 0.0f;
    float hoveredThumbScale = 1.0f;
    std::string trackColor = "rgba(231, 231, 234, 0.94)";
    std::string fillColor = "#007AFFFF";
    std::string thumbColor = "#FFFFFFFF";
    std::string outlineColor = "rgba(0, 0, 0, 0.07)";
    std::string thumbOutlineColor = "#00000000";
    float outlineWidth = 0.8f;
    float thumbOutlineWidth = 0.0f;

    bool smoothScrubbing = false;
    float visualSmoothingRate = 24.0f;
    std::function<void(const SliderInputEvent&)> onChanged;
    std::function<void(const SliderInputEvent&)> onCommitted;
    bool renderThumbShadow = true;
};

inline UiSliderOptions ui_slider_options_from_theme(const UiTheme& theme, float value = 0.0f)
{
    const UiSwitchOptions switchOptions = ui_switch_options_from_theme(theme, false);
    UiSliderOptions options = {};
    options.value = value;
    options.fillStyle = UiSliderFillStyle::eFlatFillClippedByTrack;
    options.trackColor = switchOptions.offColor;
    options.fillColor = switchOptions.onColor;
    options.thumbColor = switchOptions.knobColor;
    options.outlineColor = switchOptions.offOutlineColor;
    options.thumbOutlineColor = "#00000000";
    options.outlineWidth = switchOptions.trackOutlineWidth;
    options.thumbOutlineWidth = 0.0f;
    options.renderThumbShadow = true;
    return options;
}

inline UiControlHandle ui_create_slider(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiSliderOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    const float trackHeight = std::max(size.y, 4.0f);
    const glm::vec2 requestedThumbSize = options.thumbSize.x > 0.0f && options.thumbSize.y > 0.0f ?
        options.thumbSize :
        options.thumbDiameter > 0.0f ? glm::vec2(options.thumbDiameter) : UiControlAppearance::sliderThumbSize;
    const glm::vec2 thumbSize {
        std::min(std::max(requestedThumbSize.x, options.insetThumb ? 1.0f : trackHeight), std::max(size.x, 0.0f)),
        std::max(requestedThumbSize.y, options.insetThumb ? 1.0f : trackHeight)
    };
    const float hoveredTrackHeight = std::max(options.hoveredTrackHeight, trackHeight);
    const glm::vec2 hoveredThumbSize = glm::max(
        thumbSize * std::max(options.hoveredThumbScale, 1.0f),
        glm::vec2(hoveredTrackHeight));
    const bool visibleThumb = renderer2d_hex_color(options.thumbColor).a > 0.001f;
    const float thumbExpansionReserve = visibleThumb ? 1.52f : 1.32f;
    const float reservedThumbWidth = options.insetThumb ? trackHeight :
        std::max(thumbSize.x * thumbExpansionReserve, hoveredThumbSize.x);
    const float initialNormalized = options.maxValue > options.minValue ?
        std::clamp((options.value - options.minValue) / (options.maxValue - options.minValue), 0.0f, 1.0f) :
        0.0f;
    const bool flatFill = options.fillStyle == UiSliderFillStyle::eFlatFillClippedByTrack;
    const float initialFillWidth = visibleThumb ?
        ui_slider_thumb_position(size.x, reservedThumbWidth, initialNormalized) :
        std::max(size.x * initialNormalized, 0.0f);

    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, trackHeight, ui.scale()),
        ui_macos26_control_style(ui, options.trackColor, options.outlineColor, options.outlineWidth, 1.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, trackHeight * 0.5f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaled_size(size.x, trackHeight, ui.scale()));

    entt::entity fillMask = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, trackHeight, ui.scale()),
        ui_clear_surface_style(0.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(fillMask, trackHeight * 0.5f);
    ui.set_layer(fillMask, layer + 1, order);
    ui.attach_aligned(
        fillMask,
        handle.root,
        UiAlignment::eCentre,
        glm::vec2(0.0f),
        scaled_size(size.x, trackHeight, ui.scale()));
    scene.enable_mask(fillMask, false);

    handle.indicator = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(initialFillWidth, trackHeight, ui.scale()),
        ui_macos26_control_style(ui, options.fillColor, "#00000000", 0.0f, 1.0f),
        flatFill ? Renderer2DPrimitive::eRectangle : Renderer2DPrimitive::eRoundedRectangle);
    if (!flatFill)
    {
        ui.set_shape(handle.indicator, trackHeight * 0.5f);
    }
    ui.set_layer(handle.indicator, layer + 1, order);
    ui.attach_layout(
        handle.indicator,
        fillMask,
        { 0.0f, 0.5f },
        { 0.0f, 0.5f },
        glm::vec2(0.0f),
        scaled_size(initialFillWidth, trackHeight, ui.scale()));

    ShapeStyleComponent thumbStyle = ui_macos26_control_style(
        ui,
        options.thumbColor,
        "#00000000",
        0.0f,
        1.0f);
    thumbStyle.edgeSoftness = 0.5f;
    handle.knob = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(thumbSize.x, thumbSize.y, ui.scale()),
        thumbStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.knob, thumbSize.y * 0.5f);
    ui.set_layer(handle.knob, layer + 2, order);
    ui.attach_layout(
        handle.knob,
        handle.root,
        { 0.0f, 0.5f },
        { 0.5f, 0.5f },
        scaled_offset(ui_slider_thumb_position(size.x,
            reservedThumbWidth, initialNormalized), 0.0f, ui.scale()),
        scaled_size(thumbSize.x, thumbSize.y, ui.scale()));
        
    if (options.renderThumbShadow)
    {
        ShadowComponent& thumbShadow = ui.add_shadow(
            handle.knob,
            { 0, 0 },
            8.0f,
            0.12f);
        thumbShadow.spread = 0.0f;
        thumbShadow.outsideOnly = true;
    }

    SliderInputComponent slider = {};
    slider.maskEntity = fillMask;
    slider.fillEntity = handle.indicator;
    slider.thumbEntity = handle.knob;
    slider.enabled = options.enabled;
    slider.dimWhenDisabled = options.dimWhenDisabled;
    slider.minValue = std::min(options.minValue, options.maxValue);
    slider.maxValue = std::max(options.minValue, options.maxValue);
    slider.value = std::clamp(options.value, slider.minValue, slider.maxValue);
    slider.visualValue = slider.value;
    slider.step = std::max(options.step, 0.0f);
    const float rawSnapPercentage = options.snapPercentage > 1.0f ?
        options.snapPercentage / 100.0f :
        options.snapPercentage;
    slider.normalisedStep = std::clamp(rawSnapPercentage, 0.0f, 1.0f);
    slider.trackHeight = scaled_scalar(trackHeight, ui.scale());
    slider.hoveredTrackHeight = scaled_scalar(hoveredTrackHeight, ui.scale());
    slider.thumbSize = scaled_size(thumbSize.x, thumbSize.y, ui.scale());
    slider.thumbExpansionReserve = thumbExpansionReserve;
    slider.insetThumb = options.insetThumb;
    slider.hoveredThumbSize = scaled_size(
        hoveredThumbSize.x,
        hoveredThumbSize.y,
        ui.scale());
    slider.smoothScrubbing = options.smoothScrubbing;
    slider.visualSmoothingRate = std::max(options.visualSmoothingRate, 0.0f);
    slider.onChanged = std::move(options.onChanged);
    slider.onCommitted = std::move(options.onCommitted);
    registry.emplace<SliderInputComponent>(handle.root, std::move(slider));
    registry.emplace<DisabledVisualComponent>(handle.root, DisabledVisualComponent { options.dimWhenDisabled, false });
    return handle;
}


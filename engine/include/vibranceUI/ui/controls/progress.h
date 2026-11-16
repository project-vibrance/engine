// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>

struct UiCircularProgressComponent
{

    entt::entity indicator = entt::null;
    float value = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    glm::vec4 progressColor { 1.0f };
    glm::vec4 progressEndColor { 1.0f };
    bool progressUsesGradient = false;
    bool contentUsesProgressColor = false;
};

struct UiCircularProgressOptions
{

    float value = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float thickness = 6.0f;
    float startAngleRadians = -1.57079632679f;
    bool clockwise = true;
    std::string trackColor = "#FFFFFF18";
    std::string trackEndColor {};
    std::string progressColor = "#6EE77CFF";
    std::string progressEndColor {};



    bool contentUsesProgressColor = false;
    float opacity = 1.0f;
    float edgeSoftness = 0.8f;
};

inline float ui_circular_progress_normalized_value(
    float value,
    float minValue,
    float maxValue)
{
    const float low = std::min(minValue, maxValue);
    const float high = std::max(minValue, maxValue);
    return high > low ? std::clamp((value - low) / (high - low), 0.0f, 1.0f) : 0.0f;
}

inline bool ui_set_circular_progress(
    Renderer2DScene& scene,
    entt::entity root,
    float value)
{
    entt::registry& registry = scene.registry();
    UiCircularProgressComponent* control =
        registry.try_get<UiCircularProgressComponent>(root);
    if (!control || control->indicator == entt::null ||
        !registry.valid(control->indicator))
    {
        return false;
    }

    const float low = std::min(control->minValue, control->maxValue);
    const float high = std::max(control->minValue, control->maxValue);
    control->value = std::clamp(value, low, high);
    ShapeComponent* indicator =
        registry.try_get<ShapeComponent>(control->indicator);
    if (!indicator)
    {
        return false;
    }

    const float normalizedValue = ui_circular_progress_normalized_value(
        control->value,
        control->minValue,
        control->maxValue);
    if (indicator->arcProgress == normalizedValue)
    {
        return false;
    }

    indicator->arcProgress = normalizedValue;
    scene.mark_dirty(control->indicator);
    return true;
}

inline bool ui_apply_circular_progress_content_color(
    Renderer2DScene& scene,
    entt::entity root,
    entt::entity content,
    bool mediaAsMask = false)
{
    entt::registry& registry = scene.registry();
    const UiCircularProgressComponent* control =
        registry.try_get<UiCircularProgressComponent>(root);
    if (!control || !control->contentUsesProgressColor ||
        content == entt::null || !registry.valid(content))
    {
        return false;
    }

    const glm::vec4 startColor = control->progressColor;
    const glm::vec4 endColor = control->progressEndColor;
    bool applied = false;
    if (TextStyleComponent* text =
        registry.try_get<TextStyleComponent>(content))
    {
        if (control->progressUsesGradient)
        {
            text->set_gradient(
                startColor,
                endColor,
                { 0.0f, 0.0f },
                { 1.0f, 0.0f });
        }
        else
        {
            text->set_color(startColor);
        }
        applied = true;
    }
    if (ShapeStyleComponent* shape =
        registry.try_get<ShapeStyleComponent>(content))
    {
        shape->fill = control->progressUsesGradient ?
            Renderer2DFill::eLinearGradient :
            Renderer2DFill::eSolid;
        shape->color0 = startColor;
        shape->color1 = control->progressUsesGradient ? endColor : startColor;
        shape->gradientStart = { 0.0f, 0.0f };
        shape->gradientEnd = { 1.0f, 0.0f };
        applied = true;
    }
    if (Media2DComponent* media =
        registry.try_get<Media2DComponent>(content))
    {
        if (control->progressUsesGradient)
        {
            media->set_tint_gradient(
                startColor,
                endColor,
                { 0.0f, 0.0f },
                { 1.0f, 0.0f });
        }
        else
        {
            media->set_tint(startColor);
        }
        if (mediaAsMask)
        {
            media->set_tint_as_mask(true);
        }
        applied = true;
    }

    if (applied)
    {
        scene.mark_dirty(content);
    }
    return applied;
}

inline UiControlHandle ui_create_circular_progress(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    float diameter,
    int32_t layer,
    uint32_t order,
    UiCircularProgressOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    const float safeDiameter = std::max(diameter, 1.0f);
    const float safeThickness = std::clamp(options.thickness, 0.0f, safeDiameter);
    const glm::vec2 scaledDiameter =
        scaled_size(safeDiameter, safeDiameter, ui.scale());
    const float scaledThickness = scaled_scalar(safeThickness, ui.scale());
    const float scaledSoftness = scaled_scalar(
        std::max(options.edgeSoftness, 0.0f),
        ui.scale());

    auto arcStyle = [&](std::string_view color, std::string_view endColor) {
        ShapeStyleComponent style = endColor.empty() ?
            make_solid_style(color, "#00000000", 0.0f, options.opacity) :
            make_gradient_style(color, endColor, "#00000000", 0.0f, options.opacity);
        style.edgeSoftness = scaledSoftness;
        return style;
    };
    auto configureArc = [&](entt::entity entity, float progress) {
        if (ShapeComponent* shape = registry.try_get<ShapeComponent>(entity))
        {
            shape->arcProgress = std::clamp(progress, 0.0f, 1.0f);
            shape->arcThickness = scaledThickness;
            shape->arcStartAngleRadians = options.startAngleRadians;
            shape->arcClockwise = options.clockwise;
        }
    };

    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaledDiameter,
        arcStyle(options.trackColor, options.trackEndColor),
        Renderer2DPrimitive::eCircularProgress);
    configureArc(handle.root, 1.0f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaledDiameter);

    handle.indicator = scene.create_shape(
        { 0.0f, 0.0f },
        scaledDiameter,
        arcStyle(options.progressColor, options.progressEndColor),
        Renderer2DPrimitive::eCircularProgress);
    configureArc(
        handle.indicator,
        ui_circular_progress_normalized_value(
            options.value,
            options.minValue,
            options.maxValue));
    ui.set_layer(handle.indicator, layer + 1, order);
    ui.attach_aligned(
        handle.indicator,
        handle.root,
        UiAlignment::eCenter,
        glm::vec2(0.0f),
        scaledDiameter);

    const float low = std::min(options.minValue, options.maxValue);
    const float high = std::max(options.minValue, options.maxValue);
    const glm::vec4 progressColor =
        renderer2d_hex_color(options.progressColor);
    const bool progressUsesGradient = !options.progressEndColor.empty();
    const glm::vec4 progressEndColor = progressUsesGradient ?
        renderer2d_hex_color(options.progressEndColor) :
        progressColor;
    registry.emplace<UiCircularProgressComponent>(
        handle.root,
        UiCircularProgressComponent {
            handle.indicator,
            std::clamp(options.value, low, high),
            low,
            high,
            progressColor,
            progressEndColor,
            progressUsesGradient,
            options.contentUsesProgressColor
        });
    return handle;
}


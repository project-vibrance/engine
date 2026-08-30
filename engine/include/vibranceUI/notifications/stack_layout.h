#pragma once

#include <vibranceUI/export.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <span>
#include <vector>

// Model-agnostic input for one notification card. groupSize controls how many
// compact backing layers reserve vertical space beneath the card.
struct NotificationStackItemLayout
{
    glm::vec2 size { 376.0f, 88.0f };
    std::size_t groupSize = 1u;
};

struct NotificationStackLayoutOptions
{
    float viewportWidth = 428.0f;
    float topPadding = 44.0f;
    float rightPadding = 36.0f;
    float gap = 8.0f;
    float layerOffset = 7.0f;
    std::size_t maximumLayers = 2u;
};

struct NotificationStackPlacement
{
    glm::vec2 offset {};
    glm::vec2 size {};
    float layerDepth = 0.0f;
};

// Places cards newest-first down the right edge. Negative padding/gap/layer
// values are clamped to zero, making partially populated development options
// safe without application-side validation.
VIBRANCE_ENGINE_API std::vector<NotificationStackPlacement>
layout_notification_stack(
    std::span<const NotificationStackItemLayout> items,
    const NotificationStackLayoutOptions& options = {});

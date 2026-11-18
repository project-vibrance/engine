// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/export.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <span>
#include <vector>



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




VIBRANCE_ENGINE_API std::vector<NotificationStackPlacement>
layout_notification_stack(
    std::span<const NotificationStackItemLayout> items,
    const NotificationStackLayoutOptions& options = {});

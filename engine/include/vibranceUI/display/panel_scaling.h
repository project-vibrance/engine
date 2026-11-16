// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <algorithm>
#include <cmath>
#include <glm/glm.hpp>

inline float advance_panel_scale(float current, float target, double elapsed)
{
    const float next = target + (current - target) *
        std::exp(-18.0f * static_cast<float>(std::clamp(elapsed, 0.0, 0.1)));
    return std::abs(next - target) < 0.001f ? target : next;
}

struct PanelScaleAnimation
{
    float visual = 1.0f;
    float native = 1.0f;



    bool advance(float target, double elapsed, bool dragging)
    {
        visual = advance_panel_scale(visual, target, elapsed);
        if (dragging || native == target)
        {
            return false;
        }
        native = target;
        return true;
    }
};

inline glm::vec2 panel_content_scale(glm::vec2 desktopScale, float factor)
{
#if defined(_WIN32)


    desktopScale = glm::vec2(1.0f);
#endif
    return desktopScale * factor;
}

inline int scaled_panel_extent(int extent, float factor)
{
    return std::max(1, static_cast<int>(std::lround(extent * factor)));
}



inline float fit_panel_scale(float requested, glm::ivec2 available, glm::ivec2 design)
{
    const float limit = std::min(
        static_cast<float>(std::max(available.x, 1)) / std::max(design.x, 1),
        static_cast<float>(std::max(available.y, 1)) / std::max(design.y, 1));
    return std::max(0.1f, std::min(requested, limit));
}

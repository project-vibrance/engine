#pragma once

#include <algorithm>
#include <glm/glm.hpp>

constexpr float kDefaultDesignWidth = 1920.0f;
constexpr float kDefaultDesignHeight = 1080.0f;

struct LayoutScale
{
    // Converts logical UI co-ordinates into framebuffer pixels for DPI-aware windows
    glm::vec2 factor { 1.0f };
    glm::vec2 origin { 0.0f };
    glm::vec2 contentScale { 1.0f };
    glm::vec2 logicalSize { kDefaultDesignWidth, kDefaultDesignHeight };
};

enum class UiAlignment
{
    // British spelling aliases are kept so app code can use centre consistently
    eTopLeft,
    eTopCenter,
    eTopCentre = eTopCenter,
    eTopRight,
    eMiddleLeft,
    eCenter,
    eCentre = eCenter,
    eMiddleCenter = eCenter,
    eMiddleCentre = eCenter,
    eMiddleRight,
    eBottomLeft,
    eBottomCenter,
    eBottomCentre = eBottomCenter,
    eBottomRight
};

inline glm::vec2 ui_alignment_anchor(UiAlignment alignment)
{
    switch (alignment)
    {
    case UiAlignment::eTopCenter:
        return { 0.5f, 0.0f };
    case UiAlignment::eTopRight:
        return { 1.0f, 0.0f };
    case UiAlignment::eMiddleLeft:
        return { 0.0f, 0.5f };
    case UiAlignment::eCenter:
        return { 0.5f, 0.5f };
    case UiAlignment::eMiddleRight:
        return { 1.0f, 0.5f };
    case UiAlignment::eBottomLeft:
        return { 0.0f, 1.0f };
    case UiAlignment::eBottomCenter:
        return { 0.5f, 1.0f };
    case UiAlignment::eBottomRight:
        return { 1.0f, 1.0f };
    case UiAlignment::eTopLeft:
    default:
        return { 0.0f, 0.0f };
    }
}

inline glm::vec2 ui_alignment_pivot(UiAlignment alignment)
{
    return ui_alignment_anchor(alignment);
}

inline LayoutScale make_layout_scale(
    int framebufferWidth,
    int framebufferHeight,
    glm::vec2 contentScale,
    glm::vec2 designSize = { kDefaultDesignWidth, kDefaultDesignHeight })
{
    // Use a uniform design scale so UI keeps its proportions across aspect ratios
    const float dpiScale = std::max(std::min(contentScale.x, contentScale.y), 0.25f);
    const glm::vec2 framebufferSize {
        static_cast<float>(framebufferWidth),
        static_cast<float>(framebufferHeight)
    };
    const glm::vec2 safeDesignSize = glm::max(designSize, glm::vec2(1.0f));
    const glm::vec2 logicalSize = framebufferSize / dpiScale;
    const float uniformScale = std::min(
        logicalSize.x / safeDesignSize.x,
        logicalSize.y / safeDesignSize.y
    );
    const glm::vec2 renderScale { uniformScale * dpiScale };
    const glm::vec2 canvasSize { safeDesignSize.x * renderScale.x, safeDesignSize.y * renderScale.y };
    return {
        renderScale,
        {
            (framebufferSize.x - canvasSize.x) * 0.5f,
            (framebufferSize.y - canvasSize.y) * 0.5f
        },
        contentScale,
        logicalSize
    };
}

inline glm::vec2 scaled_point(float x, float y, const LayoutScale& scale)
{
    return scale.origin + glm::vec2 { x, y } * scale.factor;
}

inline glm::vec2 scaled_size(float width, float height, const LayoutScale& scale)
{
    return glm::vec2 { width, height } * scale.factor;
}

inline glm::vec2 scaled_offset(float x, float y, const LayoutScale& scale)
{
    return glm::vec2 { x, y } * scale.factor;
}

inline glm::vec4 scaled_edges(float left, float top, float right, float bottom, const LayoutScale& scale)
{
    return {
        left * scale.factor.x,
        top * scale.factor.y,
        right * scale.factor.x,
        bottom * scale.factor.y
    };
}

inline float scaled_scalar(float value, const LayoutScale& scale)
{
    return value * scale.factor.x;
}

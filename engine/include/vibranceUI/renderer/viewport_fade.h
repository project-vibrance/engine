#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>
#include <glm/vec4.hpp>
#include <vibranceUI/graphics/backdrop.h>

struct Renderer2DViewportFade
{
    glm::vec4 rect { 0.0f };
    float topHeight = 0.0f;
    float bottomHeight = 0.0f;
    float minimumOpacity = 0.0f;
    bool enabled = false;
    bool operator==(const Renderer2DViewportFade&) const = default;
};

struct Renderer2DCompositeConstants
{
    glm::uvec4 data { 0u };
    glm::vec4 fadeRect { 0.0f };
    glm::vec4 fadeParameters { 0.0f };
};
static_assert(sizeof(Renderer2DCompositeConstants) == 48u);

inline float renderer2d_viewport_fade_opacity(const Renderer2DViewportFade& fade, float y)
{
    if (!fade.enabled) return 1.0f;
    if (y < fade.rect.y || y >= fade.rect.y + fade.rect.w) return 0.0f;
    const auto smooth = [](float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    };
    float alpha = 1.0f;
    if (fade.topHeight > 0.0f) alpha = std::min(alpha, smooth((y - fade.rect.y) / fade.topHeight));
    if (fade.bottomHeight > 0.0f) alpha = std::min(alpha,
        smooth((fade.rect.y + fade.rect.w - y) / fade.bottomHeight));
    return fade.minimumOpacity + (1.0f - fade.minimumOpacity) * alpha;
}

// Retain one original lens per visible card/control. Subdividing its geometry
// changes optical normals, introduces seams and consumes the native slot pool.
inline std::vector<SystemBackdropRegion> renderer2d_clip_backdrop_region(
    const SystemBackdropRegion& region, glm::vec4 clip, const Renderer2DViewportFade& fade)
{
    const float left = std::max(clip.x, fade.enabled ? fade.rect.x : clip.x);
    const float top = std::max(clip.y, fade.enabled ? fade.rect.y : clip.y);
    const float right = std::min(clip.x + clip.z, fade.enabled ? fade.rect.x + fade.rect.z : clip.x + clip.z);
    const float bottom = std::min(clip.y + clip.w, fade.enabled ? fade.rect.y + fade.rect.w : clip.y + clip.w);
    if (right <= left || bottom <= top || region.x >= right || region.x + region.width <= left ||
        region.y >= bottom || region.y + region.height <= top) return {};
    auto retained = region;
    if (fade.enabled || left > region.x || top > region.y ||
        right < region.x + region.width || bottom < region.y + region.height)
    {
        retained.viewportRect = { left, top, right - left, bottom - top };
        retained.viewportFade = { fade.enabled ? fade.topHeight : 0.0f,
            fade.enabled ? fade.bottomHeight : 0.0f, fade.enabled ? fade.minimumOpacity : 0.0f, 1.0f };
    }
    return { retained };
}

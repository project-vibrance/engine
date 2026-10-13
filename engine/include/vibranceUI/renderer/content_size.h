#pragma once

#include <vibranceUI/renderer/renderer2d_components.h>

inline glm::vec2 ui_padded_content_size(
    glm::vec2 contentSize, glm::vec4 padding, glm::vec2 minimumSize = {})
{
    padding = glm::max(padding, glm::vec4(0.0f));
    return glm::max(glm::max(contentSize, glm::vec2(0.0f)) +
        glm::vec2(padding.x + padding.z, padding.y + padding.w),
        glm::max(minimumSize, glm::vec2(0.0f)));
}

inline glm::vec2 ui_intrinsic_content_size(
    const entt::registry& registry, entt::entity content)
{
    if (!registry.valid(content)) return {};
    if (const auto* text = registry.try_get<TextComponent>(content))
        return glm::max(text->bounds, glm::vec2(0.0f));
    glm::vec2 size {};
    if (const auto* media = registry.try_get<Media2DComponent>(content)) size = media->size;
    else if (const auto* shape = registry.try_get<ShapeComponent>(content)) size = shape->size;
    if (const auto* layout = registry.try_get<Layout2DComponent>(content))
    {
        if (layout->size.x > 0.0f) size.x = layout->size.x;
        if (layout->size.y > 0.0f) size.y = layout->size.y;
    }
    return glm::max(size, glm::vec2(0.0f));
}

#pragma once

#include <algorithm>
#include <cstddef>
#include <glm/glm.hpp>

enum class UiStackAxis
{
    eHorizontal,
    eVertical
};

struct UiStackLayoutOptions
{
    UiStackAxis axis = UiStackAxis::eVertical;
    float gap = 0.0f;
    // left, top, right, bottom
    glm::vec4 padding { 0.0f };
};

struct UiStackPlacement
{
    glm::vec2 offset { 0.0f };
    glm::vec2 size { 0.0f };
    std::size_t index = 0u;
};

// Small builder-time flow layout for menus, cards, and settings sections. It
// removes manual cursor arithmetic while leaving runtime ECS layout unchanged.
// Use a renderer layout component only when children must reflow every frame.
class UiStackLayout
{
public:
    explicit UiStackLayout(UiStackLayoutOptions options = {}) :
        stackOptions(options)
    {
    }

    UiStackPlacement append(glm::vec2 size, float gapBefore = 0.0f)
    {
        size = glm::max(size, glm::vec2(0.0f));
        const float normalGap = itemCount == 0u ? 0.0f :
            std::max(stackOptions.gap, 0.0f);
        cursor += normalGap + std::max(gapBefore, 0.0f);

        UiStackPlacement placement = {};
        placement.index = itemCount++;
        placement.size = size;
        if (stackOptions.axis == UiStackAxis::eVertical)
        {
            placement.offset = { stackOptions.padding.x, stackOptions.padding.y + cursor };
            cursor += size.y;
            crossExtent = std::max(crossExtent, size.x);
        }
        else
        {
            placement.offset = { stackOptions.padding.x + cursor, stackOptions.padding.y };
            cursor += size.x;
            crossExtent = std::max(crossExtent, size.y);
        }
        return placement;
    }

    glm::vec2 content_size() const
    {
        if (stackOptions.axis == UiStackAxis::eVertical)
        {
            return {
                stackOptions.padding.x + crossExtent + stackOptions.padding.z,
                stackOptions.padding.y + cursor + stackOptions.padding.w
            };
        }
        return {
            stackOptions.padding.x + cursor + stackOptions.padding.z,
            stackOptions.padding.y + crossExtent + stackOptions.padding.w
        };
    }

    float main_extent() const
    {
        return cursor;
    }

    std::size_t size() const
    {
        return itemCount;
    }

private:
    UiStackLayoutOptions stackOptions {};
    float cursor = 0.0f;
    float crossExtent = 0.0f;
    std::size_t itemCount = 0u;
};

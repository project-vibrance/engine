#pragma once

#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace vibrance::renderer
{
    using EntityBounds = std::pair<entt::entity, glm::uvec4>;

    inline glm::uvec4 union_entity_rect(glm::uvec4 left, glm::uvec4 right)
    {
        if (left.z == 0u || left.w == 0u) return right;
        if (right.z == 0u || right.w == 0u) return left;
        const auto x = std::min(left.x, right.x);
        const auto y = std::min(left.y, right.y);
        const auto endX = std::max(left.x + left.z, right.x + right.z);
        const auto endY = std::max(left.y + left.w, right.y + right.w);
        return { x, y, endX - x, endY - y };
    }

    // Append batch envelopes cheaply, then collapse all glyph/effect batches
    // belonging to each entity in one sorted pass. The vector keeps capacity.
    inline void coalesce_entity_bounds(std::vector<EntityBounds>& bounds)
    {
        std::sort(bounds.begin(), bounds.end(), [](const auto& left, const auto& right) {
            return entt::to_integral(left.first) < entt::to_integral(right.first);
        });
        std::size_t count = 0u;
        for (const auto& entry : bounds)
        {
            if (entry.first == entt::null || entry.second.z == 0u || entry.second.w == 0u)
                continue;
            if (count > 0u && bounds[count - 1u].first == entry.first)
                bounds[count - 1u].second = union_entity_rect(bounds[count - 1u].second, entry.second);
            else
                bounds[count++] = entry;
        }
        bounds.resize(count);
    }

    // Both lists are sorted and unique. Add old and new moving envelopes,
    // deleted/inserted entities, and unchanged time-driven pixel damage.
    template <typename IncludeBounds, typename DamagePending>
    void collect_entity_damage(const std::vector<EntityBounds>& current,
        const std::vector<EntityBounds>& previous,
        IncludeBounds&& include, DamagePending&& damagePending)
    {
        std::size_t currentIndex = 0u;
        std::size_t previousIndex = 0u;
        while (currentIndex < current.size() || previousIndex < previous.size())
        {
            if (previousIndex == previous.size() ||
                (currentIndex < current.size() &&
                    entt::to_integral(current[currentIndex].first) <
                        entt::to_integral(previous[previousIndex].first)))
            {
                include(current[currentIndex++]);
            }
            else if (currentIndex == current.size() ||
                entt::to_integral(previous[previousIndex].first) <
                    entt::to_integral(current[currentIndex].first))
            {
                include(previous[previousIndex++]);
            }
            else
            {
                const auto& now = current[currentIndex++];
                const auto& before = previous[previousIndex++];
                const bool moved = now.second != before.second;
                if (moved || damagePending(now.first))
                {
                    include(now);
                    if (moved) include(before);
                }
            }
        }
    }
}

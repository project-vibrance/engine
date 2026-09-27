#pragma once
#include <vibranceUI/renderer/renderer2d_components.h>
#include <algorithm>
#include <span>
#include <tuple>
#include <vector>

namespace vibrance::metal
{
// The colour surface retains the previous frame. Compare the final ordered
// paint operations, rather than entity dirty flags (parents can affect children).
class FrameDamage
{
  public:
    struct Item
    {
        Renderer2DBatch batch;
        glm::uvec4 bounds{};
        uint32_t phase = 0;
        bool backdropEffect = false;

        bool operator==(const Item &other) const
        {
            const auto paint = [](const Renderer2DBatch &b) {
                return std::tie(b.rect, b.clipRect, b.uvRect, b.color0, b.color1, b.color2, b.shadowColor,
                                b.effect0, b.effect1, b.primitive, b.flags, b.packedData, b.mediaId,
                                b.frameIndex);
            };
            return phase == other.phase && backdropEffect == other.backdropEffect &&
                   bounds == other.bounds && paint(batch) == paint(other.batch);
        }
    };

    void invalidate()
    {
        valid = false;
    }

    glm::uvec4 update(std::span<const Item> current, uint32_t width, uint32_t height, bool hasModel)
    {
        glm::uvec4 damage{};
        const auto include = [&](glm::uvec4 rect) {
            if (!rect.z || !rect.w)
                return;
            if (!damage.z || !damage.w)
                damage = rect;
            else
            {
                const auto start = glm::min(glm::uvec2(damage), glm::uvec2(rect));
                const auto end = glm::max(glm::uvec2(damage) + glm::uvec2(damage.z, damage.w),
                                          glm::uvec2(rect) + glm::uvec2(rect.z, rect.w));
                damage = {start.x, start.y, end.x - start.x, end.y - start.y};
            }
        };
        for (size_t i = 0; i < std::max(previous.size(), current.size()); ++i)
        {
            if (i < previous.size() && i < current.size() && previous[i] == current[i])
                continue;
            if (i < previous.size())
                include(previous[i].bounds);
            if (i < current.size())
                include(current[i].bounds);
        }
        if (hasModel != previousHasModel || (damage.z && damage.w && (hasModel || previousHasModel)))
            damage = {0, 0, width, height};
        else if (damage.z && damage.w)
        {
            // A blur reads earlier paint operations beyond its output bounds.
            // Rebuild that source halo before the blur samples the retained
            // colour surface, which contains the previous frame's final layers.
            constexpr uint32_t blurReadHalo = 50;
            const auto includeBlur = [&](const Item &item) {
                if (!item.backdropEffect || !item.bounds.z || !item.bounds.w)
                    return;
                const auto left = item.bounds.x > blurReadHalo ? item.bounds.x - blurReadHalo : 0;
                const auto top = item.bounds.y > blurReadHalo ? item.bounds.y - blurReadHalo : 0;
                const auto right = std::min(width, item.bounds.x + item.bounds.z + blurReadHalo);
                const auto bottom = std::min(height, item.bounds.y + item.bounds.w + blurReadHalo);
                include({left, top, right - left, bottom - top});
            };
            for (const auto &item : previous)
                includeBlur(item);
            for (const auto &item : current)
                includeBlur(item);
        }
        if (!valid)
            damage = {0, 0, width, height};
        previous.assign(current.begin(), current.end());
        previousHasModel = hasModel;
        valid = true;
        return damage;
    }

  private:
    bool valid = false, previousHasModel = false;
    std::vector<Item> previous;
};
} // namespace vibrance::metal

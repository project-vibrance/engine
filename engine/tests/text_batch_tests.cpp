#include "../src/renderer/vulkan/text_batch.h"

#include <array>
#include <iostream>
#include <limits>
#include <random>

namespace
{
    struct Bounds
    {
        uint32_t x = 0u, y = 0u, width = 0u, height = 0u;
    };

    bool check_plan(std::span<const Bounds> bounds, std::span<const Bounds> preceding = {})
    {
        vibrance::vulkan::TextBatchPlan plan;
        vibrance::vulkan::build_text_batch_plan(bounds, plan, preceding);
        std::vector<uint32_t> order(bounds.size(), std::numeric_limits<uint32_t>::max());
        std::vector<uint32_t> serial(64u * 64u, 0u), batched(serial);
        const auto paint = [&](std::vector<uint32_t>& image, uint32_t index) {
            const auto& rect = bounds[index];
            for (uint32_t y = rect.y; y < std::min(64u, rect.y + rect.height); ++y)
            {
                for (uint32_t x = rect.x; x < std::min(64u, rect.x + rect.width); ++x)
                {
                    // A non-commutative destination operation exposes reordered
                    // overlapping writes even when the final glyph is opaque.
                    auto& pixel = image[y * 64u + x];
                    pixel = (pixel * 37u + index + 1u) % 65521u;
                }
            }
        };
        for (uint32_t index = 0u; index < bounds.size(); ++index)
        {
            paint(serial, index);
        }
        std::vector<Bounds> serialPending(preceding.begin(), preceding.end());
        uint32_t serialBarriers = 0u;
        for (const auto& rect : bounds)
        {
            if (rect.width == 0u || rect.height == 0u) { continue; }
            if (std::any_of(serialPending.begin(), serialPending.end(), [&](const Bounds& pending) {
                return vibrance::vulkan::text_batch_bounds_overlap(rect, pending);
            }))
            {
                ++serialBarriers;
                serialPending.clear();
            }
            serialPending.push_back(rect);
        }
        std::vector<Bounds> batchPending(preceding.begin(), preceding.end());
        uint32_t batchBarriers = 0u;
        uint32_t dispatched = 0u;
        for (uint32_t groupIndex = 0u; groupIndex < plan.groups.size(); ++groupIndex)
        {
            const auto& group = plan.groups[groupIndex];
            if (group.count == 0u || group.count > 64u || group.first != dispatched)
            {
                return false;
            }
            if (std::any_of(plan.glyphIndices.begin() + group.first,
                plan.glyphIndices.begin() + group.first + group.count, [&](uint32_t index) {
                    return std::any_of(batchPending.begin(), batchPending.end(), [&](const Bounds& pending) {
                        return vibrance::vulkan::text_batch_bounds_overlap(bounds[index], pending);
                    });
                }))
            {
                ++batchBarriers;
                batchPending.clear();
            }
            for (uint32_t offset = 0u; offset < group.count; ++offset)
            {
                const uint32_t index = plan.glyphIndices[group.first + offset];
                if (index >= bounds.size() || order[index] != std::numeric_limits<uint32_t>::max() ||
                    bounds[index].width == 0u || bounds[index].height == 0u ||
                    bounds[index].width > group.width || bounds[index].height > group.height)
                {
                    return false;
                }
                order[index] = groupIndex;
                batchPending.push_back(bounds[index]);
                for (uint32_t earlier = 0u; earlier < offset; ++earlier)
                {
                    if (vibrance::vulkan::text_batch_bounds_overlap(
                        bounds[index], bounds[plan.glyphIndices[group.first + earlier]]))
                    {
                        return false;
                    }
                }
                paint(batched, index);
            }
            dispatched += group.count;
        }
        for (uint32_t index = 0u; index < bounds.size(); ++index)
        {
            const bool empty = bounds[index].width == 0u || bounds[index].height == 0u;
            if (empty != (order[index] == std::numeric_limits<uint32_t>::max()))
            {
                return false;
            }
            for (uint32_t earlier = 0u; earlier < index; ++earlier)
            {
                if (vibrance::vulkan::text_batch_bounds_overlap(bounds[earlier], bounds[index]) &&
                    order[earlier] >= order[index])
                {
                    return false;
                }
            }
        }
        return serial == batched && dispatched == plan.glyphIndices.size() &&
            batchBarriers <= serialBarriers;
    }
}

int main()
{
    using vibrance::vulkan::TextBatchPlan;
    using vibrance::vulkan::build_text_batch_plan;
    const auto expect = [](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; }
        return condition;
    };
    bool passed = true;
    const std::array chain {
        Bounds { 0u, 0u, 10u, 10u }, Bounds { 9u, 0u, 10u, 10u },
        Bounds { 18u, 0u, 10u, 10u }, Bounds { 40u, 0u, 10u, 10u }
    };
    TextBatchPlan plan;
    build_text_batch_plan(std::span<const Bounds>(chain), plan);
    passed &= expect(plan.groups.size() == 3u && plan.groups[0].count == 2u &&
        plan.glyphIndices == std::vector<uint32_t> { 0u, 3u, 1u, 2u },
        "Dependency chain must preserve overlap order and combine independent glyphs.");
    passed &= expect(check_plan(chain), "Dependency-chain painting differs from serial painting.");
    const std::array prefix { Bounds { 40u, 0u, 10u, 10u } };
    const std::array entrance {
        Bounds { 0u, 0u, 10u, 10u }, Bounds { 9u, 0u, 10u, 10u },
        Bounds { 40u, 0u, 10u, 10u }
    };
    build_text_batch_plan(std::span<const Bounds>(entrance), plan, std::span<const Bounds>(prefix));
    passed &= expect(plan.groups.size() == 2u && plan.groups[0].count == 1u &&
        plan.groups[1].count == 2u && check_plan(entrance, prefix),
        "Prior non-text dependencies must not introduce an extra entrance barrier.");
    const std::array emptyAndTouching {
        Bounds { 0u, 0u, 0u, 10u }, Bounds { 0u, 0u, 10u, 10u },
        Bounds { 10u, 0u, 10u, 10u }, Bounds { 0u, 0u, 10u, 0u }
    };
    passed &= expect(check_plan(emptyAndTouching), "Empty or touching glyph bounds were mishandled.");
    std::vector<Bounds> disjoint(130u);
    for (uint32_t index = 0u; index < disjoint.size(); ++index)
    {
        disjoint[index] = { index * 2u, 0u, 1u, 1u };
    }
    build_text_batch_plan(std::span<const Bounds>(disjoint), plan);
    passed &= expect(plan.groups.size() == 3u && plan.groups[0].count == 64u &&
        plan.groups[1].count == 64u && plan.groups[2].count == 2u && check_plan(disjoint),
        "Large disjoint waves must split at 64 glyphs without dropping a glyph.");
    std::vector<Bounds> firstChunk(vibrance::vulkan::kTextBatchPlanningGlyphLimit);
    for (uint32_t index = 0u; index < firstChunk.size(); ++index)
    {
        firstChunk[index] = { index * 2u, 0u, 1u, 1u };
    }
    build_text_batch_plan(std::span<const Bounds>(firstChunk), plan);
    passed &= expect(plan.groups.size() == 4u && check_plan(firstChunk),
        "A full planning chunk must preserve all four 64-glyph dispatches.");
    const std::array nextChunk {
        Bounds { 1000u, 0u, 1u, 1u }, firstChunk.back(), firstChunk.front()
    };
    build_text_batch_plan(std::span<const Bounds>(nextChunk), plan,
        std::span<const Bounds>(firstChunk));
    passed &= expect(plan.groups.size() == 2u && plan.groups[0].count == 1u &&
        plan.groups[1].count == 2u && check_plan(nextChunk, firstChunk),
        "Following planning chunks must preserve overlapping writes from the preceding chunk.");
    build_text_batch_plan(std::span<const Bounds>(), plan);
    passed &= expect(plan.groups.empty() && plan.glyphIndices.empty(),
        "An empty run must clear the previous plan.");
    const Bounds nearLimit { std::numeric_limits<uint32_t>::max() - 3u, 0u, 8u, 1u };
    passed &= expect(vibrance::vulkan::text_batch_bounds_overlap(nearLimit, nearLimit),
        "Bounds overlap must not wrap near the coordinate limit.");

    std::mt19937 random(0x54e87u);
    for (uint32_t fixture = 0u; fixture < 2000u; ++fixture)
    {
        std::vector<Bounds> glyphs(random() % 130u);
        for (auto& rect : glyphs)
        {
            rect = { random() % 64u, random() % 64u, random() % 20u, random() % 20u };
        }
        std::vector<Bounds> preceding(random() % 12u);
        for (auto& rect : preceding)
        {
            rect = { random() % 64u, random() % 64u, random() % 20u, random() % 20u };
        }
        if (!check_plan(glyphs, preceding))
        {
            std::cerr << "Random overlap painting failed for fixture " << fixture << '\n';
            passed = false;
            break;
        }
    }
    return passed ? 0 : 1;
}

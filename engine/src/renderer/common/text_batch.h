#pragma once

#include <algorithm>
#include <cstdint>
#include <span>
#include <vector>

namespace vibrance::renderer
{
    constexpr uint32_t kTextBatchGlyphLimit = 64u;
    // Bound quadratic dependency analysis for long paragraphs. Following
    // chunks inherit all outstanding writes through their initial wave.
    constexpr uint32_t kTextBatchPlanningGlyphLimit = 256u;

    struct TextBatchGroup
    {
        uint32_t first = 0u;
        uint32_t count = 0u;
        uint32_t width = 0u;
        uint32_t height = 0u;
    };

    struct TextBatchPlan
    {
        std::vector<uint32_t> glyphIndices;
        std::vector<TextBatchGroup> groups;
        // Reuse planning storage across consecutive text runs in a frame.
        std::vector<uint32_t> levels;
        std::vector<uint32_t> waveOffsets;
    };

    template<class Bounds>
    bool text_batch_bounds_overlap(const Bounds& left, const Bounds& right)
    {
        return left.width != 0u && left.height != 0u &&
            right.width != 0u && right.height != 0u &&
            static_cast<uint64_t>(left.x) < static_cast<uint64_t>(right.x) + right.width &&
            static_cast<uint64_t>(right.x) < static_cast<uint64_t>(left.x) + left.width &&
            static_cast<uint64_t>(left.y) < static_cast<uint64_t>(right.y) + right.height &&
            static_cast<uint64_t>(right.y) < static_cast<uint64_t>(left.y) + left.height;
    }

    // Each overlapping pair retains its original paint order. Disjoint glyphs
    // can move into the same dependency wave, even across intervening glyphs.
    // Group boundaries limit dispatch depth without adding dependencies.
    template<class Bounds>
    void build_text_batch_plan(std::span<const Bounds> bounds, TextBatchPlan& plan,
        std::span<const Bounds> pendingWrites = {})
    {
        plan.glyphIndices.clear();
        plan.groups.clear();
        plan.levels.assign(bounds.size(), 0u);
        plan.waveOffsets.assign(bounds.size() + 2u, 0u);
        uint32_t waveCount = 0u;
        uint32_t glyphCount = 0u;
        for (uint32_t index = 0u; index < bounds.size(); ++index)
        {
            if (bounds[index].width == 0u || bounds[index].height == 0u)
            {
                continue;
            }
            // Previous non-text writes are an earlier wave too. Seeding their
            // dependants prevents a reordered glyph from forcing a barrier
            // before the disjoint first wave, followed by another within text.
            uint32_t level = std::any_of(pendingWrites.begin(), pendingWrites.end(),
                [&](const Bounds& pending) { return text_batch_bounds_overlap(pending, bounds[index]); }) ? 1u : 0u;
            for (uint32_t earlier = 0u; earlier < index; ++earlier)
            {
                if (text_batch_bounds_overlap(bounds[earlier], bounds[index]))
                {
                    level = std::max(level, plan.levels[earlier] + 1u);
                }
            }
            plan.levels[index] = level;
            ++plan.waveOffsets[level + 1u];
            waveCount = std::max(waveCount, level + 1u);
            ++glyphCount;
        }
        for (uint32_t wave = 1u; wave <= waveCount; ++wave)
        {
            plan.waveOffsets[wave] += plan.waveOffsets[wave - 1u];
        }
        plan.glyphIndices.resize(glyphCount);
        // Fill backwards so offsets remain the wave starts and original order
        // remains stable within each disjoint wave.
        for (std::size_t index = bounds.size(); index-- > 0u;)
        {
            if (bounds[index].width != 0u && bounds[index].height != 0u)
            {
                plan.glyphIndices[--plan.waveOffsets[plan.levels[index] + 1u]] =
                    static_cast<uint32_t>(index);
            }
        }
        for (uint32_t wave = 0u; wave < waveCount; ++wave)
        {
            const uint32_t first = plan.waveOffsets[wave + 1u];
            const uint32_t end = wave + 1u < waveCount ? plan.waveOffsets[wave + 2u] : glyphCount;
            for (uint32_t groupFirst = first; groupFirst < end; groupFirst += kTextBatchGlyphLimit)
            {
                TextBatchGroup group { groupFirst, std::min(kTextBatchGlyphLimit, end - groupFirst) };
                for (uint32_t offset = 0u; offset < group.count; ++offset)
                {
                    const auto& glyph = bounds[plan.glyphIndices[groupFirst + offset]];
                    group.width = std::max(group.width, glyph.width);
                    group.height = std::max(group.height, glyph.height);
                }
                plan.groups.push_back(group);
            }
        }
    }
}

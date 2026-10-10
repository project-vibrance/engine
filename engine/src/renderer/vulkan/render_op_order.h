#pragma once

#include "text_batch.h"

namespace vibrance::vulkan
{
    struct SimpleRenderOrder
    {
        TextBatchPlan dependencies;
        std::vector<uint32_t> indices;
    };

    // Each wave contains disjoint destination accesses. Keeping its ordinary
    // primitives together makes its foreground glyphs one consecutive run,
    // while every overlapping pair retains its original paint order.
    template<class Bounds>
    void build_simple_render_order(std::span<const Bounds> bounds,
        std::span<const uint8_t> foregroundText, SimpleRenderOrder& order)
    {
        order.indices.clear();
        if (bounds.size() != foregroundText.size()) return;
        build_text_batch_plan(bounds, order.dependencies);
        order.indices.reserve(bounds.size());
        uint32_t waveCount = 0u;
        for (uint32_t index = 0u; index < bounds.size(); ++index)
            if (bounds[index].width != 0u && bounds[index].height != 0u)
                waveCount = std::max(waveCount, order.dependencies.levels[index] + 1u);
        for (uint32_t wave = 0u; wave < waveCount; ++wave)
        {
            const uint32_t first = order.dependencies.waveOffsets[wave + 1u];
            const uint32_t end = wave + 1u < waveCount ?
                order.dependencies.waveOffsets[wave + 2u] :
                static_cast<uint32_t>(order.dependencies.glyphIndices.size());
            for (const uint8_t text : { uint8_t(0), uint8_t(1) })
                for (uint32_t position = first; position < end; ++position)
                {
                    const uint32_t index = order.dependencies.glyphIndices[position];
                    if ((foregroundText[index] != 0u) == (text != 0u))
                        order.indices.push_back(index);
                }
        }
    }
}

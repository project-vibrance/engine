#include "../src/renderer/vulkan/render_op_order.h"

#include <array>
#include <iostream>
#include <limits>
#include <random>

namespace
{
    struct Bounds { uint32_t x, y, width, height; };

    bool equivalent(std::span<const Bounds> bounds, std::span<const uint8_t> text)
    {
        vibrance::vulkan::SimpleRenderOrder order;
        vibrance::vulkan::build_simple_render_order(bounds, text, order);
        std::vector<uint32_t> serial(64u * 64u, 0u), reordered(serial);
        std::vector<uint32_t> positions(bounds.size(), std::numeric_limits<uint32_t>::max());
        const auto paint = [&](std::vector<uint32_t>& image, uint32_t index) {
            const auto& rect = bounds[index];
            const uint64_t right = std::min(uint64_t(64), uint64_t(rect.x) + rect.width);
            const uint64_t bottom = std::min(uint64_t(64), uint64_t(rect.y) + rect.height);
            for (uint32_t y = rect.y; y < bottom; ++y)
                for (uint32_t x = rect.x; x < right; ++x)
                    image[y * 64u + x] = (image[y * 64u + x] * 37u + index + 1u) % 65521u;
        };
        for (uint32_t index = 0u; index < bounds.size(); ++index) paint(serial, index);
        for (uint32_t position = 0u; position < order.indices.size(); ++position)
        {
            const uint32_t index = order.indices[position];
            if (index >= bounds.size() || positions[index] != std::numeric_limits<uint32_t>::max()) return false;
            positions[index] = position;
            paint(reordered, index);
        }
        for (uint32_t index = 0u; index < bounds.size(); ++index)
        {
            const bool empty = bounds[index].width == 0u || bounds[index].height == 0u;
            if (empty != (positions[index] == std::numeric_limits<uint32_t>::max())) return false;
            for (uint32_t earlier = 0u; earlier < index; ++earlier)
                if (vibrance::vulkan::text_batch_bounds_overlap(bounds[earlier], bounds[index]) &&
                    positions[earlier] >= positions[index]) return false;
        }
        return serial == reordered;
    }
}

int main()
{
    const std::array independent {
        Bounds { 0, 0, 8, 8 }, Bounds { 20, 0, 8, 8 },
        Bounds { 0, 20, 8, 8 }, Bounds { 20, 20, 8, 8 } };
    const std::array<uint8_t, 4> mixed { 1, 0, 1, 0 };
    vibrance::vulkan::SimpleRenderOrder order;
    vibrance::vulkan::build_simple_render_order(std::span<const Bounds>(independent), mixed, order);
    if (order.indices != std::vector<uint32_t> { 1, 3, 0, 2 } || !equivalent(independent, mixed))
    {
        std::cerr << "Independent foreground glyphs must join without changing destination pixels.\n";
        return 1;
    }
    const std::array chain {
        Bounds { 0, 0, 10, 10 }, Bounds { 9, 0, 10, 10 },
        Bounds { 18, 0, 10, 10 }, Bounds { 27, 0, 10, 10 } };
    if (!equivalent(chain, mixed) || !equivalent(std::span<const Bounds> {}, std::span<const uint8_t> {})) return 1;
    std::mt19937 random(0x52656e64u);
    for (uint32_t fixture = 0u; fixture < 2000u; ++fixture)
    {
        std::vector<Bounds> bounds;
        std::vector<uint8_t> text;
        const uint32_t count = random() % 257u;
        for (uint32_t index = 0u; index < count; ++index)
        {
            bounds.push_back({ random() % 64u, random() % 64u, random() % 18u, random() % 18u });
            text.push_back(static_cast<uint8_t>(random() % 2u));
        }
        if (!equivalent(bounds, text))
        {
            std::cerr << "Mixed primitive dependency order changed a destination operation.\n";
            return 1;
        }
    }
    return 0;
}

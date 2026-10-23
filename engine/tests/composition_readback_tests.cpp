#include "../src/renderer/vulkan/composition_readback.h"

#include <cstring>
#include <iostream>
#include <limits>
#include <vector>

int main()
{
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    constexpr std::uint32_t width = 37u, height = 19u;
    std::vector<std::uint32_t> image(width * height, 0u);
    std::vector<std::uint32_t> mapped(width * height, 0xDEADBEEFu);
    const auto copy = [&](CompositionReadbackRegion rect) {
        for (std::uint32_t row = 0u; row < rect.height; ++row)
            std::memcpy(reinterpret_cast<std::uint8_t*>(mapped.data()) +
                    rect.byteOffset + std::uint64_t(row) * width * 4u,
                image.data() + (rect.y + row) * width + rect.x,
                rect.width * 4u);
    };
    image[8u * width + 12u] = 0x40802010u;
    const auto first = composition_readback_region(width, height, 12u, 8u, 1u, 1u, true);
    copy(first);
    check(first.width == width && first.height == height && image == mapped,
        "The first upload must initialise the whole persistent image, including transparent pixels");

    for (std::uint32_t frame = 0u; frame < 80u; ++frame)
    {
        const auto x = 3u + frame % 20u, y = 2u + frame % 10u;
        image[y * width + x] = frame % 3u ? 0x7F321F08u + frame : 0u;
        const auto update = composition_readback_region(width, height, x, y, 1u, 1u, false);
        copy(update);
        check(update.byteOffset == (std::uint64_t(y) * width + x) * 4u &&
            update.byteSpan == 4u && image == mapped,
            "Partial readbacks must preserve every unchanged pixel and translucent/cleared alpha");
    }
    const auto rows = composition_readback_region(width, height, 7u, 4u, 5u, 3u, false);
    check(rows.byteOffset == (4u * width + 7u) * 4u &&
        rows.byteSpan == (2u * width + 5u) * 4u,
        "A non-origin readback must retain full-image row pitch and invalidate through its last row");
    const auto clipped = composition_readback_region(width, height, width - 2u, height - 1u,
        std::numeric_limits<std::uint32_t>::max(), 100u, false);
    check(clipped.width == 2u && clipped.height == 1u &&
        clipped.byteOffset + clipped.byteSpan == width * height * 4u,
        "Clipped readback ranges must stay inside the allocation without coordinate overflow");
    const auto empty = composition_readback_region(width, height, width + 2u, 0u, 1u, 1u, false);
    check(empty.byteSpan == 0u && empty.width == 0u && empty.height == 0u,
        "Empty or offscreen damage must not trigger a full transfer");
    const auto resized = composition_readback_region(13u, 7u, 0u, 0u, 0u, 0u, true);
    check(resized.width == 13u && resized.height == 7u && resized.byteSpan == 13u * 7u * 4u,
        "A resized readback must initialise its new extent before applying partial updates");
    const auto large = composition_readback_region(1560u, 1326u, 100u, 120u, 200u, 90u, false);
    check(std::uint64_t(large.width) * large.height * 4u == 72000u,
        "A small Settings update must read back its own area rather than the full high-DPI window");
    return passed ? 0 : 1;
}

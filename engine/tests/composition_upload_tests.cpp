#include "../src/platform/win32/composition_upload.h"
#include "../src/platform/win32/composition_bridge_abi.h"

#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
using namespace vibrance::win32;
unsigned failures = 0u;

struct LegacyUploadInfo
{
    std::uint32_t structSize = sizeof(LegacyUploadInfo);
    const void* pixels = nullptr;
    std::uint32_t width = 0u, height = 0u, rowPitch = 0u;
};
static_assert(sizeof(LegacyUploadInfo) == VIBRANCE_COMPOSITION_UPLOAD_BASE_SIZE);
static_assert(offsetof(LegacyUploadInfo, pixels) ==
    offsetof(VibranceCompositionUploadInfo, pixels));
static_assert(offsetof(LegacyUploadInfo, rowPitch) ==
    offsetof(VibranceCompositionUploadInfo, rowPitch));

void expect(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << message << '\n';
}

void reference_convert(std::vector<std::uint8_t>& destination,
    std::size_t destinationPitch, std::uint32_t width, std::uint32_t height,
    const std::vector<std::uint8_t>& source, std::size_t sourcePitch,
    std::uint32_t sourceWidth, std::uint32_t sourceHeight,
    CompositionUploadRect rect)
{
    for (auto y = rect.y; y < rect.y + rect.height; ++y)
        for (auto x = rect.x; x < rect.x + rect.width; ++x)
        {
            const auto sy = static_cast<std::uint64_t>(y) * sourceHeight / height;
            const auto sx = static_cast<std::uint64_t>(x) * sourceWidth / width;
            const auto src = sy * sourcePitch + sx * 4u;
            const auto dst = y * destinationPitch + x * 4u;
            destination[dst] = source[src + 2u];
            destination[dst + 1u] = source[src + 1u];
            destination[dst + 2u] = source[src];
            destination[dst + 3u] = source[src + 3u];
        }
}

void check_pixels()
{
    CompositionUploadPixels converter;
    for (std::uint32_t width = 1u; width < 49u; ++width)
        for (const auto sourceWidth : {width, width + 11u, std::max(width / 2u, 1u)})
        {
            const std::uint32_t height = 19u, sourceHeight = sourceWidth == width ? 19u : 7u;
            const auto sourcePitch = sourceWidth * 4u + 13u;
            const auto destinationPitch = width * 4u + 7u;
            std::vector<std::uint8_t> source(sourcePitch * sourceHeight);
            for (std::size_t index = 0u; index < source.size(); ++index)
                source[index] = static_cast<std::uint8_t>((index * 37u + 19u) % 251u);
            for (const auto rect : {CompositionUploadRect{0u, 0u, width, height},
                     CompositionUploadRect{width / 3u, 5u, std::max(width / 2u, 1u), 9u}})
            {
                std::vector<std::uint8_t> expected(destinationPitch * height, 0xa5u);
                auto actual = expected;
                reference_convert(expected, destinationPitch, width, height,
                    source, sourcePitch, sourceWidth, sourceHeight, rect);
                converter.convert(actual.data(), destinationPitch, width, height,
                    source.data(), sourcePitch, sourceWidth, sourceHeight, rect);
                expect(actual == expected,
                    "Partial RGBA uploads must match scalar BGRA scaling, preserve alpha and leave neighbours/padding unchanged");
            }
        }
}

void check_damage()
{
    CompositionUploadDamage damage;
    for (std::uint32_t index = 0u; index < 3u; ++index)
    {
        expect(damage.prepare(index, 100u, 60u, 200u, 120u,
                {4u, 3u, 5u, 7u}) == CompositionUploadRect{0u, 0u, 200u, 120u},
            "Every rotating source texture needs a complete first upload");
        damage.commit(index);
    }
    expect(damage.prepare(0u, 100u, 60u, 200u, 120u,
            {10u, 5u, 4u, 3u}) == CompositionUploadRect{8u, 6u, 20u, 14u},
        "An older ring texture must retain intervening damage even when another texture was uploaded");
    damage.commit(0u);
    expect(damage.prepare(1u, 100u, 60u, 200u, 120u,
            {15u, 7u, 4u, 3u}) == CompositionUploadRect{8u, 6u, 30u, 14u},
        "Ring texture damage must combine all preceding source updates at the physical scale");
    damage.commit(1u);
    expect(damage.prepare(0u, 100u, 60u, 200u, 120u,
            {UINT32_MAX, UINT32_MAX, UINT32_MAX, UINT32_MAX}) ==
            CompositionUploadRect{30u, 14u, 8u, 6u},
        "Out-of-bounds or overflowing damage must be clamped without damaging valid retained pixels");
    damage.commit(0u);
    expect(damage.prepare(0u, 100u, 60u, 200u, 120u, {}) ==
            CompositionUploadRect{0u, 0u, 200u, 120u},
        "Legacy callers and zero-area damage must retain full-image uploads");
    damage.commit(0u);
    expect(damage.prepare(0u, 101u, 61u, 200u, 120u, {4u, 3u, 1u, 1u}) ==
            CompositionUploadRect{0u, 0u, 200u, 120u},
        "Changing the Vulkan source extent must invalidate every ring texture");

    // Test conservative integer scaling against the exact nearest source of
    // every destination pixel, including fractional downscales and upscales.
    for (std::uint32_t sourceWidth = 1u; sourceWidth < 20u; ++sourceWidth)
        for (std::uint32_t width = 1u; width < 27u; ++width)
            for (std::uint32_t x = 0u; x < sourceWidth; ++x)
            {
                CompositionUploadDamage scaled;
                scaled.prepare(0u, sourceWidth, 1u, width, 1u, {});
                scaled.commit(0u);
                const auto rect = scaled.prepare(0u, sourceWidth, 1u, width, 1u,
                    {x, 0u, 1u, 1u});
                for (std::uint32_t pixel = 0u; pixel < width; ++pixel)
                    if (pixel * sourceWidth / width == x)
                        expect(pixel >= rect.x && pixel < rect.x + rect.width,
                            "Scaled damage must include every pixel sampling a changed source texel");
            }
}

void check_retained_frames()
{
    constexpr std::uint32_t sourceWidth = 101u, sourceHeight = 63u;
    constexpr std::size_t sourcePitch = sourceWidth * 4u + 11u;
    for (const auto extent : {CompositionUploadRect{0u, 0u, 101u, 63u},
             CompositionUploadRect{0u, 0u, 177u, 107u},
             CompositionUploadRect{0u, 0u, 59u, 37u}})
    {
        const auto width = extent.width, height = extent.height;
        const auto pitch = static_cast<std::size_t>(width) * 4u;
        std::vector<std::uint8_t> source(sourcePitch * sourceHeight, 0u);
        std::vector<std::uint8_t> cache(pitch * height, 0u), expected(cache.size(), 0u);
        std::array<std::vector<std::uint8_t>, 3u> textures;
        for (auto& texture : textures) texture.resize(cache.size(), 0xddu);
        CompositionUploadPixels converter;
        CompositionUploadDamage damage;
        for (std::uint32_t frame = 0u; frame < 90u; ++frame)
        {
            const auto index = frame % 3u;
            const CompositionUploadRect changed {12u + frame % 11u, 19u, 7u, 3u};
            // Alternate opaque paint and fully cleared pixels so stale alpha and
            // removed content cannot survive the rotation.
            for (auto y = changed.y; y < changed.y + changed.height; ++y)
                for (auto x = changed.x; x < changed.x + changed.width; ++x)
                    for (std::uint32_t channel = 0u; channel < 4u; ++channel)
                        source[y * sourcePitch + x * 4u + channel] =
                            frame % 2u ? 0u : static_cast<std::uint8_t>(frame + channel * 31u);
            const auto rect = damage.prepare(index,
                sourceWidth, sourceHeight, width, height, changed);
            converter.convert(cache.data(), pitch, width, height,
                source.data(), sourcePitch, sourceWidth, sourceHeight, rect);
            for (auto y = rect.y; y < rect.y + rect.height; ++y)
                std::memcpy(textures[index].data() + y * pitch + rect.x * 4u,
                    cache.data() + y * pitch + rect.x * 4u, rect.width * 4u);
            damage.commit(index);
            reference_convert(expected, pitch, width, height, source, sourcePitch,
                sourceWidth, sourceHeight, {0u, 0u, width, height});
            expect(textures[index] == expected,
                "Partial rotating uploads must reproduce the complete latest image including erased pixels");
        }
    }
}

void benchmark()
{
    constexpr std::uint32_t width = 2560u, height = 1440u, frames = 300u;
    const std::size_t pitch = width * 4u;
    std::vector<std::uint8_t> source(pitch * height, 0x3fu), destination(source.size());
    CompositionUploadPixels converter;
    const auto timed = [&](CompositionUploadRect rect) {
        const auto start = std::chrono::steady_clock::now();
        for (unsigned frame = 0u; frame < frames; ++frame)
        {
            source[0] = static_cast<std::uint8_t>(frame);
            converter.convert(destination.data(), pitch, width, height,
                source.data(), pitch, width, height, rect);
        }
        return std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - start).count() / frames;
    };
    const auto full = timed({0u, 0u, width, height});
    const auto small = timed({1100u, 16u, 200u, 90u});
    std::cout << "Host BGRA conversion at 2560x1440: full " << full
              << " ms/frame; 200x90 update " << small << " ms/frame; bytes "
              << source.size() << " -> " << 200u * 90u * 4u << '\n';
}
}

int main(int argc, char** argv)
{
    check_pixels();
    check_damage();
    check_retained_frames();
    if (argc > 1 && std::string_view(argv[1]) == "--benchmark") benchmark();
    if (!failures) std::cout << "Composition upload checks passed\n";
    return failures ? 1 : 0;
}

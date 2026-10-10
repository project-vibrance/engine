#pragma once

#include <span>
#include <stdexcept>
#include <vector>
#include <cstdint>
#include <stb_image.h>

namespace vibrance_text
{
    struct ColourBitmap
    {
        uint32_t width = 0, height = 0;
        int left = 0, top = 0;
        float advance = 0;
        std::vector<unsigned char> rgba;
    };

    // CBDT PNGs are decoded through the engine's existing image decoder. This
    // also works with FreeType builds that deliberately omit libpng support.
    struct FontBytes
    {
        std::span<const unsigned char> bytes;
        uint8_t byte(std::size_t at) const
        {
            if (at >= bytes.size()) throw std::out_of_range("Font table offset");
            return bytes[at];
        }
        uint16_t u16(std::size_t at) const { return (uint16_t(byte(at)) << 8) | byte(at + 1); }
        uint32_t u32(std::size_t at) const { return (uint32_t(u16(at)) << 16) | u16(at + 2); }
        FontBytes slice(std::size_t at, std::size_t size) const
        {
            if (at > bytes.size() || size > bytes.size() - at)
                throw std::out_of_range("Font table length");
            return { bytes.subspan(at, size) };
        }
        FontBytes table(uint32_t tag, unsigned faceIndex = 0) const
        {
            std::size_t directory = 0;
            if (u32(0) == 0x74746366u)
            {
                if (faceIndex >= u32(8)) return {};
                directory = u32(12 + faceIndex * 4);
            }
            for (unsigned i = 0; i < u16(directory + 4); ++i)
            {
                const auto entry = directory + 12 + i * 16;
                if (u32(entry) == tag) return slice(u32(entry + 8), u32(entry + 12));
            }
            return {};
        }
    };

    inline bool load_cbdt_bitmap(std::span<const unsigned char> font, unsigned faceIndex,
        uint32_t glyph, unsigned pixelHeight, ColourBitmap& result)
    {
        try
        {
            const FontBytes file { font };
            const auto locations = file.table(0x43424c43u, faceIndex);
            const auto data = file.table(0x43424454u, faceIndex);
            if (locations.bytes.empty() || data.bytes.empty()) return false;
            std::size_t strike = 0;
            unsigned difference = ~0u;
            for (unsigned i = 0; i < locations.u32(4); ++i)
            {
                const auto entry = 8 + i * 48;
                const unsigned height = locations.byte(entry + 45);
                const unsigned distance = height > pixelHeight ? height - pixelHeight : pixelHeight - height;
                if (glyph >= locations.u16(entry + 40) && glyph <= locations.u16(entry + 42) && distance < difference)
                { strike = entry; difference = distance; }
            }
            if (!strike) return false;
            const auto array = locations.u32(strike);
            for (unsigned i = 0; i < locations.u32(strike + 8); ++i)
            {
                const auto entry = array + i * 8;
                const auto first = locations.u16(entry), last = locations.u16(entry + 2);
                if (glyph < first || glyph > last) continue;
                const auto subtable = array + locations.u32(entry + 4);
                const auto indexFormat = locations.u16(subtable);
                const auto imageFormat = locations.u16(subtable + 2);
                std::size_t offset = locations.u32(subtable + 4), length = 0;
                const auto item = glyph - first;
                if (indexFormat == 1 || indexFormat == 3)
                {
                    const auto step = indexFormat == 1 ? 4u : 2u;
                    const auto read = [&](std::size_t at) -> uint32_t {
                        return step == 4 ? locations.u32(at) : locations.u16(at);
                    };
                    const auto start = read(subtable + 8 + item * step);
                    const auto end = read(subtable + 8 + (item + 1) * step);
                    if (end < start) return false;
                    offset += start; length = end - start;
                }
                else if (indexFormat == 2)
                { length = locations.u32(subtable + 8); offset += item * length; }
                else if (indexFormat == 4 || indexFormat == 5)
                {
                    const auto count = locations.u32(subtable + (indexFormat == 4 ? 8 : 20));
                    bool found = false;
                    for (unsigned j = 0; j < count; ++j)
                    {
                        const auto at = subtable + (indexFormat == 4 ? 12 + j * 4 : 24 + j * 2);
                        if (locations.u16(at) != glyph) continue;
                        if (indexFormat == 4)
                        {
                            const auto start = locations.u16(at + 2), end = locations.u16(at + 6);
                            if (end < start) return false;
                            offset += start; length = end - start;
                        }
                        else { length = locations.u32(subtable + 8); offset += j * length; }
                        found = true; break;
                    }
                    if (!found) return false;
                }
                else return false;
                if (!length) return false;
                const auto image = data.slice(offset, length);
                unsigned metricsSize = 0;
                FontBytes metrics;
                if (imageFormat == 17 || imageFormat == 18)
                { metrics = image; metricsSize = imageFormat == 17 ? 5 : 8; }
                else if (imageFormat == 19 && (indexFormat == 2 || indexFormat == 5))
                    metrics = locations.slice(subtable + 12, 8);
                else return false;
                const auto pngLength = image.u32(metricsSize);
                const auto png = image.slice(metricsSize + 4, pngLength);
                if (pngLength > 0x7fffffffu) return false;
                int width = 0, height = 0, channels = 0;
                auto* pixels = stbi_load_from_memory(png.bytes.data(), int(pngLength), &width, &height, &channels, 4);
                if (!pixels) return false;
                result.width = uint32_t(width); result.height = uint32_t(height);
                result.left = int8_t(metrics.byte(2)); result.top = int8_t(metrics.byte(3));
                result.advance = metrics.byte(4);
                result.rgba.assign(pixels, pixels + std::size_t(width) * height * 4);
                stbi_image_free(pixels);
                // Keep all atlas colours premultiplied for correct filtering.
                for (std::size_t p = 0; p < result.rgba.size(); p += 4)
                    for (unsigned c = 0; c < 3; ++c)
                        result.rgba[p + c] = (unsigned(result.rgba[p + c]) * result.rgba[p + 3] + 127) / 255;
                return true;
            }
        }
        catch (const std::out_of_range&) { return false; }
        return false;
    }
}

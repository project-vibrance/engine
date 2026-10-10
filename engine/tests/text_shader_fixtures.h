#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#if defined(VIBRANCE_TEXT_FREETYPE)
#include <ft2build.h>
#include FT_FREETYPE_H
#endif

struct TextPushConstants
{
    std::array<float, 4> rect, uvRect, colour0, colour1, colour2, effect0, effect1;
    std::array<std::uint32_t, 4> data;
};
static_assert(sizeof(TextPushConstants) == 128u);

struct TextGlyphRegion
{
    std::uint32_t x, y, width, height;
};

struct TextAtlasFixture
{
    static constexpr std::uint32_t side = 512u;
    // Four successive scalar distance bytes become the RGBA channels of one
    // storage texel, exactly as font_atlas.cpp packs the engine image.
    std::vector<std::uint8_t> bytes = std::vector<std::uint8_t>(side * side, 0u);
    std::vector<TextGlyphRegion> glyphs;
    TextGlyphRegion colourGlyph { 104u, 450u, 24u, 26u };
    std::string source;
};

struct TextFixture
{
    std::string name;
    TextPushConstants constants;
    std::uint32_t originX = 0u, originY = 0u, dispatchWidth = 0u, dispatchHeight = 0u;
    bool benchmark = false;
};

struct TextCropFixture
{
    TextFixture fixture;
    std::array<float, 2> atlasGlyphSize;
    float sdfPadding = 24.0f;
    bool trustedUv = true;
};

namespace text_fixture_detail
{
    // These transforms and the alpha correction mirror font_atlas.cpp. Keep
    // actual font rasterisation separate from shader coverage and filtering.
    inline void distance_transform_1d(const std::vector<float>& source,
        std::vector<float>& distances, std::uint32_t count)
    {
        constexpr float inf = 1.0e20f;
        std::vector<std::uint32_t> locations(count, 0u);
        std::vector<float> boundaries(count + 1u, 0.0f);
        std::uint32_t k = 0u;
        boundaries[0] = -inf;
        boundaries[1] = inf;
        for (std::uint32_t q = 1u; q < count; ++q)
        {
            float intersection;
            for (;;)
            {
                const float qf = static_cast<float>(q), lf = static_cast<float>(locations[k]);
                intersection = ((source[q] + qf * qf) - (source[locations[k]] + lf * lf)) /
                    (2.0f * qf - 2.0f * lf);
                if (intersection > boundaries[k] || k == 0u) break;
                --k;
            }
            if (intersection <= boundaries[k])
            {
                locations[0] = q;
                boundaries[0] = -inf;
                boundaries[1] = inf;
                k = 0u;
            }
            else
            {
                locations[++k] = q;
                boundaries[k] = intersection;
                boundaries[k + 1u] = inf;
            }
        }
        k = 0u;
        for (std::uint32_t q = 0u; q < count; ++q)
        {
            while (boundaries[k + 1u] < static_cast<float>(q)) ++k;
            const float distance = static_cast<float>(q) - static_cast<float>(locations[k]);
            distances[q] = distance * distance + source[locations[k]];
        }
    }

    inline std::vector<float> distance_transform_2d(const std::vector<float>& source,
        std::uint32_t width, std::uint32_t height)
    {
        std::vector<float> temporary(source.size()), distances(source.size());
        std::vector<float> line(std::max(width, height)), lineDistances(line.size());
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            for (std::uint32_t y = 0u; y < height; ++y) line[y] = source[y * width + x];
            distance_transform_1d(line, lineDistances, height);
            for (std::uint32_t y = 0u; y < height; ++y) temporary[y * width + x] = lineDistances[y];
        }
        for (std::uint32_t y = 0u; y < height; ++y)
        {
            for (std::uint32_t x = 0u; x < width; ++x) line[x] = temporary[y * width + x];
            distance_transform_1d(line, lineDistances, width);
            for (std::uint32_t x = 0u; x < width; ++x) distances[y * width + x] = lineDistances[x];
        }
        return distances;
    }

    inline std::vector<std::uint8_t> make_sdf(const std::vector<std::uint8_t>& alpha,
        std::uint32_t width, std::uint32_t height)
    {
        constexpr float inf = 1.0e20f;
        std::vector<float> inside(alpha.size(), inf), outside(alpha.size(), inf);
        for (std::size_t i = 0u; i < alpha.size(); ++i)
            (alpha[i] >= 128u ? inside : outside)[i] = 0.0f;
        const auto toInside = distance_transform_2d(inside, width, height);
        const auto toOutside = distance_transform_2d(outside, width, height);
        std::vector<std::uint8_t> result(alpha.size());
        for (std::size_t i = 0u; i < alpha.size(); ++i)
        {
            float distance = std::sqrt(toOutside[i]) - std::sqrt(toInside[i]);
            if (alpha[i] != 0u && alpha[i] != 255u) distance += alpha[i] / 255.0f - 0.5f;
            result[i] = static_cast<std::uint8_t>(std::clamp(std::round(180.0f + distance * 7.5f), 0.0f, 255.0f));
        }
        return result;
    }
}

inline TextAtlasFixture text_atlas_fixture(const char* fontPath = nullptr, bool includeBoundaryProbes = true)
{
    TextAtlasFixture atlas;
    std::uint32_t cursorX = 1u, cursorY = 1u, rowHeight = 0u;
    const auto append = [&](const std::vector<std::uint8_t>& sdf, std::uint32_t width, std::uint32_t height) {
        if (cursorX + width + 1u >= atlas.side)
        {
            cursorX = 1u;
            cursorY += rowHeight + 1u;
            rowHeight = 0u;
        }
        if (cursorY + height + 1u >= 450u) throw std::runtime_error("Text fixture glyph atlas is full");
        atlas.glyphs.push_back({ cursorX, cursorY, width, height });
        for (std::uint32_t y = 0u; y < height; ++y)
            std::copy_n(sdf.data() + y * width, width, atlas.bytes.data() + (cursorY + y) * atlas.side + cursorX);
        cursorX += width + 1u;
        rowHeight = std::max(rowHeight, height);
    };
#if defined(VIBRANCE_TEXT_FREETYPE)
    if (!fontPath || !*fontPath) throw std::runtime_error("Text FreeType fixtures require a font path");
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) throw std::runtime_error("Cannot initialise fixture FreeType");
    FT_Face face = nullptr;
    if (FT_New_Face(library, fontPath, 0, &face) != 0)
    {
        FT_Done_FreeType(library);
        throw std::runtime_error("Cannot load fixture font");
    }
    if (FT_Set_Pixel_Sizes(face, 0u, 96u) != 0) throw std::runtime_error("Cannot set fixture font size");
    for (unsigned char character : std::string("OgeWAi@#"))
    {
        if (FT_Load_Glyph(face, FT_Get_Char_Index(face, character), FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0 ||
            FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
            throw std::runtime_error("Cannot rasterise fixture glyph");
        const FT_Bitmap& bitmap = face->glyph->bitmap;
        const std::uint32_t width = bitmap.width + 48u, height = bitmap.rows + 48u;
        std::vector<std::uint8_t> alpha(width * height, 0u);
        if (bitmap.pixel_mode != FT_PIXEL_MODE_GRAY) throw std::runtime_error("Fixture font is not greyscale");
        for (std::uint32_t y = 0u; y < bitmap.rows; ++y)
        {
            const auto* row = bitmap.pitch >= 0 ? bitmap.buffer + y * bitmap.pitch :
                bitmap.buffer + (bitmap.rows - 1u - y) * -bitmap.pitch;
            for (std::uint32_t x = 0u; x < bitmap.width; ++x)
                alpha[(y + 24u) * width + x + 24u] = bitmap.num_grays == 256u ? row[x] :
                    static_cast<std::uint8_t>(row[x] * 255u / (bitmap.num_grays - 1u));
        }
        append(text_fixture_detail::make_sdf(alpha, width, height), width, height);
    }
    FT_Done_Face(face);
    FT_Done_FreeType(library);
    atlas.source = std::string("FreeType FontAtlas bake (96 px, padding 24, edge 180): ") + fontPath;
#else
    (void)fontPath;
    // The dependency-free path uses actual signed-distance fields, including
    // curves and stems. Supply FreeType parameters for engine-font coverage.
    for (std::uint32_t glyph = 0u; glyph < 8u; ++glyph)
    {
        constexpr std::uint32_t width = 101u, height = 121u;
        std::vector<std::uint8_t> alpha(width * height, 0u);
        for (std::uint32_t y = 24u; y < height - 24u; ++y)
            for (std::uint32_t x = 24u; x < width - 24u; ++x)
            {
                const float px = (x - 50.0f) / 24.0f, py = (y - 60.0f) / 35.0f;
                const float ring = std::abs(std::sqrt(px * px + py * py) - 0.8f);
                const bool ink = ring < 0.15f || (glyph % 2u && x < 31u);
                alpha[y * width + x] = ink ? 255u : 0u;
            }
        append(text_fixture_detail::make_sdf(alpha, width, height), width, height);
    }
    atlas.source = "synthetic signed-distance glyphs (use FreeType parameters for font fixtures)";
#endif
    if (includeBoundaryProbes)
    {
        // Probe arbitrary atlas edges for shader equivalence. Crop fixtures
        // retain the generated bake's untouched zero-filled neighbouring texels.
        for (std::uint32_t y = 450u; y < atlas.side; ++y)
            for (std::uint32_t x = 0u; x < atlas.side; ++x)
                atlas.bytes[y * atlas.side + x] = static_cast<std::uint8_t>(
                    116u + ((x * 37u + y * 13u + (x & 3u) * 19u) % 128u));
        for (std::uint32_t y = 0u; y < atlas.side; ++y)
            for (std::uint32_t x : { 0u, atlas.side - 1u })
                atlas.bytes[y * atlas.side + x] = static_cast<std::uint8_t>(158u + (y * 17u % 64u));
        for (std::uint32_t x = 0u; x < atlas.side; ++x)
            atlas.bytes[x] = static_cast<std::uint8_t>(151u + (x * 23u % 76u));
        constexpr std::array<std::uint8_t, 7> bandValues { 171u, 175u, 179u, 180u, 181u, 185u, 189u };
        for (std::uint32_t band = 0u; band < bandValues.size(); ++band)
            for (std::uint32_t y = 480u + band * 4u; y < 484u + band * 4u; ++y)
            {
                for (std::uint32_t x = 64u; x < 192u; ++x) atlas.bytes[y * atlas.side + x] = bandValues[band];
                for (std::uint32_t x = 256u; x < 384u; ++x)
                    atlas.bytes[y * atlas.side + x] = static_cast<std::uint8_t>(bandValues[band] +
                        static_cast<int>((x & 3u) * 2u) - 3);
            }
    }
    // Colour bitmaps use physical RGBA texels and premultiplied channels in
    // this same image. Vary colour and alpha independently to expose indexing,
    // filtering and sixteen-sample coverage errors in the colour glyph path.
    const auto& colourGlyph = atlas.colourGlyph;
    for (std::uint32_t y = 0u; y < colourGlyph.height; ++y)
        for (std::uint32_t x = 0u; x < colourGlyph.width; ++x)
        {
            const std::uint32_t alpha = x == 0u || y == 0u || x + 1u == colourGlyph.width ||
                y + 1u == colourGlyph.height ? 0u : 31u + (x * 47u + y * 29u) % 225u;
            const std::array<std::uint32_t, 3> colour {
                (x * 71u + y * 13u) % 256u, (x * 19u + y * 83u) % 256u,
                (x * 43u + y * 31u) % 256u
            };
            auto* texel = atlas.bytes.data() + (colourGlyph.y + y) * atlas.side +
                (colourGlyph.x + x) * 4u;
            for (std::uint32_t channel = 0u; channel < 3u; ++channel)
                texel[channel] = static_cast<std::uint8_t>((colour[channel] * alpha + 127u) / 255u);
            texel[3] = static_cast<std::uint8_t>(alpha);
        }
    return atlas;
}

inline std::vector<TextFixture> text_fixtures(const TextAtlasFixture& atlas)
{
    constexpr std::uint32_t gradient = 1u, outline = 1u << 1u, shadow = 1u << 3u;
    constexpr std::uint32_t glow = 1u << 4u, blur = 1u << 5u, atlasText = 1u << 8u;
    constexpr std::uint32_t colourGlyph = 1u << 10u;
    constexpr std::uint32_t shapeMask = 1u << 15u, edgeFade = 1u << 25u;
    const auto uv = [](TextGlyphRegion region) {
        return std::array<float, 4> { region.x / 512.0f, region.y / 512.0f,
            (region.x + region.width) / 512.0f, (region.y + region.height) / 512.0f };
    };
    const auto& glyph = atlas.glyphs.front();
    const TextPushConstants base {
        { 24.35f, 22.65f, glyph.width * 0.63f, glyph.height * 0.63f }, uv(glyph),
        { 1.0f, 1.0f, 1.0f, 0.73f }, { 0.83f, 0.16f, 0.39f, 0.58f },
        { 0.13f, 0.72f, 0.37f, 0.61f }, { 24.0f, 0.0f, 0.0f, 0.87f },
        { 0.0f, 0.0f, 0.0f, 0.0f }, { 0u, atlasText, 2048u << 2u, 0u }
    };
    std::vector<TextFixture> fixtures;
    const auto append = [&](std::string name, TextPushConstants value, bool benchmark = false) {
        fixtures.push_back({ std::move(name), value, 0u, 0u, 0u, 0u, benchmark });
    };
    append("atlas_white_fractional", base, true);
    auto value = base;
    value.colour0 = { 0.0f, 0.0f, 0.0f, 0.79f };
    append("atlas_black_fractional", value, true);
    value.colour0 = { 0.17f, 0.38f, 0.91f, 0.43f };
    append("atlas_arbitrary_rgb", value, true);
    value.colour0 = { 1.0f, 0.0f, 1.0f, 0.43f };
    append("atlas_binary_rgb", value);
    for (const auto& colour : { std::array { 1.0f, 1.0f, 1.0f, 0.73f },
        std::array { 0.0f, 0.0f, 0.0f, 0.73f }, std::array { 0.17f, 0.38f, 0.91f, 0.43f } })
    {
        value = base;
        value.rect = { 25.137f, 24.893f, 20.37f, 30.17f };
        value.colour0 = colour;
        append("small_glyph_" + std::to_string(fixtures.size()), value, true);
    }
    for (std::size_t i = 0u; i < atlas.glyphs.size(); ++i)
        for (float scale : { 0.1875f, 0.4137f, 1.137f })
        {
            value = base;
            value.uvRect = uv(atlas.glyphs[i]);
            value.rect = { 9.137f + static_cast<float>(i % 4u) * 0.217f, 8.893f,
                atlas.glyphs[i].width * scale, atlas.glyphs[i].height * scale };
            append("glyph_" + std::to_string(i) + "_scale_" + std::to_string(scale), value);
        }
    for (std::uint32_t channel = 0u; channel < 4u; ++channel)
    {
        value = base;
        value.uvRect = { (64.125f + channel) / 512.0f, 461.125f / 512.0f,
            (109.713f + channel) / 512.0f, 499.793f / 512.0f };
        append("packed_boundary_channel_" + std::to_string(channel), value);
    }
    for (const auto& bounds : { std::array { -0.07f, -0.03f, 0.023f, 0.027f },
        std::array { 0.969f, 0.957f, 1.04f, 1.02f }, std::array { 0.0f, 0.91f, 1.0f, 1.0f } })
    {
        value = base;
        value.uvRect = bounds;
        append("atlas_clamped_" + std::to_string(fixtures.size()), value);
    }
    for (std::uint32_t band = 0u; band < 7u; ++band)
        for (int weight : { -513, -185, -184, -183, -1, 0, 1, 183, 184, 185, 513 })
            for (std::uint32_t pattern = 0u; pattern < 4u; ++pattern)
            {
                value = base;
                const float startX = pattern == 1u ? 257.137f : 65.137f;
                const float spanX = pattern == 3u ? 119.173f : 11.317f;
                const float startY = 480.137f + band * 4.0f;
                value.uvRect = { startX / 512.0f, startY / 512.0f,
                    (startX + spanX) / 512.0f, (startY + 3.173f) / 512.0f };
                value.rect = { 13.137f, 12.793f, pattern == 3u ? 7.313f : 41.373f,
                    pattern == 3u ? 3.137f : 31.173f };
                value.data[2] = static_cast<std::uint32_t>(2048 + weight) << 2u;
                if (pattern == 2u)
                {
                    std::swap(value.uvRect[0], value.uvRect[2]);
                    std::swap(value.uvRect[1], value.uvRect[3]);
                }
                value.colour0 = { 0.17f, 0.381372549f, 0.91f, 0.43f };
                if (weight == 1 || weight == -1) value.effect0[3] = 0.0010037f;
                append("distance_band_" + std::to_string(band) + "_weight_" + std::to_string(weight) +
                    "_pattern_" + std::to_string(pattern), value);
            }
    for (const auto& bounds : { std::array { -0.0237f, -0.0173f, 0.0019f, 0.0031f },
        std::array { 0.9973f, 0.9931f, 1.0373f, 1.0319f },
        std::array { 1.03f, 1.07f, 0.973f, 0.917f }, std::array { 0.0f, 0.0f, 1.0f, 1.0f } })
    {
        value = base;
        value.uvRect = bounds;
        value.rect = { 1.137f, 2.791f, 13.173f, 9.137f };
        append("coverage_bounds_clamp_" + std::to_string(fixtures.size()), value);
    }
    value = base;
    value.data[1] |= gradient;
    append("atlas_gradient", value, true);
    value = base;
    value.data[1] |= outline;
    value.effect0[1] = 2.73f;
    append("atlas_outline", value, true);
    value.data[1] |= glow;
    value.effect0[2] = 6.35f;
    append("atlas_outline_glow", value, true);
    value = base;
    value.data[1] |= shadow;
    value.effect1 = { 3.17f, -2.83f, 4.73f, 0.0f };
    append("atlas_shadow", value, true);
    value.data[1] |= glow | outline | gradient;
    value.effect0[1] = 1.73f;
    value.effect0[2] = 7.31f;
    append("atlas_all_effects", value, true);
    for (std::uint32_t pass : { 1u, 2u, 3u })
    {
        value.data[2] = (2048u << 2u) | pass;
        append("atlas_effects_pass_" + std::to_string(pass), value);
    }
    value = base;
    value.data[1] |= blur;
    value.effect1[3] = 5.73f;
    append("atlas_foreground_blur", value, true);
    value.effect1[3] = 0.5f;
    append("atlas_blur_threshold", value);
    value.effect1[3] = 40.0f;
    append("atlas_blur_maximum", value);
    for (int weight : { -701, -32, 0, 479, 1907 })
    {
        value = base;
        value.data[2] = static_cast<std::uint32_t>(2048 + weight) << 2u;
        append("atlas_weight_" + std::to_string(weight), value);
    }
    for (std::uint32_t mode : { 1u, 2u, 3u })
    {
        value = base;
        value.data[1] |= edgeFade;
        value.data[2] |= (137u << 14u) | (225u << 22u) | (mode << 30u);
        append("atlas_edge_fade_" + std::to_string(mode), value);
    }
    for (std::uint32_t primitive : { 1u, 2u, 3u, 6u, 7u })
    {
        value = base;
        value.data[1] |= shapeMask;
        value.colour2 = { static_cast<float>(primitive), 13.73f, 0.87f, 4.35f };
        if (primitive == 7u) value.colour2 = { 7.0f, 14.3f, static_cast<float>(13u | (11u << 4u) | (15u << 8u)), 37.7f };
        value.effect1 = { 38.41f, 33.79f, 42.73f, 44.17f };
        append("atlas_inherited_mask_" + std::to_string(primitive), value, primitive == 6u);
    }
    for (std::uint32_t i = 0u; i < 16u; ++i)
    {
        value = base;
        value.colour0 = { (i * 13u + 0.5f) / 255.0f, (i * 11u + 0.5f) / 255.0f,
            (i * 7u + 0.5f) / 255.0f, (i + 1.0f) / 17.0f };
        value.effect0[3] = (i + 1.0f) / 19.0f;
        append("atlas_quantisation_boundary_" + std::to_string(i), value);
    }
    for (std::uint32_t channels = 0u; channels < 8u; ++channels)
        for (float opacity : { 0.0000999f, 0.0001001f, 0.0009999f, 0.0010001f, 0.00137f, 0.00373f })
        {
            value = base;
            value.colour0 = { static_cast<float>(channels & 1u), static_cast<float>((channels >> 1u) & 1u),
                static_cast<float>((channels >> 2u) & 1u), 1.0f };
            value.effect0[3] = opacity;
            append("binary_alpha_threshold_" + std::to_string(channels) + "_" + std::to_string(opacity), value);
        }
    std::uint32_t randomState = 0x9e3779b9u;
    const auto random_unit = [&]() {
        randomState ^= randomState << 13u;
        randomState ^= randomState >> 17u;
        randomState ^= randomState << 5u;
        return static_cast<float>(randomState & 65535u) / 65535.0f;
    };
    for (std::uint32_t i = 0u; i < 2048u; ++i)
    {
        value = base;
        const auto region = atlas.glyphs[i % atlas.glyphs.size()];
        const float scale = 0.117f + random_unit() * 1.013f;
        value.rect = { 3.0f + random_unit() * 47.0f, 2.0f + random_unit() * 31.0f,
            region.width * scale, region.height * scale };
        value.uvRect = uv(region);
        value.colour0 = i < 1024u ? std::array<float, 4> { static_cast<float>(i & 1u),
            static_cast<float>((i >> 1u) & 1u), static_cast<float>((i >> 2u) & 1u), random_unit() } :
            std::array<float, 4> { (static_cast<float>(i * 71u % 255u) + 0.5f) / 255.0f,
                random_unit(), (static_cast<float>(i * 43u % 255u) + 0.5f) / 255.0f, random_unit() };
        value.effect0[3] = i % 7u == 0u ? 0.0008f + random_unit() * 0.004f : random_unit();
        value.data[2] = (static_cast<std::uint32_t>(1536.0f + random_unit() * 1023.0f) << 2u) |
            (i % 9u == 0u ? 2u : 0u);
        if (i % 3u != 0u)
        {
            value.data[1] |= edgeFade;
            const std::uint32_t start = 112u + i % 79u, end = 192u + i % 63u;
            value.data[2] |= (start << 14u) | (end << 22u) | ((1u + i % 2u) << 30u);
        }
        if (i % 4u != 0u)
        {
            value.data[1] |= shapeMask;
            constexpr std::array<std::uint32_t, 5> primitives { 1u, 2u, 3u, 6u, 7u };
            const auto primitive = primitives[i % primitives.size()];
            value.colour2 = { static_cast<float>(primitive), 2.0f + random_unit() * 23.0f,
                random_unit(), 2.0f + random_unit() * 3.0f };
            if (primitive == 7u)
            {
                value.colour2[2] = static_cast<float>((i % 16u) | ((i * 7u % 16u) << 4u) |
                    ((i * 11u % 16u) << 8u));
                value.colour2[3] = 7.0f + random_unit() * 59.0f;
            }
            value.effect1 = { value.rect[0] + value.rect[2] * 0.07f,
                value.rect[1] + value.rect[3] * 0.09f, value.rect[2] * 0.83f, value.rect[3] * 0.79f };
        }
        append("random_plain_mask_fade_" + std::to_string(i), value);
    }
    value = base;
    value.colour0[3] = 0.0f;
    append("atlas_zero_alpha", value);
    value = base;
    value.effect0[3] = 0.0f;
    append("atlas_zero_opacity", value);
    value = base;
    value.data[1] |= colourGlyph;
    const auto& colourRegion = atlas.colourGlyph;
    value.uvRect = { colourRegion.x / 128.0f, colourRegion.y / 512.0f,
        (colourRegion.x + colourRegion.width) / 128.0f,
        (colourRegion.y + colourRegion.height) / 512.0f };
    value.colour0 = { 0.13f, 0.71f, 0.29f, 0.73f };
    const auto colourBase = value;
    for (float scale : { 0.173f, 0.4137f, 1.137f, 2.731f })
    {
        value = colourBase;
        value.rect = { 9.137f, 8.793f, colourRegion.width * scale, colourRegion.height * scale };
        append("colour_bitmap_scale_" + std::to_string(scale), value, true);
    }
    for (std::uint32_t pass : { 0u, 1u, 2u, 3u })
    {
        value = colourBase;
        value.data[2] |= pass;
        append("colour_bitmap_pass_" + std::to_string(pass), value);
    }
    for (std::uint32_t primitive : { 1u, 2u, 3u, 6u, 7u })
    {
        value = colourBase;
        value.data[1] |= shapeMask;
        value.colour2 = { static_cast<float>(primitive), 13.73f, 0.87f, 4.35f };
        if (primitive == 7u) value.colour2 = { 7.0f, 14.3f,
            static_cast<float>(13u | (11u << 4u) | (15u << 8u)), 37.7f };
        value.effect1 = { 38.41f, 33.79f, 42.73f, 44.17f };
        append("colour_bitmap_inherited_mask_" + std::to_string(primitive), value);
    }
    for (std::uint32_t mode : { 1u, 2u, 3u })
    {
        value = colourBase;
        value.data[1] |= edgeFade;
        value.data[2] |= (137u << 14u) | (225u << 22u) | (mode << 30u);
        append("colour_bitmap_edge_fade_" + std::to_string(mode), value);
    }
    for (float radius : { 0.5f, 2.731f, 13.137f, 40.0f })
    {
        value = colourBase;
        value.data[1] |= blur;
        value.effect1[3] = radius;
        append("colour_bitmap_blur_" + std::to_string(radius), value);
    }
    for (float opacity : { 0.0f, 0.0009999f, 0.0010001f, 0.0373f, 1.0f })
    {
        value = colourBase;
        value.effect0[3] = opacity;
        append("colour_bitmap_opacity_" + std::to_string(opacity), value);
    }
    for (const auto& bounds : { std::array { -0.0237f, -0.0173f, 0.0019f, 0.0031f },
        std::array { 0.9973f, 0.9931f, 1.0373f, 1.0319f },
        std::array { 1.03f, 1.07f, 0.973f, 0.917f }, std::array { 0.0f, 0.0f, 1.0f, 1.0f } })
    {
        value = colourBase;
        value.uvRect = bounds;
        value.rect = { 1.137f, 2.791f, 13.173f, 9.137f };
        append("colour_bitmap_uv_clamp_" + std::to_string(fixtures.size()), value);
    }
    for (int weight : { -2048, -701, -1, 1, 1907, 2047 })
    {
        value = colourBase;
        value.data[2] = (static_cast<std::uint32_t>(2048 + weight) << 2u) | 2u;
        append("colour_bitmap_weight_" + std::to_string(weight), value);
    }
    value = base;
    value.data[1] &= ~atlasText;
    value.rect = { 15.35f, 16.73f, 123.7f, 52.17f };
    value.uvRect = { 0.11f, 0.17f, 0.89f, 0.93f };
    append("pseudo_white", value, true);
    value.data[1] |= outline | glow | shadow | gradient;
    value.effect0 = { 9.73f, 2.17f, 5.31f, 0.61f };
    value.effect1 = { -2.37f, 3.13f, 4.17f, 0.0f };
    append("pseudo_all_effects", value);
    value = base;
    fixtures.push_back({ "partial_groups_origin", value, 29u, 25u, 53u, 47u, false });
    value.rect = { 167.73f, 108.19f, 37.37f, 28.73f };
    fixtures.push_back({ "partial_groups_screen_clip", value, 173u, 113u, 31u, 29u, false });
    return fixtures;
}

inline std::vector<TextCropFixture> text_crop_fixtures(const TextAtlasFixture& atlas)
{
    const auto fixtures = text_fixtures(atlas);
    const auto base = fixtures.front().constants;
    std::vector<TextCropFixture> result;
    const auto append = [&](std::string name, std::size_t glyphIndex, float scaleX, float scaleY,
        float range, int weight, float outline, std::uint32_t variant) {
        const auto& region = atlas.glyphs[glyphIndex];
        auto value = base;
        value.rect = { 29.137f + (variant % 5u) * 0.173f, 23.793f + (variant % 7u) * 0.191f,
            region.width * scaleX, region.height * scaleY };
        // Keep the visible bitmap edge in view even when padding is magnified
        // far beyond the framebuffer, and exercise negative clipped origins.
        if (scaleX > 2.0f) value.rect[0] -= 24.0f * scaleX;
        if (scaleY > 2.0f) value.rect[1] -= 24.0f * scaleY;
        if (variant % 11u == 0u) value.rect[0] -= 31.731f;
        if (variant % 13u == 0u) value.rect[1] -= 27.137f;
        value.uvRect = { region.x / 512.0f, region.y / 512.0f,
            (region.x + region.width) / 512.0f, (region.y + region.height) / 512.0f };
        value.effect0[0] = range;
        value.data[2] = (static_cast<std::uint32_t>(2048 + weight) << 2u) | 2u;
        value.colour0 = { (variant % 19u + 0.5f) / 19.0f, 0.381372549f,
            variant % 3u == 0u ? 1.0f : 0.173f, 0.37f + variant % 7u * 0.073f };
        if (outline > 0.0f)
        {
            value.data[1] |= 1u << 1u;
            value.effect0[1] = outline;
        }
        if (variant % 4u == 1u)
        {
            value.data[1] |= 1u << 15u;
            value.colour2 = { 6.0f, 5.73f, 0.83f, 4.13f };
            value.effect1 = { value.rect[0] + value.rect[2] * 0.13f,
                value.rect[1] + value.rect[3] * 0.09f,
                value.rect[2] * 0.83f, value.rect[3] * 0.79f };
        }
        if (variant % 4u == 2u)
        {
            value.data[1] |= 1u << 25u;
            value.data[2] |= (137u << 14u) | (225u << 22u) | (2u << 30u);
        }
        if (variant % 4u == 3u) value.data[1] |= 1u;
        result.push_back({ { std::move(name), value },
            { static_cast<float>(region.width), static_cast<float>(region.height) }, 24.0f });
    };
    const std::array<std::array<float, 2>, 15> scales {{
        { 0.0007f, 0.0007f }, { 0.0063f, 0.0063f }, { 0.0137f, 0.0137f },
        { 0.0731f, 0.0731f }, { 0.173f, 0.173f }, { 0.4137f, 0.4137f },
        { 0.731f, 0.731f }, { 1.137f, 1.137f }, { 2.731f, 2.731f },
        { 13.137f, 13.137f }, { 100.137f, 100.137f }, { 0.173f, 1.137f },
        { 2.137f, 0.1137f }, { 0.0137f, 0.731f }, { 1.137f, 0.0067f }
    }};
    std::uint32_t variant = 0u;
    for (std::size_t glyph = 0u; glyph < atlas.glyphs.size(); ++glyph)
        for (const auto& scale : scales)
            for (float range : { 4.0f, 8.0f, 24.0f, 48.0f })
            {
                append("crop_scale_" + std::to_string(variant), glyph, scale[0], scale[1], range,
                    variant % 3u == 0u ? 479 : variant % 3u == 1u ? -701 : 0,
                    variant % 5u == 0u ? 2.731f : 0.0f, variant);
                ++variant;
            }
    for (std::size_t glyph = 0u; glyph < atlas.glyphs.size(); ++glyph)
        for (float range : { 4.0f, 8.0f, 24.0f, 48.0f })
            for (int weight : { -2048, -701, -185, -1, 0, 1, 185, 1907, 2047 })
                for (float outline : { 0.0f, 0.731f, 8.137f, 47.731f })
                {
                    append("crop_weight_outline_" + std::to_string(variant), glyph, 0.731f, 1.137f,
                        range, weight, outline, variant);
                    ++variant;
                }
    std::uint32_t randomState = 0x5d47139bu;
    const auto random_unit = [&]() {
        randomState ^= randomState << 13u;
        randomState ^= randomState >> 17u;
        randomState ^= randomState << 5u;
        return static_cast<float>(randomState & 65535u) / 65535.0f;
    };
    for (std::uint32_t index = 0u; index < 512u; ++index)
    {
        append("crop_random_" + std::to_string(index), index % atlas.glyphs.size(),
            std::exp2(-7.0f + random_unit() * 11.0f), std::exp2(-7.0f + random_unit() * 11.0f),
            std::array { 4.0f, 8.0f, 24.0f, 48.0f }[index % 4u],
            static_cast<int>(random_unit() * 4095.0f) - 2048,
            index % 3u == 0u ? random_unit() * 13.0f : 0.0f, variant++);
    }
    for (std::uint32_t phase = 0u; phase < 32u; ++phase)
        for (int weight : { -1, 0, 1 })
            for (float opacity : { 0.00137f, 0.0373f, 1.0f })
            {
                append("crop_subpixel_quantisation_" + std::to_string(variant), phase % atlas.glyphs.size(),
                    0.4137f, 0.731f, 24.0f, weight, 0.0f, variant);
                ++variant;
                auto& value = result.back().fixture.constants;
                value.rect[0] = 19.0f + phase / 32.0f;
                value.rect[1] = 13.0f + ((phase * 13u) % 32u) / 32.0f;
                value.effect0[3] = opacity;
                value.colour0 = { 1.0f, 0.0f, 1.0f, 1.0f };
            }
    for (std::size_t glyph = 0u; glyph < atlas.glyphs.size(); ++glyph)
        for (std::uint32_t fallback = 0u; fallback < 8u; ++fallback)
        {
            append("crop_fallback_" + std::to_string(glyph) + "_" + std::to_string(fallback),
                glyph, 0.4137f, 0.731f, 24.0f, 0, 0.0f, variant++);
            auto& added = result.back();
            if (fallback == 0u) added.sdfPadding = 0.0f;
            if (fallback == 1u) added.atlasGlyphSize = { 0.0f, 0.0f };
            if (fallback == 2u) added.fixture.constants.rect[2] = 0.731f;
            if (fallback == 3u) added.fixture.constants.rect[3] = 0.731f;
            if (fallback == 4u)
            {
                added.fixture.constants.data[1] |= 1u << 5u;
                added.fixture.constants.effect1[3] = 3.137f;
            }
            if (fallback == 5u || fallback == 6u)
            {
                added.fixture.constants.data[2] = (added.fixture.constants.data[2] & ~3u) |
                    (fallback == 5u ? 0u : 1u);
                added.fixture.constants.data[1] |= (1u << 3u) | (1u << 4u);
                added.fixture.constants.effect0[2] = 7.31f;
                added.fixture.constants.effect1 = { 3.17f, -2.83f, 4.73f, 0.0f };
            }
            if (fallback == 7u)
            {
                added.trustedUv = false;
                added.fixture.constants.uvRect[0] += 0.003137f;
            }
        }
    for (const auto& fixture : fixtures)
        if ((fixture.constants.data[1] & (1u << 10u)) != 0u)
            result.push_back({ fixture, { 24.0f, 26.0f }, 0.0f });
    return result;
}

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "../src/renderer/common/font_positioning.h"
#include <array>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

namespace
{
using Bytes = std::vector<unsigned char>;

void expect(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void word(Bytes& bytes, std::uint16_t value)
{
    bytes.push_back(static_cast<unsigned char>(value >> 8u));
    bytes.push_back(static_cast<unsigned char>(value));
}

void long_word(Bytes& bytes, std::uint32_t value)
{
    word(bytes, static_cast<std::uint16_t>(value >> 16u));
    word(bytes, static_cast<std::uint16_t>(value));
}

void patch_word(Bytes& bytes, std::size_t offset, std::uint16_t value)
{
    bytes[offset] = static_cast<unsigned char>(value >> 8u);
    bytes[offset + 1u] = static_cast<unsigned char>(value);
}

void patch_long(Bytes& bytes, std::size_t offset, std::uint32_t value)
{
    patch_word(bytes, offset, static_cast<std::uint16_t>(value >> 16u));
    patch_word(bytes, offset + 2u, static_cast<std::uint16_t>(value));
}

void words(Bytes& bytes, std::initializer_list<std::uint16_t> values)
{
    for (const auto value : values) word(bytes, value);
}

Bytes font_file(Bytes pairs, Bytes tracking)
{
    Bytes result;
    long_word(result, 0x00010000u);
    words(result, { 2u, 0u, 0u, 0u });
    long_word(result, 0x47504f53u); long_word(result, 0u);
    long_word(result, 44u); long_word(result, static_cast<std::uint32_t>(pairs.size()));
    long_word(result, 0x7472616bu); long_word(result, 0u);
    long_word(result, 44u + static_cast<std::uint32_t>(pairs.size()));
    long_word(result, static_cast<std::uint32_t>(tracking.size()));
    result.insert(result.end(), pairs.begin(), pairs.end());
    result.insert(result.end(), tracking.begin(), tracking.end());
    return result;
}

Bytes pair_positioning(bool extension = false)
{
    Bytes explicitPair;
    words(explicitPair, { 1u, 18u, 4u, 0u, 1u, 12u, 1u, 20u, std::uint16_t(-160), 1u, 1u, 10u });
    Bytes classes;
    words(classes, { 2u, 24u, 4u, 0u, 34u, 42u, 2u, 2u, 0u, 0u, 0u, std::uint16_t(-270),
        2u, 1u, 11u, 11u, 0u, 1u, 11u, 1u, 1u, 2u, 1u, 21u, 21u, 1u });
    Bytes result;
    words(result, { 1u, 0u, 0u, 10u, 24u, 1u });
    long_word(result, 0x6b65726eu);
    words(result, { 8u, 0u, 1u, 0u, 1u, 4u });
    words(result, { std::uint16_t(extension ? 9u : 2u), 8u, 2u, 10u,
        std::uint16_t(10u + explicitPair.size() + (extension ? 8u : 0u)) });
    if (extension) { words(result, { 1u, 2u }); long_word(result, 8u); }
    result.insert(result.end(), explicitPair.begin(), explicitPair.end());
    if (extension) { words(result, { 1u, 2u }); long_word(result, 8u); }
    result.insert(result.end(), classes.begin(), classes.end());
    return result;
}

Bytes tracking_table()
{
    Bytes result;
    long_word(result, 0x00010000u);
    words(result, { 0u, 12u, 0u, 0u, 3u, 2u });
    long_word(result, 44u);
    long_word(result, std::uint32_t(-65536)); words(result, { 0u, 52u });
    long_word(result, 0u); words(result, { 0u, 56u });
    long_word(result, 2u * 65536u); words(result, { 0u, 60u });
    long_word(result, 13u * 65536u); long_word(result, 80u * 65536u);
    words(result, { 52u, std::uint16_t(-28), 80u, 0u, 60u, std::uint16_t(-44) });
    return result;
}

void synthetic_tests()
{
    const auto bytes = font_file(pair_positioning(), tracking_table());
    const vibrance_text::FontPositioning parsed(bytes);
    expect(parsed.has_pair_adjustments() && parsed.pair_adjustment(10u, 20u) == -160 &&
        parsed.pair_adjustment(11u, 21u) == -270 && parsed.pair_adjustment(10u, 21u) == 0,
        "Explicit pairs and class pairs must resolve their authored x advances");
    std::vector<std::array<float, 2>> points;
    for (const auto& point : parsed.tracking_points()) points.push_back({ point.first, point.second });
    expect(points.size() == 2u && vibrance_text::FontPositioning::tracking_value(points, 13.0f) == 80.0f &&
        vibrance_text::FontPositioning::tracking_value(points, 46.5f) == 40.0f &&
        vibrance_text::FontPositioning::tracking_value(points, 100.0f) == 0.0f,
        "Neutral tracking must interpolate and clamp the font's size curve");
    const auto extended = font_file(pair_positioning(true), {});
    const vibrance_text::FontPositioning extension(extended);
    expect(extension.pair_adjustment(10u, 20u) == -160 && extension.pair_adjustment(11u, 21u) == -270,
        "Extension lookups must retain the same pair positioning");
    auto unsupportedPairs = pair_positioning();
    patch_word(unsupportedPairs, 42u, 1u);
    const auto unsupported = font_file(unsupportedPairs, {});
    expect(vibrance_text::FontPositioning(unsupported).pair_adjustment(10u, 20u) == 0,
        "Unsupported positioning records must not become guessed kerning");
    for (std::size_t length = 0u; length < bytes.size(); ++length)
    {
        const vibrance_text::FontPositioning truncated(std::span<const unsigned char>(bytes).first(length));
        truncated.pair_adjustment(10u, 20u);
        truncated.pair_adjustment(11u, 21u);
    }
    auto invalid = bytes;
    patch_long(invalid, 20u, 0xfffffff0u);
    expect(!vibrance_text::FontPositioning(invalid).has_pair_adjustments(),
        "A table outside its font must be rejected");
    auto invalidTracking = tracking_table();
    patch_long(invalidTracking, 48u, 12u * 65536u);
    const auto unordered = font_file({}, invalidTracking);
    expect(vibrance_text::FontPositioning(unordered).tracking_points().empty(),
        "Unordered optical size curves must be rejected");
}

void real_font(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    const Bytes bytes { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
    const auto started = std::chrono::steady_clock::now();
    const vibrance_text::FontPositioning font(bytes);
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now() - started).count();
    std::vector<std::array<float, 2>> points;
    for (const auto& point : font.tracking_points()) points.push_back({ point.first, point.second });
    std::cout << "Font positioning parsed in " << micros << " us; tracking points: " << points.size() << '\n';
    if (path.filename() == "SF-Pro-Display-Regular.otf")
        expect(font.has_pair_adjustments() && points.size() == 20u &&
            vibrance_text::FontPositioning::tracking_value(points, 13.0f) == 80.0f &&
            vibrance_text::FontPositioning::tracking_value(points, 16.0f) == 52.0f,
            "SF Pro Display must retain its real optical tracking curve");
}
}

int main(int argc, char** argv)
{
    try
    {
        synthetic_tests();
        if (argc > 1) real_font(argv[1]);
        std::cout << "Authored kerning, optical tracking and malformed table checks passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

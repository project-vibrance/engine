// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/renderer/font_file.h>
#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <fstream>
#include <span>
#include <string_view>
#include "platform/font_file_native.h"

namespace font_files
{
namespace
{
constexpr std::uintmax_t maximumFontBytes = 64u * 1024u * 1024u;

std::uint32_t big_endian(std::span<const unsigned char> bytes, std::size_t offset)
{
    return std::uint32_t(bytes[offset]) << 24u | std::uint32_t(bytes[offset + 1u]) << 16u |
        std::uint32_t(bytes[offset + 2u]) << 8u | std::uint32_t(bytes[offset + 3u]);
}

std::uint16_t short_big_endian(std::span<const unsigned char> bytes, std::size_t offset)
{
    return std::uint16_t(bytes[offset]) << 8u | bytes[offset + 1u];
}

void append_utf8(std::string& text, std::uint32_t codepoint)
{
    if (codepoint < 0x80u) text += char(codepoint);
    else if (codepoint < 0x800u)
    {
        text += char(0xC0u | (codepoint >> 6u));
        text += char(0x80u | (codepoint & 0x3Fu));
    }
    else if (codepoint < 0x10000u)
    {
        text += char(0xE0u | (codepoint >> 12u));
        text += char(0x80u | ((codepoint >> 6u) & 0x3Fu));
        text += char(0x80u | (codepoint & 0x3Fu));
    }
    else
    {
        text += char(0xF0u | (codepoint >> 18u));
        text += char(0x80u | ((codepoint >> 12u) & 0x3Fu));
        text += char(0x80u | ((codepoint >> 6u) & 0x3Fu));
        text += char(0x80u | (codepoint & 0x3Fu));
    }
}

std::string unicode_font_name(std::span<const unsigned char> bytes)
{
    if (bytes.empty() || bytes.size() % 2u != 0u || bytes.size() > 4096u) return {};
    std::string result;
    for (std::size_t offset = 0u; offset < bytes.size(); offset += 2u)
    {
        std::uint32_t codepoint = short_big_endian(bytes, offset);
        if (codepoint >= 0xD800u && codepoint <= 0xDBFFu)
        {
            if (bytes.size() - offset < 4u) return {};
            const auto low = short_big_endian(bytes, offset + 2u);
            if (low < 0xDC00u || low > 0xDFFFu) return {};
            codepoint = 0x10000u + ((codepoint - 0xD800u) << 10u) + low - 0xDC00u;
            offset += 2u;
        }
        else if (codepoint >= 0xDC00u && codepoint <= 0xDFFFu) return {};
        if (codepoint < 0x20u || codepoint == 0x7Fu) return {};
        append_utf8(result, codepoint);
    }
    return result.find_first_not_of(' ') == std::string::npos ? std::string {} : result;
}

std::string mac_roman_font_name(std::span<const unsigned char> bytes)
{
    if (bytes.empty() || bytes.size() > 4096u) return {};
    constexpr std::array<std::uint16_t, 128u> extended {
        0x00C4, 0x00C5, 0x00C7, 0x00C9, 0x00D1, 0x00D6, 0x00DC, 0x00E1,
        0x00E0, 0x00E2, 0x00E4, 0x00E3, 0x00E5, 0x00E7, 0x00E9, 0x00E8,
        0x00EA, 0x00EB, 0x00ED, 0x00EC, 0x00EE, 0x00EF, 0x00F1, 0x00F3,
        0x00F2, 0x00F4, 0x00F6, 0x00F5, 0x00FA, 0x00F9, 0x00FB, 0x00FC,
        0x2020, 0x00B0, 0x00A2, 0x00A3, 0x00A7, 0x2022, 0x00B6, 0x00DF,
        0x00AE, 0x00A9, 0x2122, 0x00B4, 0x00A8, 0x2260, 0x00C6, 0x00D8,
        0x221E, 0x00B1, 0x2264, 0x2265, 0x00A5, 0x00B5, 0x2202, 0x2211,
        0x220F, 0x03C0, 0x222B, 0x00AA, 0x00BA, 0x03A9, 0x00E6, 0x00F8,
        0x00BF, 0x00A1, 0x00AC, 0x221A, 0x0192, 0x2248, 0x2206, 0x00AB,
        0x00BB, 0x2026, 0x00A0, 0x00C0, 0x00C3, 0x00D5, 0x0152, 0x0153,
        0x2013, 0x2014, 0x201C, 0x201D, 0x2018, 0x2019, 0x00F7, 0x25CA,
        0x00FF, 0x0178, 0x2044, 0x20AC, 0x2039, 0x203A, 0xFB01, 0xFB02,
        0x2021, 0x00B7, 0x201A, 0x201E, 0x2030, 0x00C2, 0x00CA, 0x00C1,
        0x00CB, 0x00C8, 0x00CD, 0x00CE, 0x00CF, 0x00CC, 0x00D3, 0x00D4,
        0xF8FF, 0x00D2, 0x00DA, 0x00DB, 0x00D9, 0x0131, 0x02C6, 0x02DC,
        0x00AF, 0x02D8, 0x02D9, 0x02DA, 0x00B8, 0x02DD, 0x02DB, 0x02C7
    };
    std::string result;
    for (const auto value : bytes)
    {
        if (value < 0x20u || value == 0x7Fu) return {};
        append_utf8(result, value < 0x80u ? value : extended[value - 0x80u]);
    }
    return result.find_first_not_of(' ') == std::string::npos ? std::string {} : result;
}

std::string embedded_font_name(std::span<const unsigned char> bytes, std::size_t face, std::span<const std::uint16_t> wanted)
{
    const auto tableCount = short_big_endian(bytes, face + 4u);
    std::string result;
    int bestScore = -1;
    for (unsigned tableIndex = 0u; tableIndex < tableCount; ++tableIndex)
    {
        const auto record = face + 12u + std::size_t(tableIndex) * 16u;
        if (big_endian(bytes, record) != 0x6E616D65u) continue;
        const auto tableOffset = big_endian(bytes, record + 8u);
        const auto tableLength = big_endian(bytes, record + 12u);
        const auto table = bytes.subspan(tableOffset, tableLength);
        if (table.size() < 6u || short_big_endian(table, 0u) > 1u) break;
        const auto count = short_big_endian(table, 2u), storage = short_big_endian(table, 4u);
        if (storage > table.size() || std::size_t(count) * 12u > table.size() - 6u) break;
        for (unsigned index = 0u; index < count; ++index)
        {
            const auto entry = 6u + std::size_t(index) * 12u;
            const auto platform = short_big_endian(table, entry), encoding = short_big_endian(table, entry + 2u);
            const auto language = short_big_endian(table, entry + 4u), name = short_big_endian(table, entry + 6u);
            const auto length = short_big_endian(table, entry + 8u), offset = short_big_endian(table, entry + 10u);
            const auto priority = std::find(wanted.begin(), wanted.end(), name);
            if (priority == wanted.end()) continue;
            if (offset > table.size() - storage || length > table.size() - storage - offset) continue;
            const auto text = table.subspan(std::size_t(storage) + offset, length);
            std::string candidate;
            if (platform == 0u || (platform == 3u && (encoding == 0u || encoding == 1u || encoding == 10u)))
                candidate = unicode_font_name(text);
            else if (platform == 1u && encoding == 0u) candidate = mac_roman_font_name(text);
            if (candidate.empty()) continue;
            const int score = int(wanted.end() - priority) * 1000 +
                (platform == 3u && language == 0x0409u ? 30 : platform == 0u ? 20 : 0);
            if (score > bestScore) { result = std::move(candidate); bestScore = score; }
        }
        break;
    }
    return result;
}

bool sfnt(std::span<const unsigned char> bytes, std::size_t offset)
{
    if (offset > bytes.size() || bytes.size() - offset < 12u) return false;
    const auto signature = big_endian(bytes, offset);
    if (signature != 0x00010000u && signature != 0x4F54544Fu && signature != 0x74727565u) return false;
    const auto count = std::uint32_t(bytes[offset + 4u]) << 8u | bytes[offset + 5u];
    if (count == 0u || count > 256u || bytes.size() - offset - 12u < std::size_t(count) * 16u) return false;
    bool head = false, maximumProfile = false;
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        const auto record = offset + 12u + std::size_t(index) * 16u;
        const auto tag = big_endian(bytes, record);
        const auto table = big_endian(bytes, record + 8u), length = big_endian(bytes, record + 12u);
        if (table > bytes.size() || length > bytes.size() - table) return false;
        head = head || tag == 0x68656164u;
        maximumProfile = maximumProfile || tag == 0x6D617870u;
    }
    return head && maximumProfile;
}

std::optional<std::vector<unsigned char>> read_font_tables(const std::filesystem::path& source, std::string& error)
{
    auto extension = source.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) { return char(std::tolower(value)); });
    if (extension != ".ttf" && extension != ".otf" && extension != ".ttc" && extension != ".otc")
    { error = "Choose a TrueType or OpenType font file."; return {}; }
    std::error_code fileError;
    const auto size = std::filesystem::file_size(source, fileError);
    if (fileError || size < 12u || size > maximumFontBytes)
    { error = "The font is unavailable, empty or larger than 64 MB."; return {}; }
    std::ifstream input(source, std::ios::binary);
    std::vector<unsigned char> bytes(static_cast<std::size_t>(size));
    if (!input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
    { error = "The font file could not be read."; return {}; }
    bool valid = false;
    if (big_endian(bytes, 0u) == 0x74746366u)
    {
        const auto count = big_endian(bytes, 8u);
        valid = count > 0u && count <= 512u && bytes.size() - 12u >= std::size_t(count) * 4u;
        for (std::uint32_t index = 0u; valid && index < count; ++index)
            valid = sfnt(bytes, big_endian(bytes, 12u + std::size_t(index) * 4u));
    }
    else valid = sfnt(bytes, 0u);
    if (!valid) { error = "The font contains invalid or unsupported font tables."; return {}; }
    error.clear();
    return bytes;
}



}

std::optional<std::vector<unsigned char>> read_font(const std::filesystem::path& source, std::string& error)
{
    auto bytes = read_font_tables(source, error);
    if (!bytes) return {};
#if defined(_WIN32) || defined(__APPLE__)
    if (!native_font_valid(source))
    { error = "The selected font could not be opened by the system font reader."; return {}; }
#endif
    return bytes;
}

bool validate_font_file(const std::filesystem::path& source, std::string& error)
{
    return read_font(source, error).has_value();
}

std::vector<FaceNames> font_face_names(const std::filesystem::path& source, std::string& error)
{
    const auto bytes = read_font_tables(source, error);
    if (!bytes) return {};
    const bool collection = big_endian(*bytes, 0u) == 0x74746366u;
    const auto count = collection ? big_endian(*bytes, 8u) : 1u;
    std::vector<FaceNames> result;
    result.reserve(count);
    constexpr std::array<std::uint16_t, 2> family { 16u, 1u }, style { 17u, 2u };
    constexpr std::array<std::uint16_t, 3> display { 4u, 16u, 1u };
    for (std::uint32_t index = 0u; index < count; ++index)
    {
        const auto face = collection ? big_endian(*bytes, 12u + index * 4u) : 0u;
        result.push_back({ embedded_font_name(*bytes, face, family), embedded_font_name(*bytes, face, style),
            embedded_font_name(*bytes, face, display) });
    }
    return result;
}

std::string font_display_name(const std::filesystem::path& source)
{
    std::string error;
    if (const auto bytes = read_font_tables(source, error))
    {
        const auto face = big_endian(*bytes, 0u) == 0x74746366u ? big_endian(*bytes, 12u) : 0u;
        constexpr std::array<std::uint16_t, 3> display { 4u, 16u, 1u };
        if (const auto name = embedded_font_name(*bytes, face, display); !name.empty()) return name;
    }
    const auto name = source.stem().u8string();
    return std::string(reinterpret_cast<const char*>(name.data()), name.size());
}
}

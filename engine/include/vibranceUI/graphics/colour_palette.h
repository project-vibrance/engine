// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/export.h>
#include <array>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace colour_palette
{
struct Hsv
{
    float hue = 0.0f;
    float saturation = 0.0f;
    float value = 0.0f;
};

struct Cmyk
{
    float cyan = 0.0f;
    float magenta = 0.0f;
    float yellow = 0.0f;
    float black = 0.0f;
};

struct NamedColour
{
    std::string_view name;
    std::uint32_t rgb = 0u;
};

struct VIBRANCE_ENGINE_API Image
{
    std::string name;
    std::filesystem::path sourcePath;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
    std::vector<unsigned char> rgba;

    bool valid() const noexcept;
    std::optional<std::uint32_t> sample(float x, float y, float areaWidth,
        float areaHeight) const noexcept;
};

VIBRANCE_ENGINE_API Hsv rgb_to_hsv(std::uint32_t rgb) noexcept;
VIBRANCE_ENGINE_API std::uint32_t hsv_to_rgb(Hsv hsv) noexcept;
VIBRANCE_ENGINE_API Cmyk rgb_to_cmyk(std::uint32_t rgb) noexcept;
VIBRANCE_ENGINE_API std::uint32_t cmyk_to_rgb(Cmyk cmyk) noexcept;
VIBRANCE_ENGINE_API std::string hex_colour(std::uint32_t rgb);
VIBRANCE_ENGINE_API std::optional<std::uint32_t> parse_hex_colour(std::string_view text) noexcept;
VIBRANCE_ENGINE_API std::span<const NamedColour> named_colours() noexcept;
VIBRANCE_ENGINE_API std::span<const NamedColour> pencil_colours() noexcept;
VIBRANCE_ENGINE_API Image colour_wheel(std::uint32_t size = 384u);
VIBRANCE_ENGINE_API Image colour_spectrum(std::uint32_t width = 384u, std::uint32_t height = 384u);
VIBRANCE_ENGINE_API bool write_image_preview(const Image& image, const std::filesystem::path& path,
    std::string& error);

class VIBRANCE_ENGINE_API ImageLibrary
{
public:
    static constexpr std::size_t maximumImages = 4u;
    bool add(Image image);
    bool rename(std::size_t index, std::string name);
    bool remove(std::size_t index);
    std::span<const Image> images() const noexcept;
private:
    std::vector<Image> entries;
};
}

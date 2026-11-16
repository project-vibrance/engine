// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/graphics/colour_palette.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <limits>

namespace colour_palette
{
namespace
{
float unit(float value) noexcept
{
    return std::isfinite(value) ? std::clamp(value, 0.0f, 1.0f) : 0.0f;
}

std::uint32_t channel(float value) noexcept
{
    return static_cast<std::uint32_t>(std::lround(unit(value) * 255.0f));
}

std::uint32_t rgb(float red, float green, float blue) noexcept
{
    return (channel(red) << 16u) | (channel(green) << 8u) | channel(blue);
}

constexpr std::array<NamedColour, 10> named {{
    { "Blue", 0x0000ffu }, { "Brown", 0x996633u }, { "Cyan", 0x00ffffu },
    { "Green", 0x00ff00u }, { "Magenta", 0xff00ffu }, { "Orange", 0xff8000u },
    { "Purple", 0x800080u }, { "Red", 0xff0000u }, { "Yellow", 0xffff00u },
    { "White", 0xffffffu }
}};

constexpr std::array<NamedColour, 48> pencils {{
    { "Liquorice", 0x000000u }, { "Lead", 0x191919u }, { "Tungsten", 0x333333u },
    { "Iron", 0x4c4c4cu }, { "Steel", 0x666666u }, { "Tin", 0x7f7f7fu },
    { "Nickel", 0x808080u }, { "Aluminium", 0x999999u }, { "Magnesium", 0xb3b3b3u },
    { "Silver", 0xccccccu }, { "Mercury", 0xe6e6e6u }, { "Snow", 0xffffffu },
    { "Cayenne", 0x800000u }, { "Mocha", 0x804000u }, { "Asparagus", 0x808000u },
    { "Fern", 0x408000u }, { "Clover", 0x008000u }, { "Moss", 0x008040u },
    { "Teal", 0x008080u }, { "Ocean", 0x004080u }, { "Midnight", 0x000080u },
    { "Eggplant", 0x400080u }, { "Plum", 0x800080u }, { "Maroon", 0x800040u },
    { "Maraschino", 0xff0000u }, { "Tangerine", 0xff8000u }, { "Lemon", 0xffff00u },
    { "Lime", 0x80ff00u }, { "Spring", 0x00ff00u }, { "Sea Foam", 0x00ff80u },
    { "Turquoise", 0x00ffffu }, { "Aqua", 0x0080ffu }, { "Blueberry", 0x0000ffu },
    { "Grape", 0x8000ffu }, { "Magenta", 0xff00ffu }, { "Strawberry", 0xff0080u },
    { "Salmon", 0xff6666u }, { "Cantaloupe", 0xffcc66u }, { "Banana", 0xffff66u },
    { "Honeydew", 0xccff66u }, { "Flora", 0x66ff66u }, { "Spindrift", 0x66ffccu },
    { "Ice", 0x66ffffu }, { "Sky", 0x66ccffu }, { "Orchid", 0x6666ffu },
    { "Lavender", 0xcc66ffu }, { "Bubblegum", 0xff66ffu }, { "Carnation", 0xff66ccu }
}};

void set_pixel(Image& image, std::size_t index, std::uint32_t colour,
    unsigned char alpha = 255u)
{
    image.rgba[index] = static_cast<unsigned char>(colour >> 16u);
    image.rgba[index + 1u] = static_cast<unsigned char>(colour >> 8u);
    image.rgba[index + 2u] = static_cast<unsigned char>(colour);
    image.rgba[index + 3u] = alpha;
}
}

bool Image::valid() const noexcept
{
    return width != 0u && height != 0u && width <= 1024u && height <= 1024u &&
        rgba.size() == static_cast<std::size_t>(width) * height * 4u;
}

std::optional<std::uint32_t> Image::sample(float x, float y,
    float areaWidth, float areaHeight) const noexcept
{
    if (!valid() || !std::isfinite(x) || !std::isfinite(y) ||
        !std::isfinite(areaWidth) || !std::isfinite(areaHeight) ||
        areaWidth <= 0.0f || areaHeight <= 0.0f) return {};
    const float scale = std::min(areaWidth / width, areaHeight / height);
    const float displayWidth = width * scale, displayHeight = height * scale;
    const float localX = x - (areaWidth - displayWidth) * 0.5f;
    const float localY = y - (areaHeight - displayHeight) * 0.5f;
    if (localX < 0.0f || localY < 0.0f || localX >= displayWidth || localY >= displayHeight)
        return {};
    const auto imageX = std::min(static_cast<std::uint32_t>(localX / scale), width - 1u);
    const auto imageY = std::min(static_cast<std::uint32_t>(localY / scale), height - 1u);
    const auto offset = (static_cast<std::size_t>(imageY) * width + imageX) * 4u;
    const auto alpha = rgba[offset + 3u];
    const auto composite = [&](std::size_t index) {
        return (static_cast<std::uint32_t>(rgba[offset + index]) * alpha +
            255u * (255u - alpha) + 127u) / 255u;
    };
    return (composite(0u) << 16u) | (composite(1u) << 8u) | composite(2u);
}

Hsv rgb_to_hsv(std::uint32_t colour) noexcept
{
    const float red = static_cast<float>((colour >> 16u) & 255u) / 255.0f;
    const float green = static_cast<float>((colour >> 8u) & 255u) / 255.0f;
    const float blue = static_cast<float>(colour & 255u) / 255.0f;
    const float maximum = std::max({ red, green, blue });
    const float minimum = std::min({ red, green, blue });
    const float delta = maximum - minimum;
    Hsv result { 0.0f, maximum > 0.0f ? delta / maximum : 0.0f, maximum };
    if (delta <= std::numeric_limits<float>::epsilon()) return result;
    if (maximum == red) result.hue = 60.0f * std::fmod((green - blue) / delta, 6.0f);
    else if (maximum == green) result.hue = 60.0f * ((blue - red) / delta + 2.0f);
    else result.hue = 60.0f * ((red - green) / delta + 4.0f);
    if (result.hue < 0.0f) result.hue += 360.0f;
    return result;
}

std::uint32_t hsv_to_rgb(Hsv hsv) noexcept
{
    float hue = std::isfinite(hsv.hue) ? std::fmod(hsv.hue, 360.0f) : 0.0f;
    if (hue < 0.0f) hue += 360.0f;
    const float saturation = unit(hsv.saturation), value = unit(hsv.value);
    const float chroma = value * saturation;
    const float other = chroma * (1.0f - std::abs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float base = value - chroma;
    if (hue < 60.0f) return rgb(chroma + base, other + base, base);
    if (hue < 120.0f) return rgb(other + base, chroma + base, base);
    if (hue < 180.0f) return rgb(base, chroma + base, other + base);
    if (hue < 240.0f) return rgb(base, other + base, chroma + base);
    if (hue < 300.0f) return rgb(other + base, base, chroma + base);
    return rgb(chroma + base, base, other + base);
}

Cmyk rgb_to_cmyk(std::uint32_t colour) noexcept
{
    const float red = static_cast<float>((colour >> 16u) & 255u) / 255.0f;
    const float green = static_cast<float>((colour >> 8u) & 255u) / 255.0f;
    const float blue = static_cast<float>(colour & 255u) / 255.0f;
    const float black = 1.0f - std::max({ red, green, blue });
    if (black >= 1.0f) return { 0.0f, 0.0f, 0.0f, 1.0f };
    return { (1.0f - red - black) / (1.0f - black),
        (1.0f - green - black) / (1.0f - black),
        (1.0f - blue - black) / (1.0f - black), black };
}

std::uint32_t cmyk_to_rgb(Cmyk cmyk) noexcept
{
    const float scale = 1.0f - unit(cmyk.black);
    return rgb((1.0f - unit(cmyk.cyan)) * scale,
        (1.0f - unit(cmyk.magenta)) * scale, (1.0f - unit(cmyk.yellow)) * scale);
}

std::string hex_colour(std::uint32_t colour)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string result(6u, '0');
    for (unsigned index = 0u; index < 6u; ++index)
        result[index] = digits[(colour >> ((5u - index) * 4u)) & 15u];
    return result;
}

std::optional<std::uint32_t> parse_hex_colour(std::string_view text) noexcept
{
    const auto whitespace = [](char character) {
        return character == ' ' || character == '\t' || character == '\r' || character == '\n';
    };
    while (!text.empty() && whitespace(text.front())) text.remove_prefix(1u);
    while (!text.empty() && whitespace(text.back())) text.remove_suffix(1u);
    if (!text.empty() && text.front() == '#') text.remove_prefix(1u);
    if (text.size() != 3u && text.size() != 6u) return {};
    std::uint32_t result = 0u;
    for (const char character : text)
    {
        unsigned value = 0u;
        if (character >= '0' && character <= '9') value = character - '0';
        else if (character >= 'a' && character <= 'f') value = character - 'a' + 10u;
        else if (character >= 'A' && character <= 'F') value = character - 'A' + 10u;
        else return {};
        result = (result << (text.size() == 3u ? 8u : 4u)) | (text.size() == 3u ? value * 17u : value);
    }
    return result;
}

std::span<const NamedColour> named_colours() noexcept { return named; }
std::span<const NamedColour> pencil_colours() noexcept { return pencils; }

Image colour_wheel(std::uint32_t requestedSize)
{
    const auto size = std::clamp(requestedSize, 16u, 1024u);
    Image image { "Colour Wheel", {}, size, size,
        std::vector<unsigned char>(static_cast<std::size_t>(size) * size * 4u) };
    for (std::uint32_t y = 0u; y < size; ++y)
        for (std::uint32_t x = 0u; x < size; ++x)
        {
            const float localX = (static_cast<float>(x) + 0.5f) * 2.0f / size - 1.0f;
            const float localY = 1.0f - (static_cast<float>(y) + 0.5f) * 2.0f / size;
            const float radius = std::sqrt(localX * localX + localY * localY);
            const float rim = std::clamp(1.3f - (1.0f - radius) * size * .5f, 0.0f, 1.0f);
            const auto colour = hsv_to_rgb({ std::atan2(localY, localX) * 180.0f /
                3.14159265358979323846f, std::min(radius, 1.0f), 1.0f - .18f * rim });
            set_pixel(image, (static_cast<std::size_t>(y) * size + x) * 4u, colour,
                static_cast<unsigned char>(std::lround(std::clamp((1.0f - radius) * size,
                    0.0f, 1.0f) * 255.0f)));
        }
    return image;
}

Image colour_spectrum(std::uint32_t requestedWidth, std::uint32_t requestedHeight)
{
    const auto width = std::clamp(requestedWidth, 16u, 1024u);
    const auto height = std::clamp(requestedHeight, 16u, 1024u);
    Image image { "Spectrum", {}, width, height,
        std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4u) };
    for (std::uint32_t y = 0u; y < height; ++y)
        for (std::uint32_t x = 0u; x < width; ++x)
        {
            const float horizontal = static_cast<float>(x) / (width - 1u);
            const auto colour = hsv_to_rgb({ 360.0f * static_cast<float>(y) / (height - 1u),
                std::min(horizontal * 2.0f, 1.0f), std::min((1.0f - horizontal) * 2.0f, 1.0f) });
            set_pixel(image, (static_cast<std::size_t>(y) * width + x) * 4u, colour);
        }
    return image;
}

bool write_image_preview(const Image& image, const std::filesystem::path& path,
    std::string& error)
{
    if (!image.valid()) { error = "The palette image is empty or invalid."; return false; }
    std::error_code fileError;
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path(), fileError);
    if (fileError) { error = "The palette image cache could not be created."; return false; }
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output << "P6\n" << image.width << ' ' << image.height << "\n255\n";
    std::vector<unsigned char> row(static_cast<std::size_t>(image.width) * 3u);
    for (std::uint32_t y = 0u; y < image.height; ++y)
    {
        for (std::uint32_t x = 0u; x < image.width; ++x)
        {
            const auto offset = (static_cast<std::size_t>(y) * image.width + x) * 4u;
            const auto alpha = image.rgba[offset + 3u];
            for (unsigned channel = 0u; channel < 3u; ++channel)
                row[static_cast<std::size_t>(x) * 3u + channel] = static_cast<unsigned char>(
                    (static_cast<unsigned>(image.rgba[offset + channel]) * alpha +
                        255u * (255u - alpha) + 127u) / 255u);
        }
        output.write(reinterpret_cast<const char*>(row.data()), static_cast<std::streamsize>(row.size()));
    }
    if (!output) { error = "The palette image preview could not be written."; return false; }
    error.clear();
    return true;
}

bool ImageLibrary::add(Image image)
{
    if (!image.valid()) return false;
    if (entries.size() == maximumImages) entries.erase(entries.begin());
    entries.push_back(std::move(image));
    return true;
}

bool ImageLibrary::rename(std::size_t index, std::string name)
{
    if (index >= entries.size() || name.empty() || name.size() > 128u) return false;
    entries[index].name = std::move(name);
    return true;
}

bool ImageLibrary::remove(std::size_t index)
{
    if (index >= entries.size()) return false;
    entries.erase(entries.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

std::span<const Image> ImageLibrary::images() const noexcept { return entries; }
}

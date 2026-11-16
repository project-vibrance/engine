// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vibrance
{
class SvgColourTemplate
{
public:
    explicit SvgColourTemplate(std::string marker = "#FF00FF", std::string filenamePrefix = "colour")
        : colourMarker(std::move(marker)), filenamePrefix(std::move(filenamePrefix)) {}
    static constexpr std::size_t maximumSourceBytes = 256u * 1024u;

    bool load(const std::filesystem::path& path, std::string& error)
    {
        if (colourMarker.size() != 7u || colourMarker.front() != '#' ||
            colourMarker.find_first_not_of("0123456789abcdefABCDEF", 1u) != std::string::npos)
        { error = "The colour marker must be a six-digit hexadecimal colour."; return false; }
        std::transform(colourMarker.begin(), colourMarker.end(), colourMarker.begin(), [](unsigned char c) {
            return c >= 'a' && c <= 'f' ? char(c - 'a' + 'A') : char(c);
        });
        source.clear();
        colourOffsets.clear();
        fingerprint = 14695981039346656037ull;
        std::error_code fileError;
        const auto size = std::filesystem::file_size(path, fileError);
        if (fileError || size == 0u || size > maximumSourceBytes)
        {
            error = "The SVG template is unavailable or larger than 256 KB.";
            return false;
        }
        std::ifstream input(path, std::ios::binary);
        std::string candidate(static_cast<std::size_t>(size), '\0');
        if (!input.read(candidate.data(), static_cast<std::streamsize>(candidate.size())))
        {
            error = "The SVG template could not be read.";
            return false;
        }
        if (candidate.find("<svg") == std::string::npos)
        {
            error = "The SVG template must be an SVG file.";
            return false;
        }
        for (std::size_t offset = 0u; offset + colourMarker.size() <= candidate.size(); ++offset)
        {
            bool matches = true;
            for (std::size_t index = 0u; index < colourMarker.size(); ++index)
            {
                const unsigned char value = static_cast<unsigned char>(candidate[offset + index]);
                const char upper = value >= 'a' && value <= 'z' ? char(value - 'a' + 'A') : char(value);
                if (upper != colourMarker[index]) { matches = false; break; }
            }
            if (matches) colourOffsets.push_back(offset + 1u);
        }
        if (colourOffsets.empty())
        {
            error = "The SVG template needs a " + colourMarker + " recolourable region.";
            return false;
        }
        for (const unsigned char value : colourMarker)
            fingerprint = (fingerprint ^ value) * 1099511628211ull;
        for (const unsigned char value : candidate)
            fingerprint = (fingerprint ^ value) * 1099511628211ull;
        source = std::move(candidate);
        error.clear();
        return true;
    }

    bool valid() const noexcept { return !source.empty() && !colourOffsets.empty(); }

    std::optional<std::string> render(std::uint32_t rgb) const
    {
        if (!valid()) return {};
        auto result = source;
        const auto colour = hexadecimal(rgb & 0xFFFFFFu, 6u);
        for (const auto offset : colourOffsets) std::copy_n(colour.begin(), 6u, result.begin() + offset);
        return result;
    }

    std::string filename(std::uint32_t rgb) const
    {
        return filenamePrefix + "-" + hexadecimal(fingerprint, 16u) + "-" + hexadecimal(rgb & 0xFFFFFFu, 6u) + ".svg";
    }

    bool write(std::uint32_t rgb, const std::filesystem::path& path, std::string& error) const
    {
        const auto rendered = render(rgb);
        if (!rendered)
        {
            error = "The SVG template has not been loaded.";
            return false;
        }
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(rendered->data(), static_cast<std::streamsize>(rendered->size()));
        output.close();
        if (!output)
        {
            error = "The coloured SVG preview could not be written.";
            return false;
        }
        error.clear();
        return true;
    }

private:
    static std::string hexadecimal(std::uint64_t value, std::size_t width)
    {
        constexpr std::string_view digits = "0123456789ABCDEF";
        std::string result(width, '0');
        for (std::size_t index = 0u; index < width; ++index)
            result[index] = digits[(value >> ((width - index - 1u) * 4u)) & 15u];
        return result;
    }

    std::string colourMarker;
    std::string filenamePrefix;
    std::string source;
    std::vector<std::size_t> colourOffsets;
    std::uint64_t fingerprint = 14695981039346656037ull;
};
}

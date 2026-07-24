#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/renderer/image.h>
#include <vibranceUI/core/logger.h>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <fstream>
#include <limits>
#include <unordered_set>
#include <vector>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_MULTIPLE_MASTERS_H
#include FT_TRUETYPE_TABLES_H

namespace
{
    constexpr uint32_t kAtlasWidth = 4096;
    constexpr uint32_t kAtlasHeight = 4096;
    constexpr int kSdfPadding = 48;
    constexpr unsigned char kSdfOnEdgeValue = 180;

    std::vector<unsigned char> read_binary_file(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file)
        {
            return {};
        }

        const std::streamsize size = file.tellg();
        if (size <= 0)
        {
            return {};
        }

        std::vector<unsigned char> data(static_cast<size_t>(size));
        file.seekg(0, std::ios::beg);
        file.read(reinterpret_cast<char*>(data.data()), size);
        return data;
    }

    bool upload_rgba_to_image(
        VmaAllocator& allocator,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        StorageImage& image,
        const std::vector<unsigned char>& rgba)
    {
        // Font atlas upload uses a short-lived staging buffer and keeps the image shader-readable
        Logger* logger = Logger::fetch_logger();
        const vk::DeviceSize uploadSize = static_cast<vk::DeviceSize>(rgba.size());
        if (uploadSize == 0)
        {
            return false;
        }

        vk::BufferCreateInfo bufferInfo = {};
        bufferInfo.size = uploadSize;
        bufferInfo.usage = vk::BufferUsageFlagBits::eTransferSrc;
        bufferInfo.sharingMode = vk::SharingMode::eExclusive;

        VmaAllocationCreateInfo allocationInfo = {};
        allocationInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
            VMA_ALLOCATION_CREATE_MAPPED_BIT;
        allocationInfo.usage = VMA_MEMORY_USAGE_AUTO;

        VkBuffer stagingBuffer = VK_NULL_HANDLE;
        VmaAllocation stagingAllocation = nullptr;
        VmaAllocationInfo stagingInfo = {};
        VkBufferCreateInfo rawBufferInfo = bufferInfo;
        if (vmaCreateBuffer(allocator, &rawBufferInfo, &allocationInfo,
            &stagingBuffer, &stagingAllocation, &stagingInfo) != VK_SUCCESS)
        {
            logger->vulkan("Failed to create font atlas staging buffer.");
            return false;
        }

        std::memcpy(stagingInfo.pMappedData, rgba.data(), rgba.size());

        vk::Result result = commandBuffer.reset();
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to reset font atlas upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        result = commandBuffer.begin(beginInfo);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to begin font atlas upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        transition_image_layout(commandBuffer, image.image,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal,
            vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite,
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer);

        vk::BufferImageCopy region = {};
        region.bufferOffset = 0;
        region.bufferRowLength = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = vk::Offset3D { 0, 0, 0 };
        region.imageExtent = vk::Extent3D { image.extent.width, image.extent.height, 1 };

        commandBuffer.copyBufferToImage(stagingBuffer, image.image,
            vk::ImageLayout::eTransferDstOptimal, 1, &region);

        transition_image_layout(commandBuffer, image.image,
            vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eGeneral,
            vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
            vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader);

        result = commandBuffer.end();
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to end font atlas upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        vk::SubmitInfo submitInfo = {};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        result = queue.submit(1, &submitInfo, nullptr);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to submit font atlas upload.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        result = queue.waitIdle();
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to wait for font atlas upload.");
            return false;
        }

        return true;
    }

    void add_existing_font_path(std::vector<std::filesystem::path>& paths, const std::filesystem::path& path)
    {
        if (path.empty() || !std::filesystem::exists(path))
        {
            return;
        }
        if (std::find(paths.begin(), paths.end(), path) == paths.end())
        {
            paths.push_back(path);
        }
    }

    std::filesystem::path existing_font_path(const std::filesystem::path& path)
    {
        return !path.empty() && std::filesystem::exists(path) ? path : std::filesystem::path {};
    }

    std::string normalised_locale(std::string_view locale)
    {
        std::string value(locale);
        for (char& character : value)
        {
            character = character == '-' ?
                '_' :
                static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        }
        return value;
    }

    bool locale_starts_with(std::string_view locale, std::string_view prefix)
    {
        return locale.size() >= prefix.size() && locale.substr(0u, prefix.size()) == prefix;
    }

    std::vector<std::filesystem::path> resolve_profile_font_paths(
        const std::vector<std::filesystem::path>& assetFontDirectories,
        const std::string& value)
    {
        // Relative profile paths use the highest-priority layer containing the
        // requested font, then fall back through the packaged directories.
        if (value.empty())
        {
            return {};
        }

        std::vector<std::filesystem::path> paths;
        std::filesystem::path path(value);
        if (path.is_absolute())
        {
            add_existing_font_path(paths, path);
            return paths;
        }
        for (auto it = assetFontDirectories.rbegin(); it != assetFontDirectories.rend(); ++it)
        {
            add_existing_font_path(paths, *it / path);
        }
        add_existing_font_path(paths, path);
        return paths;
    }

    class FontProfileJsonParser
    {
    public:
        FontProfileJsonParser(
            std::string_view input,
            const std::vector<std::filesystem::path>& assetFontDirectories) :
            input(input),
            assetFontDirectories(assetFontDirectories)
        {
        }

        bool parse(Renderer2DFontProfile& outProfile)
        {
            // Profiles are intentionally small so apps can edit font mappings without recompiling
            outProfile = {};
            skip_whitespace();
            if (!consume('{'))
            {
                return false;
            }

            while (position < input.size())
            {
                skip_whitespace();
                if (consume('}'))
                {
                    return true;
                }

                std::string key;
                if (!parse_string(key))
                {
                    return false;
                }
                skip_whitespace();
                if (!consume(':'))
                {
                    return false;
                }
                if (!parse_profile_value(outProfile, key))
                {
                    return false;
                }
                skip_whitespace();
                if (consume('}'))
                {
                    return true;
                }
                if (!consume(','))
                {
                    return false;
                }
            }
            return false;
        }

    private:
        std::string_view input;
        const std::vector<std::filesystem::path>& assetFontDirectories;
        std::size_t position = 0u;

        void skip_whitespace()
        {
            while (position < input.size() &&
                std::isspace(static_cast<unsigned char>(input[position])))
            {
                ++position;
            }
        }

        bool consume(char expected)
        {
            skip_whitespace();
            if (position >= input.size() || input[position] != expected)
            {
                return false;
            }
            ++position;
            return true;
        }

        bool parse_string(std::string& out)
        {
            skip_whitespace();
            if (position >= input.size() || input[position] != '"')
            {
                return false;
            }
            ++position;
            out.clear();
            while (position < input.size())
            {
                const char character = input[position++];
                if (character == '"')
                {
                    return true;
                }
                if (character != '\\')
                {
                    out.push_back(character);
                    continue;
                }
                if (position >= input.size())
                {
                    return false;
                }
                const char escaped = input[position++];
                switch (escaped)
                {
                    case '"':
                    case '\\':
                    case '/':
                        out.push_back(escaped);
                        break;
                    case 'b':
                        out.push_back('\b');
                        break;
                    case 'f':
                        out.push_back('\f');
                        break;
                    case 'n':
                        out.push_back('\n');
                        break;
                    case 'r':
                        out.push_back('\r');
                        break;
                    case 't':
                        out.push_back('\t');
                        break;
                    case 'u':
                        position = std::min(position + 4u, input.size());
                        out.push_back('?');
                        break;
                    default:
                        out.push_back(escaped);
                        break;
                }
            }
            return false;
        }

        bool skip_value()
        {
            // Unknown profile keys are skipped so newer config files remain backwards compatible
            skip_whitespace();
            if (position >= input.size())
            {
                return false;
            }
            if (input[position] == '"')
            {
                std::string ignored;
                return parse_string(ignored);
            }
            if (input[position] == '{')
            {
                ++position;
                while (position < input.size())
                {
                    skip_whitespace();
                    if (consume('}'))
                    {
                        return true;
                    }
                    std::string key;
                    if (!parse_string(key) || !consume(':') || !skip_value())
                    {
                        return false;
                    }
                    skip_whitespace();
                    if (consume('}'))
                    {
                        return true;
                    }
                    if (!consume(','))
                    {
                        return false;
                    }
                }
                return false;
            }
            if (input[position] == '[')
            {
                ++position;
                while (position < input.size())
                {
                    skip_whitespace();
                    if (consume(']'))
                    {
                        return true;
                    }
                    if (!skip_value())
                    {
                        return false;
                    }
                    skip_whitespace();
                    if (consume(']'))
                    {
                        return true;
                    }
                    if (!consume(','))
                    {
                        return false;
                    }
                }
                return false;
            }

            while (position < input.size() &&
                input[position] != ',' &&
                input[position] != '}' &&
                input[position] != ']')
            {
                ++position;
            }
            return true;
        }

        bool parse_string_array(std::vector<std::string>& out)
        {
            out.clear();
            if (!consume('['))
            {
                return false;
            }
            while (position < input.size())
            {
                skip_whitespace();
                if (consume(']'))
                {
                    return true;
                }
                std::string value;
                if (!parse_string(value))
                {
                    return false;
                }
                out.push_back(std::move(value));
                skip_whitespace();
                if (consume(']'))
                {
                    return true;
                }
                if (!consume(','))
                {
                    return false;
                }
            }
            return false;
        }

        bool parse_locales(Renderer2DFontProfile& profile)
        {
            // Locale-specific fonts override the default only for matching language codes
            if (!consume('{'))
            {
                return false;
            }
            while (position < input.size())
            {
                skip_whitespace();
                if (consume('}'))
                {
                    return true;
                }
                std::string locale;
                std::string value;
                if (!parse_string(locale) || !consume(':') || !parse_string(value))
                {
                    return false;
                }
                std::vector<std::filesystem::path> paths =
                    resolve_profile_font_paths(assetFontDirectories, value);
                if (!paths.empty())
                {
                    const std::string key = normalised_locale(locale);
                    profile.localeFontPaths[key] = paths.front();
                    profile.localeFontCandidates[key] = std::move(paths);
                }
                skip_whitespace();
                if (consume('}'))
                {
                    return true;
                }
                if (!consume(','))
                {
                    return false;
                }
            }
            return false;
        }

        bool parse_profile_value(Renderer2DFontProfile& profile, const std::string& key)
        {
            if (key == "default" || key == "defaultFont" || key == "primary" || key == "font")
            {
                std::string value;
                if (!parse_string(value))
                {
                    return false;
                }
                profile.defaultFontCandidates =
                    resolve_profile_font_paths(assetFontDirectories, value);
                profile.defaultFontPath = profile.defaultFontCandidates.empty()
                    ? std::filesystem::path {}
                    : profile.defaultFontCandidates.front();
                return true;
            }
            if (key == "fallbacks" || key == "fallbackFonts")
            {
                std::vector<std::string> values;
                if (!parse_string_array(values))
                {
                    return false;
                }
                for (const std::string& value : values)
                {
                    for (const std::filesystem::path& path :
                        resolve_profile_font_paths(assetFontDirectories, value))
                    {
                        add_existing_font_path(profile.fallbackFontPaths, path);
                    }
                }
                return true;
            }
            if (key == "locales" || key == "localeFonts")
            {
                return parse_locales(profile);
            }
            return skip_value();
        }
    };

    uint64_t kerning_key(uint32_t left, uint32_t right)
    {
        return (static_cast<uint64_t>(left) << 32u) | static_cast<uint64_t>(right);
    }

    bool append_utf8_codepoint(std::vector<uint32_t>& out, std::string_view text, std::size_t& offset)
    {
        // Invalid UTF-8 falls back to a visible question mark rather than dropping layout
        if (offset >= text.size())
        {
            return false;
        }

        const unsigned char first = static_cast<unsigned char>(text[offset++]);
        if (first < 0x80u)
        {
            out.push_back(first);
            return true;
        }

        uint32_t codepoint = 0u;
        int continuationCount = 0;
        if ((first & 0xE0u) == 0xC0u)
        {
            codepoint = first & 0x1Fu;
            continuationCount = 1;
        }
        else if ((first & 0xF0u) == 0xE0u)
        {
            codepoint = first & 0x0Fu;
            continuationCount = 2;
        }
        else if ((first & 0xF8u) == 0xF0u)
        {
            codepoint = first & 0x07u;
            continuationCount = 3;
        }
        else
        {
            out.push_back('?');
            return false;
        }

        for (int i = 0; i < continuationCount; ++i)
        {
            if (offset >= text.size())
            {
                out.push_back('?');
                return false;
            }

            const unsigned char next = static_cast<unsigned char>(text[offset++]);
            if ((next & 0xC0u) != 0x80u)
            {
                out.push_back('?');
                return false;
            }
            codepoint = (codepoint << 6u) | (next & 0x3Fu);
        }

        out.push_back(codepoint);
        return true;
    }

    std::vector<uint32_t> decode_utf8(std::string_view text)
    {
        // Layout operates on codepoints so glyph lookup and kerning stay simple
        std::vector<uint32_t> out;
        out.reserve(text.size());
        std::size_t offset = 0u;
        while (offset < text.size())
        {
            append_utf8_codepoint(out, text, offset);
        }
        return out;
    }

    std::vector<uint32_t> build_codepoint_list(const Renderer2DFontAtlasLoadOptions& options)
    {
        std::vector<uint32_t> codepoints;
        std::unordered_set<uint32_t> seen;

        auto add_codepoint = [&](uint32_t codepoint) {
            if (codepoint == 0u || seen.contains(codepoint))
            {
                return;
            }
            seen.insert(codepoint);
            codepoints.push_back(codepoint);
        };

        if (options.includeBasicLatin)
        {
            for (uint32_t codepoint = 32u; codepoint <= 126u; ++codepoint)
            {
                add_codepoint(codepoint);
            }
        }

        for (const Renderer2DCodepointRange& range : options.ranges)
        {
            const uint32_t first = std::min(range.first, range.last);
            const uint32_t last = std::max(range.first, range.last);
            for (uint32_t codepoint = first; codepoint <= last; ++codepoint)
            {
                add_codepoint(codepoint);
                if (codepoint == UINT32_MAX)
                {
                    break;
                }
            }
        }

        for (uint32_t codepoint : decode_utf8(options.preloadText))
        {
            add_codepoint(codepoint);
        }

        add_codepoint('?');
        std::sort(codepoints.begin(), codepoints.end());
        return codepoints;
    }

    float ft_26_6_to_float(FT_Pos value)
    {
        return static_cast<float>(value) / 64.0f;
    }

    int clamped_font_weight(int weight)
    {
        return weight > 0 ? std::clamp(weight, 1, 1000) : 0;
    }

    std::string lowercase_string(const char* value)
    {
        std::string out = value ? value : "";
        for (char& c : out)
        {
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
        return out;
    }

    int inferred_face_weight(FT_Face face)
    {
        if (TT_OS2* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(face, ft_sfnt_os2));
            os2 && os2->usWeightClass > 0)
        {
            return static_cast<int>(os2->usWeightClass);
        }

        const std::string style = lowercase_string(face ? face->style_name : nullptr);
        if (style.find("black") != std::string::npos)
        {
            return 900;
        }
        if (style.find("extra bold") != std::string::npos || style.find("extrabold") != std::string::npos)
        {
            return 800;
        }
        if (style.find("bold") != std::string::npos)
        {
            return 700;
        }
        if (style.find("semi bold") != std::string::npos || style.find("semibold") != std::string::npos)
        {
            return 600;
        }
        if (style.find("medium") != std::string::npos)
        {
            return 500;
        }
        if (style.find("light") != std::string::npos)
        {
            return 300;
        }
        if (style.find("thin") != std::string::npos)
        {
            return 100;
        }
        if (style.find("regular") != std::string::npos || style.find("normal") != std::string::npos)
        {
            return 400;
        }
        return 0;
    }

    FT_Long choose_weighted_face_index(
        FT_Library library,
        const std::vector<unsigned char>& fontData,
        int requestedWeight)
    {
        requestedWeight = clamped_font_weight(requestedWeight);
        if (requestedWeight <= 0)
        {
            return 0;
        }

        FT_Face firstFace = nullptr;
        if (FT_New_Memory_Face(
            library,
            fontData.data(),
            static_cast<FT_Long>(fontData.size()),
            0,
            &firstFace) != 0)
        {
            return 0;
        }

        const FT_Long faceCount = std::max<FT_Long>(firstFace->num_faces, 1);
        FT_Done_Face(firstFace);
        if (faceCount <= 1)
        {
            return 0;
        }

        FT_Long bestIndex = 0;
        int bestScore = std::numeric_limits<int>::max();
        for (FT_Long index = 0; index < faceCount; ++index)
        {
            FT_Face candidate = nullptr;
            if (FT_New_Memory_Face(
                library,
                fontData.data(),
                static_cast<FT_Long>(fontData.size()),
                index,
                &candidate) != 0)
            {
                continue;
            }

            const int faceWeight = inferred_face_weight(candidate);
            if (faceWeight > 0)
            {
                const int score = std::abs(faceWeight - requestedWeight);
                if (score < bestScore)
                {
                    bestScore = score;
                    bestIndex = index;
                }
            }
            FT_Done_Face(candidate);
        }

        return bestIndex;
    }

    bool apply_font_weight_axis(
        FT_Library library,
        FT_Face face,
        int requestedWeight)
    {
        // Variable fonts expose weight through an axis rather than separate faces
        requestedWeight = clamped_font_weight(requestedWeight);
        if (requestedWeight <= 0 || !face)
        {
            return false;
        }

        FT_MM_Var* variation = nullptr;
        if (FT_Get_MM_Var(face, &variation) != 0 || variation == nullptr)
        {
            return false;
        }

        std::vector<FT_Fixed> coordinates(variation->num_axis, 0);
        bool foundWeightAxis = false;
        for (FT_UInt axisIndex = 0; axisIndex < variation->num_axis; ++axisIndex)
        {
            const FT_Var_Axis& axis = variation->axis[axisIndex];
            coordinates[axisIndex] = axis.def;
            if (axis.tag == FT_MAKE_TAG('w', 'g', 'h', 't'))
            {
                const FT_Fixed requested = static_cast<FT_Fixed>(requestedWeight << 16);
                coordinates[axisIndex] = std::clamp(requested, axis.minimum, axis.maximum);
                foundWeightAxis = true;
            }
        }

        const bool applied = foundWeightAxis &&
            FT_Set_Var_Design_Coordinates(face, variation->num_axis, coordinates.data()) == 0;
        FT_Done_MM_Var(library, variation);
        return applied;
    }

    unsigned char clamp_sdf_value(float value)
    {
        return static_cast<unsigned char>(std::clamp(std::round(value), 0.0f, 255.0f));
    }

    unsigned char bitmap_alpha_at(const FT_Bitmap& bitmap, uint32_t x, uint32_t y)
    {
        // FreeType bitmap pitch can be negative when rows are stored bottom-up
        if (bitmap.buffer == nullptr || x >= bitmap.width || y >= bitmap.rows)
        {
            return 0;
        }

        const int pitch = bitmap.pitch;
        const unsigned char* row = pitch >= 0
            ? bitmap.buffer + static_cast<size_t>(y) * static_cast<size_t>(pitch)
            : bitmap.buffer + static_cast<size_t>(bitmap.rows - 1u - y) * static_cast<size_t>(-pitch);

        switch (bitmap.pixel_mode)
        {
        case FT_PIXEL_MODE_GRAY:
            if (bitmap.num_grays > 1 && bitmap.num_grays != 256)
            {
                return static_cast<unsigned char>(
                    (static_cast<uint32_t>(row[x]) * 255u) / static_cast<uint32_t>(bitmap.num_grays - 1));
            }
            return row[x];
        case FT_PIXEL_MODE_MONO:
            return (row[x >> 3u] & (0x80u >> (x & 7u))) != 0 ? 255 : 0;
        default:
            return 0;
        }
    }

    std::vector<unsigned char> padded_glyph_alpha(const FT_Bitmap& bitmap, uint32_t width, uint32_t height)
    {
        // Padding gives the signed-distance field room for glow and soft edges
        std::vector<unsigned char> alpha(static_cast<size_t>(width) * static_cast<size_t>(height), 0);
        for (uint32_t y = 0; y < bitmap.rows; ++y)
        {
            for (uint32_t x = 0; x < bitmap.width; ++x)
            {
                const uint32_t dstX = x + static_cast<uint32_t>(kSdfPadding);
                const uint32_t dstY = y + static_cast<uint32_t>(kSdfPadding);
                alpha[static_cast<size_t>(dstY) * width + dstX] = bitmap_alpha_at(bitmap, x, y);
            }
        }
        return alpha;
    }

    void distance_transform_1d(const std::vector<float>& source, std::vector<float>& distances, uint32_t count)
    {
        // Felzenszwalb distance transform builds exact squared distances in linear time
        constexpr float inf = 1.0e20f;
        std::vector<uint32_t> locations(count, 0u);
        std::vector<float> boundaries(static_cast<size_t>(count) + 1u, 0.0f);

        uint32_t k = 0u;
        locations[0] = 0u;
        boundaries[0] = -inf;
        boundaries[1] = inf;

        for (uint32_t q = 1u; q < count; ++q)
        {
            float intersection = 0.0f;
            for (;;)
            {
                const uint32_t location = locations[k];
                const float qf = static_cast<float>(q);
                const float lf = static_cast<float>(location);
                intersection = ((source[q] + qf * qf) - (source[location] + lf * lf)) /
                    (2.0f * qf - 2.0f * lf);

                if (intersection > boundaries[k])
                {
                    break;
                }
                if (k == 0u)
                {
                    break;
                }
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
                ++k;
                locations[k] = q;
                boundaries[k] = intersection;
                boundaries[k + 1u] = inf;
            }
        }

        k = 0u;
        for (uint32_t q = 0u; q < count; ++q)
        {
            while (boundaries[k + 1u] < static_cast<float>(q))
            {
                ++k;
            }
            const float distance = static_cast<float>(q) - static_cast<float>(locations[k]);
            distances[q] = distance * distance + source[locations[k]];
        }
    }

    void distance_transform_2d(
        const std::vector<float>& source,
        uint32_t width,
        uint32_t height,
        std::vector<float>& distances)
    {
        // Two 1D passes are enough because squared Euclidean distance is separable
        std::vector<float> temp(source.size(), 0.0f);
        std::vector<float> line(std::max(width, height), 0.0f);
        std::vector<float> lineDistances(std::max(width, height), 0.0f);

        for (uint32_t x = 0u; x < width; ++x)
        {
            for (uint32_t y = 0u; y < height; ++y)
            {
                line[y] = source[static_cast<size_t>(y) * width + x];
            }
            distance_transform_1d(line, lineDistances, height);
            for (uint32_t y = 0u; y < height; ++y)
            {
                temp[static_cast<size_t>(y) * width + x] = lineDistances[y];
            }
        }

        for (uint32_t y = 0u; y < height; ++y)
        {
            for (uint32_t x = 0u; x < width; ++x)
            {
                line[x] = temp[static_cast<size_t>(y) * width + x];
            }
            distance_transform_1d(line, lineDistances, width);
            for (uint32_t x = 0u; x < width; ++x)
            {
                distances[static_cast<size_t>(y) * width + x] = lineDistances[x];
            }
        }
    }

    std::vector<unsigned char> make_sdf_from_alpha(
        const std::vector<unsigned char>& alpha,
        uint32_t width,
        uint32_t height)
    {
        // Inside and outside distance fields are subtracted to create a signed edge value
        constexpr float inf = 1.0e20f;
        const size_t pixelCount = static_cast<size_t>(width) * static_cast<size_t>(height);
        std::vector<float> insideSource(pixelCount, inf);
        std::vector<float> outsideSource(pixelCount, inf);
        bool hasInside = false;
        bool hasOutside = false;

        for (size_t i = 0u; i < pixelCount; ++i)
        {
            const bool inside = alpha[i] >= 128u;
            if (inside)
            {
                insideSource[i] = 0.0f;
                hasInside = true;
            }
            else
            {
                outsideSource[i] = 0.0f;
                hasOutside = true;
            }
        }

        if (!hasInside || !hasOutside)
        {
            return {};
        }

        std::vector<float> distanceToInside(pixelCount, 0.0f);
        std::vector<float> distanceToOutside(pixelCount, 0.0f);
        distance_transform_2d(insideSource, width, height, distanceToInside);
        distance_transform_2d(outsideSource, width, height, distanceToOutside);

        const float pixelDistScale = static_cast<float>(kSdfOnEdgeValue) / static_cast<float>(kSdfPadding);
        std::vector<unsigned char> sdf(pixelCount, 0);
        for (size_t i = 0u; i < pixelCount; ++i)
        {
            float signedDistance = std::sqrt(distanceToOutside[i]) - std::sqrt(distanceToInside[i]);
            if (alpha[i] != 0u && alpha[i] != 255u)
            {
                signedDistance += static_cast<float>(alpha[i]) / 255.0f - 0.5f;
            }
            sdf[i] = clamp_sdf_value(static_cast<float>(kSdfOnEdgeValue) + signedDistance * pixelDistScale);
        }
        return sdf;
    }

    struct LoadedFontFace
    {
        std::filesystem::path path;
        std::vector<unsigned char> data;
        FT_Face face = nullptr;
    };

    std::vector<std::filesystem::path> font_candidate_paths(
        const std::filesystem::path& primaryPath,
        const Renderer2DFontAtlasLoadOptions& options)
    {
        // Primary font is tried first, then configured fallbacks in stable order
        std::vector<std::filesystem::path> paths;
        auto add_path = [&paths](const std::filesystem::path& candidate) {
            if (candidate.empty())
            {
                return;
            }
            if (std::find(paths.begin(), paths.end(), candidate) == paths.end())
            {
                paths.push_back(candidate);
            }
        };

        add_path(primaryPath);
        for (const std::filesystem::path& fallback : options.fallbackFontPaths)
        {
            add_path(fallback);
        }
        return paths;
    }

    bool load_font_face(
        FT_Library library,
        const std::filesystem::path& path,
        int requestedFontWeight,
        float pixelHeight,
        LoadedFontFace& out)
    {
        Logger* logger = Logger::fetch_logger();
        out.path = path;
        out.data = read_binary_file(path);
        if (out.data.empty())
        {
            logger->vulkan("Failed to read Renderer2D font: " + path.string());
            return false;
        }

        if (FT_New_Memory_Face(
            library,
            out.data.data(),
            static_cast<FT_Long>(out.data.size()),
            choose_weighted_face_index(library, out.data, requestedFontWeight),
            &out.face) != 0)
        {
            logger->vulkan("Failed to initialise Renderer2D font face: " + path.string());
            return false;
        }

        if (FT_Select_Charmap(out.face, FT_ENCODING_UNICODE) != 0 && out.face->num_charmaps > 0)
        {
            FT_Set_Charmap(out.face, out.face->charmaps[0]);
        }

        const bool appliedVariableWeight = apply_font_weight_axis(library, out.face, requestedFontWeight);
        if (requestedFontWeight > 0)
        {
            // Logging the chosen weight makes mismatched font faces easier to diagnose
            const int selectedStaticWeight = inferred_face_weight(out.face);
            if (appliedVariableWeight)
            {
                logger->vulkan("Applied Renderer2D font variable weight " +
                    std::to_string(requestedFontWeight) + " for " + path.string() + ".");
            }
            else if (selectedStaticWeight > 0)
            {
                logger->vulkan("Selected Renderer2D font face weight " +
                    std::to_string(selectedStaticWeight) + " for requested weight " +
                    std::to_string(requestedFontWeight) + ": " + path.string() + ".");
            }
        }

        if (FT_Set_Pixel_Sizes(out.face, 0, static_cast<FT_UInt>(std::max(pixelHeight, 1.0f))) != 0)
        {
            logger->vulkan("Failed to set Renderer2D font size: " + path.string());
            FT_Done_Face(out.face);
            out.face = nullptr;
            return false;
        }

        return true;
    }

    std::size_t choose_face_for_codepoint(const std::vector<LoadedFontFace>& faces, uint32_t codepoint)
    {
        for (std::size_t i = 0u; i < faces.size(); ++i)
        {
            if (faces[i].face && FT_Get_Char_Index(faces[i].face, codepoint) != 0u)
            {
                return i;
            }
        }
        return static_cast<std::size_t>(-1);
    }
}

std::vector<std::filesystem::path> renderer2d_common_font_fallbacks(
    const std::filesystem::path& assetFontDirectory)
{
    return renderer2d_common_font_fallbacks(
        assetFontDirectory.empty()
            ? std::vector<std::filesystem::path> {}
            : std::vector<std::filesystem::path> { assetFontDirectory });
}

std::vector<std::filesystem::path> renderer2d_font_candidates(
    const std::vector<std::filesystem::path>& assetFontDirectories,
    const std::filesystem::path& relativeFontPath)
{
    std::vector<std::filesystem::path> paths;
    if (relativeFontPath.is_absolute())
    {
        add_existing_font_path(paths, relativeFontPath);
        return paths;
    }
    for (auto it = assetFontDirectories.rbegin(); it != assetFontDirectories.rend(); ++it)
    {
        add_existing_font_path(paths, *it / relativeFontPath);
    }
    return paths;
}

std::vector<std::filesystem::path> renderer2d_common_font_fallbacks(
    const std::vector<std::filesystem::path>& assetFontDirectories)
{
    std::vector<std::filesystem::path> paths;
    const auto addLayeredFont = [&paths, &assetFontDirectories](std::string_view fileName) {
        for (std::filesystem::path& candidate :
            renderer2d_font_candidates(assetFontDirectories, std::filesystem::path(fileName)))
        {
            add_existing_font_path(paths, candidate);
        }
    };

    // Bundled Noto fonts cover common scripts while preserving Inter as primary Latin
    addLayeredFont("NotoSans-Variable.ttf");
    addLayeredFont("NotoSansSC-VariableFont_wght.ttf");
    addLayeredFont("NotoSansTC-VariableFont_wght.ttf");
    addLayeredFont("NotoSansJP-VariableFont_wght.ttf");
    addLayeredFont("NotoSansKR-Regular.ttf");
    addLayeredFont("NotoSansArabic-Variable.ttf");
    addLayeredFont("NotoSansThai-Variable.ttf");

    // Optional system fonts help with Devanagari and extra Cyrillic coverage when installed
    add_existing_font_path(paths, std::filesystem::path("C:/Windows/Fonts/Nirmala.ttc"));
    add_existing_font_path(paths, std::filesystem::path("C:/Windows/Fonts/Mangal.ttf"));
    add_existing_font_path(paths, std::filesystem::path("C:/Windows/Fonts/arial.ttf"));
    add_existing_font_path(paths, std::filesystem::path("C:/Windows/Fonts/segoeui.ttf"));
    add_existing_font_path(paths, std::filesystem::path("C:/Windows/Fonts/msyh.ttc"));
    add_existing_font_path(paths, std::filesystem::path("C:/Windows/Fonts/YuGothR.ttc"));
    add_existing_font_path(paths, std::filesystem::path("C:/Windows/Fonts/malgun.ttf"));

    return paths;
}

bool renderer2d_load_font_profile(
    const std::filesystem::path& profilePath,
    const std::filesystem::path& assetFontDirectory,
    Renderer2DFontProfile& outProfile)
{
    return renderer2d_load_font_profile(
        profilePath,
        assetFontDirectory.empty()
            ? std::vector<std::filesystem::path> {}
            : std::vector<std::filesystem::path> { assetFontDirectory },
        outProfile);
}

bool renderer2d_load_font_profile(
    const std::filesystem::path& profilePath,
    const std::vector<std::filesystem::path>& assetFontDirectories,
    Renderer2DFontProfile& outProfile)
{
    outProfile = {};
    if (profilePath.empty())
    {
        return false;
    }

    std::vector<unsigned char> data = read_binary_file(profilePath);
    if (data.empty())
    {
        return false;
    }

    const std::string json(data.begin(), data.end());
    FontProfileJsonParser parser(json, assetFontDirectories);
    return parser.parse(outProfile);
}

bool renderer2d_load_font_profile_layers(
    const std::vector<std::filesystem::path>& profilePaths,
    const std::vector<std::filesystem::path>& assetFontDirectories,
    Renderer2DFontProfile& outProfile)
{
    outProfile = {};
    bool loadedAny = false;
    for (const std::filesystem::path& profilePath : profilePaths)
    {
        Renderer2DFontProfile layer = {};
        if (!renderer2d_load_font_profile(profilePath, assetFontDirectories, layer))
        {
            if (Logger* logger = Logger::fetch_logger())
            {
                logger->warning(
                    "Ignoring invalid font profile layer and keeping lower-priority defaults: " +
                    profilePath.string());
            }
            continue;
        }

        loadedAny = true;
        if (!layer.defaultFontPath.empty())
        {
            outProfile.defaultFontPath = std::move(layer.defaultFontPath);
            outProfile.defaultFontCandidates = std::move(layer.defaultFontCandidates);
        }
        for (std::filesystem::path& path : layer.fallbackFontPaths)
        {
            if (std::find(outProfile.fallbackFontPaths.begin(), outProfile.fallbackFontPaths.end(), path) ==
                outProfile.fallbackFontPaths.end())
            {
                outProfile.fallbackFontPaths.push_back(std::move(path));
            }
        }
        for (auto& [locale, path] : layer.localeFontPaths)
        {
            outProfile.localeFontPaths[locale] = std::move(path);
        }
        for (auto& [locale, paths] : layer.localeFontCandidates)
        {
            outProfile.localeFontCandidates[locale] = std::move(paths);
        }
    }
    return loadedAny;
}

std::filesystem::path renderer2d_primary_font_for_locale(
    const std::filesystem::path& assetFontDirectory,
    std::string_view locale,
    const std::filesystem::path& defaultFontPath)
{
    return renderer2d_primary_font_for_locale(
        assetFontDirectory.empty()
            ? std::vector<std::filesystem::path> {}
            : std::vector<std::filesystem::path> { assetFontDirectory },
        locale,
        defaultFontPath);
}

std::filesystem::path renderer2d_primary_font_for_locale(
    const std::vector<std::filesystem::path>& assetFontDirectories,
    std::string_view locale,
    const std::filesystem::path& defaultFontPath)
{
    const std::string value = normalised_locale(locale);
    auto font = [&assetFontDirectories](std::string_view filename) {
        for (auto it = assetFontDirectories.rbegin(); it != assetFontDirectories.rend(); ++it)
        {
            if (std::filesystem::path path = existing_font_path(*it / std::string(filename)); !path.empty())
            {
                return path;
            }
        }
        return std::filesystem::path {};
    };

    if (locale_starts_with(value, "zh_cn") || locale_starts_with(value, "zh_sg"))
    {
        if (std::filesystem::path path = font("NotoSansSC-VariableFont_wght.ttf"); !path.empty())
        {
            return path;
        }
    }
    if (locale_starts_with(value, "zh_tw") ||
        locale_starts_with(value, "zh_hk") ||
        locale_starts_with(value, "zh_mo"))
    {
        if (std::filesystem::path path = font("NotoSansTC-VariableFont_wght.ttf"); !path.empty())
        {
            return path;
        }
    }
    if (locale_starts_with(value, "ja"))
    {
        if (std::filesystem::path path = font("NotoSansJP-VariableFont_wght.ttf"); !path.empty())
        {
            return path;
        }
    }
    if (locale_starts_with(value, "ko"))
    {
        if (std::filesystem::path path = font("NotoSansKR-Regular.ttf"); !path.empty())
        {
            return path;
        }
    }
    if (locale_starts_with(value, "ar"))
    {
        if (std::filesystem::path path = font("NotoSansArabic-Variable.ttf"); !path.empty())
        {
            return path;
        }
    }
    if (locale_starts_with(value, "hi"))
    {
        if (std::filesystem::path path = existing_font_path("C:/Windows/Fonts/Nirmala.ttc"); !path.empty())
        {
            return path;
        }
        if (std::filesystem::path path = existing_font_path("C:/Windows/Fonts/Mangal.ttf"); !path.empty())
        {
            return path;
        }
        if (std::filesystem::path path = font("NotoSans-Variable.ttf"); !path.empty())
        {
            return path;
        }
    }
    if (locale_starts_with(value, "ru") ||
        locale_starts_with(value, "uk") ||
        locale_starts_with(value, "bg") ||
        locale_starts_with(value, "sr"))
    {
        if (std::filesystem::path path = font("NotoSans-Variable.ttf"); !path.empty())
        {
            return path;
        }
    }

    if (!defaultFontPath.empty())
    {
        return defaultFontPath;
    }
    if (std::filesystem::path path = font("Inter.ttc"); !path.empty())
    {
        return path;
    }
    if (std::filesystem::path path = font("NotoSans-Variable.ttf"); !path.empty())
    {
        return path;
    }
    return {};
}

std::filesystem::path renderer2d_primary_font_for_locale(
    const Renderer2DFontProfile& profile,
    std::string_view locale,
    const std::filesystem::path& defaultFontPath)
{
    const std::string value = normalised_locale(locale);
    auto findProfileFont = [&profile](std::string_view key) -> std::filesystem::path {
        const auto found = profile.localeFontPaths.find(std::string(key));
        return found == profile.localeFontPaths.end() ? std::filesystem::path {} : found->second;
    };

    if (std::filesystem::path path = findProfileFont(value); !path.empty())
    {
        return path;
    }

    const std::size_t separator = value.find('_');
    if (separator != std::string::npos)
    {
        if (std::filesystem::path path = findProfileFont(std::string_view(value).substr(0u, separator)); !path.empty())
        {
            return path;
        }
    }

    if (!defaultFontPath.empty())
    {
        return defaultFontPath;
    }
    return profile.defaultFontPath;
}

bool Renderer2DFontAtlas::load_from_file(
    const std::filesystem::path& path,
    const Renderer2DFontAtlasLoadOptions& options,
    VmaAllocator& allocator,
    vk::CommandBuffer commandBuffer,
    vk::Queue queue,
    vk::Device logicalDevice,
    std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
    std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue)
{
    Logger* logger = Logger::fetch_logger();
    glyphs.clear();
    kerningTable.clear();
    isLoaded = false;
    const int requestedFontWeight = clamped_font_weight(options.fontWeight);
    const std::vector<std::filesystem::path> fontPaths = font_candidate_paths(path, options);
    fontName = fontPaths.empty() ? path.filename().string() : fontPaths.front().filename().string();
    if (fontPaths.size() > 1u)
    {
        fontName += "+fallbacks";
    }
    if (requestedFontWeight > 0)
    {
        fontName += "@wght";
        fontName += std::to_string(requestedFontWeight);
    }
    atlasSize = { kAtlasWidth, kAtlasHeight };
    const std::vector<uint32_t> requestedCodepoints = build_codepoint_list(options);

    std::vector<unsigned char> rgba(kAtlasWidth * kAtlasHeight * 4, 0);
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0)
    {
        logger->vulkan("Failed to initialise FreeType for Renderer2D font loading.");
    }
    else
    {
        std::vector<LoadedFontFace> faces;
        faces.reserve(fontPaths.size());
        for (const std::filesystem::path& fontPath : fontPaths)
        {
            LoadedFontFace loadedFace = {};
            if (load_font_face(library, fontPath, requestedFontWeight, bakedPixelHeight, loadedFace))
            {
                faces.push_back(std::move(loadedFace));
            }
        }

        if (faces.empty())
        {
            logger->vulkan("Renderer2D font atlas has no usable font faces.");
        }
        else
        {
            baseline = ft_26_6_to_float(faces.front().face->size->metrics.ascender);
            lineHeight = ft_26_6_to_float(faces.front().face->size->metrics.height);
            for (const LoadedFontFace& loadedFace : faces)
            {
                lineHeight = std::max(lineHeight, ft_26_6_to_float(loadedFace.face->size->metrics.height));
            }
            if (lineHeight <= 0.0f)
            {
                lineHeight = std::max(baseline, bakedPixelHeight);
            }
            sdfPixelRange = static_cast<float>(kSdfPadding);

            uint32_t cursorX = 1;
            uint32_t cursorY = 1;
            uint32_t rowHeight = 0;
            uint32_t renderedGlyphs = 0;
            uint32_t missingGlyphs = 0;
            bool packedAllGlyphs = true;

            for (uint32_t codepoint : requestedCodepoints)
            {
                GlyphRecord& record = glyphs[codepoint];
                const std::size_t faceIndex = choose_face_for_codepoint(faces, codepoint);
                if (faceIndex == static_cast<std::size_t>(-1))
                {
                    ++missingGlyphs;
                    continue;
                }

                FT_Face face = faces[faceIndex].face;
                const FT_UInt glyphIndex = FT_Get_Char_Index(face, codepoint);
                if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT) != 0)
                {
                    ++missingGlyphs;
                    continue;
                }

                record.advance = ft_26_6_to_float(face->glyph->advance.x);
                record.sourceFace = static_cast<uint32_t>(faceIndex);
                record.valid = true;

                if (codepoint == ' ')
                {
                    continue;
                }

                if (FT_Render_Glyph(face->glyph, FT_RENDER_MODE_NORMAL) != 0)
                {
                    continue;
                }

                const FT_Bitmap& bitmap = face->glyph->bitmap;
                if (bitmap.width == 0u || bitmap.rows == 0u)
                {
                    continue;
                }

                const uint32_t glyphWidth = bitmap.width + static_cast<uint32_t>(kSdfPadding * 2);
                const uint32_t glyphHeight = bitmap.rows + static_cast<uint32_t>(kSdfPadding * 2);
                const std::vector<unsigned char> alpha = padded_glyph_alpha(bitmap, glyphWidth, glyphHeight);
                const std::vector<unsigned char> sdf = make_sdf_from_alpha(alpha, glyphWidth, glyphHeight);
                if (sdf.empty())
                {
                    continue;
                }

                if (cursorX + glyphWidth + 1u >= kAtlasWidth)
                {
                    cursorX = 1u;
                    cursorY += rowHeight + 1u;
                    rowHeight = 0u;
                }

                if (cursorY + glyphHeight + 1u >= kAtlasHeight)
                {
                    packedAllGlyphs = false;
                    record = {};
                    continue;
                }

                for (uint32_t y = 0u; y < glyphHeight; ++y)
                {
                    for (uint32_t x = 0u; x < glyphWidth; ++x)
                    {
                        const uint32_t atlasX = cursorX + x;
                        const uint32_t atlasY = cursorY + y;
                        const size_t dst = (static_cast<size_t>(atlasY) * kAtlasWidth + atlasX) * 4u;
                        const unsigned char value = sdf[static_cast<size_t>(y) * glyphWidth + x];
                        rgba[dst + 0u] = value;
                        rgba[dst + 1u] = value;
                        rgba[dst + 2u] = value;
                        rgba[dst + 3u] = value;
                    }
                }

                record.offset = {
                    static_cast<float>(face->glyph->bitmap_left - kSdfPadding),
                    static_cast<float>(-face->glyph->bitmap_top - kSdfPadding)
                };
                record.size = { static_cast<float>(glyphWidth), static_cast<float>(glyphHeight) };
                record.uvMin = {
                    static_cast<float>(cursorX) / static_cast<float>(kAtlasWidth),
                    static_cast<float>(cursorY) / static_cast<float>(kAtlasHeight)
                };
                record.uvMax = {
                    static_cast<float>(cursorX + glyphWidth) / static_cast<float>(kAtlasWidth),
                    static_cast<float>(cursorY + glyphHeight) / static_cast<float>(kAtlasHeight)
                };
                record.drawable = true;

                cursorX += glyphWidth + 1u;
                rowHeight = std::max(rowHeight, glyphHeight);
                ++renderedGlyphs;
            }

            for (uint32_t left : requestedCodepoints)
            {
                const auto leftIt = glyphs.find(left);
                if (leftIt == glyphs.end() || !leftIt->second.valid)
                {
                    continue;
                }
                for (uint32_t right : requestedCodepoints)
                {
                    const auto rightIt = glyphs.find(right);
                    if (rightIt == glyphs.end() ||
                        !rightIt->second.valid ||
                        leftIt->second.sourceFace != rightIt->second.sourceFace ||
                        leftIt->second.sourceFace >= faces.size())
                    {
                        continue;
                    }

                    FT_Face face = faces[leftIt->second.sourceFace].face;
                    if (!FT_HAS_KERNING(face))
                    {
                        continue;
                    }

                    const FT_UInt leftGlyph = FT_Get_Char_Index(face, left);
                    const FT_UInt rightGlyph = FT_Get_Char_Index(face, right);
                    if (leftGlyph == 0u || rightGlyph == 0u)
                    {
                        continue;
                    }

                    FT_Vector kerning = {};
                    if (FT_Get_Kerning(face, leftGlyph, rightGlyph, FT_KERNING_DEFAULT, &kerning) == 0)
                    {
                        kerningTable[kerning_key(left, right)] = ft_26_6_to_float(kerning.x);
                    }
                }
            }

            const bool incompleteRequiredGlyphs =
                options.requireRequestedGlyphs &&
                (missingGlyphs > 0u || !packedAllGlyphs);
            isLoaded = renderedGlyphs > 0u && !incompleteRequiredGlyphs;
            if (isLoaded)
            {
                logger->vulkan("Loaded Renderer2D SDF font atlas from " + fontName + ".");
                if (!packedAllGlyphs)
                {
                    logger->vulkan("Renderer2D font atlas did not fit every requested glyph: " + fontName + ".");
                }
            }
            else if (incompleteRequiredGlyphs)
            {
                logger->vulkan("Renderer2D font is missing or could not pack required glyphs: " + fontName + ".");
            }
            else if (!packedAllGlyphs)
            {
                logger->vulkan("Renderer2D font atlas was too small for every glyph in " + fontName + ".");
            }
            else
            {
                logger->vulkan("Renderer2D font produced no drawable glyphs: " + fontName + ".");
            }

            if (missingGlyphs > 0u)
            {
                logger->vulkan("Renderer2D font is missing " + std::to_string(missingGlyphs) +
                    " requested glyphs: " + fontName + ".");
            }
        }

        for (LoadedFontFace& loadedFace : faces)
        {
            if (loadedFace.face != nullptr)
            {
                FT_Done_Face(loadedFace.face);
                loadedFace.face = nullptr;
            }
        }
        FT_Done_FreeType(library);
    }

    if (atlasImage == nullptr ||
        atlasImage->extent.width != kAtlasWidth ||
        atlasImage->extent.height != kAtlasHeight)
    {
        atlasImage = new StorageImage(allocator, vk::Format::eR8G8B8A8Unorm,
            vk::Extent2D { kAtlasWidth, kAtlasHeight }, commandBuffer, queue,
            logicalDevice, vmaDeletionQueue, deviceDeletionQueue);
    }
    if (!upload_rgba_to_image(allocator, commandBuffer, queue, *atlasImage, rgba))
    {
        logger->vulkan("Failed to upload Renderer2D font atlas.");
        isLoaded = false;
    }

    return isLoaded;
}

bool Renderer2DFontAtlas::load_from_file(
    const std::filesystem::path& path,
    VmaAllocator& allocator,
    vk::CommandBuffer commandBuffer,
    vk::Queue queue,
    vk::Device logicalDevice,
    std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
    std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue)
{
    Renderer2DFontAtlasLoadOptions options = {};
    return load_from_file(
        path,
        options,
        allocator,
        commandBuffer,
        queue,
        logicalDevice,
        vmaDeletionQueue,
        deviceDeletionQueue);
}

Renderer2DTextLayout Renderer2DFontAtlas::layout_text(std::string_view text, float fontSize) const
{
    Renderer2DTextLayout layout;
    if (!isLoaded || fontSize <= 0.0f)
    {
        return layout;
    }

    const float scale = fontSize / std::max(bakedPixelHeight, 1.0f);
    const float scaledLineHeight = lineHeight * scale;
    const float scaledBaseline = baseline * scale;
    float penX = 0.0f;
    float penY = 0.0f;
    float maxX = 0.0f;
    glm::vec2 inkMin { std::numeric_limits<float>::max() };
    glm::vec2 inkMax { std::numeric_limits<float>::lowest() };
    uint32_t previous = 0;

    for (uint32_t codepoint : decode_utf8(text))
    {
        if (codepoint == '\n')
        {
            maxX = std::max(maxX, penX);
            penX = 0.0f;
            penY += scaledLineHeight;
            previous = 0;
            continue;
        }

        const GlyphRecord* glyph = glyph_for(codepoint);
        if (glyph == nullptr || !glyph->valid)
        {
            glyph = glyph_for('?');
            codepoint = '?';
        }
        if (glyph == nullptr || !glyph->valid)
        {
            continue;
        }

        if (previous != 0)
        {
            penX += kerning(previous, codepoint) * scale;
        }

        if (glyph->drawable)
        {
            MSDFGlyph outGlyph = {};
            outGlyph.codepoint = codepoint;
            outGlyph.position = {
                penX + glyph->offset.x * scale,
                penY + scaledBaseline + glyph->offset.y * scale
            };
            outGlyph.size = glyph->size * scale;
            outGlyph.uvMin = glyph->uvMin;
            outGlyph.uvMax = glyph->uvMax;
            outGlyph.advance = glyph->advance * scale;
            // Align by visible ink, not the padded SDF atlas rect, so mixed font sizes share the same edge
            const float inkInset = std::min(
                sdfPixelRange * scale,
                std::max(std::min(outGlyph.size.x, outGlyph.size.y) * 0.45f, 0.0f));
            const glm::vec2 glyphInkMin = outGlyph.position + glm::vec2(inkInset);
            const glm::vec2 glyphInkMax = outGlyph.position + outGlyph.size - glm::vec2(inkInset);
            if (glyphInkMax.x > glyphInkMin.x && glyphInkMax.y > glyphInkMin.y)
            {
                inkMin = glm::min(inkMin, glyphInkMin);
                inkMax = glm::max(inkMax, glyphInkMax);
            }
            else
            {
                inkMin = glm::min(inkMin, outGlyph.position);
                inkMax = glm::max(inkMax, outGlyph.position + outGlyph.size);
            }
            layout.glyphs.push_back(outGlyph);
        }

        penX += glyph->advance * scale;
        maxX = std::max(maxX, penX);
        previous = codepoint;
    }

    layout.lineHeight = scaledLineHeight;
    if (!layout.glyphs.empty())
    {
        const glm::vec2 origin = (std::isfinite(inkMin.x) && std::isfinite(inkMin.y)) ? inkMin : glm::vec2(0.0f);
        for (MSDFGlyph& glyph : layout.glyphs)
        {
            glyph.position -= origin;
        }
        layout.bounds = glm::max(inkMax - origin, glm::vec2(0.0f));
    }
    else
    {
        layout.bounds = {
            std::max(maxX, penX),
            penY + scaledLineHeight
        };
    }
    return layout;
}

StorageImage* Renderer2DFontAtlas::image() const
{
    return atlasImage;
}

bool Renderer2DFontAtlas::loaded() const
{
    return isLoaded;
}

uint32_t Renderer2DFontAtlas::atlas_id() const
{
    return atlasId;
}

glm::uvec2 Renderer2DFontAtlas::texture_size() const
{
    return atlasSize;
}

float Renderer2DFontAtlas::pixel_range() const
{
    return sdfPixelRange;
}

const std::string& Renderer2DFontAtlas::name() const
{
    return fontName;
}

const Renderer2DFontAtlas::GlyphRecord* Renderer2DFontAtlas::glyph_for(uint32_t codepoint) const
{
    const auto it = glyphs.find(codepoint);
    return it != glyphs.end() ? &it->second : nullptr;
}

float Renderer2DFontAtlas::kerning(uint32_t left, uint32_t right) const
{
    const auto it = kerningTable.find(kerning_key(left, right));
    return it != kerningTable.end() ? it->second : 0.0f;
}

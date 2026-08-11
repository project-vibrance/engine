#include <vibranceUI/renderer/media2d.h>
#include <vibranceUI/renderer/descriptors.h>
#include <vibranceUI/core/file.h>
#include <vibranceUI/core/logger.h>
#include "../animation/lottie.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include <stb_image.h>

#ifndef VIBRANCE_HAS_NANOSVG
#define VIBRANCE_HAS_NANOSVG 0
#endif

#ifndef VIBRANCE_HAS_FFMPEG
#define VIBRANCE_HAS_FFMPEG 0
#endif

#if VIBRANCE_HAS_NANOSVG
#define NANOSVG_IMPLEMENTATION
#include <nanosvg.h>
#define NANOSVGRAST_IMPLEMENTATION
#include <nanosvgrast.h>
#endif

#if VIBRANCE_HAS_FFMPEG
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
}
#endif

namespace
{
    struct DecodedMediaFrame
    {
        // CPU-side RGBA frame before it is uploaded to a StorageImage
        std::vector<unsigned char> rgba;
        uint32_t width = 0;
        uint32_t height = 0;
        double durationSeconds = 0.0;

        bool valid() const
        {
            return !rgba.empty() && width > 0u && height > 0u;
        }
    };

    struct DecodedMediaAnimation
    {
        std::vector<DecodedMediaFrame> frames;
        // Preserve container/stream metadata independently from any optional
        // frame sampling used to bound GPU memory.
        double sourceDurationSeconds = 0.0;
        double sourceFrameRate = 0.0;

        bool valid() const
        {
            return !frames.empty() && frames.front().valid();
        }

        double duration_seconds() const
        {
            double duration = 0.0;
            for (const DecodedMediaFrame& frame : frames)
            {
                duration += std::max(frame.durationSeconds, 0.0);
            }
            return duration;
        }
    };

    float smoothstep01(float edge0, float edge1, float value)
    {
        const float t = std::clamp((value - edge0) / std::max(edge1 - edge0, 0.0001f), 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float black_background_sample_score(const DecodedMediaFrame& frame, float u, float v)
    {
        if (!frame.valid())
        {
            return 0.0f;
        }

        const uint32_t x = std::min(
            static_cast<uint32_t>(std::clamp(u, 0.0f, 1.0f) * static_cast<float>(frame.width)),
            frame.width - 1u);
        const uint32_t y = std::min(
            static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * static_cast<float>(frame.height)),
            frame.height - 1u);
        const std::size_t at = (static_cast<std::size_t>(y) * frame.width + x) * 4u;
        if (at + 3u >= frame.rgba.size())
        {
            return 0.0f;
        }

        const float r = static_cast<float>(frame.rgba[at + 0u]) / 255.0f;
        const float g = static_cast<float>(frame.rgba[at + 1u]) / 255.0f;
        const float b = static_cast<float>(frame.rgba[at + 2u]) / 255.0f;
        const float a = static_cast<float>(frame.rgba[at + 3u]) / 255.0f;
        const float alphaWeight = smoothstep01(0.08f, 0.35f, a);
        const float luminance = r * 0.2126f + g * 0.7152f + b * 0.0722f;
        const float darkness = 1.0f - smoothstep01(0.06f, 0.22f, luminance);
        return darkness * alphaWeight;
    }

    bool media_frame_has_black_background(const DecodedMediaFrame& frame)
    {
        // Detect likely black matte images so the shader can lift dark transparent edges
        if (!frame.valid())
        {
            return false;
        }

        float majorityScore = 0.0f;
        for (int y = 0; y < 4; ++y)
        {
            for (int x = 0; x < 4; ++x)
            {
                majorityScore += black_background_sample_score(
                    frame,
                    (static_cast<float>(x) + 0.5f) * 0.25f,
                    (static_cast<float>(y) + 0.5f) * 0.25f);
            }
        }
        majorityScore *= 1.0f / 16.0f;

        float edgeScore = 0.0f;
        edgeScore += black_background_sample_score(frame, 0.02f, 0.02f);
        edgeScore += black_background_sample_score(frame, 0.50f, 0.02f);
        edgeScore += black_background_sample_score(frame, 0.98f, 0.02f);
        edgeScore += black_background_sample_score(frame, 0.02f, 0.50f);
        edgeScore += black_background_sample_score(frame, 0.98f, 0.50f);
        edgeScore += black_background_sample_score(frame, 0.02f, 0.98f);
        edgeScore += black_background_sample_score(frame, 0.50f, 0.98f);
        edgeScore += black_background_sample_score(frame, 0.98f, 0.98f);
        edgeScore *= 1.0f / 8.0f;

        const float mostlyBlack = smoothstep01(0.40f, 0.66f, majorityScore);
        const float blackEdges = smoothstep01(0.42f, 0.68f, edgeScore);
        return std::max(mostlyBlack, blackEdges) > 0.5f;
    }

    std::string to_lower(std::string value)
    {
        std::transform(value.begin(), value.end(), value.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return value;
    }

    Media2DSourceType source_type_from_path(const std::filesystem::path& path)
    {
        // Dispatch media loading by extension while exposing one Media2DHandle type
        const std::string extension = to_lower(path.extension().string());
        if (extension == ".svg" || extension == ".svgz")
        {
            return Media2DSourceType::eSvg;
        }

        if (extension == ".json")
        {
            return Media2DSourceType::eLottie;
        }

        if (extension == ".mp4" || extension == ".mov" || extension == ".m4v" ||
            extension == ".webm" || extension == ".mkv" || extension == ".avi")
        {
            return Media2DSourceType::eVideo;
        }

        return Media2DSourceType::eRasterImage;
    }

    bool has_any(std::string_view text, std::initializer_list<std::string_view> needles)
    {
        for (std::string_view needle : needles)
        {
            if (text.find(needle) != std::string_view::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::optional<float> parse_float_prefix(std::string_view value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        {
            value.remove_prefix(1);
        }

        std::size_t count = 0;
        bool sawDigit = false;
        while (count < value.size())
        {
            const char c = value[count];
            if (std::isdigit(static_cast<unsigned char>(c)))
            {
                sawDigit = true;
                ++count;
                continue;
            }
            if (c == '+' || c == '-' || c == '.' || c == 'e' || c == 'E')
            {
                ++count;
                continue;
            }
            break;
        }

        if (!sawDigit || count == 0)
        {
            return std::nullopt;
        }

        try
        {
            return std::stof(std::string(value.substr(0, count)));
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    std::optional<float> parse_svg_attribute_float(std::string_view svg, std::string_view name)
    {
        // SVG dimensions are read directly so raster size can be inferred when needed
        const std::string quoted = std::string(name) + "=";
        std::size_t at = svg.find(quoted);
        if (at == std::string_view::npos)
        {
            return std::nullopt;
        }

        at += quoted.size();
        if (at >= svg.size())
        {
            return std::nullopt;
        }

        const char quote = svg[at];
        if (quote != '"' && quote != '\'')
        {
            return parse_float_prefix(svg.substr(at));
        }

        const std::size_t end = svg.find(quote, at + 1);
        if (end == std::string_view::npos)
        {
            return std::nullopt;
        }

        return parse_float_prefix(svg.substr(at + 1, end - at - 1));
    }

    glm::uvec2 parse_svg_pixel_size(std::string_view svg)
    {
        glm::uvec2 size { 0u };
        const std::optional<float> width = parse_svg_attribute_float(svg, "width");
        const std::optional<float> height = parse_svg_attribute_float(svg, "height");
        if (width && height && *width > 0.0f && *height > 0.0f)
        {
            return {
                static_cast<uint32_t>(std::round(*width)),
                static_cast<uint32_t>(std::round(*height))
            };
        }

        const std::size_t viewBoxAt = svg.find("viewBox=");
        if (viewBoxAt == std::string_view::npos)
        {
            return size;
        }

        std::size_t valueAt = viewBoxAt + 8u;
        if (valueAt >= svg.size())
        {
            return size;
        }

        const char quote = svg[valueAt];
        if (quote == '"' || quote == '\'')
        {
            ++valueAt;
        }

        const std::size_t valueEnd = quote == '"' || quote == '\''
            ? svg.find(quote, valueAt)
            : svg.find_first_of(" \t\r\n>", valueAt);
        if (valueEnd == std::string_view::npos || valueEnd <= valueAt)
        {
            return size;
        }

        std::stringstream stream(std::string(svg.substr(valueAt, valueEnd - valueAt)));
        float minX = 0.0f;
        float minY = 0.0f;
        float viewWidth = 0.0f;
        float viewHeight = 0.0f;
        stream >> minX >> minY >> viewWidth >> viewHeight;
        if (viewWidth > 0.0f && viewHeight > 0.0f)
        {
            size = {
                static_cast<uint32_t>(std::round(viewWidth)),
                static_cast<uint32_t>(std::round(viewHeight))
            };
        }
        return size;
    }

    bool svg_looks_animated(std::string_view svg)
    {
        return has_any(svg, {
            "<animate",
            "<set",
            "<animateTransform",
            "<animateMotion",
            "@keyframes",
            "animation:"
        });
    }

    std::string trim_copy(std::string_view value)
    {
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
        {
            value.remove_prefix(1);
        }
        while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
        {
            value.remove_suffix(1);
        }
        return std::string(value);
    }

    std::vector<std::string> split_css_tokens(std::string_view value)
    {
        std::vector<std::string> tokens;
        std::size_t cursor = 0;
        while (cursor < value.size())
        {
            while (cursor < value.size() && std::isspace(static_cast<unsigned char>(value[cursor])))
            {
                ++cursor;
            }
            const std::size_t start = cursor;
            while (cursor < value.size() && !std::isspace(static_cast<unsigned char>(value[cursor])))
            {
                ++cursor;
            }
            if (cursor > start)
            {
                tokens.emplace_back(value.substr(start, cursor - start));
            }
        }
        return tokens;
    }

    std::vector<std::string> split_css_list(std::string_view value, char separator)
    {
        std::vector<std::string> items;
        std::size_t start = 0;
        while (start <= value.size())
        {
            const std::size_t end = value.find(separator, start);
            const std::size_t count = end == std::string_view::npos ? value.size() - start : end - start;
            std::string item = trim_copy(value.substr(start, count));
            if (!item.empty())
            {
                items.push_back(std::move(item));
            }
            if (end == std::string_view::npos)
            {
                break;
            }
            start = end + 1u;
        }
        return items;
    }

    std::string remove_css_comments(std::string_view css)
    {
        std::string output;
        output.reserve(css.size());
        std::size_t cursor = 0;
        while (cursor < css.size())
        {
            if (cursor + 1u < css.size() && css[cursor] == '/' && css[cursor + 1u] == '*')
            {
                const std::size_t end = css.find("*/", cursor + 2u);
                cursor = end == std::string_view::npos ? css.size() : end + 2u;
                continue;
            }
            output.push_back(css[cursor++]);
        }
        return output;
    }

    std::optional<double> parse_css_time_seconds(std::string_view value)
    {
        const std::string trimmed = trim_copy(value);
        const std::optional<float> number = parse_float_prefix(trimmed);
        if (!number)
        {
            return std::nullopt;
        }

        if (trimmed.find("ms") != std::string::npos)
        {
            return static_cast<double>(*number) / 1000.0;
        }
        return static_cast<double>(*number);
    }

    std::unordered_map<std::string, std::string> parse_css_declarations(std::string_view body)
    {
        std::unordered_map<std::string, std::string> declarations;
        std::size_t start = 0;
        while (start < body.size())
        {
            const std::size_t end = body.find(';', start);
            const std::size_t count = end == std::string_view::npos ? body.size() - start : end - start;
            std::string_view declaration = body.substr(start, count);
            const std::size_t colon = declaration.find(':');
            if (colon != std::string_view::npos)
            {
                std::string key = to_lower(trim_copy(declaration.substr(0, colon)));
                std::string value = trim_copy(declaration.substr(colon + 1u));
                if (!key.empty() && !value.empty())
                {
                    declarations[std::move(key)] = std::move(value);
                }
            }
            if (end == std::string_view::npos)
            {
                break;
            }
            start = end + 1u;
        }
        return declarations;
    }

    std::size_t find_matching_brace(std::string_view text, std::size_t openBrace)
    {
        if (openBrace >= text.size() || text[openBrace] != '{')
        {
            return std::string_view::npos;
        }

        uint32_t depth = 0u;
        for (std::size_t cursor = openBrace; cursor < text.size(); ++cursor)
        {
            if (text[cursor] == '{')
            {
                ++depth;
            }
            else if (text[cursor] == '}')
            {
                --depth;
                if (depth == 0u)
                {
                    return cursor;
                }
            }
        }
        return std::string_view::npos;
    }

    struct SvgCssClassStyle
    {
        std::unordered_map<std::string, std::string> declarations;
        std::string animationName;
        double animationDurationSeconds = 0.0;
        double animationDelaySeconds = 0.0;
    };

    struct SvgCssTransform
    {
        double translateX = 0.0;
        double translateY = 0.0;
        double scale = 1.0;
    };

    struct SvgCssKeyframe
    {
        double offset = 0.0;
        std::optional<SvgCssTransform> transform;
        std::optional<double> opacity;
    };

    struct SvgCssKeyframes
    {
        std::vector<SvgCssKeyframe> frames;
    };

    struct SvgCssAnimationDocument
    {
        std::unordered_map<std::string, SvgCssClassStyle> classes;
        std::unordered_map<std::string, SvgCssKeyframes> keyframes;

        bool valid() const
        {
            return !classes.empty() && !keyframes.empty();
        }
    };

    std::optional<SvgCssTransform> parse_css_transform(std::string_view value)
    {
        SvgCssTransform transform = {};
        bool sawTransform = false;

        const std::size_t translateAt = value.find("translate");
        if (translateAt != std::string_view::npos)
        {
            const std::size_t open = value.find('(', translateAt);
            const std::size_t close = open == std::string_view::npos ? std::string_view::npos : value.find(')', open + 1u);
            if (open != std::string_view::npos && close != std::string_view::npos)
            {
                const std::string args = std::string(value.substr(open + 1u, close - open - 1u));
                std::vector<std::string> parts = split_css_list(args, ',');
                if (parts.size() < 2u)
                {
                    parts = split_css_tokens(args);
                }
                if (!parts.empty())
                {
                    if (const std::optional<float> x = parse_float_prefix(parts[0]))
                    {
                        transform.translateX = static_cast<double>(*x);
                        sawTransform = true;
                    }
                }
                if (parts.size() > 1u)
                {
                    if (const std::optional<float> y = parse_float_prefix(parts[1]))
                    {
                        transform.translateY = static_cast<double>(*y);
                        sawTransform = true;
                    }
                }
            }
        }

        const std::size_t scaleAt = value.find("scale");
        if (scaleAt != std::string_view::npos)
        {
            const std::size_t open = value.find('(', scaleAt);
            const std::size_t close = open == std::string_view::npos ? std::string_view::npos : value.find(')', open + 1u);
            if (open != std::string_view::npos && close != std::string_view::npos)
            {
                if (const std::optional<float> scale = parse_float_prefix(value.substr(open + 1u, close - open - 1u)))
                {
                    transform.scale = static_cast<double>(*scale);
                    sawTransform = true;
                }
            }
        }

        return sawTransform ? std::optional<SvgCssTransform>(transform) : std::nullopt;
    }

    void parse_css_animation_shorthand(std::string_view value, SvgCssClassStyle& style)
    {
        for (const std::string& token : split_css_tokens(value))
        {
            const std::string lower = to_lower(token);
            if (lower == "infinite" || lower == "linear" || lower == "ease" ||
                lower == "ease-in" || lower == "ease-out" || lower == "ease-in-out" ||
                lower.rfind("cubic-bezier", 0) == 0)
            {
                continue;
            }

            if (token.find('s') != std::string::npos)
            {
                if (const std::optional<double> seconds = parse_css_time_seconds(token);
                    seconds && *seconds > 0.0 && style.animationDurationSeconds <= 0.0)
                {
                    style.animationDurationSeconds = *seconds;
                    continue;
                }
            }

            if (style.animationName.empty())
            {
                style.animationName = token;
            }
        }
    }

    std::optional<SvgCssKeyframes> parse_css_keyframes_block(std::string_view body)
    {
        SvgCssKeyframes keyframes = {};
        std::size_t cursor = 0;
        while (cursor < body.size())
        {
            const std::size_t open = body.find('{', cursor);
            if (open == std::string_view::npos)
            {
                break;
            }
            const std::size_t close = find_matching_brace(body, open);
            if (close == std::string_view::npos)
            {
                break;
            }

            const std::string selector = trim_copy(body.substr(cursor, open - cursor));
            const auto declarations = parse_css_declarations(body.substr(open + 1u, close - open - 1u));
            SvgCssKeyframe frame = {};
            if (const auto transformIt = declarations.find("transform"); transformIt != declarations.end())
            {
                frame.transform = parse_css_transform(transformIt->second);
            }
            if (const auto opacityIt = declarations.find("opacity"); opacityIt != declarations.end())
            {
                if (const std::optional<float> opacity = parse_float_prefix(opacityIt->second))
                {
                    frame.opacity = std::clamp(static_cast<double>(*opacity), 0.0, 1.0);
                }
            }

            for (const std::string& part : split_css_list(selector, ','))
            {
                SvgCssKeyframe keyedFrame = frame;
                const std::string lower = to_lower(part);
                if (lower == "from")
                {
                    keyedFrame.offset = 0.0;
                }
                else if (lower == "to")
                {
                    keyedFrame.offset = 1.0;
                }
                else if (const std::optional<float> percent = parse_float_prefix(part))
                {
                    keyedFrame.offset = std::clamp(static_cast<double>(*percent) / 100.0, 0.0, 1.0);
                }
                else
                {
                    continue;
                }
                keyframes.frames.push_back(std::move(keyedFrame));
            }
            cursor = close + 1u;
        }

        std::sort(keyframes.frames.begin(), keyframes.frames.end(),
            [](const SvgCssKeyframe& a, const SvgCssKeyframe& b) {
                return a.offset < b.offset;
            });
        return keyframes.frames.empty() ? std::nullopt : std::optional<SvgCssKeyframes>(std::move(keyframes));
    }

    std::string extract_svg_style_text(std::string_view svg)
    {
        std::string styles;
        std::size_t cursor = 0;
        while (cursor < svg.size())
        {
            const std::size_t styleStart = svg.find("<style", cursor);
            if (styleStart == std::string_view::npos)
            {
                break;
            }
            const std::size_t styleBodyStart = svg.find('>', styleStart);
            if (styleBodyStart == std::string_view::npos)
            {
                break;
            }
            const std::size_t styleEnd = svg.find("</style>", styleBodyStart + 1u);
            if (styleEnd == std::string_view::npos)
            {
                break;
            }
            styles.append(svg.substr(styleBodyStart + 1u, styleEnd - styleBodyStart - 1u));
            styles.push_back('\n');
            cursor = styleEnd + 8u;
        }
        return styles;
    }

    std::string remove_svg_style_blocks(std::string_view svg)
    {
        std::string output;
        output.reserve(svg.size());
        std::size_t cursor = 0;
        while (cursor < svg.size())
        {
            const std::size_t styleStart = svg.find("<style", cursor);
            if (styleStart == std::string_view::npos)
            {
                output.append(svg.substr(cursor));
                break;
            }
            output.append(svg.substr(cursor, styleStart - cursor));
            const std::size_t styleBodyStart = svg.find('>', styleStart);
            const std::size_t styleEnd = styleBodyStart == std::string_view::npos
                ? std::string_view::npos
                : svg.find("</style>", styleBodyStart + 1u);
            if (styleEnd == std::string_view::npos)
            {
                break;
            }
            cursor = styleEnd + 8u;
        }
        return output;
    }

    SvgCssAnimationDocument parse_svg_css_animations(std::string_view svg)
    {
        SvgCssAnimationDocument document = {};
        const std::string css = remove_css_comments(extract_svg_style_text(svg));
        std::size_t cursor = 0;
        while (cursor < css.size())
        {
            while (cursor < css.size() && std::isspace(static_cast<unsigned char>(css[cursor])))
            {
                ++cursor;
            }
            if (cursor >= css.size())
            {
                break;
            }

            if (css.compare(cursor, 10u, "@keyframes") == 0)
            {
                const std::size_t nameStart = cursor + 10u;
                const std::size_t open = css.find('{', nameStart);
                if (open == std::string::npos)
                {
                    break;
                }
                const std::string name = trim_copy(std::string_view(css).substr(nameStart, open - nameStart));
                const std::size_t close = find_matching_brace(css, open);
                if (close == std::string_view::npos)
                {
                    break;
                }
                if (const std::optional<SvgCssKeyframes> keyframes =
                        parse_css_keyframes_block(std::string_view(css).substr(open + 1u, close - open - 1u));
                    keyframes && !name.empty())
                {
                    document.keyframes[name] = *keyframes;
                }
                cursor = close + 1u;
                continue;
            }

            const std::size_t open = css.find('{', cursor);
            if (open == std::string::npos)
            {
                break;
            }
            const std::size_t close = find_matching_brace(css, open);
            if (close == std::string_view::npos)
            {
                break;
            }

            const std::string selector = trim_copy(std::string_view(css).substr(cursor, open - cursor));
            const auto declarations = parse_css_declarations(std::string_view(css).substr(open + 1u, close - open - 1u));
            for (const std::string& part : split_css_list(selector, ','))
            {
                if (part.empty() || part.front() != '.')
                {
                    continue;
                }
                const std::string className = part.substr(1u);
                SvgCssClassStyle& style = document.classes[className];
                for (const auto& [key, value] : declarations)
                {
                    style.declarations[key] = value;
                }
                if (const auto animationIt = declarations.find("animation"); animationIt != declarations.end())
                {
                    parse_css_animation_shorthand(animationIt->second, style);
                }
                if (const auto nameIt = declarations.find("animation-name"); nameIt != declarations.end())
                {
                    style.animationName = nameIt->second;
                }
                if (const auto durationIt = declarations.find("animation-duration"); durationIt != declarations.end())
                {
                    if (const std::optional<double> seconds = parse_css_time_seconds(durationIt->second))
                    {
                        style.animationDurationSeconds = *seconds;
                    }
                }
                if (const auto delayIt = declarations.find("animation-delay"); delayIt != declarations.end())
                {
                    if (const std::optional<double> seconds = parse_css_time_seconds(delayIt->second))
                    {
                        style.animationDelaySeconds = *seconds;
                    }
                }
            }
            cursor = close + 1u;
        }
        return document;
    }

    std::optional<std::pair<std::size_t, std::size_t>> find_svg_attribute_span(
        std::string_view tag,
        std::string_view name)
    {
        const std::string needle = std::string(name) + "=";
        std::size_t at = tag.find(needle);
        while (at != std::string_view::npos)
        {
            const bool startsAttribute = at == 0u ||
                std::isspace(static_cast<unsigned char>(tag[at - 1u]));
            if (startsAttribute)
            {
                const std::size_t valueStart = at + needle.size();
                if (valueStart < tag.size())
                {
                    const char quote = tag[valueStart];
                    if (quote == '"' || quote == '\'')
                    {
                        const std::size_t valueEnd = tag.find(quote, valueStart + 1u);
                        if (valueEnd != std::string_view::npos)
                        {
                            return std::make_pair(at, valueEnd + 1u);
                        }
                    }
                    else
                    {
                        const std::size_t valueEnd = tag.find_first_of(" \t\r\n/>", valueStart);
                        return std::make_pair(at, valueEnd == std::string_view::npos ? tag.size() : valueEnd);
                    }
                }
            }
            at = tag.find(needle, at + 1u);
        }
        return std::nullopt;
    }

    std::optional<std::string> svg_tag_attribute(std::string_view tag, std::string_view name)
    {
        const std::optional<std::pair<std::size_t, std::size_t>> span = find_svg_attribute_span(tag, name);
        if (!span)
        {
            return std::nullopt;
        }

        const std::size_t equals = tag.find('=', span->first);
        if (equals == std::string_view::npos || equals + 1u >= span->second)
        {
            return std::nullopt;
        }

        std::size_t valueStart = equals + 1u;
        std::size_t valueEnd = span->second;
        if (tag[valueStart] == '"' || tag[valueStart] == '\'')
        {
            ++valueStart;
            --valueEnd;
        }
        if (valueEnd < valueStart)
        {
            return std::nullopt;
        }
        return std::string(tag.substr(valueStart, valueEnd - valueStart));
    }

    std::string set_svg_tag_attribute(std::string tag, std::string_view name, std::string_view value)
    {
        const std::string replacement = std::string(name) + "=\"" + std::string(value) + "\"";
        const std::optional<std::pair<std::size_t, std::size_t>> span = find_svg_attribute_span(tag, name);
        if (span)
        {
            tag.replace(span->first, span->second - span->first, replacement);
            return tag;
        }

        std::size_t insertAt = tag.rfind("/>");
        if (insertAt == std::string::npos)
        {
            insertAt = tag.rfind('>');
        }
        if (insertAt == std::string::npos)
        {
            return tag;
        }

        tag.insert(insertAt, " " + replacement);
        return tag;
    }

    std::string svg_number(double value)
    {
        if (std::abs(value) < 0.000001)
        {
            value = 0.0;
        }
        std::ostringstream stream;
        stream << value;
        return stream.str();
    }

    std::string svg_transform_attribute(const SvgCssTransform& transform)
    {
        return "translate(" + svg_number(transform.translateX) + " " + svg_number(transform.translateY) +
            ") scale(" + svg_number(transform.scale) + ")";
    }

    struct SvgCssElementAnimation
    {
        std::string name;
        double durationSeconds = 0.0;
        double delaySeconds = 0.0;
    };

    struct SvgCssComputedFrame
    {
        std::optional<SvgCssTransform> transform;
        std::optional<double> opacity;
    };

    std::optional<SvgCssElementAnimation> find_css_element_animation(
        const SvgCssAnimationDocument& document,
        const std::vector<std::string>& classes)
    {
        SvgCssElementAnimation animation = {};
        for (const std::string& className : classes)
        {
            const auto styleIt = document.classes.find(className);
            if (styleIt == document.classes.end())
            {
                continue;
            }

            const SvgCssClassStyle& style = styleIt->second;
            if (!style.animationName.empty() && document.keyframes.find(style.animationName) != document.keyframes.end())
            {
                animation.name = style.animationName;
                animation.durationSeconds = style.animationDurationSeconds;
            }
            if (style.animationDelaySeconds != 0.0)
            {
                animation.delaySeconds = style.animationDelaySeconds;
            }
        }

        return !animation.name.empty() && animation.durationSeconds > 0.0
            ? std::optional<SvgCssElementAnimation>(animation)
            : std::nullopt;
    }

    std::string css_static_style_for_classes(
        const SvgCssAnimationDocument& document,
        const std::vector<std::string>& classes)
    {
        std::unordered_map<std::string, std::string> declarations;
        for (const std::string& className : classes)
        {
            const auto styleIt = document.classes.find(className);
            if (styleIt == document.classes.end())
            {
                continue;
            }
            for (const auto& [key, value] : styleIt->second.declarations)
            {
                if (key.rfind("animation", 0) == 0)
                {
                    continue;
                }
                declarations[key] = value;
            }
        }

        std::string style;
        for (const auto& [key, value] : declarations)
        {
            style += key + ":" + value + ";";
        }
        return style;
    }

    SvgCssTransform interpolate_transform(
        const SvgCssTransform& from,
        const SvgCssTransform& to,
        double amount)
    {
        amount = std::clamp(amount, 0.0, 1.0);
        return {
            from.translateX + (to.translateX - from.translateX) * amount,
            from.translateY + (to.translateY - from.translateY) * amount,
            from.scale + (to.scale - from.scale) * amount
        };
    }

    SvgCssComputedFrame sample_css_keyframes(const SvgCssKeyframes& keyframes, double progress)
    {
        SvgCssComputedFrame computed = {};
        if (keyframes.frames.empty())
        {
            return computed;
        }

        progress = std::clamp(progress, 0.0, 1.0);
        const SvgCssKeyframe* previous = &keyframes.frames.front();
        const SvgCssKeyframe* next = &keyframes.frames.back();
        for (const SvgCssKeyframe& frame : keyframes.frames)
        {
            if (frame.offset <= progress)
            {
                previous = &frame;
            }
            if (frame.offset >= progress)
            {
                next = &frame;
                break;
            }
        }

        const double span = std::max(next->offset - previous->offset, 0.0);
        const double amount = span > 0.0 ? (progress - previous->offset) / span : 0.0;
        if (previous->transform && next->transform)
        {
            computed.transform = interpolate_transform(*previous->transform, *next->transform, amount);
        }
        else if (previous->transform)
        {
            computed.transform = previous->transform;
        }
        else if (next->transform)
        {
            computed.transform = next->transform;
        }

        if (previous->opacity && next->opacity)
        {
            computed.opacity = *previous->opacity + (*next->opacity - *previous->opacity) * amount;
        }
        else if (previous->opacity)
        {
            computed.opacity = previous->opacity;
        }
        else if (next->opacity)
        {
            computed.opacity = next->opacity;
        }
        return computed;
    }

    double css_animation_progress(double timeSeconds, const SvgCssElementAnimation& animation)
    {
        if (animation.durationSeconds <= 0.0)
        {
            return 0.0;
        }

        const double localTime = timeSeconds - animation.delaySeconds;
        if (localTime <= 0.0)
        {
            return 0.0;
        }
        return std::fmod(localTime, animation.durationSeconds) / animation.durationSeconds;
    }

    double css_animation_cycle_duration(const SvgCssAnimationDocument& document)
    {
        double duration = 0.0;
        for (const auto& [className, style] : document.classes)
        {
            (void)className;
            if (!style.animationName.empty() &&
                document.keyframes.find(style.animationName) != document.keyframes.end())
            {
                duration = std::max(duration, style.animationDurationSeconds);
            }
        }
        return duration;
    }

    double css_animation_timeline_start(const SvgCssAnimationDocument& document)
    {
        double delay = 0.0;
        for (const auto& [className, style] : document.classes)
        {
            (void)className;
            delay = std::max(delay, style.animationDelaySeconds);
        }
        return delay;
    }

    std::string apply_css_animation_frame(
        std::string_view svg,
        const SvgCssAnimationDocument& document,
        double timeSeconds)
    {
        const std::string source = remove_svg_style_blocks(svg);
        std::string output;
        output.reserve(source.size());

        std::size_t cursor = 0;
        while (cursor < source.size())
        {
            const std::size_t tagStart = source.find('<', cursor);
            if (tagStart == std::string::npos)
            {
                output.append(source.substr(cursor));
                break;
            }
            output.append(source.substr(cursor, tagStart - cursor));

            const std::size_t tagEnd = source.find('>', tagStart);
            if (tagEnd == std::string::npos)
            {
                output.append(source.substr(tagStart));
                break;
            }

            std::string tag = source.substr(tagStart, tagEnd - tagStart + 1u);
            const bool mutableElement =
                tag.size() > 2u &&
                tag[1u] != '/' &&
                tag[1u] != '!' &&
                tag[1u] != '?';
            if (mutableElement)
            {
                const std::optional<std::string> classAttribute = svg_tag_attribute(tag, "class");
                if (classAttribute)
                {
                    const std::vector<std::string> classes = split_css_tokens(*classAttribute);
                    const std::string staticStyle = css_static_style_for_classes(document, classes);
                    if (!staticStyle.empty())
                    {
                        std::string style = svg_tag_attribute(tag, "style").value_or("");
                        if (!style.empty() && style.back() != ';')
                        {
                            style.push_back(';');
                        }
                        style += staticStyle;
                        tag = set_svg_tag_attribute(std::move(tag), "style", style);
                    }

                    if (const std::optional<SvgCssElementAnimation> animation =
                            find_css_element_animation(document, classes))
                    {
                        const auto keyframesIt = document.keyframes.find(animation->name);
                        if (keyframesIt != document.keyframes.end())
                        {
                            const SvgCssComputedFrame computed = sample_css_keyframes(
                                keyframesIt->second,
                                css_animation_progress(timeSeconds, *animation));
                            if (computed.transform)
                            {
                                tag = set_svg_tag_attribute(std::move(tag), "transform", svg_transform_attribute(*computed.transform));
                            }
                            if (computed.opacity)
                            {
                                tag = set_svg_tag_attribute(std::move(tag), "opacity", svg_number(std::clamp(*computed.opacity, 0.0, 1.0)));
                            }
                        }
                    }
                }
            }

            output += tag;
            cursor = tagEnd + 1u;
        }
        return output;
    }

    bool path_has_extension(const std::filesystem::path& path, std::string_view extension)
    {
        return to_lower(path.extension().string()) == extension;
    }

    struct PngChunk
    {
        std::string type;
        std::vector<unsigned char> data;
    };

    struct ApngFrameControl
    {
        uint32_t width = 0;
        uint32_t height = 0;
        uint32_t xOffset = 0;
        uint32_t yOffset = 0;
        uint16_t delayNumerator = 1;
        uint16_t delayDenominator = 10;
        uint8_t disposeOp = 0;
        uint8_t blendOp = 0;
    };

    struct ApngFrameSource
    {
        ApngFrameControl control = {};
        std::vector<std::vector<unsigned char>> imageChunks;
        bool hasControl = false;
    };

    uint16_t read_be16(const unsigned char* data)
    {
        return static_cast<uint16_t>(
            (static_cast<uint16_t>(data[0]) << 8u) |
            static_cast<uint16_t>(data[1]));
    }

    uint32_t read_be32(const unsigned char* data)
    {
        return
            (static_cast<uint32_t>(data[0]) << 24u) |
            (static_cast<uint32_t>(data[1]) << 16u) |
            (static_cast<uint32_t>(data[2]) << 8u) |
            static_cast<uint32_t>(data[3]);
    }

    void write_be32(std::vector<unsigned char>& data, uint32_t offset, uint32_t value)
    {
        if (offset + 4u > data.size())
        {
            return;
        }

        data[offset + 0u] = static_cast<unsigned char>((value >> 24u) & 0xffu);
        data[offset + 1u] = static_cast<unsigned char>((value >> 16u) & 0xffu);
        data[offset + 2u] = static_cast<unsigned char>((value >> 8u) & 0xffu);
        data[offset + 3u] = static_cast<unsigned char>(value & 0xffu);
    }

    void append_be32(std::vector<unsigned char>& data, uint32_t value)
    {
        data.push_back(static_cast<unsigned char>((value >> 24u) & 0xffu));
        data.push_back(static_cast<unsigned char>((value >> 16u) & 0xffu));
        data.push_back(static_cast<unsigned char>((value >> 8u) & 0xffu));
        data.push_back(static_cast<unsigned char>(value & 0xffu));
    }

    uint32_t crc32_update(uint32_t crc, unsigned char value)
    {
        crc ^= static_cast<uint32_t>(value);
        for (uint32_t bit = 0; bit < 8u; ++bit)
        {
            const uint32_t mask = 0u - (crc & 1u);
            crc = (crc >> 1u) ^ (0xedb88320u & mask);
        }
        return crc;
    }

    uint32_t png_chunk_crc(std::string_view type, const std::vector<unsigned char>& data)
    {
        uint32_t crc = 0xffffffffu;
        for (char c : type)
        {
            crc = crc32_update(crc, static_cast<unsigned char>(c));
        }
        for (unsigned char value : data)
        {
            crc = crc32_update(crc, value);
        }
        return crc ^ 0xffffffffu;
    }

    void append_png_chunk(
        std::vector<unsigned char>& png,
        std::string_view type,
        const std::vector<unsigned char>& data)
    {
        append_be32(png, static_cast<uint32_t>(data.size()));
        for (char c : type)
        {
            png.push_back(static_cast<unsigned char>(c));
        }
        png.insert(png.end(), data.begin(), data.end());
        append_be32(png, png_chunk_crc(type, data));
    }

    bool is_png_signature(const std::vector<unsigned char>& bytes)
    {
        static constexpr unsigned char kSignature[] = {
            137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u
        };
        return bytes.size() >= sizeof(kSignature) &&
            std::memcmp(bytes.data(), kSignature, sizeof(kSignature)) == 0;
    }

    bool apng_header_chunk(std::string_view type)
    {
        return type != "IHDR" &&
            type != "acTL" &&
            type != "fcTL" &&
            type != "fdAT" &&
            type != "IDAT" &&
            type != "IEND";
    }

    std::optional<ApngFrameControl> parse_apng_frame_control(const unsigned char* data, std::size_t size)
    {
        if (size < 26u)
        {
            return std::nullopt;
        }

        ApngFrameControl control = {};
        control.width = read_be32(data + 4u);
        control.height = read_be32(data + 8u);
        control.xOffset = read_be32(data + 12u);
        control.yOffset = read_be32(data + 16u);
        control.delayNumerator = read_be16(data + 20u);
        control.delayDenominator = read_be16(data + 22u);
        control.disposeOp = data[24u];
        control.blendOp = data[25u];
        return control;
    }

    double apng_frame_duration_seconds(const ApngFrameControl& control)
    {
        if (control.delayNumerator == 0u)
        {
            return 0.1;
        }

        const uint16_t denominator = control.delayDenominator == 0u ? 100u : control.delayDenominator;
        return std::max(
            static_cast<double>(control.delayNumerator) / static_cast<double>(denominator),
            0.01);
    }

    std::vector<unsigned char> make_png_from_apng_frame(
        const std::vector<unsigned char>& ihdr,
        const std::vector<PngChunk>& headerChunks,
        const ApngFrameSource& frame)
    {
        static constexpr unsigned char kSignature[] = {
            137u, 80u, 78u, 71u, 13u, 10u, 26u, 10u
        };

        std::vector<unsigned char> png;
        png.insert(png.end(), std::begin(kSignature), std::end(kSignature));

        std::vector<unsigned char> frameIhdr = ihdr;
        write_be32(frameIhdr, 0u, frame.control.width);
        write_be32(frameIhdr, 4u, frame.control.height);
        append_png_chunk(png, "IHDR", frameIhdr);

        for (const PngChunk& chunk : headerChunks)
        {
            append_png_chunk(png, chunk.type, chunk.data);
        }
        for (const std::vector<unsigned char>& imageData : frame.imageChunks)
        {
            append_png_chunk(png, "IDAT", imageData);
        }
        append_png_chunk(png, "IEND", {});
        return png;
    }

    DecodedMediaFrame decode_png_bytes(const std::vector<unsigned char>& bytes, double durationSeconds)
    {
        DecodedMediaFrame frame = {};
        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            bytes.data(),
            static_cast<int>(std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()))),
            &width,
            &height,
            &channels,
            4);
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            if (pixels != nullptr)
            {
                stbi_image_free(pixels);
            }
            return frame;
        }

        frame.width = static_cast<uint32_t>(width);
        frame.height = static_cast<uint32_t>(height);
        frame.durationSeconds = durationSeconds;
        const std::size_t rgbaSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        frame.rgba.assign(pixels, pixels + rgbaSize);
        stbi_image_free(pixels);
        return frame;
    }

    void blend_pixel_over(unsigned char* dst, const unsigned char* src)
    {
        const float srcA = static_cast<float>(src[3]) / 255.0f;
        const float dstA = static_cast<float>(dst[3]) / 255.0f;
        const float outA = srcA + dstA * (1.0f - srcA);
        if (outA <= 0.0001f)
        {
            dst[0] = 0u;
            dst[1] = 0u;
            dst[2] = 0u;
            dst[3] = 0u;
            return;
        }

        for (uint32_t channel = 0; channel < 3u; ++channel)
        {
            const float srcValue = static_cast<float>(src[channel]) / 255.0f;
            const float dstValue = static_cast<float>(dst[channel]) / 255.0f;
            const float outValue = (srcValue * srcA + dstValue * dstA * (1.0f - srcA)) / outA;
            dst[channel] = static_cast<unsigned char>(std::round(std::clamp(outValue, 0.0f, 1.0f) * 255.0f));
        }
        dst[3] = static_cast<unsigned char>(std::round(std::clamp(outA, 0.0f, 1.0f) * 255.0f));
    }

    void clear_apng_region(DecodedMediaFrame& canvas, const ApngFrameControl& control)
    {
        if (!canvas.valid())
        {
            return;
        }

        const uint32_t maxX = std::min(canvas.width, control.xOffset + control.width);
        const uint32_t maxY = std::min(canvas.height, control.yOffset + control.height);
        for (uint32_t y = control.yOffset; y < maxY; ++y)
        {
            for (uint32_t x = control.xOffset; x < maxX; ++x)
            {
                const std::size_t at =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(canvas.width) +
                     static_cast<std::size_t>(x)) * 4u;
                canvas.rgba[at + 0u] = 0u;
                canvas.rgba[at + 1u] = 0u;
                canvas.rgba[at + 2u] = 0u;
                canvas.rgba[at + 3u] = 0u;
            }
        }
    }

    void composite_apng_frame(
        DecodedMediaFrame& canvas,
        const DecodedMediaFrame& frame,
        const ApngFrameControl& control)
    {
        const uint32_t width = std::min(frame.width, control.width);
        const uint32_t height = std::min(frame.height, control.height);
        for (uint32_t y = 0; y < height; ++y)
        {
            const uint32_t canvasY = control.yOffset + y;
            if (canvasY >= canvas.height)
            {
                continue;
            }

            for (uint32_t x = 0; x < width; ++x)
            {
                const uint32_t canvasX = control.xOffset + x;
                if (canvasX >= canvas.width)
                {
                    continue;
                }

                const std::size_t srcAt =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                     static_cast<std::size_t>(x)) * 4u;
                const std::size_t dstAt =
                    (static_cast<std::size_t>(canvasY) * static_cast<std::size_t>(canvas.width) +
                     static_cast<std::size_t>(canvasX)) * 4u;

                if (control.blendOp == 0u)
                {
                    std::memcpy(canvas.rgba.data() + dstAt, frame.rgba.data() + srcAt, 4u);
                }
                else
                {
                    blend_pixel_over(canvas.rgba.data() + dstAt, frame.rgba.data() + srcAt);
                }
            }
        }
    }

    DecodedMediaAnimation decode_apng_animation(
        const std::filesystem::path& path,
        const Media2DLoadOptions& options)
    {
        DecodedMediaAnimation animation = {};
        const std::vector<unsigned char> bytes = read_binary_file(path);
        if (!is_png_signature(bytes))
        {
            return animation;
        }

        std::vector<unsigned char> ihdr;
        std::vector<PngChunk> headerChunks;
        std::vector<ApngFrameSource> frameSources;
        ApngFrameSource currentFrame = {};
        bool sawAnimationControl = false;
        bool sawImageData = false;
        uint32_t canvasWidth = 0;
        uint32_t canvasHeight = 0;

        auto finish_current_frame = [&]() {
            if (currentFrame.hasControl && !currentFrame.imageChunks.empty())
            {
                frameSources.push_back(std::move(currentFrame));
            }
            currentFrame = {};
        };

        std::size_t cursor = 8u;
        while (cursor + 8u <= bytes.size())
        {
            const uint32_t length = read_be32(bytes.data() + cursor);
            const std::size_t typeAt = cursor + 4u;
            const std::size_t dataAt = cursor + 8u;
            const std::size_t crcAt = dataAt + static_cast<std::size_t>(length);
            if (crcAt + 4u > bytes.size())
            {
                return {};
            }

            const std::string type(reinterpret_cast<const char*>(bytes.data() + typeAt), 4u);
            const unsigned char* chunkData = bytes.data() + dataAt;
            if (type == "IHDR")
            {
                ihdr.assign(chunkData, chunkData + length);
                if (ihdr.size() >= 8u)
                {
                    canvasWidth = read_be32(ihdr.data());
                    canvasHeight = read_be32(ihdr.data() + 4u);
                }
            }
            else if (type == "acTL")
            {
                sawAnimationControl = length >= 8u;
            }
            else if (type == "fcTL")
            {
                finish_current_frame();
                const std::optional<ApngFrameControl> control = parse_apng_frame_control(chunkData, length);
                if (!control)
                {
                    return {};
                }
                currentFrame.control = *control;
                currentFrame.hasControl = true;
            }
            else if (type == "IDAT")
            {
                sawImageData = true;
                if (currentFrame.hasControl)
                {
                    currentFrame.imageChunks.emplace_back(chunkData, chunkData + length);
                }
            }
            else if (type == "fdAT")
            {
                sawImageData = true;
                if (currentFrame.hasControl && length > 4u)
                {
                    currentFrame.imageChunks.emplace_back(chunkData + 4u, chunkData + length);
                }
            }
            else if (type == "IEND")
            {
                break;
            }
            else if (!sawImageData && apng_header_chunk(type))
            {
                headerChunks.push_back(PngChunk {
                    type,
                    std::vector<unsigned char>(chunkData, chunkData + length)
                });
            }

            cursor = crcAt + 4u;
        }

        finish_current_frame();
        if (!sawAnimationControl || ihdr.size() != 13u ||
            canvasWidth == 0u || canvasHeight == 0u ||
            frameSources.empty())
        {
            return {};
        }

        const uint32_t maxFrames = options.maxAnimationFrames > 0u
            ? options.maxAnimationFrames
            : std::numeric_limits<uint32_t>::max();
        const uint32_t frameLimit = std::min<uint32_t>(
            static_cast<uint32_t>(frameSources.size()),
            maxFrames);
        if (frameLimit == 0u)
        {
            return {};
        }

        DecodedMediaFrame canvas = {};
        canvas.width = canvasWidth;
        canvas.height = canvasHeight;
        canvas.rgba.assign(static_cast<std::size_t>(canvasWidth) * static_cast<std::size_t>(canvasHeight) * 4u, 0u);

        animation.frames.reserve(frameLimit);
        for (uint32_t frameIndex = 0; frameIndex < frameLimit; ++frameIndex)
        {
            const ApngFrameSource& source = frameSources[frameIndex];
            if (source.control.width == 0u || source.control.height == 0u)
            {
                continue;
            }

            const std::vector<unsigned char> framePng = make_png_from_apng_frame(ihdr, headerChunks, source);
            const DecodedMediaFrame decodedFrame = decode_png_bytes(
                framePng,
                apng_frame_duration_seconds(source.control));
            if (!decodedFrame.valid())
            {
                continue;
            }

            std::vector<unsigned char> previousCanvas;
            if (source.control.disposeOp == 2u)
            {
                previousCanvas = canvas.rgba;
            }

            composite_apng_frame(canvas, decodedFrame, source.control);

            DecodedMediaFrame outputFrame = {};
            outputFrame.width = canvas.width;
            outputFrame.height = canvas.height;
            outputFrame.durationSeconds = decodedFrame.durationSeconds;
            outputFrame.rgba = canvas.rgba;
            animation.frames.push_back(std::move(outputFrame));

            if (source.control.disposeOp == 1u)
            {
                clear_apng_region(canvas, source.control);
            }
            else if (source.control.disposeOp == 2u && !previousCanvas.empty())
            {
                canvas.rgba = std::move(previousCanvas);
            }
        }

        return animation;
    }

    std::optional<double> parse_duration_attribute_seconds(std::string_view text)
    {
        const std::size_t durAt = text.find("dur=");
        if (durAt == std::string_view::npos)
        {
            return std::nullopt;
        }

        std::size_t valueAt = durAt + 4u;
        if (valueAt >= text.size())
        {
            return std::nullopt;
        }

        const char quote = text[valueAt];
        if (quote == '"' || quote == '\'')
        {
            ++valueAt;
        }

        const std::size_t valueEnd = quote == '"' || quote == '\''
            ? text.find(quote, valueAt)
            : text.find_first_of(" \t\r\n/>", valueAt);
        if (valueEnd == std::string_view::npos || valueEnd <= valueAt)
        {
            return std::nullopt;
        }

        const std::string value(text.substr(valueAt, valueEnd - valueAt));
        const std::optional<float> numeric = parse_float_prefix(value);
        if (!numeric || *numeric <= 0.0f)
        {
            return std::nullopt;
        }

        if (value.find("ms") != std::string::npos)
        {
            return static_cast<double>(*numeric) / 1000.0;
        }
        return static_cast<double>(*numeric);
    }

    unsigned char sample_channel_nearest(
        const DecodedMediaFrame& source,
        int32_t x,
        int32_t y,
        uint32_t channel)
    {
        if (x < 0 || y < 0 ||
            x >= static_cast<int32_t>(source.width) ||
            y >= static_cast<int32_t>(source.height) ||
            channel >= 4u)
        {
            return 0u;
        }

        const std::size_t at =
            (static_cast<std::size_t>(y) * static_cast<std::size_t>(source.width) +
             static_cast<std::size_t>(x)) * 4u +
            channel;
        return source.rgba[at];
    }

    DecodedMediaFrame rotate_frame(const DecodedMediaFrame& source, double phase, double durationSeconds)
    {
        DecodedMediaFrame frame = {};
        if (!source.valid())
        {
            return frame;
        }

        frame.width = source.width;
        frame.height = source.height;
        frame.durationSeconds = durationSeconds;
        frame.rgba.assign(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4u, 0u);

        const float angle = static_cast<float>(phase * 6.28318530717958647692);
        const float c = std::cos(-angle);
        const float s = std::sin(-angle);
        const float cx = (static_cast<float>(frame.width) - 1.0f) * 0.5f;
        const float cy = (static_cast<float>(frame.height) - 1.0f) * 0.5f;
        const float pulse = 0.94f + 0.06f * std::sin(angle * 2.0f);
        const float inverseScale = 1.0f / std::max(pulse, 0.0001f);

        for (uint32_t y = 0; y < frame.height; ++y)
        {
            for (uint32_t x = 0; x < frame.width; ++x)
            {
                const float dx = (static_cast<float>(x) - cx) * inverseScale;
                const float dy = (static_cast<float>(y) - cy) * inverseScale;
                const int32_t sx = static_cast<int32_t>(std::round(c * dx - s * dy + cx));
                const int32_t sy = static_cast<int32_t>(std::round(s * dx + c * dy + cy));
                const std::size_t out =
                    (static_cast<std::size_t>(y) * static_cast<std::size_t>(frame.width) +
                     static_cast<std::size_t>(x)) * 4u;
                frame.rgba[out + 0u] = sample_channel_nearest(source, sx, sy, 0u);
                frame.rgba[out + 1u] = sample_channel_nearest(source, sx, sy, 1u);
                frame.rgba[out + 2u] = sample_channel_nearest(source, sx, sy, 2u);
                frame.rgba[out + 3u] = sample_channel_nearest(source, sx, sy, 3u);
            }
        }

        return frame;
    }

    DecodedMediaAnimation make_svg_animation_frames(
        const DecodedMediaFrame& source,
        std::string_view svgText,
        const Media2DLoadOptions& options)
    {
        DecodedMediaAnimation animation = {};
        if (!source.valid())
        {
            return animation;
        }

        const double frameRate = options.svgAnimationFrameRate > 0.0
            ? options.svgAnimationFrameRate
            : 60.0;
        const double duration = parse_duration_attribute_seconds(svgText)
            .value_or(options.svgAnimationFrames > 1u
                ? static_cast<double>(options.svgAnimationFrames) / frameRate
                : 1.0);
        const uint32_t maxFrames = options.maxAnimationFrames > 0u
            ? options.maxAnimationFrames
            : std::numeric_limits<uint32_t>::max();
        const uint32_t requestedFrames = options.svgAnimationFrames > 1u
            ? options.svgAnimationFrames
            : std::max(2u, static_cast<uint32_t>(std::ceil(std::max(duration, 1.0 / frameRate) * frameRate)));
        const uint32_t frameCount = std::max(2u, std::min(requestedFrames, maxFrames));
        const double frameDuration = std::max(duration / static_cast<double>(frameCount), 1.0 / 120.0);

        animation.frames.reserve(frameCount);
        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const double phase = static_cast<double>(frameIndex) / static_cast<double>(frameCount);
            animation.frames.push_back(rotate_frame(source, phase, frameDuration));
        }
        return animation;
    }

    glm::uvec2 target_video_size(uint32_t width, uint32_t height, const Media2DLoadOptions& options)
    {
        if (width == 0u || height == 0u)
        {
            return { 0u, 0u };
        }

        if (options.rasterWidth > 0u && options.rasterHeight > 0u)
        {
            return { options.rasterWidth, options.rasterHeight };
        }

        const double aspect = static_cast<double>(width) / static_cast<double>(height);
        if (options.rasterWidth > 0u)
        {
            return {
                options.rasterWidth,
                std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(options.rasterWidth) / aspect)))
            };
        }

        if (options.rasterHeight > 0u)
        {
            return {
                std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(options.rasterHeight) * aspect))),
                options.rasterHeight
            };
        }

        if (options.maxVideoPixels > 0u)
        {
            const double pixelCount = static_cast<double>(width) * static_cast<double>(height);
            if (pixelCount > static_cast<double>(options.maxVideoPixels))
            {
                const double scale = std::sqrt(static_cast<double>(options.maxVideoPixels) / pixelCount);
                return {
                    std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(width) * scale))),
                    std::max(1u, static_cast<uint32_t>(std::round(static_cast<double>(height) * scale)))
                };
            }
        }

        return { width, height };
    }

    uint32_t mip_count_for_extent(vk::Extent2D extent)
    {
        const uint32_t longestSide = std::max(extent.width, extent.height);
        return longestSide > 0u
            ? static_cast<uint32_t>(std::floor(std::log2(static_cast<float>(longestSide)))) + 1u
            : 1u;
    }

    void premultiply_alpha(std::vector<unsigned char>& rgba)
    {
        for (std::size_t i = 0; i + 3u < rgba.size(); i += 4u)
        {
            const uint32_t alpha = rgba[i + 3u];
            rgba[i + 0u] = static_cast<unsigned char>((static_cast<uint32_t>(rgba[i + 0u]) * alpha + 127u) / 255u);
            rgba[i + 1u] = static_cast<unsigned char>((static_cast<uint32_t>(rgba[i + 1u]) * alpha + 127u) / 255u);
            rgba[i + 2u] = static_cast<unsigned char>((static_cast<uint32_t>(rgba[i + 2u]) * alpha + 127u) / 255u);
        }
    }

    void bleed_transparent_rgb(std::vector<unsigned char>& rgba, uint32_t width, uint32_t height)
    {
        if (rgba.empty() || width == 0u || height == 0u)
        {
            return;
        }

        std::vector<unsigned char> source;
        for (uint32_t pass = 0; pass < 4u; ++pass)
        {
            bool changed = false;
            source = rgba;
            for (uint32_t y = 0; y < height; ++y)
            {
                for (uint32_t x = 0; x < width; ++x)
                {
                    const std::size_t at = (static_cast<std::size_t>(y) * width + x) * 4u;
                    if (source[at + 3u] != 0u)
                    {
                        continue;
                    }

                    uint32_t r = 0u;
                    uint32_t g = 0u;
                    uint32_t b = 0u;
                    uint32_t weight = 0u;
                    for (int32_t dy = -1; dy <= 1; ++dy)
                    {
                        const int32_t ny = static_cast<int32_t>(y) + dy;
                        if (ny < 0 || ny >= static_cast<int32_t>(height))
                        {
                            continue;
                        }
                        for (int32_t dx = -1; dx <= 1; ++dx)
                        {
                            const int32_t nx = static_cast<int32_t>(x) + dx;
                            if ((dx == 0 && dy == 0) || nx < 0 || nx >= static_cast<int32_t>(width))
                            {
                                continue;
                            }

                            const std::size_t neighbour =
                                (static_cast<std::size_t>(ny) * width + static_cast<std::size_t>(nx)) * 4u;
                            const uint32_t alpha = source[neighbour + 3u];
                            if (alpha == 0u)
                            {
                                continue;
                            }

                            r += static_cast<uint32_t>(source[neighbour + 0u]) * alpha;
                            g += static_cast<uint32_t>(source[neighbour + 1u]) * alpha;
                            b += static_cast<uint32_t>(source[neighbour + 2u]) * alpha;
                            weight += alpha;
                        }
                    }

                    if (weight == 0u)
                    {
                        continue;
                    }

                    rgba[at + 0u] = static_cast<unsigned char>((r + weight / 2u) / weight);
                    rgba[at + 1u] = static_cast<unsigned char>((g + weight / 2u) / weight);
                    rgba[at + 2u] = static_cast<unsigned char>((b + weight / 2u) / weight);
                    changed = true;
                }
            }

            if (!changed)
            {
                break;
            }
        }
    }

    DecodedMediaFrame decode_raster_image(const std::filesystem::path& path)
    {
        DecodedMediaFrame frame = {};
        std::vector<unsigned char> bytes = read_binary_file(path);
        if (bytes.empty())
        {
            return frame;
        }

        int width = 0;
        int height = 0;
        int channels = 0;
        stbi_uc* pixels = stbi_load_from_memory(
            bytes.data(),
            static_cast<int>(std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()))),
            &width,
            &height,
            &channels,
            4);
        if (pixels == nullptr || width <= 0 || height <= 0)
        {
            if (pixels != nullptr)
            {
                stbi_image_free(pixels);
            }
            return frame;
        }

        frame.width = static_cast<uint32_t>(width);
        frame.height = static_cast<uint32_t>(height);
        const std::size_t rgbaSize = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;
        frame.rgba.assign(pixels, pixels + rgbaSize);
        stbi_image_free(pixels);
        return frame;
    }

    DecodedMediaAnimation decode_gif_animation(
        const std::filesystem::path& path,
        const Media2DLoadOptions& options)
    {
        DecodedMediaAnimation animation = {};
        std::vector<unsigned char> bytes = read_binary_file(path);
        if (bytes.empty())
        {
            return animation;
        }

        int width = 0;
        int height = 0;
        int frameCount = 0;
        int channels = 0;
        int* delays = nullptr;
        stbi_uc* pixels = stbi_load_gif_from_memory(
            bytes.data(),
            static_cast<int>(std::min<std::size_t>(bytes.size(), static_cast<std::size_t>(std::numeric_limits<int>::max()))),
            &delays,
            &width,
            &height,
            &frameCount,
            &channels,
            4);
        if (pixels == nullptr || width <= 0 || height <= 0 || frameCount <= 0)
        {
            if (pixels != nullptr)
            {
                stbi_image_free(pixels);
            }
            if (delays != nullptr)
            {
                stbi_image_free(delays);
            }
            return animation;
        }

        const uint32_t maxFrames = options.maxAnimationFrames > 0u
            ? options.maxAnimationFrames
            : std::numeric_limits<uint32_t>::max();
        const uint32_t uploadFrameCount = std::min(static_cast<uint32_t>(frameCount), maxFrames);
        const std::size_t frameByteSize =
            static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4u;

        animation.frames.reserve(uploadFrameCount);
        for (uint32_t frameIndex = 0; frameIndex < uploadFrameCount; ++frameIndex)
        {
            DecodedMediaFrame frame = {};
            frame.width = static_cast<uint32_t>(width);
            frame.height = static_cast<uint32_t>(height);
            frame.durationSeconds = delays != nullptr && delays[frameIndex] > 0
                ? static_cast<double>(delays[frameIndex]) / 1000.0
                : 0.1;
            const stbi_uc* frameStart = pixels + static_cast<std::size_t>(frameIndex) * frameByteSize;
            frame.rgba.assign(frameStart, frameStart + frameByteSize);
            animation.frames.push_back(std::move(frame));
        }

        stbi_image_free(pixels);
        if (delays != nullptr)
        {
            stbi_image_free(delays);
        }
        return animation;
    }

#if VIBRANCE_HAS_FFMPEG
    struct AVFormatContextDeleter
    {
        void operator()(AVFormatContext* context) const
        {
            avformat_close_input(&context);
        }
    };

    struct AVCodecContextDeleter
    {
        void operator()(AVCodecContext* context) const
        {
            avcodec_free_context(&context);
        }
    };

    struct AVFrameDeleter
    {
        void operator()(AVFrame* frame) const
        {
            av_frame_free(&frame);
        }
    };

    struct AVPacketDeleter
    {
        void operator()(AVPacket* packet) const
        {
            av_packet_free(&packet);
        }
    };

    double av_rational_to_double(AVRational value)
    {
        return value.den != 0
            ? static_cast<double>(value.num) / static_cast<double>(value.den)
            : 0.0;
    }

    double ffmpeg_stream_frame_rate(const AVStream* stream)
    {
        if (stream == nullptr)
        {
            return 0.0;
        }

        double frameRate = av_rational_to_double(stream->avg_frame_rate);
        if (frameRate <= 0.0)
        {
            frameRate = av_rational_to_double(stream->r_frame_rate);
        }
        return frameRate;
    }

    double ffmpeg_stream_duration_seconds(const AVFormatContext* formatContext, const AVStream* stream)
    {
        if (stream != nullptr && stream->duration != AV_NOPTS_VALUE)
        {
            const double duration = static_cast<double>(stream->duration) * av_rational_to_double(stream->time_base);
            if (duration > 0.0 && std::isfinite(duration))
            {
                return duration;
            }
        }

        if (formatContext != nullptr && formatContext->duration != AV_NOPTS_VALUE)
        {
            const double duration = static_cast<double>(formatContext->duration) / static_cast<double>(AV_TIME_BASE);
            if (duration > 0.0 && std::isfinite(duration))
            {
                return duration;
            }
        }

        return 0.0;
    }

    uint64_t ffmpeg_estimated_frame_count(const AVStream* stream, double durationSeconds, double frameRate)
    {
        if (stream != nullptr && stream->nb_frames > 0)
        {
            return static_cast<uint64_t>(stream->nb_frames);
        }

        if (durationSeconds > 0.0 && frameRate > 0.0)
        {
            return static_cast<uint64_t>(std::llround(durationSeconds * frameRate));
        }

        return 0u;
    }

    double ffmpeg_frame_time_seconds(
        const AVFrame* frame,
        const AVStream* stream,
        uint64_t decodedFrameIndex,
        double fallbackDuration)
    {
        if (frame != nullptr && stream != nullptr)
        {
            int64_t timestamp = frame->best_effort_timestamp;
            if (timestamp == AV_NOPTS_VALUE)
            {
                timestamp = frame->pts;
            }

            if (timestamp != AV_NOPTS_VALUE)
            {
                if (stream->start_time != AV_NOPTS_VALUE)
                {
                    timestamp -= stream->start_time;
                }

                const double seconds = static_cast<double>(timestamp) * av_rational_to_double(stream->time_base);
                if (std::isfinite(seconds))
                {
                    return std::max(0.0, seconds);
                }
            }
        }

        return static_cast<double>(decodedFrameIndex) * fallbackDuration;
    }

    DecodedMediaAnimation decode_video_animation_ffmpeg(
        const std::filesystem::path& path,
        const Media2DLoadOptions& options)
    {
        DecodedMediaAnimation animation = {};
        const uint32_t frameLimit = options.maxVideoFrames > 0u
            ? options.maxVideoFrames
            : std::numeric_limits<uint32_t>::max();
        if (frameLimit == 0u)
        {
            return animation;
        }

        AVFormatContext* rawFormatContext = nullptr;
        const std::string pathString = path.string();
        if (avformat_open_input(&rawFormatContext, pathString.c_str(), nullptr, nullptr) < 0)
        {
            return animation;
        }
        std::unique_ptr<AVFormatContext, AVFormatContextDeleter> formatContext(rawFormatContext);

        if (avformat_find_stream_info(formatContext.get(), nullptr) < 0)
        {
            return animation;
        }

        const int streamIndex = av_find_best_stream(
            formatContext.get(),
            AVMEDIA_TYPE_VIDEO,
            -1,
            -1,
            nullptr,
            0);
        if (streamIndex < 0)
        {
            return animation;
        }

        AVStream* stream = formatContext->streams[streamIndex];
        const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (decoder == nullptr)
        {
            return animation;
        }

        std::unique_ptr<AVCodecContext, AVCodecContextDeleter> codecContext(avcodec_alloc_context3(decoder));
        if (!codecContext ||
            avcodec_parameters_to_context(codecContext.get(), stream->codecpar) < 0 ||
            avcodec_open2(codecContext.get(), decoder, nullptr) < 0)
        {
            return animation;
        }

        std::unique_ptr<AVFrame, AVFrameDeleter> decodedFrame(av_frame_alloc());
        std::unique_ptr<AVPacket, AVPacketDeleter> packet(av_packet_alloc());
        if (!decodedFrame || !packet)
        {
            return animation;
        }

        const double sourceFrameRate = ffmpeg_stream_frame_rate(stream);
        const double sourceDuration = ffmpeg_stream_duration_seconds(formatContext.get(), stream);
        animation.sourceFrameRate = sourceFrameRate;
        animation.sourceDurationSeconds = sourceDuration;
        const uint64_t estimatedFrameCount = ffmpeg_estimated_frame_count(stream, sourceDuration, sourceFrameRate);
        const bool frameLimitIsFinite = options.maxVideoFrames > 0u;
        const bool sampleAcrossSourceDuration =
            frameLimitIsFinite &&
            sourceDuration > 0.0 &&
            (estimatedFrameCount == 0u || estimatedFrameCount > static_cast<uint64_t>(frameLimit));
        const double sampledFrameDuration = sampleAcrossSourceDuration
            ? sourceDuration / static_cast<double>(frameLimit)
            : 0.0;
        const double fallbackDuration = sourceFrameRate > 0.0
            ? 1.0 / sourceFrameRate
            : 1.0 / 30.0;
        SwsContext* scaleContext = nullptr;
        uint64_t decodedFrameCount = 0u;

        auto append_decoded_frame = [&](double durationSeconds) -> bool {
            if (decodedFrame->width <= 0 || decodedFrame->height <= 0)
            {
                return false;
            }

            const glm::uvec2 targetSize = target_video_size(
                static_cast<uint32_t>(decodedFrame->width),
                static_cast<uint32_t>(decodedFrame->height),
                options);
            if (targetSize.x == 0u || targetSize.y == 0u)
            {
                return false;
            }

            scaleContext = sws_getCachedContext(
                scaleContext,
                decodedFrame->width,
                decodedFrame->height,
                static_cast<AVPixelFormat>(decodedFrame->format),
                static_cast<int>(targetSize.x),
                static_cast<int>(targetSize.y),
                AV_PIX_FMT_RGBA,
                SWS_BILINEAR,
                nullptr,
                nullptr,
                nullptr);
            if (scaleContext == nullptr)
            {
                return false;
            }

            DecodedMediaFrame frame = {};
            frame.width = targetSize.x;
            frame.height = targetSize.y;
            frame.durationSeconds = durationSeconds;
            if (frame.durationSeconds <= 0.0 || frame.durationSeconds > 10.0)
            {
                frame.durationSeconds = fallbackDuration;
            }
            frame.rgba.assign(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4u, 0u);

            uint8_t* dstData[4] = { frame.rgba.data(), nullptr, nullptr, nullptr };
            int dstLineSize[4] = { static_cast<int>(frame.width * 4u), 0, 0, 0 };
            sws_scale(
                scaleContext,
                decodedFrame->data,
                decodedFrame->linesize,
                0,
                decodedFrame->height,
                dstData,
                dstLineSize);
            animation.frames.push_back(std::move(frame));
            return animation.frames.size() < frameLimit;
        };

        auto process_decoded_frame = [&]() -> bool {
            const uint64_t frameIndex = decodedFrameCount++;
            const double frameTime = ffmpeg_frame_time_seconds(
                decodedFrame.get(),
                stream,
                frameIndex,
                fallbackDuration);
            double frameDuration = decodedFrame->duration > 0
                ? static_cast<double>(decodedFrame->duration) * av_rational_to_double(stream->time_base)
                : fallbackDuration;

            if (frameDuration <= 0.0 || frameDuration > 10.0 || !std::isfinite(frameDuration))
            {
                frameDuration = fallbackDuration;
            }

            if (sampleAcrossSourceDuration)
            {
                const double nextSampleTime = static_cast<double>(animation.frames.size()) * sampledFrameDuration;
                if (!animation.frames.empty() && frameTime + fallbackDuration * 0.5 < nextSampleTime)
                {
                    return true;
                }
                frameDuration = sampledFrameDuration;
            }

            return append_decoded_frame(frameDuration);
        };

        while (animation.frames.size() < frameLimit && av_read_frame(formatContext.get(), packet.get()) >= 0)
        {
            if (packet->stream_index != streamIndex)
            {
                av_packet_unref(packet.get());
                continue;
            }

            if (avcodec_send_packet(codecContext.get(), packet.get()) >= 0)
            {
                while (animation.frames.size() < frameLimit)
                {
                    const int receiveResult = avcodec_receive_frame(codecContext.get(), decodedFrame.get());
                    if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
                    {
                        break;
                    }
                    if (receiveResult < 0)
                    {
                        break;
                    }
                    if (!process_decoded_frame())
                    {
                        break;
                    }
                }
            }
            av_packet_unref(packet.get());
        }

        if (animation.frames.size() < frameLimit && avcodec_send_packet(codecContext.get(), nullptr) >= 0)
        {
            while (animation.frames.size() < frameLimit)
            {
                const int receiveResult = avcodec_receive_frame(codecContext.get(), decodedFrame.get());
                if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
                {
                    break;
                }
                if (receiveResult < 0 || !process_decoded_frame())
                {
                    break;
                }
            }
        }

        if (scaleContext != nullptr)
        {
            sws_freeContext(scaleContext);
        }
        if (sourceDuration > 0.0 && animation.valid())
        {
            double uploadedDuration = animation.duration_seconds();
            if (sampleAcrossSourceDuration || uploadedDuration <= 0.0 || !std::isfinite(uploadedDuration))
            {
                const double frameDuration = sourceDuration / static_cast<double>(animation.frames.size());
                for (DecodedMediaFrame& frame : animation.frames)
                {
                    frame.durationSeconds = frameDuration;
                }
            }
            else
            {
                const double durationScale = sourceDuration / uploadedDuration;
                if (durationScale > 0.0 && std::isfinite(durationScale))
                {
                    for (DecodedMediaFrame& frame : animation.frames)
                    {
                        frame.durationSeconds *= durationScale;
                    }
                }
            }
        }
        return animation;
    }
#endif

    DecodedMediaAnimation decode_video_animation(
        const std::filesystem::path& path,
        const Media2DLoadOptions& options)
    {
#if VIBRANCE_HAS_FFMPEG
        DecodedMediaAnimation ffmpegAnimation = decode_video_animation_ffmpeg(path, options);
        if (ffmpegAnimation.valid())
        {
            return ffmpegAnimation;
        }
#endif

        (void)path;
        (void)options;
        return {};
    }

    std::optional<std::string> next_ppm_token(std::string_view text, std::size_t& cursor)
    {
        while (cursor < text.size())
        {
            const char c = text[cursor];
            if (std::isspace(static_cast<unsigned char>(c)))
            {
                ++cursor;
                continue;
            }

            if (c == '#')
            {
                cursor = text.find('\n', cursor);
                if (cursor == std::string_view::npos)
                {
                    cursor = text.size();
                }
                continue;
            }

            break;
        }

        if (cursor >= text.size())
        {
            return std::nullopt;
        }

        const std::size_t start = cursor;
        while (cursor < text.size() && !std::isspace(static_cast<unsigned char>(text[cursor])) && text[cursor] != '#')
        {
            ++cursor;
        }
        return std::string(text.substr(start, cursor - start));
    }

    std::optional<int> next_ppm_int(std::string_view text, std::size_t& cursor)
    {
        std::optional<std::string> token = next_ppm_token(text, cursor);
        if (!token)
        {
            return std::nullopt;
        }

        try
        {
            return std::stoi(*token);
        }
        catch (...)
        {
            return std::nullopt;
        }
    }

    DecodedMediaFrame decode_ascii_ppm(const std::filesystem::path& path)
    {
        DecodedMediaFrame frame = {};
        const std::string text = read_text_file(path);
        std::size_t cursor = 0;
        const std::optional<std::string> magic = next_ppm_token(text, cursor);
        if (!magic || *magic != "P3")
        {
            return frame;
        }

        const std::optional<int> width = next_ppm_int(text, cursor);
        const std::optional<int> height = next_ppm_int(text, cursor);
        const std::optional<int> maxValue = next_ppm_int(text, cursor);
        if (!width || !height || !maxValue || *width <= 0 || *height <= 0 || *maxValue <= 0)
        {
            return frame;
        }

        frame.width = static_cast<uint32_t>(*width);
        frame.height = static_cast<uint32_t>(*height);
        frame.rgba.reserve(static_cast<std::size_t>(*width) * static_cast<std::size_t>(*height) * 4u);

        const auto scale_channel = [&](int value) -> unsigned char {
            const float normalized = static_cast<float>(std::clamp(value, 0, *maxValue)) /
                static_cast<float>(*maxValue);
            return static_cast<unsigned char>(std::round(normalized * 255.0f));
        };

        const std::size_t pixelCount = static_cast<std::size_t>(*width) * static_cast<std::size_t>(*height);
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel)
        {
            const std::optional<int> r = next_ppm_int(text, cursor);
            const std::optional<int> g = next_ppm_int(text, cursor);
            const std::optional<int> b = next_ppm_int(text, cursor);
            if (!r || !g || !b)
            {
                frame.rgba.clear();
                frame.width = 0;
                frame.height = 0;
                return frame;
            }

            frame.rgba.push_back(scale_channel(*r));
            frame.rgba.push_back(scale_channel(*g));
            frame.rgba.push_back(scale_channel(*b));
            frame.rgba.push_back(255u);
        }

        return frame;
    }

#if VIBRANCE_HAS_NANOSVG
    DecodedMediaFrame decode_svg_text(std::string_view svgText, const Media2DLoadOptions& options)
    {
        DecodedMediaFrame frame = {};
        std::vector<char> mutableSvg(svgText.begin(), svgText.end());
        mutableSvg.push_back('\0');
        NSVGimage* svg = nsvgParse(mutableSvg.data(), "px", 96.0f);
        if (svg == nullptr || svg->width <= 0.0f || svg->height <= 0.0f)
        {
            if (svg != nullptr)
            {
                nsvgDelete(svg);
            }
            return frame;
        }

        const float targetWidth = options.rasterWidth > 0u ? static_cast<float>(options.rasterWidth) : svg->width;
        const float scale = targetWidth / std::max(svg->width, 1.0f);
        frame.width = std::max(1u, static_cast<uint32_t>(std::round(svg->width * scale)));
        frame.height = options.rasterHeight > 0u
            ? options.rasterHeight
            : std::max(1u, static_cast<uint32_t>(std::round(svg->height * scale)));
        frame.rgba.assign(static_cast<std::size_t>(frame.width) * static_cast<std::size_t>(frame.height) * 4u, 0u);

        NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
        if (rasterizer != nullptr)
        {
            const float xOffset = (static_cast<float>(frame.width) - svg->width * scale) * 0.5f;
            const float yOffset = (static_cast<float>(frame.height) - svg->height * scale) * 0.5f;
            nsvgRasterize(rasterizer, svg, xOffset, yOffset, scale,
                frame.rgba.data(), static_cast<int>(frame.width), static_cast<int>(frame.height),
                static_cast<int>(frame.width * 4u));
            nsvgDeleteRasterizer(rasterizer);
        }
        nsvgDelete(svg);
        return frame;
    }

    DecodedMediaFrame decode_svg_image(const std::filesystem::path& path, const Media2DLoadOptions& options)
    {
        return decode_svg_text(read_text_file(path), options);
    }
#else
    DecodedMediaFrame decode_svg_text(std::string_view, const Media2DLoadOptions&)
    {
        return {};
    }

    DecodedMediaFrame decode_svg_image(const std::filesystem::path&, const Media2DLoadOptions&)
    {
        return {};
    }
#endif

    DecodedMediaAnimation decode_lottie_animation(
        const vibrance::animation::LottieSvgAnimation& source,
        const Media2DLoadOptions& options)
    {
        DecodedMediaAnimation animation = {};
        if (!source.valid())
        {
            return animation;
        }

        animation.sourceDurationSeconds = source.durationSeconds;
        animation.sourceFrameRate = source.frameRate;
        animation.frames.reserve(source.frames.size());
        for (const vibrance::animation::LottieSvgFrame& sourceFrame : source.frames)
        {
            DecodedMediaFrame frame = decode_svg_text(sourceFrame.svg, options);
            if (!frame.valid())
            {
                animation.frames.clear();
                return animation;
            }
            frame.durationSeconds = sourceFrame.durationSeconds;
            animation.frames.push_back(std::move(frame));
        }
        return animation;
    }

    DecodedMediaAnimation make_css_svg_animation_frames(
        std::string_view svgText,
        const Media2DLoadOptions& options)
    {
        DecodedMediaAnimation animation = {};
        const SvgCssAnimationDocument cssAnimation = parse_svg_css_animations(svgText);
        if (!cssAnimation.valid())
        {
            return animation;
        }

        const double frameRate = options.svgAnimationFrameRate > 0.0
            ? options.svgAnimationFrameRate
            : 60.0;
        const double duration = css_animation_cycle_duration(cssAnimation);
        if (duration <= 0.0 || frameRate <= 0.0)
        {
            return animation;
        }

        const uint32_t maxFrames = options.maxAnimationFrames > 0u
            ? options.maxAnimationFrames
            : std::numeric_limits<uint32_t>::max();
        const uint32_t requestedFrames = options.svgAnimationFrames > 1u
            ? options.svgAnimationFrames
            : std::max(2u, static_cast<uint32_t>(std::ceil(duration * frameRate)));
        const uint32_t frameCount = std::max(2u, std::min(requestedFrames, maxFrames));
        const double frameDuration = duration / static_cast<double>(frameCount);
        const double timelineStart = css_animation_timeline_start(cssAnimation);

        animation.frames.reserve(frameCount);
        for (uint32_t frameIndex = 0; frameIndex < frameCount; ++frameIndex)
        {
            const double frameTime = timelineStart + static_cast<double>(frameIndex) * frameDuration;
            const std::string frameSvg = apply_css_animation_frame(svgText, cssAnimation, frameTime);
            DecodedMediaFrame frame = decode_svg_text(frameSvg, options);
            if (!frame.valid())
            {
                animation.frames.clear();
                return animation;
            }
            frame.durationSeconds = frameDuration;
            animation.frames.push_back(std::move(frame));
        }

        return animation;
    }

    vk::Sampler make_media_sampler(
        vk::Device logicalDevice,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
        uint32_t mipLevels)
    {
        vk::SamplerCreateInfo samplerInfo = {};
        samplerInfo.magFilter = vk::Filter::eLinear;
        samplerInfo.minFilter = vk::Filter::eLinear;
        samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;
        samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
        samplerInfo.mipLodBias = 0.0f;
        samplerInfo.anisotropyEnable = VK_FALSE;
        samplerInfo.maxAnisotropy = 1.0f;
        samplerInfo.compareEnable = VK_FALSE;
        samplerInfo.compareOp = vk::CompareOp::eAlways;
        samplerInfo.minLod = 0.0f;
        samplerInfo.maxLod = static_cast<float>(std::max(1u, mipLevels) - 1u);
        samplerInfo.borderColor = vk::BorderColor::eIntTransparentBlack;
        samplerInfo.unnormalizedCoordinates = VK_FALSE;

        auto result = logicalDevice.createSampler(samplerInfo);
        if (result.result != vk::Result::eSuccess)
        {
            Logger::fetch_logger()->vulkan("Failed to create 2D media sampler.");
            return nullptr;
        }

        VkSampler samplerHandle = result.value;
        deviceDeletionQueue.push_back([samplerHandle](vk::Device device) {
            device.destroySampler(samplerHandle);
        });
        return result.value;
    }

    bool upload_rgba_to_image(
        VmaAllocator& allocator,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        StorageImage& image,
        const std::vector<unsigned char>& rgba,
        std::string_view label)
    {
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
            logger->vulkan("Failed to create 2D media staging buffer for " + std::string(label) + ".");
            return false;
        }

        std::memcpy(stagingInfo.pMappedData, rgba.data(), rgba.size());

        vk::Result result = commandBuffer.reset();
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to reset 2D media upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        vk::CommandBufferBeginInfo beginInfo = {};
        beginInfo.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit;
        result = commandBuffer.begin(beginInfo);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to begin 2D media upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        transition_image_layout(commandBuffer, image.image,
            vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal,
            vk::AccessFlagBits::eNone, vk::AccessFlagBits::eTransferWrite,
            vk::PipelineStageFlagBits::eTopOfPipe, vk::PipelineStageFlagBits::eTransfer,
            vk::ImageAspectFlagBits::eColor, 0, image.mipLevels);

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

        if (image.mipLevels > 1u)
        {
            int32_t mipWidth = static_cast<int32_t>(image.extent.width);
            int32_t mipHeight = static_cast<int32_t>(image.extent.height);
            for (uint32_t mipLevel = 1; mipLevel < image.mipLevels; ++mipLevel)
            {
                transition_image_layout(commandBuffer, image.image,
                    vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eTransferSrcOptimal,
                    vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eTransferRead,
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eTransfer,
                    vk::ImageAspectFlagBits::eColor, mipLevel - 1u, 1u);

                vk::ImageBlit blit = {};
                blit.srcOffsets[0] = vk::Offset3D { 0, 0, 0 };
                blit.srcOffsets[1] = vk::Offset3D { mipWidth, mipHeight, 1 };
                blit.srcSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                blit.srcSubresource.mipLevel = mipLevel - 1u;
                blit.srcSubresource.baseArrayLayer = 0;
                blit.srcSubresource.layerCount = 1;

                const int32_t nextMipWidth = std::max(1, mipWidth / 2);
                const int32_t nextMipHeight = std::max(1, mipHeight / 2);
                blit.dstOffsets[0] = vk::Offset3D { 0, 0, 0 };
                blit.dstOffsets[1] = vk::Offset3D { nextMipWidth, nextMipHeight, 1 };
                blit.dstSubresource.aspectMask = vk::ImageAspectFlagBits::eColor;
                blit.dstSubresource.mipLevel = mipLevel;
                blit.dstSubresource.baseArrayLayer = 0;
                blit.dstSubresource.layerCount = 1;

                commandBuffer.blitImage(image.image, vk::ImageLayout::eTransferSrcOptimal,
                    image.image, vk::ImageLayout::eTransferDstOptimal, 1, &blit, vk::Filter::eLinear);

                transition_image_layout(commandBuffer, image.image,
                    vk::ImageLayout::eTransferSrcOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                    vk::AccessFlagBits::eTransferRead, vk::AccessFlagBits::eShaderRead,
                    vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                    vk::ImageAspectFlagBits::eColor, mipLevel - 1u, 1u);

                mipWidth = nextMipWidth;
                mipHeight = nextMipHeight;
            }

            transition_image_layout(commandBuffer, image.image,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader,
                vk::ImageAspectFlagBits::eColor, image.mipLevels - 1u, 1u);
        }
        else
        {
            transition_image_layout(commandBuffer, image.image,
                vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal,
                vk::AccessFlagBits::eTransferWrite, vk::AccessFlagBits::eShaderRead,
                vk::PipelineStageFlagBits::eTransfer, vk::PipelineStageFlagBits::eComputeShader);
        }

        result = commandBuffer.end();
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to end 2D media upload command buffer.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        vk::SubmitInfo submitInfo = {};
        submitInfo.commandBufferCount = 1;
        submitInfo.pCommandBuffers = &commandBuffer;
        result = queue.submit(1, &submitInfo, nullptr);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to submit 2D media upload.");
            vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
            return false;
        }

        result = queue.waitIdle();
        vmaDestroyBuffer(allocator, stagingBuffer, stagingAllocation);
        if (result != vk::Result::eSuccess)
        {
            logger->vulkan("Failed to wait for 2D media upload.");
            return false;
        }

        return true;
    }

    bool make_media_texture(
        const DecodedMediaFrame& frame,
        const Media2DLoadOptions& options,
        VmaAllocator& allocator,
        std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        vk::Device logicalDevice,
        vk::DescriptorPool descriptorPool,
        vk::DescriptorSetLayout descriptorSetLayout,
        Media2DAsset& asset)
    {
        if (!frame.valid() || !descriptorPool || !descriptorSetLayout)
        {
            return false;
        }

        std::vector<unsigned char> rgba = frame.rgba;
        if (options.premultiplyAlpha)
        {
            bleed_transparent_rgb(rgba, frame.width, frame.height);
            premultiply_alpha(rgba);
        }

        const uint32_t mipLevels = options.generateMipmaps
            ? mip_count_for_extent(vk::Extent2D { frame.width, frame.height })
            : 1u;
        asset.image = std::make_unique<StorageImage>(
            allocator,
            options.srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm,
            vk::Extent2D { frame.width, frame.height },
            commandBuffer,
            queue,
            logicalDevice,
            vmaDeletionQueue,
            deviceDeletionQueue,
            vk::ImageUsageFlagBits::eSampled,
            mipLevels,
            false);
        if (!upload_rgba_to_image(allocator, commandBuffer, queue, *asset.image, rgba, asset.name))
        {
            asset.image.reset();
            return false;
        }

        asset.sampler = make_media_sampler(logicalDevice, deviceDeletionQueue, mipLevels);
        if (!asset.sampler)
        {
            asset.image.reset();
            return false;
        }

        asset.descriptorSet = allocate_descriptor_set(logicalDevice, descriptorPool, descriptorSetLayout);
        if (!asset.descriptorSet)
        {
            asset.image.reset();
            asset.sampler = nullptr;
            return false;
        }

        vk::DescriptorImageInfo imageInfo = asset.image->descriptor;
        imageInfo.sampler = asset.sampler;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        asset.image->descriptor = imageInfo;

        vk::WriteDescriptorSet write = {};
        write.dstSet = asset.descriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &imageInfo;
        logicalDevice.updateDescriptorSets(1, &write, 0, nullptr);
        asset.drawable = true;
        return true;
    }

    bool make_extra_media_frame_texture(
        const DecodedMediaFrame& frame,
        const Media2DLoadOptions& options,
        VmaAllocator& allocator,
        std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
        std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
        vk::CommandBuffer commandBuffer,
        vk::Queue queue,
        vk::Device logicalDevice,
        vk::DescriptorPool descriptorPool,
        vk::DescriptorSetLayout descriptorSetLayout,
        Media2DAsset& asset)
    {
        if (!frame.valid() || !asset.sampler || !descriptorPool || !descriptorSetLayout)
        {
            return false;
        }

        std::vector<unsigned char> rgba = frame.rgba;
        if (options.premultiplyAlpha)
        {
            bleed_transparent_rgb(rgba, frame.width, frame.height);
            premultiply_alpha(rgba);
        }

        const uint32_t mipLevels = options.generateMipmaps
            ? mip_count_for_extent(vk::Extent2D { frame.width, frame.height })
            : 1u;
        auto image = std::make_unique<StorageImage>(
            allocator,
            options.srgb ? vk::Format::eR8G8B8A8Srgb : vk::Format::eR8G8B8A8Unorm,
            vk::Extent2D { frame.width, frame.height },
            commandBuffer,
            queue,
            logicalDevice,
            vmaDeletionQueue,
            deviceDeletionQueue,
            vk::ImageUsageFlagBits::eSampled,
            mipLevels,
            false);
        if (!upload_rgba_to_image(allocator, commandBuffer, queue, *image, rgba, asset.name))
        {
            return false;
        }

        vk::DescriptorSet descriptorSet = allocate_descriptor_set(logicalDevice, descriptorPool, descriptorSetLayout);
        if (!descriptorSet)
        {
            return false;
        }

        vk::DescriptorImageInfo imageInfo = image->descriptor;
        imageInfo.sampler = asset.sampler;
        imageInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
        image->descriptor = imageInfo;

        vk::WriteDescriptorSet write = {};
        write.dstSet = descriptorSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorCount = 1;
        write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
        write.pImageInfo = &imageInfo;
        logicalDevice.updateDescriptorSets(1, &write, 0, nullptr);

        asset.extraFrameImages.push_back(std::move(image));
        asset.frameDescriptorSets.push_back(descriptorSet);
        return true;
    }

    void apply_animation_metadata(Media2DAsset& asset, const DecodedMediaAnimation& animation)
    {
        asset.frameDurationsSeconds.clear();
        asset.frameCount = animation.valid()
            ? static_cast<uint32_t>(animation.frames.size())
            : 1u;
        asset.animated = asset.frameCount > 1u || asset.animated;

        double duration = 0.0;
        if (animation.valid())
        {
            asset.frameDurationsSeconds.reserve(animation.frames.size());
            for (const DecodedMediaFrame& frame : animation.frames)
            {
                const double frameDuration = frame.durationSeconds > 0.0 ? frame.durationSeconds : 0.1;
                asset.frameDurationsSeconds.push_back(frameDuration);
                duration += frameDuration;
            }
        }

        asset.durationSeconds = animation.sourceDurationSeconds > 0.0
            ? animation.sourceDurationSeconds
            : duration;
        asset.frameRate = animation.sourceFrameRate > 0.0
            ? animation.sourceFrameRate
            : duration > 0.0 && asset.frameCount > 0u
            ? static_cast<double>(asset.frameCount) / duration
            : 0.0;
    }

    void clamp_animation_metadata_to_uploaded_frames(
        Media2DAsset& asset,
        const DecodedMediaAnimation& animation)
    {
        const uint32_t uploadedFrameCount = static_cast<uint32_t>(asset.frameDescriptorSets.size());
        asset.frameCount = std::max(uploadedFrameCount, 1u);
        if (asset.frameDurationsSeconds.size() > asset.frameCount)
        {
            asset.frameDurationsSeconds.resize(asset.frameCount);
        }

        double duration = 0.0;
        for (double frameDuration : asset.frameDurationsSeconds)
        {
            duration += std::max(frameDuration, 0.0);
        }

        asset.durationSeconds = animation.sourceDurationSeconds > 0.0
            ? animation.sourceDurationSeconds
            : duration;
        asset.frameRate = animation.sourceFrameRate > 0.0
            ? animation.sourceFrameRate
            : duration > 0.0 && asset.frameCount > 0u
            ? static_cast<double>(asset.frameCount) / duration
            : 0.0;
        asset.animated = asset.frameCount > 1u;
    }
}

Media2DHandle load_media_2d_asset(
    const std::filesystem::path& path,
    const Media2DLoadOptions& options,
    uint32_t mediaId,
    VmaAllocator& allocator,
    std::deque<std::function<void(VmaAllocator)>>& vmaDeletionQueue,
    std::deque<std::function<void(vk::Device)>>& deviceDeletionQueue,
    vk::CommandBuffer commandBuffer,
    vk::Queue queue,
    vk::Device logicalDevice,
    vk::DescriptorPool descriptorPool,
    vk::DescriptorSetLayout descriptorSetLayout,
    Media2DAsset& outAsset)
{
    Logger* logger = Logger::fetch_logger();
    outAsset = {};
    outAsset.id = mediaId;
    outAsset.path = std::filesystem::absolute(path).lexically_normal();
    outAsset.name = outAsset.path.filename().string();
    outAsset.sourceType = source_type_from_path(outAsset.path);
    outAsset.frameCount = 1;
    outAsset.premultipliedAlpha = options.premultiplyAlpha;

    DecodedMediaFrame frame = {};
    DecodedMediaAnimation animation = {};
    if (outAsset.sourceType == Media2DSourceType::eSvg)
    {
        const std::string svgText = read_text_file(outAsset.path);
        SvgCssAnimationDocument cssDocument = {};
        if (!svgText.empty())
        {
            outAsset.pixelSize = parse_svg_pixel_size(svgText);
            outAsset.animated = svg_looks_animated(svgText);
            cssDocument = parse_svg_css_animations(svgText);
            if (outAsset.animated)
            {
                animation = make_css_svg_animation_frames(svgText, options);
                if (animation.valid())
                {
                    frame = animation.frames.front();
                }
            }
        }

        if (!frame.valid())
        {
            const std::string staticSvgText = !cssDocument.classes.empty()
                ? apply_css_animation_frame(svgText, cssDocument, 0.0)
                : svgText;
            frame = decode_svg_text(staticSvgText, options);
        }
        if (!frame.valid())
        {
            logger->vulkan(
                "Registered SVG media metadata for " + outAsset.path.string() +
                ". SVG rasterization is not available in this build.");
            return outAsset.handle();
        }

        if (outAsset.animated && !animation.valid())
        {
            animation = make_svg_animation_frames(frame, svgText, options);
        }
    }
    else if (outAsset.sourceType == Media2DSourceType::eLottie)
    {
        const std::string jsonText = read_text_file(outAsset.path);
        const vibrance::animation::LottieSvgAnimation lottie =
            vibrance::animation::make_lottie_svg_animation(
                jsonText,
                options.lottieAnimationFrames,
                options.maxAnimationFrames);
        if (!lottie.valid())
        {
            logger->vulkan(
                "Failed to parse Lottie animation " + outAsset.path.string() +
                (lottie.error.empty() ? "." : ": " + lottie.error));
            return {};
        }

        if (lottie.ignoredExpressionCount > 0u)
        {
            logger->warning(
                "Lottie animation " + outAsset.path.string() + " contains " +
                std::to_string(lottie.ignoredExpressionCount) +
                " After Effects expression(s); authored keyframes are used as the portable fallback.");
        }
        if (lottie.unsupportedFeatureCount > 0u)
        {
            logger->warning(
                "Lottie animation " + outAsset.path.string() + " contains " +
                std::to_string(lottie.unsupportedFeatureCount) +
                " unsupported feature(s); supported vector layers continue rendering.");
        }

        outAsset.animated = lottie.frames.size() > 1u;
        animation = decode_lottie_animation(lottie, options);
        if (animation.valid())
        {
            frame = animation.frames.front();
        }
        else
        {
            logger->vulkan(
                "Failed to rasterize Lottie animation frames: " +
                outAsset.path.string());
            return {};
        }
    }
    else if (outAsset.sourceType == Media2DSourceType::eVideo)
    {
        outAsset.animated = true;
        animation = decode_video_animation(outAsset.path, options);
        if (animation.valid())
        {
            frame = animation.frames.front();
        }
        else
        {
            logger->vulkan(
                "Registered video media metadata for " + outAsset.path.string() +
                ". No available video decoder produced frames.");
            return outAsset.handle();
        }
    }
    else
    {
        if (path_has_extension(outAsset.path, ".gif"))
        {
            animation = decode_gif_animation(outAsset.path, options);
            if (animation.valid())
            {
                frame = animation.frames.front();
            }
        }
        else if (path_has_extension(outAsset.path, ".png"))
        {
            animation = decode_apng_animation(outAsset.path, options);
            if (animation.valid())
            {
                frame = animation.frames.front();
            }
        }

        if (!frame.valid())
        {
            frame = decode_raster_image(outAsset.path);
        }
        if (!frame.valid() && path_has_extension(outAsset.path, ".ppm"))
        {
            frame = decode_ascii_ppm(outAsset.path);
        }
        if (!frame.valid())
        {
            logger->vulkan("Failed to decode 2D media image: " + outAsset.path.string());
            return {};
        }
    }

    const DecodedMediaFrame& firstFrame = animation.valid() ? animation.frames.front() : frame;
    outAsset.pixelSize = { firstFrame.width, firstFrame.height };
    outAsset.hasBlackBackground = media_frame_has_black_background(firstFrame);
    if (!make_media_texture(
        firstFrame,
        options,
        allocator,
        vmaDeletionQueue,
        deviceDeletionQueue,
        commandBuffer,
        queue,
        logicalDevice,
        descriptorPool,
        descriptorSetLayout,
        outAsset))
    {
        logger->vulkan("Failed to upload 2D media image: " + outAsset.path.string());
        return {};
    }

    outAsset.frameDescriptorSets.clear();
    outAsset.frameDescriptorSets.push_back(outAsset.descriptorSet);
    if (animation.valid())
    {
        apply_animation_metadata(outAsset, animation);
        for (std::size_t frameIndex = 1; frameIndex < animation.frames.size(); ++frameIndex)
        {
            if (!make_extra_media_frame_texture(
                animation.frames[frameIndex],
                options,
                allocator,
                vmaDeletionQueue,
                deviceDeletionQueue,
                commandBuffer,
                queue,
                logicalDevice,
                descriptorPool,
                descriptorSetLayout,
                outAsset))
            {
                logger->vulkan("Stopped uploading animation frames for " + outAsset.path.string() +
                    " at frame " + std::to_string(frameIndex) + ".");
                break;
            }
        }
        clamp_animation_metadata_to_uploaded_frames(outAsset, animation);
    }
    else
    {
        outAsset.frameCount = 1u;
        outAsset.durationSeconds = 0.0;
        outAsset.frameRate = 0.0;
        outAsset.animated = false;
        outAsset.frameDurationsSeconds.clear();
    }

    logger->vulkan("Registered 2D media " + outAsset.path.string() +
        " as handle " + std::to_string(mediaId) + " (" +
        std::to_string(outAsset.pixelSize.x) + "x" +
        std::to_string(outAsset.pixelSize.y) + ", " +
        std::to_string(outAsset.frameCount) + " frame" +
        (outAsset.frameCount == 1u ? "" : "s") + ").");
    return outAsset.handle();
}

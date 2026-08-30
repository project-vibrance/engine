#include <vibranceUI/media/presentation.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace media_ui
{
    namespace
    {
        float supported_media_ratio(const Media2DHandle& media)
        {
            const float sourceRatio = media.pixelSize.y > 0u ?
                static_cast<float>(media.pixelSize.x) /
                    static_cast<float>(media.pixelSize.y) :
                1.0f;
            constexpr std::array<float, 3> ratios {
                1.0f,
                4.0f / 3.0f,
                16.0f / 9.0f
            };
            return *std::min_element(
                ratios.begin(),
                ratios.end(),
                [sourceRatio](float left, float right) {
                    return std::abs(left - sourceRatio) <
                        std::abs(right - sourceRatio);
                });
        }
    }

    std::string ellipsize_utf8(
        std::string_view value,
        std::size_t maxCharacters)
    {
        std::size_t cursor = 0u;
        std::size_t characters = 0u;
        while (cursor < value.size() && characters < maxCharacters)
        {
            const unsigned char lead =
                static_cast<unsigned char>(value[cursor]);
            std::size_t sequenceLength = 1u;
            if ((lead & 0xE0u) == 0xC0u)
            {
                sequenceLength = 2u;
            }
            else if ((lead & 0xF0u) == 0xE0u)
            {
                sequenceLength = 3u;
            }
            else if ((lead & 0xF8u) == 0xF0u)
            {
                sequenceLength = 4u;
            }
            if (cursor + sequenceLength > value.size())
            {
                break;
            }
            cursor += sequenceLength;
            ++characters;
        }

        if (cursor >= value.size())
        {
            return std::string(value);
        }
        return std::string(value.substr(0u, cursor)) + "...";
    }

    std::string media_time(std::int64_t milliseconds)
    {
        const std::int64_t totalSeconds =
            std::max(milliseconds, std::int64_t(0)) / 1000;
        const std::int64_t minutes = totalSeconds / 60;
        const std::int64_t seconds = totalSeconds % 60;
        return std::to_string(minutes) +
            (seconds < 10 ? ":0" : ":") +
            std::to_string(seconds);
    }

    glm::vec2 supported_media_size(const Media2DHandle& media)
    {
        return supported_media_size(media, { 112.0f, 66.0f });
    }

    glm::vec2 supported_media_size(
        const Media2DHandle& media,
        glm::vec2 maximumSize)
    {
        const float maximumWidth = std::max(maximumSize.x, 0.0f);
        const float maximumHeight = std::max(maximumSize.y, 0.0f);
        const float ratio = supported_media_ratio(media);
        const float height = std::min(maximumHeight, maximumWidth / ratio);
        return { height * ratio, height };
    }

    float media_ease_out_back(float value, float overshoot)
    {
        const float t = std::clamp(value, 0.0f, 1.0f) - 1.0f;
        const float safeOvershoot = std::max(overshoot, 0.0f);
        return 1.0f + (safeOvershoot + 1.0f) * t * t * t +
            safeOvershoot * t * t;
    }
}

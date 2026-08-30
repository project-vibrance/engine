#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/renderer/renderer2d_components.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace media_ui
{
    VIBRANCE_ENGINE_API std::string ellipsize_utf8(
        std::string_view value,
        std::size_t maxCharacters);
    VIBRANCE_ENGINE_API std::string media_time(
        std::int64_t milliseconds);
    VIBRANCE_ENGINE_API glm::vec2 supported_media_size(
        const Media2DHandle& media);
    VIBRANCE_ENGINE_API glm::vec2 supported_media_size(
        const Media2DHandle& media,
        glm::vec2 maximumSize);
    VIBRANCE_ENGINE_API float media_ease_out_back(
        float value,
        float overshoot = 0.55f);
}

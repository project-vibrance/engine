// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/graphics/colour_palette.h>
#include <functional>
#include <stop_token>

namespace colour_palette
{
enum class ImageSourceKind { eFile, eEncoded, eDib, eRgba };

struct ImageSource
{
    ImageSourceKind kind = ImageSourceKind::eFile;
    std::filesystem::path path;
    std::string name;
    std::vector<unsigned char> bytes;
    std::uint32_t width = 0u;
    std::uint32_t height = 0u;
};

struct ScreenPickState
{
    bool primaryDown = false;
    bool cancelled = false;
};

VIBRANCE_ENGINE_API std::optional<ImageSource> choose_image_file(void* owner, std::string& error);
VIBRANCE_ENGINE_API std::optional<ImageSource> clipboard_image(void* owner, std::string& error);
VIBRANCE_ENGINE_API Image decode_image(const ImageSource& source, std::string& error,
    std::stop_token cancellation = {});
VIBRANCE_ENGINE_API bool copy_image_to_clipboard(void* owner, const Image& image, std::string& error);
VIBRANCE_ENGINE_API std::optional<std::uint32_t> screen_colour_at_cursor(std::string& error);
VIBRANCE_ENGINE_API ScreenPickState screen_pick_state() noexcept;
VIBRANCE_ENGINE_API bool begin_native_screen_pick(std::function<void(std::optional<std::uint32_t>)> completed);
}

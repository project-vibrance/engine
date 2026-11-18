// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/image_services.h>


namespace colour_palette
{
std::optional<ImageSource> clipboard_image(void*, std::string& error)
{ error = "Image palette clipboard access is unavailable on this platform."; return {}; }
Image decode_image(const ImageSource&, std::string& error, std::stop_token)
{ error = "Image palette decoding is unavailable on this platform."; return {}; }
bool copy_image_to_clipboard(void*, const Image&, std::string& error)
{ error = "Image palette clipboard access is unavailable on this platform."; return false; }
std::optional<std::uint32_t> screen_colour_at_cursor(std::string& error)
{ error = "Screen colour sampling is unavailable on this platform."; return {}; }
ScreenPickState screen_pick_state() noexcept { return { false, true }; }
bool begin_native_screen_pick(std::function<void(std::optional<std::uint32_t>)>) { return false; }
}

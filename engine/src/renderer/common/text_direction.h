#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// Internal Unicode helpers shared by atlas population and text layout. Text
// remains logical UTF-8 in TextComponent; only the glyph run is visual order.
std::vector<std::uint32_t> renderer2d_decode_utf8(std::string_view text);
std::vector<std::uint32_t> renderer2d_visual_codepoints(std::string_view text);
bool renderer2d_text_is_right_to_left(std::string_view text);

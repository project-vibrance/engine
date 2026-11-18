// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>
#include <vector>



std::vector<std::uint32_t> renderer2d_decode_utf8(std::string_view text);
std::vector<std::uint32_t> renderer2d_visual_codepoints(std::string_view text);
bool renderer2d_text_is_right_to_left(std::string_view text);

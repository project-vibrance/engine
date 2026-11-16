// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/ui/input.h>
#include <string_view>

struct UiTextRange
{
    std::size_t start = 0u;
    std::size_t end = 0u;
};

VIBRANCE_ENGINE_API std::string ui_text_encode_codepoint(uint32_t codepoint);
VIBRANCE_ENGINE_API std::vector<std::size_t> ui_text_grapheme_boundaries(std::string_view text);
VIBRANCE_ENGINE_API void ui_text_input_sync(TextInputComponent& input);
VIBRANCE_ENGINE_API std::size_t ui_text_previous_character(const TextInputComponent& input, std::size_t position);
VIBRANCE_ENGINE_API std::size_t ui_text_next_character(const TextInputComponent& input, std::size_t position);
VIBRANCE_ENGINE_API UiTextRange ui_text_selection(const TextInputComponent& input);
VIBRANCE_ENGINE_API UiTextRange ui_text_word_at(const TextInputComponent& input, std::size_t position);
VIBRANCE_ENGINE_API std::size_t ui_text_previous_word(const TextInputComponent& input, std::size_t position);
VIBRANCE_ENGINE_API std::size_t ui_text_next_word(const TextInputComponent& input, std::size_t position);
VIBRANCE_ENGINE_API std::size_t ui_text_line_start(const TextInputComponent& input, std::size_t position);
VIBRANCE_ENGINE_API std::size_t ui_text_line_end(const TextInputComponent& input, std::size_t position);
VIBRANCE_ENGINE_API void ui_text_move_caret(TextInputComponent& input, std::size_t position, bool extend);
VIBRANCE_ENGINE_API bool ui_text_replace_selection(TextInputComponent& input, std::string_view text,
    TextInputEditKind kind = TextInputEditKind::eReplace, double timeSeconds = 0.0);
VIBRANCE_ENGINE_API bool ui_text_erase(TextInputComponent& input, bool forward,
    bool word = false, bool toLineBoundary = false, double timeSeconds = 0.0);
VIBRANCE_ENGINE_API bool ui_text_undo(TextInputComponent& input, bool redo = false);

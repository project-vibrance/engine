// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/ui/text_editing.h>
#include <hb.h>

#include <algorithm>
#include <cmath>

namespace
{
    struct Character { uint32_t codepoint; std::size_t start; std::size_t end; };

    std::vector<Character> decode(std::string_view text)
    {
        std::vector<Character> result;
        for (std::size_t start = 0u; start < text.size();)
        {
            const auto lead = static_cast<unsigned char>(text[start]);
            const unsigned count = lead < 0x80 ? 1u : lead >= 0xC2 && lead <= 0xDF ? 2u :
                lead >= 0xE0 && lead <= 0xEF ? 3u : lead >= 0xF0 && lead <= 0xF4 ? 4u : 0u;
            uint32_t value = count == 1 ? lead : lead & (0x7Fu >> count);
            bool valid = count != 0u && start + count <= text.size();
            for (unsigned index = 1u; valid && index < count; ++index)
            {
                const auto next = static_cast<unsigned char>(text[start + index]);
                valid = (next & 0xC0u) == 0x80u;
                value = (value << 6u) | (next & 0x3Fu);
            }
            valid = valid && !(value >= 0xD800u && value <= 0xDFFFu) && value <= 0x10FFFFu &&
                (count < 2u || value >= 0x80u) && (count < 3u || value >= 0x800u) && (count < 4u || value >= 0x10000u);
            const auto end = start + (valid ? count : 1u);
            result.push_back({ valid ? value : 0xFFFDu, start, end });
            start = end;
        }
        return result;
    }

    auto category(uint32_t value) { return hb_unicode_general_category(hb_unicode_funcs_get_default(), value); }
    bool mark(uint32_t value)
    {
        const auto kind = category(value);
        return kind == HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK ||
            kind == HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK || kind == HB_UNICODE_GENERAL_CATEGORY_ENCLOSING_MARK ||
            (value >= 0x1F3FBu && value <= 0x1F3FFu) || (value >= 0xE0020u && value <= 0xE007Fu);
    }
    bool regional(uint32_t value) { return value >= 0x1F1E6u && value <= 0x1F1FFu; }
    bool pictographic(uint32_t value)
    {
        return (value >= 0x1F000u && value <= 0x1FAFFu) || (value >= 0x2300u && value <= 0x27FFu) ||
            value == 0xA9u || value == 0xAEu || value == 0x3030u || value == 0x303Du || value == 0x3297u || value == 0x3299u;
    }
    int hangul(uint32_t value)
    {
        if ((value >= 0x1100u && value <= 0x115Fu) || (value >= 0xA960u && value <= 0xA97Cu)) return 1;
        if ((value >= 0x1160u && value <= 0x11A7u) || (value >= 0xD7B0u && value <= 0xD7C6u)) return 2;
        if ((value >= 0x11A8u && value <= 0x11FFu) || (value >= 0xD7CBu && value <= 0xD7FBu)) return 3;
        if (value >= 0xAC00u && value <= 0xD7A3u) return (value - 0xAC00u) % 28u == 0u ? 4 : 5;
        return 0;
    }
    int word_kind(std::string_view text, std::size_t position)
    {
        if (position >= text.size()) return 0;
        const auto chars = decode(text.substr(position));
        const uint32_t value = chars.front().codepoint;
        const auto kind = category(value);
        if (value == '\n' || value == '\r' || value == '\t' || kind == HB_UNICODE_GENERAL_CATEGORY_SPACE_SEPARATOR ||
            kind == HB_UNICODE_GENERAL_CATEGORY_LINE_SEPARATOR || kind == HB_UNICODE_GENERAL_CATEGORY_PARAGRAPH_SEPARATOR) return 0;
        switch (kind)
        {
        case HB_UNICODE_GENERAL_CATEGORY_LOWERCASE_LETTER:
        case HB_UNICODE_GENERAL_CATEGORY_UPPERCASE_LETTER:
        case HB_UNICODE_GENERAL_CATEGORY_TITLECASE_LETTER:
        case HB_UNICODE_GENERAL_CATEGORY_MODIFIER_LETTER:
        case HB_UNICODE_GENERAL_CATEGORY_OTHER_LETTER:
        case HB_UNICODE_GENERAL_CATEGORY_DECIMAL_NUMBER:
        case HB_UNICODE_GENERAL_CATEGORY_LETTER_NUMBER:
        case HB_UNICODE_GENERAL_CATEGORY_OTHER_NUMBER:
        case HB_UNICODE_GENERAL_CATEGORY_CONNECT_PUNCTUATION:
        case HB_UNICODE_GENERAL_CATEGORY_NON_SPACING_MARK:
        case HB_UNICODE_GENERAL_CATEGORY_SPACING_MARK: return 1;
        default: return 2;
        }
    }
    std::size_t boundary(const TextInputComponent& input, std::size_t position)
    {
        position = std::min(position, input.value.size());
        const auto next = std::upper_bound(input.graphemeBoundaries.begin(), input.graphemeBoundaries.end(), position);
        return next == input.graphemeBoundaries.begin() ? 0u : *std::prev(next);
    }
    TextInputEditSnapshot snapshot(const TextInputComponent& input)
    {
        return { input.value, input.caretByte, input.selectionAnchorByte };
    }
    void remember(std::vector<TextInputEditSnapshot>& history, TextInputEditSnapshot value)
    {
        constexpr std::size_t maximumHistory = 100u;
        if (history.size() >= maximumHistory) history.erase(history.begin());
        history.push_back(std::move(value));
    }
    void edited(TextInputComponent& input)
    {
        input.observedValue = input.value;
        input.graphemeBoundaries = ui_text_grapheme_boundaries(input.value);
        input.caretByte = boundary(input, input.caretByte);
        input.selectionAnchorByte = boundary(input, input.selectionAnchorByte);
        input.caretWakeRequested = true;
        input.preferredCaretX = std::numeric_limits<float>::quiet_NaN();
    }
    std::string sanitise(std::string_view text, bool multiline)
    {
        std::string result;
        bool previousCarriageReturn = false;
        for (const auto& character : decode(text))
        {
            const auto value = character.codepoint;
            if (value == '\n' && previousCarriageReturn) { previousCarriageReturn = false; continue; }
            previousCarriageReturn = value == '\r';
            if (value == '\r' || value == '\n') result += multiline ? '\n' : ' ';
            else if (value == '\t') result += multiline ? '\t' : ' ';
            else if (value >= 32u && value != 127u) result += ui_text_encode_codepoint(value);
        }
        return result;
    }
    bool replace(TextInputComponent& input, UiTextRange range, std::string replacement,
        TextInputEditKind kind, double timeSeconds)
    {
        if (input.readOnly || !input.enabled) return false;
        const auto remaining = input.value.size() - (range.end - range.start);
        const auto capacity = input.maxBytes > remaining ? input.maxBytes - remaining : 0u;
        if (replacement.size() > capacity)
        {
            const auto boundaries = ui_text_grapheme_boundaries(replacement);
            const auto end = std::upper_bound(boundaries.begin(), boundaries.end(), capacity);
            replacement.resize(end == boundaries.begin() ? 0u : *std::prev(end));
            if (replacement.empty()) return false;
        }
        if (range.start == range.end && replacement.empty()) return false;
        if (input.value.compare(range.start, range.end - range.start, replacement) == 0)
        {
            input.caretByte = input.selectionAnchorByte = range.start + replacement.size();
            input.lastEditKind = TextInputEditKind::eNone;
            input.caretWakeRequested = true;
            return false;
        }
        const bool coalesce = kind != TextInputEditKind::eReplace && kind == input.lastEditKind &&
            input.caretByte == input.selectionAnchorByte && timeSeconds > 0.0 && input.lastEditSeconds > 0.0 &&
            timeSeconds >= input.lastEditSeconds && timeSeconds - input.lastEditSeconds <= 0.75;
        if (!coalesce) remember(input.undoHistory, snapshot(input));
        input.redoHistory.clear();
        input.value.replace(range.start, range.end - range.start, replacement);
        input.caretByte = input.selectionAnchorByte = range.start + replacement.size();
        input.lastEditKind = kind;
        input.lastEditSeconds = timeSeconds;
        edited(input);
        return true;
    }
}

std::string ui_text_encode_codepoint(uint32_t value)
{
    if (value > 0x10FFFFu || (value >= 0xD800u && value <= 0xDFFFu)) return {};
    std::string result;
    if (value < 0x80u) result += static_cast<char>(value);
    else if (value < 0x800u)
    {
        result += static_cast<char>(0xC0u | (value >> 6u));
        result += static_cast<char>(0x80u | (value & 0x3Fu));
    }
    else if (value < 0x10000u)
    {
        result += static_cast<char>(0xE0u | (value >> 12u));
        result += static_cast<char>(0x80u | ((value >> 6u) & 0x3Fu));
        result += static_cast<char>(0x80u | (value & 0x3Fu));
    }
    else
    {
        result += static_cast<char>(0xF0u | (value >> 18u));
        result += static_cast<char>(0x80u | ((value >> 12u) & 0x3Fu));
        result += static_cast<char>(0x80u | ((value >> 6u) & 0x3Fu));
        result += static_cast<char>(0x80u | (value & 0x3Fu));
    }
    return result;
}

std::vector<std::size_t> ui_text_grapheme_boundaries(std::string_view text)
{
    const auto characters = decode(text);
    std::vector<std::size_t> result { 0u };
    unsigned regionalCount = 0u;
    bool emojiSequence = false, virama = false;
    for (std::size_t index = 0u; index < characters.size(); ++index)
    {
        const auto value = characters[index].codepoint;
        const auto previous = index ? characters[index - 1u].codepoint : 0u;
        const int before = hangul(previous), after = hangul(value);
        const bool hangulJoin = (before == 1 && (after == 1 || after == 2 || after == 4 || after == 5)) ||
            ((before == 2 || before == 4) && (after == 2 || after == 3)) || ((before == 3 || before == 5) && after == 3);
        const bool newline = value == '\n' || value == '\r' || previous == '\n' || previous == '\r';
        const bool join = index && ((previous == '\r' && value == '\n') || (!newline &&
            (mark(value) || value == 0x200Du || (previous == 0x200Du && emojiSequence && pictographic(value)) ||
                (virama && word_kind(text, characters[index].start) == 1) || hangulJoin ||
                (regional(previous) && regional(value) && regionalCount % 2u == 1u))));
        if (index && !join) result.push_back(characters[index].start);
        if (!join) { emojiSequence = false; virama = false; }
        if (pictographic(value)) emojiSequence = true;
        if (hb_unicode_combining_class(hb_unicode_funcs_get_default(), value) == HB_UNICODE_COMBINING_CLASS_VIRAMA) virama = true;
        else if (!mark(value) && value != 0x200Du) virama = false;
        regionalCount = regional(value) ? regionalCount + 1u : 0u;
    }
    if (result.back() != text.size()) result.push_back(text.size());
    return result;
}

void ui_text_input_sync(TextInputComponent& input)
{
    if (input.value != input.observedValue)
    {
        input.observedValue = input.value;
        input.caretByte = input.selectionAnchorByte = input.value.size();
        input.displayStartByte = 0u;
        input.displayEndByte = input.value.size();
        input.verticalScroll = 0.0f;
        input.caretWakeRequested = true;
        input.undoHistory.clear();
        input.redoHistory.clear();
        input.lastEditKind = TextInputEditKind::eNone;
        input.graphemeBoundaries.clear();
        input.preferredCaretX = std::numeric_limits<float>::quiet_NaN();
    }
    if (input.graphemeBoundaries.empty()) input.graphemeBoundaries = ui_text_grapheme_boundaries(input.value);
    input.caretByte = boundary(input, input.caretByte);
    input.selectionAnchorByte = boundary(input, input.selectionAnchorByte);
}

std::size_t ui_text_previous_character(const TextInputComponent& input, std::size_t position)
{
    const auto found = std::lower_bound(input.graphemeBoundaries.begin(), input.graphemeBoundaries.end(), position);
    return found == input.graphemeBoundaries.begin() ? 0u : *std::prev(found);
}
std::size_t ui_text_next_character(const TextInputComponent& input, std::size_t position)
{
    const auto found = std::upper_bound(input.graphemeBoundaries.begin(), input.graphemeBoundaries.end(), position);
    return found == input.graphemeBoundaries.end() ? input.value.size() : *found;
}
UiTextRange ui_text_selection(const TextInputComponent& input)
{
    return { std::min(input.caretByte, input.selectionAnchorByte), std::max(input.caretByte, input.selectionAnchorByte) };
}
UiTextRange ui_text_word_at(const TextInputComponent& input, std::size_t position)
{
    position = boundary(input, position);
    if (position == input.value.size() && position) position = ui_text_previous_character(input, position);
    const auto kind = word_kind(input.value, position);
    UiTextRange range { position, ui_text_next_character(input, position) };
    while (range.start && word_kind(input.value, ui_text_previous_character(input, range.start)) == kind)
        range.start = ui_text_previous_character(input, range.start);
    while (range.end < input.value.size() && word_kind(input.value, range.end) == kind)
        range.end = ui_text_next_character(input, range.end);
    return range;
}
std::size_t ui_text_previous_word(const TextInputComponent& input, std::size_t position)
{
    while (position && word_kind(input.value, ui_text_previous_character(input, position)) == 0)
        position = ui_text_previous_character(input, position);
    if (!position) return 0u;
    const auto kind = word_kind(input.value, ui_text_previous_character(input, position));
    while (position && word_kind(input.value, ui_text_previous_character(input, position)) == kind)
        position = ui_text_previous_character(input, position);
    return position;
}
std::size_t ui_text_next_word(const TextInputComponent& input, std::size_t position)
{
    const auto kind = word_kind(input.value, position);
    while (position < input.value.size() && word_kind(input.value, position) == kind)
        position = ui_text_next_character(input, position);
    while (position < input.value.size() && word_kind(input.value, position) == 0)
        position = ui_text_next_character(input, position);
    return position;
}
std::size_t ui_text_line_start(const TextInputComponent& input, std::size_t position)
{
    const auto found = position ? input.value.rfind('\n', position - 1u) : std::string::npos;
    return found == std::string::npos ? 0u : found + 1u;
}
std::size_t ui_text_line_end(const TextInputComponent& input, std::size_t position)
{
    const auto found = input.value.find('\n', position);
    return found == std::string::npos ? input.value.size() : found;
}
void ui_text_move_caret(TextInputComponent& input, std::size_t position, bool extend)
{
    ui_text_input_sync(input);
    input.caretByte = boundary(input, position);
    if (!extend) input.selectionAnchorByte = input.caretByte;
    input.lastEditKind = TextInputEditKind::eNone;
    input.caretWakeRequested = true;
}
bool ui_text_replace_selection(TextInputComponent& input, std::string_view text, TextInputEditKind kind, double timeSeconds)
{
    ui_text_input_sync(input);
    auto clean = sanitise(text, input.multiline);
    if (!text.empty() && clean.empty()) return false;
    auto range = ui_text_selection(input);
    if (input.overwrite && kind == TextInputEditKind::eTyping && range.start == range.end &&
        range.end < input.value.size() && input.value[range.end] != '\n' && !clean.empty() && !mark(decode(clean).front().codepoint))
        range.end = ui_text_next_character(input, range.end);
    return replace(input, range, std::move(clean), kind, timeSeconds);
}
bool ui_text_erase(TextInputComponent& input, bool forward, bool word, bool toLineBoundary, double timeSeconds)
{
    ui_text_input_sync(input);
    auto range = ui_text_selection(input);
    if (range.start == range.end)
    {
        if (forward) range.end = toLineBoundary ? ui_text_line_end(input, range.end) :
            word ? ui_text_next_word(input, range.end) : ui_text_next_character(input, range.end);
        else range.start = toLineBoundary ? ui_text_line_start(input, range.start) :
            word ? ui_text_previous_word(input, range.start) : ui_text_previous_character(input, range.start);
    }
    return replace(input, range, {}, forward ? TextInputEditKind::eDelete : TextInputEditKind::eBackspace, timeSeconds);
}
bool ui_text_undo(TextInputComponent& input, bool redo)
{
    ui_text_input_sync(input);
    auto& source = redo ? input.redoHistory : input.undoHistory;
    auto& destination = redo ? input.undoHistory : input.redoHistory;
    if (!input.enabled || input.readOnly || source.empty()) return false;
    remember(destination, snapshot(input));
    auto restored = std::move(source.back());
    source.pop_back();
    input.value = std::move(restored.value);
    input.caretByte = restored.caret;
    input.selectionAnchorByte = restored.anchor;
    input.lastEditKind = TextInputEditKind::eNone;
    edited(input);
    return true;
}

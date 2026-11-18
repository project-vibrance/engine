// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/ui/text_editing.h>

struct UiTextInputStop { std::size_t byte = 0u; float x = 0.0f; };
struct UiTextInputLine
{
    std::size_t start = 0u, end = 0u;
    float width = 0.0f;
    bool rightToLeft = false;
    std::vector<UiTextInputStop> stops;
};
struct UiTextInputGeometry
{
    std::vector<UiTextInputLine> lines;
    float lineHeight = 0.0f, height = 0.0f, baseline = 0.0f, inkOffsetX = 0.0f;
};

inline float ui_text_input_advance(const Renderer2DFontAtlas& atlas, std::string_view value, float fontSize)
{
    if (atlas.loaded()) return atlas.layout_text(value, fontSize).advanceWidth;
    return static_cast<float>(ui_text_grapheme_boundaries(value).size() - 1u) * fontSize * 0.55f;
}

inline UiTextInputGeometry ui_text_input_geometry(const Renderer2DFontAtlas& atlas,
    std::string_view value, float fontSize, std::size_t baseByte = 0u)
{
    UiTextInputGeometry result;
    const auto layout = atlas.layout_text(value, fontSize);
    const auto reference = atlas.layout_text("Ag", fontSize);
    result.lineHeight = layout.lineHeight > 0.0f ? layout.lineHeight : fontSize * 1.25f;
    result.baseline = atlas.loaded() ? (value.empty() ? reference.baseline : layout.baseline) : fontSize * 0.8f;
    result.inkOffsetX = layout.inkOffset.x;
    for (std::size_t start = 0u;;)
    {
        const auto newline = value.find('\n', start);
        const auto end = newline == std::string_view::npos ? value.size() : newline;
        const auto lineText = value.substr(start, end - start);
        UiTextInputLine line;
        line.start = baseByte + start;
        line.end = baseByte + end;
        line.width = ui_text_input_advance(atlas, lineText, fontSize);
        line.rightToLeft = atlas.layout_text(lineText, fontSize).rightToLeft;
        for (const auto offset : ui_text_grapheme_boundaries(lineText))
        {
            const float advance = ui_text_input_advance(atlas, lineText.substr(0u, offset), fontSize);
            line.stops.push_back({ baseByte + start + offset,
                (line.rightToLeft ? line.width - advance : advance) - result.inkOffsetX });
        }
        result.lines.push_back(std::move(line));
        if (newline == std::string_view::npos) break;
        start = end + 1u;
    }
    result.height = atlas.loaded() && layout.bounds.y > 0.0f ? layout.bounds.y :
        result.lineHeight * static_cast<float>(result.lines.size());
    return result;
}

inline std::size_t ui_text_input_line_index(const UiTextInputGeometry& geometry, std::size_t byte)
{
    for (std::size_t line = 0u; line + 1u < geometry.lines.size(); ++line)
        if (byte <= geometry.lines[line].end) return line;
    return geometry.lines.size() - 1u;
}
inline float ui_text_input_stop_x(const UiTextInputLine& line, std::size_t byte)
{
    const auto next = std::upper_bound(line.stops.begin(), line.stops.end(), byte,
        [](std::size_t position, const UiTextInputStop& stop) { return position < stop.byte; });
    return next == line.stops.begin() ? line.stops.front().x : std::prev(next)->x;
}
inline std::size_t ui_text_input_nearest_stop(const UiTextInputLine& line, float x)
{
    const UiTextInputStop* nearest = &line.stops.front();
    for (const auto& stop : line.stops)
        if (std::abs(stop.x - x) < std::abs(nearest->x - x)) nearest = &stop;
    return nearest->byte;
}

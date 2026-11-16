// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <ft2build.h>
#include FT_FREETYPE_H
#include <algorithm>
#include <cmath>
#include <cstring>

namespace vibrance_text
{
struct UiFontMetrics
{
    float xHeight = 0.0f;
    float capHeight = 0.0f;

    float visible_height() const { return xHeight * 0.65f + capHeight * 0.35f; }
};

inline UiFontMetrics ui_font_metrics(FT_Face face)
{
    UiFontMetrics result;
    if (!face || !FT_IS_SCALABLE(face) || FT_HAS_COLOR(face)) return result;
    const auto height = [face](FT_ULong codepoint) {
        const auto glyph = FT_Get_Char_Index(face, codepoint);
        if (!glyph || FT_Load_Glyph(face, glyph, FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) != 0) return 0.0f;
        return float(face->glyph->metrics.height) / 64.0f;
    };
    result.xHeight = height('x');
    result.capHeight = height('H');
    return result;
}

inline bool ui_font_reference_face(FT_Face face)
{
    return face && face->family_name && std::strcmp(face->family_name, "Inter") == 0;
}

inline bool ui_font_is_latin_primary(FT_Face face)
{
    if (!face || !FT_IS_SCALABLE(face) || FT_HAS_COLOR(face) ||
        !FT_Get_Char_Index(face, 'x') || !FT_Get_Char_Index(face, 'H') || !FT_Get_Char_Index(face, 0x00E9u)) return false;
    for (const FT_ULong codepoint : { 0x4E2Du, 0x0627u, 0x0905u, 0x0E01u })
        if (FT_Get_Char_Index(face, codepoint)) return false;
    return true;
}

inline float normalise_ui_font(FT_Face primary, FT_Face reference, float pixelHeight)
{
    if (!reference || primary == reference || ui_font_reference_face(primary) ||
        !ui_font_is_latin_primary(primary) || !std::isfinite(pixelHeight) || pixelHeight <= 0.0f) return 1.0f;
    const auto original = ui_font_metrics(primary);
    const auto target = ui_font_metrics(reference);
    if (original.xHeight <= 0.0f || original.capHeight <= 0.0f || target.xHeight <= 0.0f || target.capHeight <= 0.0f)
        return 1.0f;
    const float scale = std::clamp(target.visible_height() / original.visible_height(), 0.8f, 1.25f);
    if (std::abs(scale - 1.0f) < 0.001f) return 1.0f;
    const auto height = static_cast<FT_F26Dot6>(std::lround(pixelHeight * scale * 64.0f));
    return FT_Set_Char_Size(primary, 0, height, 72u, 72u) == 0 ? scale : 1.0f;
}
}

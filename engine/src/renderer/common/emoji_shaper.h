#pragma once

#include "text_direction.h"
#include <hb.h>
#include <hb-ot.h>
#include <memory>
#include <span>
#include <string>
#include <unordered_set>
#include <unordered_map>

class Renderer2DEmojiShaper
{
public:
    struct Face
    {
        std::shared_ptr<const unsigned char> owner;
        hb_font_t* font = nullptr;
        unsigned source = 0;
        ~Face() { if (font) hb_font_destroy(font); }
    };
    std::vector<std::shared_ptr<Face>> faces;

    void add(std::span<const unsigned char> bytes, std::shared_ptr<const unsigned char> owner,
        unsigned faceIndex, unsigned source)
    {
        auto face = std::make_shared<Face>();
        face->owner = std::move(owner); face->source = source;
        auto* blob = hb_blob_create(reinterpret_cast<const char*>(bytes.data()), unsigned(bytes.size()),
            HB_MEMORY_MODE_READONLY, nullptr, nullptr);
        auto* hbFace = hb_face_create(blob, faceIndex);
        face->font = hb_font_create(hbFace);
        hb_ot_font_set_funcs(face->font);
        hb_face_destroy(hbFace); hb_blob_destroy(blob);
        faces.push_back(std::move(face));
    }

    static bool emoji_base(uint32_t cp)
    {
        return (cp >= 0x1f000 && cp <= 0x1faff) || (cp >= 0x2600 && cp <= 0x27ff) ||
            (cp >= 0x2190 && cp <= 0x23ff) || (cp >= 0x2b00 && cp <= 0x2bff) ||
            cp == 0xa9 || cp == 0xae || cp == 0x203c || cp == 0x2049 || cp == 0x2122 || cp == 0x2139 ||
            cp == 0x3030 || cp == 0x303d || cp == 0x3297 || cp == 0x3299;
    }
    static bool extension(uint32_t cp)
    { return cp == 0x200d || cp == 0xfe0f || cp == 0x20e3 || (cp >= 0xe0020 && cp <= 0xe007f); }

    static void append_utf8(std::string& output, uint32_t cp)
    {
        if (cp < 0x80) output += char(cp);
        else if (cp < 0x800) { output += char(0xc0 | (cp >> 6)); output += char(0x80 | (cp & 63)); }
        else if (cp < 0x10000)
        { output += char(0xe0 | (cp >> 12)); output += char(0x80 | ((cp >> 6) & 63)); output += char(0x80 | (cp & 63)); }
        else
        { output += char(0xf0 | (cp >> 18)); output += char(0x80 | ((cp >> 12) & 63)); output += char(0x80 | ((cp >> 6) & 63)); output += char(0x80 | (cp & 63)); }
    }

    std::vector<uint32_t> visual(std::string_view text) const
    {
        const auto input = renderer2d_decode_utf8(text);
        std::unordered_set<uint32_t> used(input.begin(), input.end());
        std::unordered_map<uint32_t, uint32_t> placeholders;
        std::string substituted;
        uint32_t placeholder = 0xf0000;
        for (std::size_t i = 0; i < input.size();)
        {
            const auto cp = input[i];
            const bool keycap = (cp == '#' || cp == '*' || (cp >= '0' && cp <= '9')) &&
                i + 1 < input.size() && (input[i + 1] == 0x20e3 ||
                    (input[i + 1] == 0xfe0f && i + 2 < input.size() && input[i + 2] == 0x20e3));
            const bool textPresentation = i + 1 < input.size() && input[i + 1] == 0xfe0e;
            std::shared_ptr<Face> selected;
            if ((emoji_base(cp) || keycap) && !textPresentation)
                for (const auto& face : faces)
                {
                    hb_codepoint_t glyph = 0;
                    if (hb_font_get_nominal_glyph(face->font, cp, &glyph)) { selected = face; break; }
                }
            if (!selected)
            {
                append_utf8(substituted, cp); ++i;
                // Presentation selectors do not occupy their own glyph cell.
                if (textPresentation) ++i;
                continue;
            }
            std::size_t end = i + 1;
            while (end < input.size() && (emoji_base(input[end]) || extension(input[end])))
            {
                if (end + 1 < input.size() && input[end + 1] == 0xfe0e) break;
                hb_codepoint_t glyph = 0;
                if (!extension(input[end]) && !hb_font_get_nominal_glyph(selected->font, input[end], &glyph)) break;
                ++end;
            }
            auto* buffer = hb_buffer_create();
            hb_buffer_add_codepoints(buffer, input.data() + i, int(end - i), 0, int(end - i));
            hb_buffer_set_direction(buffer, HB_DIRECTION_LTR);
            hb_buffer_set_script(buffer, HB_SCRIPT_COMMON);
            hb_buffer_set_flags(buffer, HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
            hb_shape(selected->font, buffer, nullptr, 0);
            unsigned count = 0;
            const auto* glyphs = hb_buffer_get_glyph_infos(buffer, &count);
            for (unsigned g = 0; g < count; ++g)
            {
                if (!glyphs[g].codepoint) continue;
                while (used.contains(placeholder)) ++placeholder;
                // Private-use placeholders keep each shaped glyph atomic when
                // the existing Arabic shaping and bidi pass reorders the run.
                const auto key = 0x110000u + (selected->source << 16) + glyphs[g].codepoint;
                placeholders.emplace(placeholder, key);
                append_utf8(substituted, placeholder++);
            }
            hb_buffer_destroy(buffer); i = end;
        }
        auto output = renderer2d_visual_codepoints(substituted);
        for (auto& cp : output)
            if (auto found = placeholders.find(cp); found != placeholders.end()) cp = found->second;
        return output;
    }
};

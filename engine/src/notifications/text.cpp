#include <vibranceUI/notifications/card.h>
#include "../renderer/common/text_direction.h"

#include <charconv>
#include <optional>

namespace
{
    bool space(std::uint32_t scalar)
    {
        return (scalar >= 0x09u && scalar <= 0x0du) || scalar == 0x20u ||
            scalar == 0x85u || scalar == 0xa0u || scalar == 0x1680u ||
            (scalar >= 0x2000u && scalar <= 0x200au) ||
            scalar == 0x2028u || scalar == 0x2029u || scalar == 0x202fu ||
            scalar == 0x205fu || scalar == 0x3000u;
    }

    bool invisible(std::uint32_t scalar)
    {
        return scalar == 0xadu || scalar == 0x34fu || scalar == 0x61cu ||
            (scalar >= 0x200bu && scalar <= 0x200fu) ||
            (scalar >= 0x202au && scalar <= 0x202eu) || scalar == 0x2060u ||
            (scalar >= 0x2066u && scalar <= 0x2069u) || scalar == 0xfeffu;
    }

    void append_utf8(std::string& text, std::uint32_t scalar)
    {
        if (scalar <= 0x7fu) text.push_back(static_cast<char>(scalar));
        else if (scalar <= 0x7ffu)
        {
            text.push_back(static_cast<char>(0xc0u | (scalar >> 6u)));
            text.push_back(static_cast<char>(0x80u | (scalar & 0x3fu)));
        }
        else if (scalar <= 0xffffu)
        {
            text.push_back(static_cast<char>(0xe0u | (scalar >> 12u)));
            text.push_back(static_cast<char>(0x80u | ((scalar >> 6u) & 0x3fu)));
            text.push_back(static_cast<char>(0x80u | (scalar & 0x3fu)));
        }
        else
        {
            text.push_back(static_cast<char>(0xf0u | (scalar >> 18u)));
            text.push_back(static_cast<char>(0x80u | ((scalar >> 12u) & 0x3fu)));
            text.push_back(static_cast<char>(0x80u | ((scalar >> 6u) & 0x3fu)));
            text.push_back(static_cast<char>(0x80u | (scalar & 0x3fu)));
        }
    }

    std::optional<std::uint32_t> padding_entity(std::string_view entity)
    {
        if (entity == "nbsp") return 0xa0u;
        if (entity == "ensp") return 0x2002u;
        if (entity == "emsp") return 0x2003u;
        if (entity == "thinsp") return 0x2009u;
        if (entity == "hairsp") return 0x200au;
        if (entity == "zwnj") return 0x200cu;
        if (entity == "zwj") return 0x200du;
        if (entity == "ZeroWidthSpace") return 0x200bu;
        if (entity == "NoBreak") return 0x2060u;
        if (entity == "shy") return 0xadu;
        if (entity == "Tab") return 0x09u;
        if (entity == "NewLine") return 0x0au;
        if (entity.empty() || entity.front() != '#') return {};
        entity.remove_prefix(1u);
        int base = 10;
        if (!entity.empty() && (entity.front() == 'x' || entity.front() == 'X'))
        {
            base = 16;
            entity.remove_prefix(1u);
        }
        std::uint32_t scalar = 0u;
        const auto result = std::from_chars(entity.data(), entity.data() + entity.size(), scalar, base);
        if (result.ec != std::errc {} || result.ptr != entity.data() + entity.size()) return {};
        return space(scalar) || invisible(scalar) ? std::optional(scalar) : std::nullopt;
    }
}

std::string ui_normalise_notification_text(std::string_view value)
{
    // Providers already supply plain text. Decode only padding references left
    // by HTML email, leaving markup and other literal entities untouched.
    std::string decoded;
    for (std::size_t offset = 0u; offset < value.size();)
    {
        if (value[offset] == '&')
        {
            const auto entity = value.substr(offset + 1u, 24u);
            const auto end = entity.find(';');
            if (end != std::string_view::npos)
            {
                if (const auto scalar = padding_entity(entity.substr(0u, end)))
                {
                    append_utf8(decoded, *scalar);
                    offset += end + 2u;
                    continue;
                }
            }
        }
        decoded.push_back(value[offset++]);
    }

    const auto scalars = renderer2d_decode_utf8(decoded);
    std::string text;
    text.reserve(decoded.size());
    for (std::size_t index = 0u; index < scalars.size();)
    {
        if (!space(scalars[index]) && !invisible(scalars[index]))
        {
            append_utf8(text, scalars[index++]);
            continue;
        }
        const auto start = index;
        bool hasSpace = false;
        bool hasNewline = false;
        while (index < scalars.size() && (space(scalars[index]) || invisible(scalars[index])))
        {
            const auto scalar = scalars[index++];
            hasSpace = hasSpace || space(scalar);
            hasNewline = hasNewline || scalar == '\n' || scalar == '\r' ||
                scalar == 0x85u || scalar == 0x2028u || scalar == 0x2029u;
        }
        if (text.empty() || index == scalars.size()) continue;
        if (hasSpace) text.push_back(hasNewline ? '\n' : ' ');
        else
        {
            // Intra-word joiners and emoji sequences remain intact. Isolated
            // controls mixed with spaces belong to padding, not visible text.
            for (auto current = start; current < index; ++current) append_utf8(text, scalars[current]);
        }
    }
    return text;
}

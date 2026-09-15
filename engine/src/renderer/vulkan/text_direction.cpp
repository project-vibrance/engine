#include "text_direction.h"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace
{
    enum class TextDirection
    {
        eNeutral,
        eLeftToRight,
        eRightToLeft
    };

    struct ArabicJoiningForms
    {
        std::uint32_t base;
        std::uint32_t isolated;
        std::uint32_t final;
        std::uint32_t initial;
        std::uint32_t medial;
        bool joinsPrevious;
        bool joinsNext;
    };

    constexpr ArabicJoiningForms kArabicJoiningForms[] = {
        { 0x0621u, 0xFE80u, 0u, 0u, 0u, false, false },
        { 0x0622u, 0xFE81u, 0xFE82u, 0u, 0u, true, false },
        { 0x0623u, 0xFE83u, 0xFE84u, 0u, 0u, true, false },
        { 0x0624u, 0xFE85u, 0xFE86u, 0u, 0u, true, false },
        { 0x0625u, 0xFE87u, 0xFE88u, 0u, 0u, true, false },
        { 0x0626u, 0xFE89u, 0xFE8Au, 0xFE8Bu, 0xFE8Cu, true, true },
        { 0x0627u, 0xFE8Du, 0xFE8Eu, 0u, 0u, true, false },
        { 0x0628u, 0xFE8Fu, 0xFE90u, 0xFE91u, 0xFE92u, true, true },
        { 0x0629u, 0xFE93u, 0xFE94u, 0u, 0u, true, false },
        { 0x062Au, 0xFE95u, 0xFE96u, 0xFE97u, 0xFE98u, true, true },
        { 0x062Bu, 0xFE99u, 0xFE9Au, 0xFE9Bu, 0xFE9Cu, true, true },
        { 0x062Cu, 0xFE9Du, 0xFE9Eu, 0xFE9Fu, 0xFEA0u, true, true },
        { 0x062Du, 0xFEA1u, 0xFEA2u, 0xFEA3u, 0xFEA4u, true, true },
        { 0x062Eu, 0xFEA5u, 0xFEA6u, 0xFEA7u, 0xFEA8u, true, true },
        { 0x062Fu, 0xFEA9u, 0xFEAAu, 0u, 0u, true, false },
        { 0x0630u, 0xFEABu, 0xFEACu, 0u, 0u, true, false },
        { 0x0631u, 0xFEADu, 0xFEAEu, 0u, 0u, true, false },
        { 0x0632u, 0xFEAFu, 0xFEB0u, 0u, 0u, true, false },
        { 0x0633u, 0xFEB1u, 0xFEB2u, 0xFEB3u, 0xFEB4u, true, true },
        { 0x0634u, 0xFEB5u, 0xFEB6u, 0xFEB7u, 0xFEB8u, true, true },
        { 0x0635u, 0xFEB9u, 0xFEBAu, 0xFEBBu, 0xFEBCu, true, true },
        { 0x0636u, 0xFEBDu, 0xFEBEu, 0xFEBFu, 0xFEC0u, true, true },
        { 0x0637u, 0xFEC1u, 0xFEC2u, 0xFEC3u, 0xFEC4u, true, true },
        { 0x0638u, 0xFEC5u, 0xFEC6u, 0xFEC7u, 0xFEC8u, true, true },
        { 0x0639u, 0xFEC9u, 0xFECAu, 0xFECBu, 0xFECCu, true, true },
        { 0x063Au, 0xFECDu, 0xFECEu, 0xFECFu, 0xFED0u, true, true },
        { 0x0640u, 0x0640u, 0x0640u, 0x0640u, 0x0640u, true, true },
        { 0x0641u, 0xFED1u, 0xFED2u, 0xFED3u, 0xFED4u, true, true },
        { 0x0642u, 0xFED5u, 0xFED6u, 0xFED7u, 0xFED8u, true, true },
        { 0x0643u, 0xFED9u, 0xFEDAu, 0xFEDBu, 0xFEDCu, true, true },
        { 0x0644u, 0xFEDDu, 0xFEDEu, 0xFEDFu, 0xFEE0u, true, true },
        { 0x0645u, 0xFEE1u, 0xFEE2u, 0xFEE3u, 0xFEE4u, true, true },
        { 0x0646u, 0xFEE5u, 0xFEE6u, 0xFEE7u, 0xFEE8u, true, true },
        { 0x0647u, 0xFEE9u, 0xFEEAu, 0xFEEBu, 0xFEECu, true, true },
        { 0x0648u, 0xFEEDu, 0xFEEEu, 0u, 0u, true, false },
        { 0x0649u, 0xFEEFu, 0xFEF0u, 0u, 0u, true, false },
        { 0x064Au, 0xFEF1u, 0xFEF2u, 0xFEF3u, 0xFEF4u, true, true },
        { 0x0671u, 0xFB50u, 0xFB51u, 0u, 0u, true, false },
        { 0x067Eu, 0xFB56u, 0xFB57u, 0xFB58u, 0xFB59u, true, true },
        { 0x0686u, 0xFB7Au, 0xFB7Bu, 0xFB7Cu, 0xFB7Du, true, true },
        { 0x0698u, 0xFB8Au, 0xFB8Bu, 0u, 0u, true, false },
        { 0x06A9u, 0xFB8Eu, 0xFB8Fu, 0xFB90u, 0xFB91u, true, true },
        { 0x06AFu, 0xFB92u, 0xFB93u, 0xFB94u, 0xFB95u, true, true },
        { 0x06BAu, 0xFB9Eu, 0xFB9Fu, 0u, 0u, true, false },
        { 0x06BEu, 0xFBAAu, 0xFBABu, 0xFBACu, 0xFBADu, true, true },
        { 0x06C1u, 0xFBA6u, 0xFBA7u, 0xFBA8u, 0xFBA9u, true, true },
        { 0x06CCu, 0xFBFCu, 0xFBFDu, 0xFBFEu, 0xFBFFu, true, true },
        { 0x06D2u, 0xFBAEu, 0xFBAFu, 0u, 0u, true, false }
    };

    const ArabicJoiningForms* arabic_joining_forms(std::uint32_t codepoint)
    {
        const auto found = std::find_if(
            std::begin(kArabicJoiningForms),
            std::end(kArabicJoiningForms),
            [codepoint](const ArabicJoiningForms& forms) {
                return forms.base == codepoint;
            });
        return found == std::end(kArabicJoiningForms) ? nullptr : &*found;
    }

    bool arabic_transparent_mark(std::uint32_t codepoint)
    {
        return (codepoint >= 0x0610u && codepoint <= 0x061Au) ||
            (codepoint >= 0x064Bu && codepoint <= 0x065Fu) ||
            codepoint == 0x0670u ||
            (codepoint >= 0x06D6u && codepoint <= 0x06EDu);
    }

    bool bidi_control(std::uint32_t codepoint)
    {
        return codepoint == 0x200Eu || codepoint == 0x200Fu ||
            (codepoint >= 0x202Au && codepoint <= 0x202Eu) ||
            (codepoint >= 0x2066u && codepoint <= 0x2069u);
    }

    TextDirection codepoint_direction(std::uint32_t codepoint)
    {
        if ((codepoint >= 0x0030u && codepoint <= 0x0039u) ||
            (codepoint >= 0x0660u && codepoint <= 0x0669u) ||
            (codepoint >= 0x06F0u && codepoint <= 0x06F9u))
        {
            return TextDirection::eLeftToRight;
        }
        if ((codepoint >= 0x0590u && codepoint <= 0x08FFu) ||
            (codepoint >= 0xFB1Du && codepoint <= 0xFDFFu) ||
            (codepoint >= 0xFE70u && codepoint <= 0xFEFFu))
        {
            return arabic_transparent_mark(codepoint) ?
                TextDirection::eNeutral : TextDirection::eRightToLeft;
        }
        const bool leftToRightScript =
            (codepoint >= 0x00C0u && codepoint <= 0x02AFu) ||
            (codepoint >= 0x0370u && codepoint <= 0x058Fu) ||
            (codepoint >= 0x0900u && codepoint <= 0x1FFFu) ||
            (codepoint >= 0x2C00u && codepoint <= 0xA4CFu) ||
            (codepoint >= 0xAC00u && codepoint <= 0xD7AFu) ||
            (codepoint >= 0xF900u && codepoint <= 0xFAFFu) ||
            (codepoint >= 0xFF21u && codepoint <= 0xFF3Au) ||
            (codepoint >= 0xFF41u && codepoint <= 0xFF5Au);
        if ((codepoint >= 'A' && codepoint <= 'Z') ||
            (codepoint >= 'a' && codepoint <= 'z') ||
            leftToRightScript)
        {
            return TextDirection::eLeftToRight;
        }
        return TextDirection::eNeutral;
    }

    std::uint32_t mirrored_rtl_codepoint(std::uint32_t codepoint)
    {
        switch (codepoint)
        {
        case '(':
            return ')';
        case ')':
            return '(';
        case '[':
            return ']';
        case ']':
            return '[';
        case '{':
            return '}';
        case '}':
            return '{';
        case '<':
            return '>';
        case '>':
            return '<';
        default:
            return codepoint;
        }
    }

    std::vector<std::uint32_t> shape_arabic_codepoints(
        const std::vector<std::uint32_t>& logical)
    {
        std::vector<std::uint32_t> shaped = logical;
        const auto previousLetter = [&logical](std::size_t index) {
            while (index > 0u)
            {
                --index;
                if (!arabic_transparent_mark(logical[index]))
                {
                    return index;
                }
            }
            return logical.size();
        };
        const auto nextLetter = [&logical](std::size_t index) {
            for (++index; index < logical.size(); ++index)
            {
                if (!arabic_transparent_mark(logical[index]))
                {
                    return index;
                }
            }
            return logical.size();
        };

        for (std::size_t index = 0u; index < logical.size(); ++index)
        {
            const ArabicJoiningForms* forms =
                arabic_joining_forms(logical[index]);
            if (!forms)
            {
                continue;
            }
            const std::size_t previousIndex = previousLetter(index);
            const std::size_t nextIndex = nextLetter(index);
            const ArabicJoiningForms* previous =
                previousIndex < logical.size() ?
                    arabic_joining_forms(logical[previousIndex]) : nullptr;
            const ArabicJoiningForms* next = nextIndex < logical.size() ?
                arabic_joining_forms(logical[nextIndex]) : nullptr;
            const bool connectsPrevious = previous &&
                previous->joinsNext && forms->joinsPrevious;

            if (logical[index] == 0x0644u &&
                nextIndex == index + 1u && next)
            {
                std::uint32_t isolatedLigature = 0u;
                std::uint32_t finalLigature = 0u;
                switch (logical[nextIndex])
                {
                case 0x0622u:
                    isolatedLigature = 0xFEF5u;
                    finalLigature = 0xFEF6u;
                    break;
                case 0x0623u:
                    isolatedLigature = 0xFEF7u;
                    finalLigature = 0xFEF8u;
                    break;
                case 0x0625u:
                    isolatedLigature = 0xFEF9u;
                    finalLigature = 0xFEFAu;
                    break;
                case 0x0627u:
                    isolatedLigature = 0xFEFBu;
                    finalLigature = 0xFEFCu;
                    break;
                default:
                    break;
                }
                if (isolatedLigature != 0u)
                {
                    shaped[index] = connectsPrevious ?
                        finalLigature : isolatedLigature;
                    shaped[nextIndex] = 0u;
                    ++index;
                    continue;
                }
            }

            const bool connectsNext = next &&
                forms->joinsNext && next->joinsPrevious;
            if (connectsPrevious && connectsNext && forms->medial != 0u)
            {
                shaped[index] = forms->medial;
            }
            else if (connectsPrevious && forms->final != 0u)
            {
                shaped[index] = forms->final;
            }
            else if (connectsNext && forms->initial != 0u)
            {
                shaped[index] = forms->initial;
            }
            else
            {
                shaped[index] = forms->isolated;
            }
        }
        shaped.erase(
            std::remove(shaped.begin(), shaped.end(), 0u),
            shaped.end());
        return shaped;
    }

    TextDirection base_direction(
        const std::vector<std::uint32_t>& codepoints)
    {
        for (const std::uint32_t codepoint : codepoints)
        {
            const TextDirection direction = codepoint_direction(codepoint);
            if (direction != TextDirection::eNeutral)
            {
                return direction;
            }
        }
        return TextDirection::eLeftToRight;
    }

    std::vector<std::uint32_t> visual_line(
        const std::vector<std::uint32_t>& logical)
    {
        std::vector<std::uint32_t> codepoints =
            shape_arabic_codepoints(logical);
        if (codepoints.empty())
        {
            return {};
        }

        std::vector<TextDirection> directions;
        directions.reserve(codepoints.size());
        const TextDirection base = base_direction(codepoints);
        for (const std::uint32_t codepoint : codepoints)
        {
            directions.push_back(codepoint_direction(codepoint));
        }

        for (std::size_t index = 0u; index < directions.size(); ++index)
        {
            if (directions[index] != TextDirection::eNeutral)
            {
                continue;
            }
            TextDirection before = base;
            for (std::size_t scan = index; scan > 0u;)
            {
                --scan;
                if (directions[scan] != TextDirection::eNeutral)
                {
                    before = directions[scan];
                    break;
                }
            }
            TextDirection after = base;
            for (std::size_t scan = index + 1u;
                scan < directions.size(); ++scan)
            {
                if (directions[scan] != TextDirection::eNeutral)
                {
                    after = directions[scan];
                    break;
                }
            }
            directions[index] = before == after ? before : base;
        }

        const auto reverseRuns = [&codepoints, &directions](
            TextDirection direction) {
            for (std::size_t start = 0u; start < directions.size();)
            {
                if (directions[start] != direction)
                {
                    ++start;
                    continue;
                }
                std::size_t end = start + 1u;
                while (end < directions.size() &&
                    directions[end] == direction)
                {
                    ++end;
                }
                std::reverse(
                    codepoints.begin() + static_cast<std::ptrdiff_t>(start),
                    codepoints.begin() + static_cast<std::ptrdiff_t>(end));
                std::reverse(
                    directions.begin() + static_cast<std::ptrdiff_t>(start),
                    directions.begin() + static_cast<std::ptrdiff_t>(end));
                start = end;
            }
        };

        if (base == TextDirection::eRightToLeft)
        {
            reverseRuns(TextDirection::eLeftToRight);
            std::reverse(codepoints.begin(), codepoints.end());
            std::reverse(directions.begin(), directions.end());
        }
        else
        {
            reverseRuns(TextDirection::eRightToLeft);
        }

        for (std::size_t index = 0u; index < codepoints.size(); ++index)
        {
            if (directions[index] == TextDirection::eRightToLeft)
            {
                codepoints[index] = mirrored_rtl_codepoint(codepoints[index]);
            }
        }
        return codepoints;
    }
}

std::vector<std::uint32_t> renderer2d_decode_utf8(std::string_view text)
{
    std::vector<std::uint32_t> out;
    out.reserve(text.size());
    std::size_t offset = 0u;
    while (offset < text.size())
    {
        const unsigned char first = static_cast<unsigned char>(text[offset++]);
        if (first < 0x80u)
        {
            out.push_back(first);
            continue;
        }

        std::uint32_t codepoint = 0u;
        int continuationCount = 0;
        if ((first & 0xE0u) == 0xC0u)
        {
            codepoint = first & 0x1Fu;
            continuationCount = 1;
        }
        else if ((first & 0xF0u) == 0xE0u)
        {
            codepoint = first & 0x0Fu;
            continuationCount = 2;
        }
        else if ((first & 0xF8u) == 0xF0u)
        {
            codepoint = first & 0x07u;
            continuationCount = 3;
        }
        else
        {
            out.push_back('?');
            continue;
        }

        bool valid = true;
        for (int index = 0; index < continuationCount; ++index)
        {
            if (offset >= text.size())
            {
                valid = false;
                break;
            }
            const unsigned char next =
                static_cast<unsigned char>(text[offset]);
            if ((next & 0xC0u) != 0x80u)
            {
                valid = false;
                break;
            }
            ++offset;
            codepoint = (codepoint << 6u) | (next & 0x3Fu);
        }
        out.push_back(valid ? codepoint : static_cast<std::uint32_t>('?'));
    }
    return out;
}

std::vector<std::uint32_t> renderer2d_visual_codepoints(std::string_view text)
{
    const std::vector<std::uint32_t> decoded = renderer2d_decode_utf8(text);
    std::vector<std::uint32_t> visual;
    visual.reserve(decoded.size());
    std::vector<std::uint32_t> logicalLine;
    logicalLine.reserve(decoded.size());

    const auto appendLine = [&visual, &logicalLine]() {
        std::vector<std::uint32_t> filtered;
        filtered.reserve(logicalLine.size());
        for (const std::uint32_t codepoint : logicalLine)
        {
            if (!bidi_control(codepoint))
            {
                filtered.push_back(codepoint);
            }
        }
        std::vector<std::uint32_t> line = visual_line(filtered);
        visual.insert(visual.end(), line.begin(), line.end());
        logicalLine.clear();
    };

    for (const std::uint32_t codepoint : decoded)
    {
        if (codepoint == '\n')
        {
            appendLine();
            visual.push_back('\n');
        }
        else
        {
            logicalLine.push_back(codepoint);
        }
    }
    appendLine();
    return visual;
}

bool renderer2d_text_is_right_to_left(std::string_view text)
{
    for (const std::uint32_t codepoint : renderer2d_decode_utf8(text))
    {
        const TextDirection direction = codepoint_direction(codepoint);
        if (direction != TextDirection::eNeutral)
        {
            return direction == TextDirection::eRightToLeft;
        }
    }
    return false;
}

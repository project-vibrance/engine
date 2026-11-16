// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iterator>
#include <optional>
#include <span>
#include <utility>
#include <vector>

namespace vibrance_text
{
class FontPositioning
{
    struct Bytes
    {
        std::span<const unsigned char> data;
        bool contains(std::size_t offset, std::size_t length) const
        { return offset <= data.size() && length <= data.size() - offset; }
        std::uint16_t u16(std::size_t offset) const
        { return contains(offset, 2u) ? (std::uint16_t(data[offset]) << 8u) | data[offset + 1u] : 0u; }
        std::int16_t i16(std::size_t offset) const
        { return static_cast<std::int16_t>(u16(offset)); }
        std::uint32_t u32(std::size_t offset) const
        { return contains(offset, 4u) ? (std::uint32_t(u16(offset)) << 16u) | u16(offset + 2u) : 0u; }
        std::int32_t i32(std::size_t offset) const
        { return static_cast<std::int32_t>(u32(offset)); }
    };

    struct PairTable
    {
        std::size_t offset = 0u;
        std::uint16_t format = 0u;
    };

    Bytes positioning;
    std::vector<std::vector<PairTable>> pairLookups;
    std::vector<std::pair<float, float>> trackingPoints;

    static constexpr std::uint32_t tag(char a, char b, char c, char d)
    {
        return (std::uint32_t(a) << 24u) | (std::uint32_t(b) << 16u) |
            (std::uint32_t(c) << 8u) | std::uint32_t(d);
    }

    static Bytes table(Bytes font, std::size_t directory, std::uint32_t wanted)
    {
        if (!font.contains(directory, 12u)) return {};
        const auto count = font.u16(directory + 4u);
        if (count > 4096u || !font.contains(directory + 12u, std::size_t(count) * 16u)) return {};
        for (std::size_t index = 0u; index < count; ++index)
        {
            const auto record = directory + 12u + index * 16u;
            if (font.u32(record) != wanted) continue;
            const auto offset = font.u32(record + 8u), length = font.u32(record + 12u);
            return font.contains(offset, length) ? Bytes { font.data.subspan(offset, length) } : Bytes {};
        }
        return {};
    }

    bool valid_coverage(std::size_t offset) const
    {
        if (!positioning.contains(offset, 4u)) return false;
        const auto format = positioning.u16(offset), count = positioning.u16(offset + 2u);
        return (format == 1u || format == 2u) &&
            positioning.contains(offset + 4u, std::size_t(count) * (format == 1u ? 2u : 6u));
    }

    std::optional<std::uint16_t> coverage(std::size_t offset, std::uint16_t glyph) const
    {
        const auto format = positioning.u16(offset), count = positioning.u16(offset + 2u);
        std::size_t low = 0u, high = count;
        while (low < high)
        {
            const auto middle = low + (high - low) / 2u;
            const auto record = offset + 4u + middle * (format == 1u ? 2u : 6u);
            const auto start = positioning.u16(record);
            const auto end = format == 1u ? start : positioning.u16(record + 2u);
            if (glyph < start) high = middle;
            else if (glyph > end) low = middle + 1u;
            else return format == 1u ? static_cast<std::uint16_t>(middle) :
                static_cast<std::uint16_t>(positioning.u16(record + 4u) + glyph - start);
        }
        return {};
    }

    bool valid_classes(std::size_t offset) const
    {
        if (!positioning.contains(offset, 4u)) return false;
        if (positioning.u16(offset) == 1u)
            return positioning.contains(offset, 6u) &&
                positioning.contains(offset + 6u, std::size_t(positioning.u16(offset + 4u)) * 2u);
        return positioning.u16(offset) == 2u &&
            positioning.contains(offset + 4u, std::size_t(positioning.u16(offset + 2u)) * 6u);
    }

    std::uint16_t glyph_class(std::size_t offset, std::uint16_t glyph) const
    {
        if (positioning.u16(offset) == 1u)
        {
            const auto first = positioning.u16(offset + 2u), count = positioning.u16(offset + 4u);
            return glyph >= first && std::uint32_t(glyph) - first < count ?
                positioning.u16(offset + 6u + std::size_t(glyph - first) * 2u) : 0u;
        }
        std::size_t low = 0u, high = positioning.u16(offset + 2u);
        while (low < high)
        {
            const auto middle = low + (high - low) / 2u;
            const auto record = offset + 4u + middle * 6u;
            if (glyph < positioning.u16(record)) high = middle;
            else if (glyph > positioning.u16(record + 2u)) low = middle + 1u;
            else return positioning.u16(record + 4u);
        }
        return 0u;
    }

    std::optional<PairTable> pair_table(std::size_t offset) const
    {
        if (!positioning.contains(offset, 10u) || positioning.u16(offset + 4u) != 4u ||
            positioning.u16(offset + 6u) != 0u || !valid_coverage(offset + positioning.u16(offset + 2u))) return {};
        const auto format = positioning.u16(offset);
        if (format == 1u)
        {
            const auto count = positioning.u16(offset + 8u);
            if (!positioning.contains(offset + 10u, std::size_t(count) * 2u)) return {};
            for (std::size_t index = 0u; index < count; ++index)
            {
                const auto pairSet = offset + positioning.u16(offset + 10u + index * 2u);
                if (!positioning.contains(pairSet, 2u) ||
                    !positioning.contains(pairSet + 2u, std::size_t(positioning.u16(pairSet)) * 4u)) return {};
            }
        }
        else if (format == 2u)
        {
            if (!positioning.contains(offset, 16u) ||
                !valid_classes(offset + positioning.u16(offset + 8u)) ||
                !valid_classes(offset + positioning.u16(offset + 10u)) ||
                !positioning.contains(offset + 16u, std::size_t(positioning.u16(offset + 12u)) *
                    positioning.u16(offset + 14u) * 2u)) return {};
        }
        else return {};
        return PairTable { offset, format };
    }

    std::optional<int> pair_value(const PairTable& pair, std::uint16_t left, std::uint16_t right) const
    {
        const auto offset = pair.offset;
        const auto covered = coverage(offset + positioning.u16(offset + 2u), left);
        if (!covered) return {};
        if (pair.format == 1u)
        {
            if (*covered >= positioning.u16(offset + 8u)) return {};
            const auto pairSet = offset + positioning.u16(offset + 10u + std::size_t(*covered) * 2u);
            std::size_t low = 0u, high = positioning.u16(pairSet);
            while (low < high)
            {
                const auto middle = low + (high - low) / 2u;
                const auto record = pairSet + 2u + middle * 4u;
                const auto glyph = positioning.u16(record);
                if (right < glyph) high = middle;
                else if (right > glyph) low = middle + 1u;
                else return positioning.i16(record + 2u);
            }
            return {};
        }
        const auto class1 = glyph_class(offset + positioning.u16(offset + 8u), left);
        const auto class2 = glyph_class(offset + positioning.u16(offset + 10u), right);
        const auto count1 = positioning.u16(offset + 12u), count2 = positioning.u16(offset + 14u);
        if (class1 >= count1 || class2 >= count2) return {};
        return positioning.i16(offset + 16u + (std::size_t(class1) * count2 + class2) * 2u);
    }

    void read_pairs()
    {
        if (!positioning.contains(0u, 10u) || positioning.u16(0u) != 1u) return;
        const auto features = positioning.u16(6u), lookups = positioning.u16(8u);
        if (!positioning.contains(features, 2u) || !positioning.contains(lookups, 2u)) return;
        const auto featureCount = positioning.u16(features), lookupCount = positioning.u16(lookups);
        if (featureCount > 4096u || lookupCount > 4096u ||
            !positioning.contains(features + 2u, std::size_t(featureCount) * 6u) ||
            !positioning.contains(lookups + 2u, std::size_t(lookupCount) * 2u)) return;
        std::vector<std::uint16_t> indices;
        for (std::size_t index = 0u; index < featureCount; ++index)
        {
            const auto record = features + 2u + index * 6u;
            if (positioning.u32(record) != tag('k', 'e', 'r', 'n')) continue;
            const auto feature = features + positioning.u16(record + 4u);
            if (!positioning.contains(feature, 4u)) continue;
            const auto count = positioning.u16(feature + 2u);
            if (!positioning.contains(feature + 4u, std::size_t(count) * 2u)) continue;
            for (std::size_t item = 0u; item < count; ++item)
            {
                const auto lookup = positioning.u16(feature + 4u + item * 2u);
                if (lookup < lookupCount && std::find(indices.begin(), indices.end(), lookup) == indices.end())
                    indices.push_back(lookup);
            }
        }
        for (const auto index : indices)
        {
            const auto lookup = lookups + positioning.u16(lookups + 2u + std::size_t(index) * 2u);
            if (!positioning.contains(lookup, 6u)) continue;
            const auto type = positioning.u16(lookup), flags = positioning.u16(lookup + 2u);
            const auto count = positioning.u16(lookup + 4u);
            if ((type != 2u && type != 9u) || (flags & ~8u) != 0u || count > 4096u ||
                !positioning.contains(lookup + 6u, std::size_t(count) * 2u)) continue;
            std::vector<PairTable> subtables;
            for (std::size_t item = 0u; item < count; ++item)
            {
                auto offset = lookup + positioning.u16(lookup + 6u + item * 2u);
                if (type == 9u)
                {
                    if (!positioning.contains(offset, 8u) || positioning.u16(offset) != 1u ||
                        positioning.u16(offset + 2u) != 2u) continue;
                    offset += positioning.u32(offset + 4u);
                }
                if (const auto parsed = pair_table(offset)) subtables.push_back(*parsed);
            }
            if (!subtables.empty()) pairLookups.push_back(std::move(subtables));
        }
    }

    void read_tracking(Bytes tracking)
    {
        if (!tracking.contains(0u, 12u) || tracking.u32(0u) != 0x00010000u || tracking.u16(4u) != 0u) return;
        const auto horizontal = tracking.u16(6u);
        if (!horizontal || !tracking.contains(horizontal, 8u)) return;
        const auto tracks = tracking.u16(horizontal), sizes = tracking.u16(horizontal + 2u);
        const auto sizeOffset = tracking.u32(horizontal + 4u);
        if (tracks > 1024u || sizes < 1u || sizes > 1024u ||
            !tracking.contains(horizontal + 8u, std::size_t(tracks) * 8u) ||
            !tracking.contains(sizeOffset, std::size_t(sizes) * 4u)) return;
        for (std::size_t index = 0u; index < tracks; ++index)
        {
            const auto track = horizontal + 8u + index * 8u;
            if (tracking.i32(track) != 0) continue;
            const auto values = tracking.u16(track + 6u);
            if (!tracking.contains(values, std::size_t(sizes) * 2u)) return;
            for (std::size_t size = 0u; size < sizes; ++size)
            {
                const float points = float(tracking.i32(sizeOffset + size * 4u)) / 65536.0f;
                if (points <= 0.0f || (!trackingPoints.empty() && points <= trackingPoints.back().first))
                { trackingPoints.clear(); return; }
                trackingPoints.emplace_back(points, float(tracking.i16(values + size * 2u)));
            }
            return;
        }
    }

public:
    FontPositioning(std::span<const unsigned char> bytes, std::uint32_t faceIndex = 0u)
    {
        const Bytes font { bytes };
        std::size_t directory = 0u;
        if (font.u32(0u) == tag('t', 't', 'c', 'f'))
        {
            if (!font.contains(0u, 12u) || faceIndex >= font.u32(8u) ||
                !font.contains(12u + std::size_t(faceIndex) * 4u, 4u)) return;
            directory = font.u32(12u + std::size_t(faceIndex) * 4u);
        }
        const auto signature = font.u32(directory);
        if (signature != 0x00010000u && signature != tag('O', 'T', 'T', 'O') &&
            signature != tag('t', 'r', 'u', 'e')) return;
        positioning = table(font, directory, tag('G', 'P', 'O', 'S'));
        read_pairs();
        read_tracking(table(font, directory, tag('t', 'r', 'a', 'k')));
    }

    int pair_adjustment(std::uint16_t left, std::uint16_t right) const
    {
        int result = 0;
        for (const auto& lookup : pairLookups)
            for (const auto& subtable : lookup)
                if (const auto value = pair_value(subtable, left, right)) { result += *value; break; }
        return result;
    }

    bool has_pair_adjustments() const { return !pairLookups.empty(); }
    const std::vector<std::pair<float, float>>& tracking_points() const { return trackingPoints; }

    template<typename Points>
    static float tracking_value(const Points& points, float size)
    {
        if (points.empty() || !std::isfinite(size) || size <= 0.0f) return 0.0f;
        if (size <= points.front()[0]) return points.front()[1];
        if (size >= points.back()[0]) return points.back()[1];
        const auto next = std::upper_bound(points.begin(), points.end(), size,
            [](float value, const auto& point) { return value < point[0]; });
        const auto previous = std::prev(next);
        const float fraction = (size - (*previous)[0]) / ((*next)[0] - (*previous)[0]);
        return (*previous)[1] + ((*next)[1] - (*previous)[1]) * fraction;
    }
};
}

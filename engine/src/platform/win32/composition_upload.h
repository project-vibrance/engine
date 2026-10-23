#pragma once

#include "composition_bridge_abi.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

#if defined(_M_ARM64) && defined(_MSC_VER) && !defined(__clang__)
#include <arm64_neon.h>
#elif defined(__aarch64__)
#include <arm_neon.h>
#elif defined(_M_X64) || defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace vibrance::win32
{
struct CompositionUploadRect
{
    std::uint32_t x = 0u, y = 0u, width = 0u, height = 0u;
    bool empty() const noexcept { return width == 0u || height == 0u; }
    bool operator==(const CompositionUploadRect&) const = default;
};

// Each rotating D3D source texture retains its own image. A small update must
// include all changes since that particular texture was last uploaded.
class CompositionUploadDamage
{
public:
    CompositionUploadRect prepare(std::uint32_t index,
        std::uint32_t sourceWidth, std::uint32_t sourceHeight,
        std::uint32_t width, std::uint32_t height,
        CompositionUploadRect sourceDamage) noexcept
    {
        if (index >= pending_.size() || !sourceWidth || !sourceHeight ||
            !width || !height) return {};
        if (sourceWidth_ != sourceWidth || sourceHeight_ != sourceHeight ||
            width_ != width || height_ != height)
        {
            sourceWidth_ = sourceWidth;
            sourceHeight_ = sourceHeight;
            width_ = width;
            height_ = height;
            pending_.fill({0u, 0u, width, height});
        }
        CompositionUploadRect damage {0u, 0u, width, height};
        if (!sourceDamage.empty())
        {
            const auto sourceRight = std::min<std::uint64_t>(sourceWidth,
                static_cast<std::uint64_t>(sourceDamage.x) + sourceDamage.width);
            const auto sourceBottom = std::min<std::uint64_t>(sourceHeight,
                static_cast<std::uint64_t>(sourceDamage.y) + sourceDamage.height);
            const auto left = std::min(sourceDamage.x, sourceWidth);
            const auto top = std::min(sourceDamage.y, sourceHeight);
            const auto scale_down = [](std::uint64_t value,
                std::uint32_t destination, std::uint32_t source) {
                return static_cast<std::uint32_t>(value * destination / source);
            };
            const auto scale_up = [](std::uint64_t value,
                std::uint32_t destination, std::uint32_t source) {
                return static_cast<std::uint32_t>(
                    (value * destination + source - 1u) / source);
            };
            damage.x = scale_down(left, width, sourceWidth);
            damage.y = scale_down(top, height, sourceHeight);
            damage.width = scale_up(sourceRight, width, sourceWidth) - damage.x;
            damage.height = scale_up(sourceBottom, height, sourceHeight) - damage.y;
        }
        for (auto& pending : pending_) pending = unite(pending, damage);
        return pending_[index];
    }

    void commit(std::uint32_t index) noexcept
    {
        if (index < pending_.size()) pending_[index] = {};
    }

private:
    static CompositionUploadRect unite(CompositionUploadRect a,
        CompositionUploadRect b) noexcept
    {
        if (a.empty()) return b;
        if (b.empty()) return a;
        const auto right = std::max(a.x + a.width, b.x + b.width);
        const auto bottom = std::max(a.y + a.height, b.y + b.height);
        const auto x = std::min(a.x, b.x), y = std::min(a.y, b.y);
        return {x, y, right - x, bottom - y};
    }

    std::uint32_t sourceWidth_ = 0u, sourceHeight_ = 0u;
    std::uint32_t width_ = 0u, height_ = 0u;
    std::array<CompositionUploadRect, VIBRANCE_COMPOSITION_MAX_BUFFERS> pending_ {};
};

inline void composition_upload_bgra_row(std::uint8_t* destination,
    const std::uint8_t* source, std::size_t pixels) noexcept
{
    std::size_t pixel = 0u;
#if defined(_M_ARM64) || defined(__aarch64__)
    const auto keep = vdupq_n_u32(0xff00ff00u);
    const auto channel = vdupq_n_u32(0x000000ffu);
    for (; pixel + 4u <= pixels; pixel += 4u)
    {
        const auto rgba = vreinterpretq_u32_u8(vld1q_u8(source + pixel * 4u));
        const auto bgra = vorrq_u32(vandq_u32(rgba, keep),
            vorrq_u32(vandq_u32(vshrq_n_u32(rgba, 16), channel),
                vshlq_n_u32(vandq_u32(rgba, channel), 16)));
        vst1q_u8(destination + pixel * 4u, vreinterpretq_u8_u32(bgra));
    }
#elif defined(_M_X64) || defined(__SSE2__)
    const auto keep = _mm_set1_epi32(static_cast<int>(0xff00ff00u));
    const auto channel = _mm_set1_epi32(0x000000ffu);
    for (; pixel + 4u <= pixels; pixel += 4u)
    {
        const auto rgba = _mm_loadu_si128(
            reinterpret_cast<const __m128i*>(source + pixel * 4u));
        const auto bgra = _mm_or_si128(_mm_and_si128(rgba, keep),
            _mm_or_si128(_mm_and_si128(_mm_srli_epi32(rgba, 16), channel),
                _mm_slli_epi32(_mm_and_si128(rgba, channel), 16)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(destination + pixel * 4u), bgra);
    }
#endif
    for (; pixel < pixels; ++pixel)
    {
        std::uint32_t rgba;
        std::memcpy(&rgba, source + pixel * 4u, sizeof(rgba));
        const auto bgra = (rgba & 0xff00ff00u) |
            ((rgba >> 16u) & 0xffu) | ((rgba & 0xffu) << 16u);
        std::memcpy(destination + pixel * 4u, &bgra, sizeof(bgra));
    }
}

class CompositionUploadPixels
{
public:
    void convert(std::uint8_t* destination, std::size_t destinationRowPitch,
        std::uint32_t width, std::uint32_t height,
        const std::uint8_t* source, std::size_t sourceRowPitch,
        std::uint32_t sourceWidth, std::uint32_t sourceHeight,
        CompositionUploadRect rect)
    {
        if (rect.empty()) return;
        const bool sameColumns = sourceWidth == width;
        if (!sameColumns && (columnsWidth_ != width || columnsSourceWidth_ != sourceWidth))
        {
            columns_.resize(width);
            for (std::uint32_t x = 0u; x < width; ++x)
                columns_[x] = static_cast<std::uint32_t>(
                    static_cast<std::uint64_t>(x) * sourceWidth / width);
            columnsWidth_ = width;
            columnsSourceWidth_ = sourceWidth;
        }
        std::uint32_t previousSourceY = UINT32_MAX;
        for (std::uint32_t y = rect.y; y < rect.y + rect.height; ++y)
        {
            const auto sourceY = static_cast<std::uint32_t>(
                static_cast<std::uint64_t>(y) * sourceHeight / height);
            auto* destinationRow = destination +
                static_cast<std::size_t>(y) * destinationRowPitch + rect.x * 4u;
            if (sourceY == previousSourceY)
            {
                std::memcpy(destinationRow, destinationRow - destinationRowPitch,
                    static_cast<std::size_t>(rect.width) * 4u);
                continue;
            }
            const auto* sourceRow = source +
                static_cast<std::size_t>(sourceY) * sourceRowPitch;
            if (sameColumns)
            {
                composition_upload_bgra_row(destinationRow,
                    sourceRow + rect.x * 4u, rect.width);
            }
            else
            {
                for (std::uint32_t x = 0u; x < rect.width; ++x)
                    composition_upload_bgra_row(destinationRow + x * 4u,
                        sourceRow + columns_[rect.x + x] * 4u, 1u);
            }
            previousSourceY = sourceY;
        }
    }

private:
    std::uint32_t columnsWidth_ = 0u, columnsSourceWidth_ = 0u;
    std::vector<std::uint32_t> columns_;
};
}

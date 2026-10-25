#pragma once

#include "composition_bridge_abi.h"
#include <algorithm>
#include <array>
#include <cstdint>

namespace vibrance::win32
{
struct CompositionDamageRect
{
    std::int32_t left = 0, top = 0, right = 0, bottom = 0;
    bool empty() const noexcept { return right <= left || bottom <= top; }
    bool operator==(const CompositionDamageRect&) const = default;
    std::uint64_t pixels() const noexcept
    {
        return empty() ? 0u : static_cast<std::uint64_t>(right - left) * (bottom - top);
    }
};

inline CompositionDamageRect composition_damage_union(CompositionDamageRect a,
    CompositionDamageRect b) noexcept
{
    if (a.empty()) return b;
    if (b.empty()) return a;
    return {std::min(a.left, b.left), std::min(a.top, b.top),
        std::max(a.right, b.right), std::max(a.bottom, b.bottom)};
}

struct CompositionPresentDamage
{
    CompositionDamageRect copy, present;
};

// A rotating UI texture needs changes missed since its last accepted present.
// Native optical sampling also needs its complete DXGI invalidation halo, but
// that halo does not change pixels in the app-owned sharp UI source texture.
class CompositionPresentDamageHistory
{
public:
    CompositionPresentDamage prepare(std::uint32_t index, std::uint32_t width,
        std::uint32_t height, CompositionDamageRect ui, CompositionDamageRect optical) noexcept
    {
        if (index >= copy_.size() || !width || !height) return {};
        if (width_ != width || height_ != height)
        {
            width_ = width;
            height_ = height;
            const CompositionDamageRect full {0, 0, static_cast<std::int32_t>(width),
                static_cast<std::int32_t>(height)};
            copy_.fill(full);
            present_.fill(full);
        }
        const auto invalidation = composition_damage_union(ui, optical);
        for (auto& pending : copy_) pending = composition_damage_union(pending, ui);
        for (auto& pending : present_) pending = composition_damage_union(pending, invalidation);
        return {copy_[index], present_[index]};
    }

    void commit(std::uint32_t index) noexcept
    {
        if (index < copy_.size()) { copy_[index] = {}; present_[index] = {}; }
    }
    void reset() noexcept { *this = {}; }

private:
    std::uint32_t width_ = 0u, height_ = 0u;
    std::array<CompositionDamageRect, VIBRANCE_COMPOSITION_MAX_BUFFERS> copy_ {}, present_ {};
};
}

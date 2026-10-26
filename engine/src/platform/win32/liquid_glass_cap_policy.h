#pragma once

namespace vibrance::win32
{
// The optical shader replaces the material with cap paint before reading its
// backdrop when both gradient endpoints clamp to one. Surface opacity still
// applies to that paint; it does not expose the unused optical material.
inline bool liquid_glass_cap_fully_opaque(bool paintCapGradient,
    float firstAlpha, float secondAlpha) noexcept
{
    return paintCapGradient && firstAlpha >= 1.0f && secondAlpha >= 1.0f;
}
}

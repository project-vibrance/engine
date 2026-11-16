// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cmath>

namespace vibrance::glass
{
inline float blur_radius(float amount)
{
    return std::sqrt(std::clamp(amount, 0.0f, 1.0f)) * 30.0f;
}

inline float edge_thickness(float width, float height)
{
    return std::clamp(std::min(width, height) * 0.22f, 3.0f, 40.0f);
}

inline float sampling_radius(float diffusionRadius, float width, float height)
{
    return std::max(diffusionRadius, std::min(std::min(width, height) * 0.5f, 28.5f));
}
}

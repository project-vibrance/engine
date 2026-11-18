// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <algorithm>
#include <cmath>
#include "liquid_glass_refraction.h"

namespace vibrance::glass
{
inline float blur_radius(float amount)
{
    return std::sqrt(std::clamp(amount, 0.0f, 1.0f)) * 30.0f;
}

inline float edge_thickness(float width, float height, float radius = -1.0f)
{
    return glass_edge_thickness(width, height, radius);
}

inline float sampling_radius(float diffusionRadius, float width, float height)
{
    return std::max(diffusionRadius, std::min(std::min(width, height) * 0.5f, 28.5f));
}
}

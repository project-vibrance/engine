// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#ifndef VIBRANCE_LIQUID_GLASS_REFRACTION_H
#define VIBRANCE_LIQUID_GLASS_REFRACTION_H

#if defined(__cplusplus)
#include <algorithm>
#include <cmath>
namespace vibrance::glass {
#define GLASS_INLINE inline
#define GLASS_OUT(type) type&
#define GLASS_MIN std::min
#define GLASS_MAX std::max
#define GLASS_SQRT std::sqrt
#else
#define GLASS_INLINE
#define GLASS_OUT(type) out type
#define GLASS_MIN min
#define GLASS_MAX max
#define GLASS_SQRT sqrt
#endif

GLASS_INLINE float glass_clamp(float value, float low, float high)
{
    return GLASS_MIN(GLASS_MAX(value, low), high);
}

GLASS_INLINE float glass_body_blend(float width, float height)
{
    float t = glass_clamp((GLASS_MIN(width, height) - 48.0f) / 92.0f, 0.0f, 1.0f);
    return t * t * (3.0f - 2.0f * t);
}

GLASS_INLINE float glass_optical_diameter(float width, float height, float radius)
{
    float shortSide = GLASS_MIN(width, height);
    float diameter = radius < 0.0f ? shortSide : 2.0f * radius;
    return glass_clamp(diameter, shortSide * 0.20f, shortSide);
}

GLASS_INLINE float glass_edge_thickness(float width, float height, float radius)
{
    float body = glass_body_blend(width, height);
    float shortSide = GLASS_MIN(width, height);
    float diameter = glass_optical_diameter(width, height, radius);
    float opticalSize = shortSide + (diameter - shortSide) * body;
    return GLASS_MAX(opticalSize * (0.22f - 0.10f * body), 3.0f);
}

GLASS_INLINE float glass_refraction_strength(float width, float height, float amount, float radius)
{
    amount = glass_clamp(amount, 0.0f, 1.0f);
    float shortSide = GLASS_MIN(width, height);
    float size = glass_clamp(width / 500.0f, 0.35f, 2.0f);
    float thin = GLASS_MIN(amount * 56.0f * size, shortSide * 0.40f);
    float remaining = 1.0f - amount;
    float body = glass_optical_diameter(width, height, radius) * 0.40f *
        (1.0f - remaining * remaining * remaining);
    return thin + (body - thin) * glass_body_blend(width, height);
}

GLASS_INLINE float glass_dispersion_strength(float amount)
{
    return glass_clamp(amount, 0.0f, 1.0f) * 0.65f;
}

GLASS_INLINE float glass_lens_band(float distance, float edge, float width, float height, float normalX)
{
    float bevelWidth = edge * (1.0f + 3.0f * glass_body_blend(width, height) * normalX * normalX);
    return glass_clamp(1.0f + distance / GLASS_MAX(bevelWidth, 1.0f), 0.0f, 1.0f);
}

GLASS_INLINE void glass_lens_offset(float distance, float edge, float strength,
    float normalX, float normalY, float localX, float localY, float width, float height,
    GLASS_OUT(float) offsetX, GLASS_OUT(float) offsetY)
{
    float band = glass_lens_band(distance, edge, width, height, normalX);
    offsetX = offsetY = 0.0f;
    if (band <= 0.0f || strength <= 0.0f) return;

    float curve = band * GLASS_SQRT(band) /
        (1.0f + 0.8f * GLASS_SQRT(GLASS_MAX(1.0f - band, 0.0001f)));
    float thinX = -normalX * curve;
    float thinY = -normalY * curve;
    float body = glass_body_blend(width, height);
    if (body <= 0.0f)
    {
        offsetX = thinX * strength;
        offsetY = thinY * strength;
        return;
    }

    float u = glass_clamp(localX * 2.0f / GLASS_MAX(width, 1.0f) - 1.0f, -1.0f, 1.0f);
    float v = glass_clamp(localY * 2.0f / GLASS_MAX(height, 1.0f) - 1.0f, -1.0f, 1.0f);
    float shoulder = band * (2.0f - band);
    float bevel = band;
    float nx = normalX * bevel + u * 0.24f * shoulder;
    float ny = normalY * bevel + v * 0.24f * shoulder;
    float nz = GLASS_SQRT(GLASS_MAX(1.0f - bevel * bevel, 0.0001f));
    float inverseNormal = 1.0f / GLASS_SQRT(nx * nx + ny * ny + nz * nz);
    nx *= inverseNormal;
    ny *= inverseNormal;
    nz *= inverseNormal;

    float index = 1.45f;
    float eta = 1.0f / index;
    float transmission = eta * nz - GLASS_SQRT(GLASS_MAX(
        1.0f - eta * eta * (1.0f - nz * nz), 0.0001f));
    float rayX = transmission * nx * index;
    float rayY = transmission * ny * index;
    float rayLength = GLASS_SQRT(rayX * rayX + rayY * rayY);
    float exitCosine = GLASS_SQRT(GLASS_MAX(1.0f - rayLength * rayLength, 0.0144f));
    float projection = rayLength / exitCosine;
    float foldCurve = band * band / (0.20f + 0.80f * band);
    float travel = foldCurve * (0.85f + 0.15f * GLASS_MIN(1.07f * projection / (0.55f + projection), 1.0f));
    float thickness = 1.0f - 0.14f * u * u - 0.04f * v * v;
    float rayScale = travel * thickness / GLASS_MAX(rayLength, 0.0001f);
    offsetX = (thinX + (rayX * rayScale - thinX) * body) * strength;
    offsetY = (thinY + (rayY * rayScale - thinY) * body) * strength;
}

GLASS_INLINE float glass_lens_footprint(float distance, float edge, float strength,
    float normalX, float normalY, float localX, float localY, float width, float height)
{
    float forwardX, forwardY, backwardX, backwardY;
    glass_lens_offset(distance + 0.5f, edge, strength, normalX, normalY,
        localX + normalX * 0.5f, localY + normalY * 0.5f, width, height, forwardX, forwardY);
    glass_lens_offset(distance - 0.5f, edge, strength, normalX, normalY,
        localX - normalX * 0.5f, localY - normalY * 0.5f, width, height, backwardX, backwardY);
    float dx = normalX + forwardX - backwardX;
    float dy = normalY + forwardY - backwardY;
    return glass_clamp(GLASS_SQRT(dx * dx + dy * dy), 1.0f, 4.0f);
}

#if defined(__cplusplus)
}
#endif
#undef GLASS_INLINE
#undef GLASS_OUT
#undef GLASS_MIN
#undef GLASS_MAX
#undef GLASS_SQRT
#endif

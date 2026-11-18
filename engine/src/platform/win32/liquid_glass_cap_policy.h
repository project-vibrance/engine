// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

namespace vibrance::win32
{



inline bool liquid_glass_cap_fully_opaque(bool paintCapGradient,
    float firstAlpha, float secondAlpha) noexcept
{
    return paintCapGradient && firstAlpha >= 1.0f && secondAlpha >= 1.0f;
}
}

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/graphics/liquid_glass_profile.h>
#include <array>
#include <iostream>

int main()
{
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    const auto sample = [](float x, float y, float width, float height, float distance,
        float nx, float ny, float strength) {
        std::array<float, 2> result;
        vibrance::glass::glass_lens_offset(distance, vibrance::glass::edge_thickness(width, height),
            strength, nx, ny, x, y, width, height, result[0], result[1]);
        return result;
    };
    for (const float size : { 12.0f, 24.0f, 48.0f, 80.0f, 160.0f, 300.0f, 640.0f })
    for (const float aspect : { 1.0f, 2.0f, 4.0f })
    for (const float strength : { 0.0f, 8.0f, 56.0f, 112.0f, 176.0f })
    for (unsigned step = 0; step <= 100; ++step)
    {
        const float edge = vibrance::glass::edge_thickness(size * aspect, size);
        const float distance = step == 100u ? -edge * 4.0f : -edge * 4.0f * step / 100.0f;
        const auto offset = sample(size * aspect * 0.9f, size + distance,
            size * aspect, size, distance, 0.6f, 0.8f, strength);
        expect(std::isfinite(offset[0]) && std::isfinite(offset[1]) &&
            std::hypot(offset[0], offset[1]) <= strength + 0.001f,
            "Every refracted ray must remain finite and inside its reserved source halo");
        if (step == 100 || strength == 0.0f)
            expect(offset[0] == 0.0f && offset[1] == 0.0f,
                "The inner face and zero-refraction endpoint must retain identity sampling");
    }
    const auto left = sample(160, 275, 640, 300, -25, 0, 1, 56);
    const auto right = sample(480, 275, 640, 300, -25, 0, 1, 56);
    const auto middle = sample(320, 275, 640, 300, -25, 0, 1, 56);
    expect(left[0] > 0.5f && right[0] < -0.5f && std::abs(left[0] + right[0]) < 0.001f &&
        std::abs(left[1] - right[1]) < 0.001f && std::abs(left[1] - middle[1]) > 0.1f,
        "A long lens must curve and magnify across its face instead of translating a uniform edge strip");
    const auto top = sample(160, 25, 640, 300, -25, 0, -1, 56);
    expect(std::abs(top[0] - left[0]) < 0.001f && std::abs(top[1] + left[1]) < 0.001f,
        "Top and bottom optical surfaces must preserve reflection symmetry");
    const auto thumb = sample(8, 22, 64, 24, -2, 0, 1, 12);
    expect(thumb[0] == 0.0f && thumb[1] < 0.0f &&
        vibrance::glass::edge_thickness(64, 24) == 24.0f * 0.22f,
        "Small control thumbs must retain their existing thin clear collar");
    const float referenceStrength = vibrance::glass::glass_refraction_strength(1222, 296, 0.7f, 148);
    const auto mainText = sample(611, 236, 1222, 296, -60, 0, 1, referenceStrength);
    const auto foldStart = sample(611, 281, 1222, 296, -15, 0, 1, referenceStrength);
    const auto foldEnd = sample(611, 295, 1222, 296, -1, 0, 1, referenceStrength);
    expect(mainText[0] == 0.0f && mainText[1] == 0.0f &&
        295.0f + foldEnd[1] < 281.0f + foldStart[1] - 28.0f,
        "The main image must stay readable while the rim inverts and compresses its reflected copy");
    for (const float height : { 160.0f, 258.0f, 296.0f, 512.0f })
    for (const float radiusFraction : { 0.25f, 0.32f, 0.5f })
    {
        const float radius = height * radiusFraction;
        float previous = 0.0f;
        for (unsigned step = 0; step <= 100; ++step)
        {
            const float amount = step / 100.0f;
            const float compact = vibrance::glass::glass_refraction_strength(height * 1.8f, height, amount, radius);
            const float wide = vibrance::glass::glass_refraction_strength(height * 4.2f, height, amount, radius);
            expect(std::isfinite(compact) && compact >= previous && compact <= height * 0.4f + 0.001f &&
                std::abs(compact - wide) < 0.001f,
                "Large-surface optics must follow corner curvature independently of panel width");
            previous = compact;
        }
        const float edge = vibrance::glass::edge_thickness(height * 1.8f, height, radius);
        const float doubled = vibrance::glass::edge_thickness(height * 3.6f, height * 2.0f, radius * 2.0f);
        expect(std::abs(doubled - edge * 2.0f) < 0.001f,
            "The bevel must follow the authored corner radius at each display scale");
    }
    const float islandEdge = vibrance::glass::edge_thickness(470, 258, 82);
    const float capsuleEdge = vibrance::glass::edge_thickness(1222, 296, 148);
    const float islandStrength = vibrance::glass::glass_refraction_strength(470, 258, 0.7f, 82);
    expect(std::abs(islandStrength / islandEdge - referenceStrength / capsuleEdge) < 0.001f,
        "The island and reference capsule must have the same normalised refraction curve");
    expect(vibrance::glass::glass_lens_band(-islandEdge * 2.0f, islandEdge, 470, 258, 1.0f) > 0.0f &&
        vibrance::glass::glass_lens_band(-islandEdge * 2.0f, islandEdge, 470, 258, 0.0f) == 0.0f,
        "The rounded ends must wrap through a broader bevel without broadening the flat bottom reflection");
    std::array<float, 2> islandUpper, islandLower;
    vibrance::glass::glass_lens_offset(-6.5f, islandEdge, islandStrength, 0, 1, 235, 251.5f,
        470, 258, islandUpper[0], islandUpper[1]);
    vibrance::glass::glass_lens_offset(-1.5f, islandEdge, islandStrength, 0, 1, 235, 256.5f,
        470, 258, islandLower[0], islandLower[1]);
    expect(256.5f + islandLower[1] < 251.5f + islandUpper[1] - 10.0f,
        "The actual island must compress the inverted image instead of stretching it vertically");
    return passed ? 0 : 1;
}

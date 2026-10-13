#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>

struct ShapePushConstants
{
    std::array<float, 4> rect, uvRect, colour0, colour1, colour2, effect0, effect1;
    std::array<std::uint32_t, 4> data;
};
static_assert(sizeof(ShapePushConstants) == 128u);

struct ShapeFixture
{
    std::string name;
    ShapePushConstants constants;
};

inline std::vector<ShapeFixture> shape_fixtures()
{
    // Dispatch 640 x 512 and initialise the RGBA8 destination with the same
    // varied coloured/translucent pattern before each baseline/variant draw.
    const ShapePushConstants base {
        { 14.35f, 18.65f, 600.1f, 480.7f }, { 0.0f, 0.0f, 1.0f, 1.0f },
        { 0.17f, 0.38f, 0.91f, 0.43f }, { 0.8f, 0.13f, 0.07f, 0.83f },
        { 0.8f, 0.6f, 0.3f, 0.7f }, { 17.0f, 0.8f, 0.7f, 0.371f },
        { 1.0f, 4.0f, 0.0f, 0.0f }, { 2u, 4u, 0u, 0u }
    };
    std::vector<ShapeFixture> fixtures;
    const auto append = [&](const char* name, const ShapePushConstants& constants) {
        fixtures.push_back({ name, constants });
    };
    auto value = base;
    append("rounded_translucent", value);
    value.colour0 = { 0.12f, 0.4f, 0.87f, 1.0f };
    value.effect0[3] = 1.0f;
    append("rounded_opaque", value);
    value = base;
    value.data[0] = 1u;
    append("rectangle_translucent", value);
    value.data[0] = 6u;
    append("squircle_translucent", value);
    value = base;
    value.effect0 = { 80.0f, 12.3f, 7.4f, 0.127f };
    append("wide_outline_softness", value);
    value = base;
    value.data[1] |= 1u << 15u;
    value.data[2] = 16u | (2u << 8u);
    value.effect1 = { 40.3f, 30.6f, 560.2f, 430.1f };
    append("inherited_rounded_mask", value);
    value.data[2] = 1u << 8u;
    append("inherited_rectangular_mask", value);
    value.data[2] = 16u | (6u << 8u) | (15u << 12u) | (10u << 16u);
    append("inherited_squircle_mask", value);
    value.data[2] = 16u | (3u << 8u);
    append("fallback_ellipse_mask", value);
    value.data[2] = 16u | (2u << 8u);
    value.data[1] |= 1u << 29u;
    append("fallback_cutout", value);
    value = base;
    value.data[1] |= 1u;
    append("fallback_linear_gradient", value);
    value.data[1] |= 1u << 11u;
    append("fallback_radial_gradient", value);
    value = base;
    value.data[1] |= 1u << 12u;
    value.effect1 = { 0.08f, 0.1f, -0.05f, 0.3f };
    append("fallback_transformed", value);
    value = base;
    value.data[1] |= 1u << 5u;
    value.effect0[2] = 5.0f;
    append("fallback_foreground_blur", value);
    value = base;
    value.data[1] |= 1u << 16u;
    value.data[2] = 16u | (4u << 8u) | (24u << 16u);
    append("fallback_custom_corners", value);
    value = base;
    value.data[0] = 3u;
    append("fallback_ellipse", value);
    value.data[0] = 7u;
    value.effect1 = { 1.0f, 4.0f, 0.94f, 64.0f };
    append("fallback_notched_squircle", value);
    value = base;
    value.data[0] = 8u;
    value.uvRect = { 0.65f, 12.0f, -1.2f, 1.0f };
    append("fallback_circular_progress", value);
    // These channel values lie on RGBA8 half-step boundaries, which expose
    // differences introduced by removing premultiply/normalise arithmetic.
    for (unsigned index = 0u; index < 16u; ++index)
    {
        value = base;
        value.colour0 = { (index * 13u + 0.5f) / 255.0f,
            (index * 11u + 0.5f) / 255.0f, (index * 7u + 0.5f) / 255.0f,
            (index + 1.0f) / 17.0f };
        value.effect0[3] = (index + 1.0f) / 19.0f;
        fixtures.push_back({ "quantisation_boundary_" + std::to_string(index), value });
    }
    return fixtures;
}

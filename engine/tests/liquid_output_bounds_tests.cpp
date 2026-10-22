#include "../src/renderer/common/scene_helpers.h"

#include <iostream>

int main()
{
    struct Surface
    {
        struct { uint32_t width = 1024u, height = 768u; } extent;
    } surface;
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    const auto contains = [](const DispatchBounds& bounds, float x, float y) {
        return !bounds.empty() && x >= bounds.x && y >= bounds.y &&
            x < bounds.x + bounds.width && y < bounds.y + bounds.height;
    };
    Renderer2DBatch lens {};
    lens.rect = { 40.0f, 24.0f, 120.0f, 36.0f };
    lens.clipRect = { 0.0f, 0.0f, 1024.0f, 768.0f };
    lens.flags = eRenderer2DStyleLiquidGlass;
    lens.effect0.x = 4.5f;
    lens.effect1.y = 0.5f;
    const auto halo = make_blur_dispatch_bounds(surface, lens);
    const auto filter = make_blur_dispatch_bounds(surface, lens, true, false);
    expect(filter.width < halo.width && filter.height < halo.height,
        "The extended reflection halo must not enlarge the Gaussian filtering work");
    const auto output = make_liquid_output_dispatch_bounds(surface, lens);
    expect(output.width < halo.width && output.height < halo.height,
        "Liquid output must avoid clearing and compositing the unused sampling halo");
    expect(contains(output, 39.5f, 23.5f) && contains(output, 160.5f, 60.5f),
        "Liquid output must retain antialiased pixels outside every lens edge");
    lens.effect1.y = 5.0f;
    const auto feathered = make_liquid_output_dispatch_bounds(surface, lens);
    expect(contains(feathered, 35.5f, 19.5f) && contains(feathered, 164.5f, 64.5f),
        "Authored soft edges must remain inside optical output coverage");
    lens.flags |= eRenderer2DStyleBlurInheritedShapeMask;
    expect(make_liquid_output_dispatch_bounds(surface, lens).width == output.width,
        "Inherited masks must use their shader's fixed antialiasing rather than mask coordinates");
    lens.clipRect = { 64.0f, 32.0f, 80.0f, 32.0f };
    const auto cropped = make_liquid_output_dispatch_bounds(surface, lens);
    expect(cropped.x == 64u && cropped.y == 32u && cropped.width == 80u,
        "Optical output must respect a cropped viewport without shrinking source samples");
    lens.clipRect = { 500.0f, 500.0f, 40.0f, 40.0f };
    expect(make_liquid_output_dispatch_bounds(surface, lens).empty(),
        "Fully clipped lenses must dispatch no optical output");
    lens.clipRect = { 0.0f, 0.0f, 1024.0f, 768.0f };
    lens.flags = eRenderer2DStyleLiquidGlass;
    lens.primitive = Renderer2DPrimitive::eNotchedSquircle;
    lens.rect = { 300.0f, 100.0f, 200.0f, 92.0f };
    lens.uvRect = { 1.0f, 4.0f, 1.0f, 92.0f };
    lens.effect1.y = 0.5f;
    const float flare = notched_squircle_flare_size({ 200.0f, 92.0f }, 1.0f, 92.0f);
    const auto notched = make_liquid_output_dispatch_bounds(surface, lens);
    expect(contains(notched, 300.0f - flare, 100.0f) &&
        contains(notched, 500.0f + flare, 100.0f),
        "Notched flares must retain their full optical output bounds");
    return passed ? 0 : 1;
}

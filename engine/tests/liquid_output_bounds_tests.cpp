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
    Renderer2DBatch header {};
    header.rect = { 16.0f, 16.0f, 200.0f, 70.0f };
    header.clipRect = { 16.0f, 16.0f, 200.0f, 112.0f };
    header.flags = eRenderer2DStyleBlurDynamicContentOnly;
    header.effect0.x = 18.0f;
    const auto headerOutput = make_content_blur_output_dispatch_bounds(surface, header, false);
    const auto headerSource = make_blur_replay_dispatch_bounds(surface, header);
    expect(headerOutput.x == 16u && headerOutput.y == 16u &&
        headerOutput.width == 200u && headerOutput.height == 70u,
        "Content-only blur must end at the authored header boundary without a sampling fringe");
    expect(headerSource.x < headerOutput.x && headerSource.y < headerOutput.y &&
        contains(headerSource, 225.0f, 100.0f),
        "Content-only blur must populate its source halo beyond the inherited output clip");
    header.flags = 0u;
    const auto ordinaryHeader = make_blur_replay_dispatch_bounds(surface, header);
    expect(ordinaryHeader.x == 16u && ordinaryHeader.y == 16u,
        "Existing ordinary blur source policies must retain their clipping");
    header.flags = eRenderer2DStyleBlurClampToClip;
    header.rect = { 16.25f, 16.25f, 200.0f, 70.05f };
    header.clipRect = { 16.25f, 16.25f, 300.0f, 300.0f };
    header.effect0.x = 12.4f;
    const auto fractionalSource = make_blur_source_dispatch_bounds(surface, header, false, false);
    expect(contains(fractionalSource, 30.0f, 99.0f),
        "Fractional header/radius filtering must populate the final bilinear source texel");
    entt::registry registry;
    const auto column = registry.create();
    auto& columnTransform = registry.emplace<Transform2DComponent>(column);
    columnTransform.position = { 100.25f, 16.5f };
    registry.emplace<ShapeComponent>(column).size = { 200.5f, 140.0f };
    auto& columnMask = registry.emplace<Mask2DComponent>(column);
    columnMask.useContentRect = false;
    const auto child = registry.create();
    registry.emplace<Parent2DComponent>(child, column);
    const auto normalClip = inherited_mask_clip_rect(registry, child);
    expect(normalClip == glm::vec4(100.25f, 16.5f, 200.5f, 140.0f),
        "Existing masks must retain their fractional clipping policy by default");
    columnMask.insetToWholePixels = true;
    const auto alignedClip = inherited_mask_clip_rect(registry, child);
    expect(alignedClip == glm::vec4(101.0f, 17.0f, 199.0f, 139.0f),
        "Opted-in descendants must preserve the column's partially covered boundary pixels");
    return passed ? 0 : 1;
}

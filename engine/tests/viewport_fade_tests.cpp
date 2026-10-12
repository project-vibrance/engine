#include <vibranceUI/ui/builder.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/surfaces.h>
#include <cmath>
#include <iostream>

int main()
{
    int failures = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    };
    for (const float dpi : { 1.0f, 1.5f, 2.0f })
    {
        Renderer2DScene scene;
        UiBuilder ui(scene, make_fixed_layout_scale(
            static_cast<int>(100 * dpi), static_cast<int>(244 * dpi), { dpi, dpi }));
        auto& registry = scene.registry();
        const auto host = scene.create_shape({}, { 100 * dpi, 244 * dpi });
        UiScrollViewOptions options;
        options.viewportMargin.y = 44;
        options.viewportHeight = 200;
        options.contentHeight = 400;
        const auto scroll = ui_create_scroll_view(ui, host, options);
        ScrollEdgeFade2DComponent fade;
        fade.topHeight = 16 * dpi;
        fade.bottomHeight = 20 * dpi;
        fade.fadeWindow = true;
        registry.emplace<ScrollEdgeFade2DComponent>(scroll.content, fade);
        const auto card = scene.create_shape({}, { 100 * dpi, 72 * dpi });
        ui.attach_aligned(card, scroll.content, UiAlignment::eTopLeft,
            {}, { 100 * dpi, 72 * dpi });
        ui.set_dynamic_cache(card);

        double now = 0;
        for (const float row : { 0.0f, -50.0f, -64.0f, -71.0f, 130.0f, 170.0f, 190.0f, 199.0f })
        {
            registry.get<Layout2DComponent>(card).offset.y = row * dpi;
            scene.mark_dirty(card);
            const auto plan = scene.build_render_plan(now += 0.1, 1u);
            const auto& mask = plan.viewportFade;
            expect(mask.enabled && std::abs(mask.rect.y - 44 * dpi) < 0.001f &&
                std::abs(mask.rect.w - 200 * dpi) < 0.001f,
                "The final gradient must remain fixed below the top bar at every DPI and scroll position");
            expect(renderer2d_viewport_fade_opacity(mask, 43 * dpi) == 0 &&
                renderer2d_viewport_fade_opacity(mask, 244 * dpi) == 0,
                "Both viewport boundaries must clip all content");
            expect(std::abs(renderer2d_viewport_fade_opacity(mask, 52 * dpi) - 0.5f) < 0.001f &&
                std::abs(renderer2d_viewport_fade_opacity(mask, 234 * dpi) - 0.5f) < 0.001f,
                "Top and bottom gradients must have the same midpoint opacity regardless of row position");
            const auto visual = scene.resolved_shape_visual_state(card, now);
            expect(visual && visual->opacity == 1 && visual->viewportFade.enabled,
                "A partially visible card must retain its own opacity before the final per-pixel mask");
            for (const auto* batches : { &plan.shapes, &plan.cachedShapes })
                for (const auto& batch : *batches)
                    if (batch.entity == card)
                        expect(batch.effect0.w == 1, "Cached paint must not acquire a whole-card fade");

            SystemBackdropRegion glass;
            glass.identity = 21;
            glass.x = visual->rect.x; glass.y = visual->rect.y;
            glass.width = visual->rect.z; glass.height = visual->rect.w;
            glass.cornerRadius = 18 * dpi;
            const auto pieces = renderer2d_clip_backdrop_region(glass, visual->clipRect, mask);
            expect(pieces.size() == 1, "An edge card must consume one native slot, regardless of the fade height");
            if (!pieces.empty())
            {
                const auto& lens = pieces.front();
                expect(lens.identity == glass.identity && lens.x == glass.x && lens.y == glass.y &&
                    lens.width == glass.width && lens.height == glass.height && lens.shape == glass.shape &&
                    lens.cornerRadius == glass.cornerRadius && lens.contourCount == glass.contourCount,
                    "Viewport fading must preserve original lens geometry and normals without artificial contours");
                expect(lens.viewportRect[1] == 44 * dpi && lens.viewportRect[3] == 200 * dpi &&
                    lens.viewportFade[0] == 16 * dpi && lens.viewportFade[1] == 20 * dpi,
                    "The retained lens must use the same fixed gradient as foreground paint");
            }
            const auto retained = scene.build_render_plan(now, 1u);
            expect(retained.viewportFade.enabled && retained.viewportFade.rect == mask.rect,
                "Reusing cached content must retain both viewport fades");
        }
        // A large inbox must use slots only for the few intersecting lenses.
        Renderer2DViewportFade viewport { { 0, 44 * dpi, 100 * dpi, 200 * dpi }, 16 * dpi, 20 * dpi, 0, true };
        std::size_t visibleLenses = 0;
        for (unsigned index = 0; index < 1000; ++index)
        {
            SystemBackdropRegion lens;
            lens.identity = index + 1; lens.y = (44 + index * 80) * dpi;
            lens.width = 100 * dpi; lens.height = 72 * dpi;
            visibleLenses += renderer2d_clip_backdrop_region(lens, viewport.rect, viewport).size();
        }
        expect(visibleLenses == 3, "A thousand inbox cards must retain only three visible native lenses");
        registry.get<ScrollEdgeFade2DComponent>(scroll.content).enabled = false;
        scene.mark_dirty();
        expect(!scene.build_render_plan(now, 1u).viewportFade.enabled,
            "Disabling overflow must remove the final window gradient");
    }
    return failures ? 1 : 0;
}

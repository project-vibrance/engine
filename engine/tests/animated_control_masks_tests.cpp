#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/visuals.h>

#include <iostream>

int main()
{
    int failures = 0;
    const auto expect = [&](bool result, const char* message) {
        if (!result) { std::cerr << message << '\n'; ++failures; }
    };
    const auto near = [](glm::vec4 a, glm::vec4 b) { return glm::length(a - b) < 0.01f; };
    for (const float dpi : { 1.0f, 1.5f, 2.0f })
    for (const glm::vec2 animatedScale : { glm::vec2(0.6f), glm::vec2(1.12f), glm::vec2(0.8f, 0.65f) })
    {
        Renderer2DScene scene;
        LayoutScale scale;
        scale.factor = glm::vec2(dpi);
        UiBuilder ui(scene, scale);
        const auto panel = scene.create_shape({ 200.0f, 100.0f }, { 480.0f * dpi, 300.0f * dpi },
            make_solid_style("#202020FF"));
        scene.enable_mask(panel, false);
        UiSliderOptions sliderOptions;
        sliderOptions.value = 0.6f;
        sliderOptions.fillStyle = UiSliderFillStyle::eFlatFillClippedByTrack;
        const auto slider = ui_create_slider(ui, panel, UiAlignment::eTopLeft,
            { 24.0f, 32.0f }, { 300.0f, 8.0f }, 2, 0u, sliderOptions);
        entt::entity scrollChild = entt::null;
        UiScrollViewportOptions viewportOptions;
        viewportOptions.contentHeight = 300.0f;
        viewportOptions.showScrollbar = false;
        viewportOptions.showEdgeFades = false;
        viewportOptions.backgroundColor = "#303030FF";
        viewportOptions.buildContent = [&](const UiScrollViewportBuildContext& context) {
            scrollChild = context.ui.scene().create_shape({}, { 300.0f * dpi, 280.0f * dpi },
                make_solid_style("#FFFFFFFF"));
            context.ui.attach_aligned(scrollChild, context.content, UiAlignment::eTopLeft, {},
                { 300.0f * dpi, 280.0f * dpi });
        };
        const auto viewport = ui_create_scroll_viewport(ui, panel, UiAlignment::eTopLeft,
            { 24.0f, 80.0f }, { 300.0f, 100.0f }, viewportOptions);
        scene.build_render_plan(0.0, 0u);
        const Renderer2DFontAtlas atlas;
        ui_update_slider_visual(scene, atlas, slider.root);
        DisplayTransition2DComponent animation;
        animation.fromOpacity = animation.toOpacity = 1.0f;
        animation.fromScale = animation.toScale = animatedScale;
        animation.durationSeconds = 2.0f;
        scene.play_display_transition(panel, animation, 1.0);
        const auto plan = scene.build_render_plan(1.0, 0u);
        const auto find = [&](entt::entity entity) -> const Renderer2DBatch* {
            for (const auto& batch : plan.shapes) if (batch.entity == entity) return &batch;
            for (const auto& batch : plan.cachedShapes) if (batch.entity == entity) return &batch;
            return nullptr;
        };
        const auto* track = find(slider.root);
        const auto* fill = find(slider.indicator);
        const auto* viewportSurface = find(viewport.viewport);
        const auto* content = find(scrollChild);
        expect(track && fill && viewportSurface && content, "Animated control fixtures must emit their visible shapes");
        if (track && fill)
        {
            expect(near(fill->clipRect, track->rect),
                "A slider fill's rectangular clip must scale and move with its animated track");
            expect((fill->flags & eRenderer2DStyleShapeMask) && near(fill->effect1, track->rect),
                "A slider fill's rounded mask must follow the track during contraction and overshoot");
        }
        if (viewportSurface && content)
        {
            expect(near(content->clipRect, viewportSurface->rect),
                "Scroll content must clip to the animated viewport rather than its unscaled bounds");
            expect((content->flags & eRenderer2DStyleShapeMask) && near(content->effect1, viewportSurface->rect),
                "A scroll viewport's curved mask must follow the same animated rectangle as its surface");
        }
        scene.clear_display_transition(panel);
        auto& interactive = scene.registry().emplace<InteractiveVisual2DComponent>(panel);
        interactive.scale = animatedScale;
        interactive.scaleOrigin = { 200.0f, 100.0f };
        scene.mark_dirty(panel);
        const auto interactivePlan = scene.build_render_plan(2.0, 0u);
        const Renderer2DBatch* interactiveTrack = nullptr;
        const Renderer2DBatch* interactiveFill = nullptr;
        for (const auto* batches : { &interactivePlan.shapes, &interactivePlan.cachedShapes })
            for (const auto& batch : *batches)
            {
                if (batch.entity == slider.root) interactiveTrack = &batch;
                if (batch.entity == slider.indicator) interactiveFill = &batch;
            }
        expect(interactiveTrack && interactiveFill && near(interactiveFill->effect1, interactiveTrack->rect),
            "Direct island stretch and return must also preserve slider-mask alignment");
    }
    return failures ? 1 : 0;
}

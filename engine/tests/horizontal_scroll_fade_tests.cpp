#include <vibranceUI/ui/builder.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/surfaces.h>

#include <array>
#include <cmath>
#include <iostream>

int main()
{
    int failures = 0;
    const auto expect = [&](bool result, const char* message) {
        if (!result) { std::cerr << message << '\n'; ++failures; }
    };
    for (const float dpi : { 1.0f, 1.5f, 2.0f })
    {
        Renderer2DScene scene;
        LayoutScale scale;
        scale.factor = glm::vec2(dpi);
        UiBuilder ui(scene, scale);
        auto& registry = scene.registry();
        const auto panel = scene.create_shape({ 100.0f, 100.0f }, { 470.0f * dpi, 210.0f * dpi });
        ui.set_padding(panel, 20.0f, 16.0f, 20.0f, 16.0f);
        const auto viewport = scene.create_shape({}, { 1.0f, 86.0f * dpi }, ui_clear_surface_style());
        ui.attach_stretch(viewport, panel, {}, { 1.0f, 0.0f }, { 0.5f, 0.0f }, {},
            { 0.0f, 48.0f * dpi }, { 0.0f, 86.0f * dpi });
        scene.enable_mask(viewport, false, 8.0f * dpi);
        const auto track = scene.create_shape({}, { 1452.0f * dpi, 68.0f * dpi }, ui_clear_surface_style());
        ui.attach_aligned(track, viewport, UiAlignment::eTopCentre,
            { (726.0f - 180.0f) * dpi, 0.0f }, { 1452.0f * dpi, 68.0f * dpi });
        ScrollEdgeFade2DComponent fade;
        fade.horizontal = true;
        fade.relativeExtents = true;
        fade.topHeight = fade.bottomHeight = 0.22f;
        fade.maximumBlurRadius = 8.0f * dpi;
        registry.emplace<ScrollEdgeFade2DComponent>(track, fade);
        std::array<entt::entity, 3> ticks, labels;
        for (int index = 0; index < 3; ++index)
        {
            const float x = static_cast<float>(index) * 180.0f * dpi;
            ticks[index] = scene.create_shape({}, { 4.0f * dpi, 38.0f * dpi });
            ui.attach_aligned(ticks[index], track, UiAlignment::eTopLeft,
                { x - 2.0f * dpi, 26.0f * dpi }, { 4.0f * dpi, 38.0f * dpi });
            const auto slot = scene.create_shape({}, { 42.0f * dpi, 22.0f * dpi }, ui_clear_surface_style());
            ui.attach_aligned(slot, track, UiAlignment::eTopLeft,
                { x - 21.0f * dpi, 0.0f }, { 42.0f * dpi, 22.0f * dpi });
            labels[index] = scene.create_text("5", {}, 18.0f * dpi);
            ui.attach_aligned(labels[index], slot, UiAlignment::eCentre, {}, { 10.0f * dpi, 18.0f * dpi });
            auto& text = registry.get<TextComponent>(labels[index]);
            text.bounds = { 10.0f * dpi, 18.0f * dpi };
            text.glyphs.push_back(MSDFGlyph { '5', {}, text.bounds });
        }
        float previousEdgeOpacity = 0.0f;
        for (const float width : { 470.0f, 600.0f })
        {
            registry.get<ShapeComponent>(panel).size.x = width * dpi;
            scene.mark_dirty(panel);
            for (const float animationScale : { 1.0f, 0.6f, 1.12f })
            {
                DisplayTransition2DComponent animation;
                animation.fromOpacity = animation.toOpacity = 1.0f;
                animation.fromScale = animation.toScale = glm::vec2(animationScale);
                animation.durationSeconds = 2.0f;
                scene.play_display_transition(panel, animation, 1.0);
                const auto plan = scene.build_render_plan(1.0, 0u);
                const auto find = [&](entt::entity entity) -> const Renderer2DBatch* {
                    for (const auto* batches : { &plan.shapes, &plan.cachedShapes, &plan.texts, &plan.cachedTexts })
                        for (const auto& batch : *batches) if (batch.entity == entity) return &batch;
                    return nullptr;
                };
                const auto viewportRect = registry.get<ShapeComponent>(viewport).size;
                expect(std::abs(viewportRect.x - (width - 40.0f) * dpi) < 0.01f,
                    "A horizontal ruler viewport must fit the current panel content width");
                for (int index = 0; index < 3; ++index)
                {
                    const auto* tick = find(ticks[index]);
                    const auto* label = find(labels[index]);
                    expect(tick && label, "The ruler must emit both ticks and labels during scale animations");
                    if (!tick || !label) continue;
                    expect(std::abs(tick->effect0.w - label->effect0.w) < 0.001f,
                        "An edge tick and its nested label must fade together regardless of their height");
                    if (index == 1)
                        expect(std::abs(tick->effect0.w - 1.0f) < 0.001f &&
                            !(tick->flags & eRenderer2DStyleBlur) && !(label->flags & eRenderer2DStyleBlur),
                            "The selected minute must remain sharp and fully visible during expansion");
                    else
                        expect(tick->effect0.w < 0.99f && (tick->flags & eRenderer2DStyleBlur) &&
                            (label->flags & eRenderer2DStyleBlur) && label->effect1.w > 0.0f,
                            "Both ruler ticks and bold labels must blur progressively at either viewport edge");
                }
                if (animationScale == 1.0f)
                    if (const auto* edge = find(ticks[0]))
                    {
                        expect(edge->effect0.w > previousEdgeOpacity,
                            "Resizing a viewport must immediately move its edge fade without rebuilding the ruler");
                        previousEdgeOpacity = edge->effect0.w;
                    }
                scene.clear_display_transition(panel);
            }
        }
    }
    return failures ? 1 : 0;
}

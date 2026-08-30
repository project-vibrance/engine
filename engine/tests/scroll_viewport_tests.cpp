#include <vibranceUI/ui/controls.h>

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "scroll viewport test failed: " << message << '\n';
        }
        return condition;
    }

    bool close(float left, float right)
    {
        return std::abs(left - right) <= 0.001f;
    }
}

int main()
{
    Renderer2DScene scene;
    LayoutScale scale = {};
    scale.factor = { 2.0f, 3.0f };
    scale.contentScale = scale.factor;
    scale.logicalSize = { 640.0f, 480.0f };
    UiBuilder ui(scene, scale);
    const entt::entity root = ui.root();

    bool builtContent = false;
    UiScrollViewportOptions options = {};
    options.contentHeight = 400.0f;
    options.contentPadding = { 7.0f, 11.0f, 13.0f, 17.0f };
    options.layer = 4;
    options.order = 8u;
    options.entrance.startScale = 1.12f;
    options.entrance.durationSeconds = 0.52f;
    options.buildContent = [&](const UiScrollViewportBuildContext& content) {
        builtContent = content.content != entt::null &&
            content.layer == 5 &&
            content.order == 8u &&
            content.viewportSize == glm::vec2(300.0f, 220.0f);
    };

    const UiScrollViewportHandle viewport = ui_create_scroll_viewport(
        ui,
        root,
        UiAlignment::eCenter,
        { 10.0f, -6.0f },
        { 300.0f, 220.0f },
        options);

    const entt::registry& registry = scene.registry();
    bool passed = true;
    passed &= expect(
        viewport.root != entt::null && viewport.viewport != entt::null &&
            viewport.border != entt::null && viewport.content != entt::null,
        "one call should create the layout root, viewport, border, and content root");
    passed &= expect(builtContent, "the content callback should receive ready-to-use placement data");

    const Layout2DComponent& rootLayout = registry.get<Layout2DComponent>(viewport.root);
    passed &= expect(
        close(rootLayout.size.x, 600.0f) && close(rootLayout.size.y, 660.0f) &&
            close(rootLayout.offset.x, 20.0f) && close(rootLayout.offset.y, -18.0f),
        "placement should scale logical size and offset exactly once");

    const ShapeComponent& shape = registry.get<ShapeComponent>(viewport.viewport);
    const ShapeStyleComponent& style = registry.get<ShapeStyleComponent>(viewport.viewport);
    passed &= expect(
        shape.primitive == Renderer2DPrimitive::eRoundedRectangle &&
            close(shape.cornerRadius, 16.0f) && close(style.outlineWidth, 0.0f),
        "the viewport surface should keep its rounded mask without an animated outline");

    const ShapeComponent& borderShape = registry.get<ShapeComponent>(viewport.border);
    const ShapeStyleComponent& borderStyle =
        registry.get<ShapeStyleComponent>(viewport.border);
    passed &= expect(
        borderShape.primitive == Renderer2DPrimitive::eRoundedRectangle &&
            close(borderShape.cornerRadius, 16.0f) &&
            close(borderStyle.outlineWidth, 3.6f),
        "the separate border should match the viewport shape and scaled outline width");
    passed &= expect(
        registry.get<InputTransparent2DComponent>(viewport.border).enabled,
        "the decorative border should not intercept viewport input");
    passed &= expect(
        scene.entity_at({ 660.0f, 702.0f }) == viewport.viewport,
        "pointer picking should pass through the border to the viewport surface");

    const ScrollInputComponent& scroll = registry.get<ScrollInputComponent>(viewport.viewport);
    passed &= expect(
        close(scroll.maxOffset, 360.0f) && close(scroll.step, 84.0f),
        "scroll range and wheel step should follow the engine's logical scaling conventions");
    passed &= expect(
        registry.all_of<StretchDynamicsBlockerComponent>(viewport.viewport),
        "a reusable scroll viewport should block ancestor stretch gestures by default");
    passed &= expect(
        registry.all_of<HitRegion2DComponent>(viewport.viewport),
        "a reusable scroll viewport should capture pointer hits across transparent gaps");

    const Layout2DComponent& contentLayout = registry.get<Layout2DComponent>(viewport.content);
    passed &= expect(
        close(contentLayout.margin.x, 14.0f) &&
            close(contentLayout.margin.y, 33.0f) &&
            close(contentLayout.margin.z, 26.0f) &&
            close(contentLayout.margin.w, 51.0f),
        "content padding should preserve per-edge scaling");

    const DisplayTransition2DComponent& transition =
        registry.get<DisplayTransition2DComponent>(viewport.border);
    passed &= expect(
        close(transition.fromOpacity, 0.0f) &&
            close(transition.fromScale.x, 1.12f) &&
            close(transition.toScale.x, 1.0f) &&
            !transition.inheritToChildren &&
            !registry.all_of<DisplayTransition2DComponent>(viewport.viewport) &&
            !registry.all_of<DisplayTransition2DComponent>(viewport.content),
        "the entrance should fade and shrink only the border");

    passed &= expect(
        viewport.scrollbar.track != entt::null && viewport.scrollbar.thumb != entt::null,
        "the reusable control should include its scrollbar by default");
    return passed ? 0 : 1;
}

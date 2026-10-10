#include "../src/renderer/common/scene_helpers.h"

#include <iostream>

namespace
{
    struct TestSurface
    {
        struct
        {
            uint32_t width = 1024u;
            uint32_t height = 768u;
        } extent;
    };

    bool expect(bool condition, const char* message)
    {
        if (!condition)
        {
            std::cerr << "dynamic coverage test failed: " << message << '\n';
        }
        return condition;
    }

    bool same_bounds(const DispatchBounds& left, const DispatchBounds& right)
    {
        return left.x == right.x && left.y == right.y &&
            left.width == right.width && left.height == right.height;
    }

    Renderer2DBatch shape(glm::vec4 rect, int layer)
    {
        Renderer2DBatch batch {};
        batch.rect = rect;
        batch.clipRect = { 0.0f, 0.0f, 1024.0f, 768.0f };
        batch.layer = layer;
        return batch;
    }
}

int main()
{
    const TestSurface surface;
    Renderer2DRenderPlan plan;
    bool passed = true;
    const auto lens = shape({ 12.0f, 16.0f, 80.0f, 32.0f }, 2);
    plan.shapes.push_back(lens);
    const auto lensCoverage = make_dynamic_coverage_bounds(surface, plan);
    passed &= expect(same_bounds(lensCoverage, { 8u, 16u, 88u, 32u }),
        "a small control with a full-window clip must keep local replay coverage");
    plan.shadows.push_back(shape({ 6.0f, 10.0f, 92.0f, 44.0f }, 1));
    plan.shadows.back().effect0.y = 8.0f;
    const auto expectedShadow = align_dispatch_bounds_to_workgroups(surface,
        union_dispatch_bounds(lensCoverage, make_dispatch_bounds(
            surface, plan.shadows.back(), DispatchBoundsMode::eShadow)));
    passed &= expect(same_bounds(make_dynamic_coverage_bounds(surface, plan), expectedShadow),
        "dynamic shadows must retain their real padded writes");

    Renderer2DRenderPlan blurPlan;
    auto blur = shape({ 40.0f, 24.0f, 120.0f, 36.0f }, 2);
    blur.flags = eRenderer2DStyleLiquidGlass;
    blur.effect0.x = 4.5f;
    blur.clipRect = { 32.0f, 16.0f, 144.0f, 64.0f };
    blurPlan.blurs.push_back(blur);
    passed &= expect(same_bounds(make_dynamic_coverage_bounds(surface, blurPlan),
        align_dispatch_bounds_to_workgroups(surface, make_blur_replay_dispatch_bounds(surface, blur))),
        "liquid replay coverage must retain its unclipped source halo independently of its visible output");

    Renderer2DRenderPlan modelPlan;
    auto maskedFade = shape({ 300.0f, 100.0f, 640.0f, 72.0f }, 5);
    maskedFade.flags = eRenderer2DStyleBlur | eRenderer2DStyleBlurInheritedShapeMask;
    maskedFade.effect0.x = 4.0f;
    maskedFade.effect1 = { 300.0f, 100.0f, 640.0f, 600.0f };
    maskedFade.clipRect = maskedFade.effect1;
    const auto fadeBounds = make_blur_dispatch_bounds(surface, maskedFade);
    passed &= expect(fadeBounds.x == 300u && fadeBounds.y == 100u &&
        fadeBounds.width == 640u && fadeBounds.height == 80u,
        "a masked backdrop fade must use its blur radius instead of the inherited viewport width");
    auto unmaskedFade = maskedFade;
    unmaskedFade.flags &= ~eRenderer2DStyleBlurInheritedShapeMask;
    unmaskedFade.effect1 = { 0.0f, 0.0f, 12.0f, 0.0f };
    passed &= expect(make_blur_dispatch_bounds(surface, unmaskedFade).height == 88u,
        "ordinary unmasked blur must retain its authored feather envelope");

    Renderer3DModelBatch model {};
    model.viewportRect = { 100.0f, 80.0f, 64.0f, 48.0f };
    model.clipRect = { 0.0f, 0.0f, 1024.0f, 768.0f };
    modelPlan.models.push_back(model);
    const auto modelCoverage = make_dynamic_coverage_bounds(surface, modelPlan);
    passed &= expect(modelCoverage.width < 96u && modelCoverage.height < 80u,
        "a hosted model must use its viewport rather than its full-window clip");

    Renderer2DRenderPlan cached;
    // Store the more distant overlay first so reaching it requires another
    // pass after a nearer pane is promoted to the dynamic surface.
    cached.cachedShapes.push_back(shape({ 88.0f, 20.0f, 80.0f, 24.0f }, 4));
    cached.cachedShapes.push_back(shape({ 64.0f, 20.0f, 40.0f, 24.0f }, 3));
    cached.cachedShapes.push_back(shape({ 600.0f, 400.0f, 80.0f, 24.0f }, 5));
    cached.cachedShapes.push_back(shape({ 0.0f, 0.0f, 1024.0f, 768.0f }, 0));
    const auto replayCoverage = expand_cached_replay_bounds(
        surface, cached, render_layer_key(lens), lensCoverage);
    passed &= expect(replayCoverage.x == 8u && replayCoverage.y == 16u &&
        replayCoverage.width == 160u && replayCoverage.height == 32u,
        "overlapping higher panes must expand replay transitively without promoting unrelated or lower content");
    passed &= expect(same_bounds(expand_cached_replay_bounds(surface, cached,
        render_layer_key(lens), {}), {}),
        "empty dynamic coverage must leave the static layer retained");

    Renderer2DRenderPlan edgeOverlays;
    edgeOverlays.cachedShapes.push_back(shape({ 103.0f, 20.0f, 80.0f, 24.0f }, 4));
    edgeOverlays.cachedShapes.push_back(shape({ 94.0f, 20.0f, 3.0f, 24.0f }, 3));
    const auto edgeCoverage = expand_cached_replay_bounds(
        surface, edgeOverlays, render_layer_key(lens), lensCoverage);
    passed &= expect(edgeCoverage.x + edgeCoverage.width >= 184u,
        "new workgroup edges must participate in transitive cached replay");

    Renderer2DRenderPlan clippedGlass;
    auto clippedLens = shape({ 200.0f, 100.0f, 80.0f, 40.0f }, 4);
    clippedLens.flags = eRenderer2DStyleLiquidGlass;
    clippedLens.effect0.x = 8.0f;
    clippedLens.clipRect = clippedLens.rect;
    clippedGlass.cachedBlurs.push_back(clippedLens);
    auto invisibleLens = clippedLens;
    invisibleLens.clipRect = { 0.0f, 0.0f, 0.0f, 0.0f };
    clippedGlass.cachedBlurs.push_back(invisibleLens);
    // This object changes the refracted source despite being wholly outside
    // the lens's output clip. A higher overlay on the opposite side must also
    // leave the flattened static image before the lens samples its backdrop.
    const auto outsideClip = shape({ 184.0f, 112.0f, 4.0f, 8.0f }, 2);
    clippedGlass.cachedShapes.push_back(shape({ 288.0f, 112.0f, 8.0f, 8.0f }, 5));
    clippedGlass.cachedShapes.push_back(shape({ 288.0f, 112.0f, 8.0f, 8.0f }, 1));
    clippedGlass.cachedShapes.push_back(shape({ 600.0f, 112.0f, 8.0f, 8.0f }, 6));
    Renderer2DRenderPlan outsidePlan;
    outsidePlan.shapes.push_back(outsideClip);
    const auto outsideCoverage = make_dynamic_coverage_bounds(surface, outsidePlan);
    passed &= expect(!dispatch_bounds_overlap(outsideCoverage,
        make_liquid_output_dispatch_bounds(surface, clippedLens)) &&
        dispatch_bounds_overlap(outsideCoverage,
            make_blur_replay_dispatch_bounds(surface, clippedLens)),
        "a liquid lens must retain sampling dependencies outside its visible clip");
    const auto sourceReplayCoverage = expand_cached_replay_bounds(surface,
        clippedGlass, render_layer_key(outsideClip), outsideCoverage);
    std::vector<std::pair<uint32_t, std::size_t>> sourceMembership;
    collect_cached_replay_membership(surface, clippedGlass, render_layer_key(outsideClip),
        sourceReplayCoverage, true, sourceMembership);
    const std::vector<std::pair<uint32_t, std::size_t>> expectedSourceMembership {
        { static_cast<uint32_t>(Renderer2DRenderOpType::eBlur), 0u },
        { static_cast<uint32_t>(Renderer2DRenderOpType::eShape), 0u }
    };
    passed &= expect(sourceMembership == expectedSourceMembership,
        "moving source pixels must replay clipped glass and higher halo content without promoting invisible, lower or unrelated layers");
    outsidePlan.shapes.front().rect.x = 400.0f;
    const auto unrelatedCoverage = expand_cached_replay_bounds(surface, clippedGlass,
        render_layer_key(outsideClip), make_dynamic_coverage_bounds(surface, outsidePlan));
    collect_cached_replay_membership(surface, clippedGlass, render_layer_key(outsideClip),
        unrelatedCoverage, true, sourceMembership);
    passed &= expect(sourceMembership.empty(),
        "movement beyond a clipped lens's sampling halo must leave it retained");

    Renderer2DScene foregroundScene;
    const auto foregroundRoot = foregroundScene.create_shape(
        { 0.0f, 0.0f }, { 160.0f, 80.0f });
    const auto foregroundChild = foregroundScene.create_shape(
        { 12.0f, 12.0f }, { 48.0f, 24.0f });
    const auto unrelatedShape = foregroundScene.create_shape(
        { 240.0f, 12.0f }, { 48.0f, 24.0f });
    foregroundScene.registry().emplace<Parent2DComponent>(
        foregroundChild, Parent2DComponent { foregroundRoot });
    foregroundScene.registry().emplace<ForegroundBlurGroup2DComponent>(
        foregroundRoot, ForegroundBlurGroup2DComponent { 8.0f });
    const auto foregroundPlan = foregroundScene.build_render_plan(0.0, 0u);
    const auto findShapeBatch = [](const auto& batches, entt::entity entity)
        -> const Renderer2DBatch* {
        const auto found = std::find_if(batches.begin(), batches.end(),
            [entity](const Renderer2DBatch& batch) { return batch.entity == entity; });
        return found == batches.end() ? nullptr : &*found;
    };
    const Renderer2DBatch* groupedChild = findShapeBatch(
        foregroundPlan.shapes, foregroundChild);
    if (!groupedChild)
    {
        groupedChild = findShapeBatch(foregroundPlan.cachedShapes, foregroundChild);
    }
    const Renderer2DBatch* ungroupedShape = findShapeBatch(
        foregroundPlan.shapes, unrelatedShape);
    if (!ungroupedShape)
    {
        ungroupedShape = findShapeBatch(foregroundPlan.cachedShapes, unrelatedShape);
    }
    passed &= expect(groupedChild &&
        groupedChild->foregroundBlurGroup == foregroundRoot,
        "visible descendants must be assigned to their active foreground blur group");
    passed &= expect(ungroupedShape &&
        ungroupedShape->foregroundBlurGroup == entt::null,
        "unrelated shapes must not be assigned to a foreground blur group");

    Renderer2DRenderPlan dynamicGlass;
    dynamicGlass.blurs.push_back(clippedLens);
    Renderer2DRenderPlan haloOverlays;
    haloOverlays.cachedShapes.push_back(clippedGlass.cachedShapes.front());
    const auto dynamicGlassCoverage = expand_cached_replay_bounds(surface, haloOverlays,
        render_layer_key(clippedLens), make_dynamic_coverage_bounds(surface, dynamicGlass));
    collect_cached_replay_membership(surface, haloOverlays, render_layer_key(clippedLens),
        dynamicGlassCoverage, true, sourceMembership);
    passed &= expect(sourceMembership == std::vector<std::pair<uint32_t, std::size_t>> {
        { static_cast<uint32_t>(Renderer2DRenderOpType::eShape), 0u } },
        "an animated liquid lens must replay higher content inside its sampling halo even outside its output clip");

    Renderer2DScene motionScene;
    const auto moving = motionScene.create_shape({ 120.0f, 100.0f }, { 32.0f, 32.0f });
    const auto firstPane = motionScene.create_shape({ 100.0f, 80.0f }, { 200.0f, 100.0f });
    const auto secondPane = motionScene.create_shape({ 500.0f, 80.0f }, { 160.0f, 100.0f });
    auto& registry = motionScene.registry();
    registry.get_or_emplace<Renderer2DCacheComponent>(moving).mode = Renderer2DCacheMode::eDynamic;
    registry.get<RenderLayer2DComponent>(moving).layer = 3;
    registry.get<RenderLayer2DComponent>(firstPane).layer = 5;
    registry.get<RenderLayer2DComponent>(secondPane).layer = 5;
    Renderer2DRenderPlan initialPlan;
    motionScene.build_render_plan(initialPlan, 0.0, 0u);
    const auto staticGeneration = motionScene.cache_generation();
    const auto membership_for = [&](const Renderer2DRenderPlan& dynamicPlan) {
        const auto movingBatch = std::find_if(dynamicPlan.shapes.begin(), dynamicPlan.shapes.end(),
            [&](const Renderer2DBatch& batch) { return batch.entity == moving; });
        std::vector<std::pair<uint32_t, std::size_t>> membership;
        if (movingBatch != dynamicPlan.shapes.end())
        {
            const auto layer = render_layer_key(*movingBatch);
            const auto coverage = expand_cached_replay_bounds(surface, initialPlan, layer,
                make_dynamic_coverage_bounds(surface, dynamicPlan));
            collect_cached_replay_membership(surface, initialPlan, layer, coverage, true, membership);
        }
        return membership;
    };
    const auto initialMembership = membership_for(initialPlan);
    passed &= expect(initialMembership.size() == 1u,
        "a moving control must initially promote only the pane it intersects");

    registry.get<Transform2DComponent>(moving).position.x += 8.0f;
    motionScene.mark_dirty(moving);
    Renderer2DRenderPlan translatedPlan;
    motionScene.build_render_plan(translatedPlan, 0.1, staticGeneration);
    passed &= expect(!translatedPlan.rebuildCachedLayer &&
        motionScene.cache_generation() == staticGeneration &&
        membership_for(translatedPlan) == initialMembership,
        "movement inside the same overlay must retain the static generation and replay selection");

    registry.get<Transform2DComponent>(moving).position.x = 520.0f;
    motionScene.mark_dirty(moving);
    Renderer2DRenderPlan crossedPlan;
    motionScene.build_render_plan(crossedPlan, 0.2, staticGeneration);
    passed &= expect(!crossedPlan.rebuildCachedLayer &&
        membership_for(crossedPlan) != initialMembership,
        "crossing into a different pane must change replay ownership even with the same static generation");

    registry.get<RenderLayer2DComponent>(moving).layer = 8;
    motionScene.mark_dirty(moving);
    Renderer2DRenderPlan reorderedPlan;
    motionScene.build_render_plan(reorderedPlan, 0.3, staticGeneration);
    passed &= expect(membership_for(reorderedPlan).empty(),
        "moving above a pane must return it to the static layer");

    registry.get<ShapeStyleComponent>(firstPane).color0.r = 0.25f;
    motionScene.mark_dirty(firstPane);
    Renderer2DRenderPlan repaintedPlan;
    motionScene.build_render_plan(repaintedPlan, 0.4, staticGeneration);
    passed &= expect(repaintedPlan.rebuildCachedLayer &&
        motionScene.cache_generation() != staticGeneration,
        "repainting cached content must still invalidate the static layer regardless of replay membership");

    {
        Renderer2DScene scrollScene;
        auto& scrollRegistry = scrollScene.registry();
        const auto viewport = scrollScene.create_shape({ 0.0f, 0.0f }, { 100.0f, 100.0f });
        scrollScene.enable_mask(viewport, false);
        ShapeStyleComponent clearStyle;
        clearStyle.color0.a = 0.0f;
        clearStyle.color1.a = 0.0f;
        const auto content = scrollScene.create_shape({ 0.0f, 0.0f }, { 100.0f, 300.0f },
            clearStyle);
        scrollRegistry.emplace<Parent2DComponent>(content, Parent2DComponent { viewport });
        auto& contentCache = scrollRegistry.get<Renderer2DCacheComponent>(content);
        contentCache.mode = Renderer2DCacheMode::eDynamic;
        contentCache.propagateToChildren = true;
        scrollRegistry.emplace<ScrollEdgeFade2DComponent>(content,
            ScrollEdgeFade2DComponent { true, 20.0f, 20.0f, 0.0f, 7.0f });
        const auto label = scrollScene.create_text("A", { 40.0f, 40.0f }, 16.0f);
        scrollRegistry.emplace<Parent2DComponent>(label, Parent2DComponent { content });
        auto& text = scrollRegistry.get<TextComponent>(label);
        text.bounds = { 8.0f, 12.0f };
        text.glyphs.push_back(MSDFGlyph { 'A', { 0.0f, 0.0f }, { 8.0f, 12.0f } });
        const auto visible = scrollScene.build_render_plan(0.0, 0u);
        const auto retainedGeneration = scrollScene.cache_generation();
        passed &= expect(!visible.texts.empty() &&
            std::binary_search(visible.dynamicEntities.begin(), visible.dynamicEntities.end(), entity_key(label)),
            "a scrolling label must own dynamic pixels before entering an edge fade");

        scrollRegistry.get<Transform2DComponent>(label).position.y = -40.0f;
        scrollScene.mark_dirty(label);
        const auto faded = scrollScene.build_render_plan(0.1, retainedGeneration);
        passed &= expect(faded.texts.empty() && !faded.rebuildCachedLayer &&
            faded.dynamicEntities == visible.dynamicEntities,
            "scrolling through a zero-opacity edge fade must change emitted paint without changing dynamic ownership");
        scrollRegistry.get<ScrollEdgeFade2DComponent>(content).enabled = false;
        const auto clipped = scrollScene.build_render_plan(0.2, retainedGeneration);
        passed &= expect(clipped.texts.empty() && !clipped.rebuildCachedLayer &&
            clipped.dynamicEntities == visible.dynamicEntities,
            "fully clipped scrolling glyphs must be omitted while retaining their cache ownership");
        scrollRegistry.get<RenderLayer2DComponent>(label).visible = false;
        const auto hidden = scrollScene.build_render_plan(0.3, retainedGeneration);
        passed &= expect(hidden.dynamicEntities == visible.dynamicEntities,
            "hierarchy visibility must not migrate a dynamic drawable into the static cache");

        contentCache.mode = Renderer2DCacheMode::eStatic;
        const auto directStatic = scrollScene.build_render_plan(0.4, retainedGeneration);
        passed &= expect(!directStatic.rebuildCachedLayer &&
            directStatic.dynamicEntities != hidden.dynamicEntities &&
            !std::binary_search(directStatic.dynamicEntities.begin(), directStatic.dynamicEntities.end(), entity_key(label)),
            "an unmarked dynamic-to-static cache policy change must remain detectable even for an invisible child");
        contentCache.mode = Renderer2DCacheMode::eDynamic;
        const auto directDynamic = scrollScene.build_render_plan(0.5, retainedGeneration);
        passed &= expect(!directDynamic.rebuildCachedLayer &&
            directDynamic.dynamicEntities == hidden.dynamicEntities,
            "an unmarked static-to-dynamic policy change must restore ownership before a hidden child emits paint");

        scrollRegistry.get<RenderLayer2DComponent>(label).visible = true;
        scrollRegistry.get<Transform2DComponent>(label).position = { -14.0f, 40.0f };
        auto& textStyle = scrollRegistry.get<TextStyleComponent>(label);
        const auto clippedPlain = scrollScene.build_render_plan(0.6, retainedGeneration);
        passed &= expect(clippedPlain.texts.empty(),
            "a plain glyph wholly outside a viewport must avoid batch construction downstream");
        textStyle.blurRadius = 8.0f;
        const auto blurredEdge = scrollScene.build_render_plan(0.7, retainedGeneration);
        passed &= expect(blurredEdge.texts.size() == 1u,
            "a clipped glyph whose foreground blur reaches the viewport must remain drawable");
        textStyle.blurRadius = 0.0f;
        textStyle.shadowOffset = { 8.0f, 0.0f };
        textStyle.shadowBlur = 2.0f;
        const auto shadowEdge = scrollScene.build_render_plan(0.8, retainedGeneration);
        passed &= expect(shadowEdge.texts.size() == 1u,
            "a clipped glyph whose shadow underlay reaches the viewport must remain drawable");
        textStyle.shadowBlur = 0.0f;
        textStyle.shadowColor.a = 0.0f;
        textStyle.glowRadius = 8.0f;
        const auto glowEdge = scrollScene.build_render_plan(0.9, retainedGeneration);
        passed &= expect(glowEdge.texts.size() == 1u &&
            glowEdge.dynamicEntities == visible.dynamicEntities,
            "a clipped glyph whose glow reaches the viewport must retain paint and stable ownership");
    }
    return passed ? 0 : 1;
}

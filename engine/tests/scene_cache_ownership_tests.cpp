#include <vibranceUI/ui/builder.h>
#include <algorithm>
#include <iostream>
#include <stdexcept>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

bool contains(const auto& batches, entt::entity entity)
{
    return std::any_of(batches.begin(), batches.end(),
        [entity](const auto& batch) { return batch.entity == entity; });
}

void builder_handoffs_rebuild_retained_paint()
{
    Renderer2DScene scene;
    UiBuilder ui(scene, {}, false);
    const auto root = scene.create_shape({0, 0}, {200, 100}, {});
    const auto child = scene.create_shape({10, 10}, {20, 20}, {});
    scene.registry().emplace<Parent2DComponent>(child, root);
    auto first = scene.build_render_plan(0, 0);
    require(contains(first.cachedShapes, root) && contains(first.cachedShapes, child),
        "The initial static plan missed its subtree");
    auto retained = scene.cache_generation();

    ui.set_dynamic_cache(root, false);
    auto promoted = scene.build_render_plan(.1, retained);
    require(promoted.rebuildCachedLayer && contains(promoted.shapes, root) &&
        !contains(promoted.cachedShapes, root) && contains(promoted.cachedShapes, child),
        "Promoting a presented shape retained duplicate static paint");
    retained = scene.cache_generation();
    const auto frame = scene.frame_generation();
    ui.set_dynamic_cache(root, false);
    require(scene.cache_generation() == retained && scene.frame_generation() == frame,
        "An unchanged dynamic policy dirtied the scene");

    ui.set_dynamic_cache(root, true);
    auto inherited = scene.build_render_plan(.2, retained);
    require(inherited.rebuildCachedLayer && contains(inherited.shapes, child) &&
        !contains(inherited.cachedShapes, child),
        "Changing propagation retained a static descendant");
    retained = scene.cache_generation();

    ui.set_timed_cache(root, 0, 0, 0);
    auto demoted = scene.build_render_plan(.3, retained);
    require(demoted.rebuildCachedLayer && contains(demoted.cachedShapes, root) &&
        contains(demoted.cachedShapes, child) && !contains(demoted.shapes, root),
        "An idle timed policy did not restore the static subtree");
    retained = scene.cache_generation();
    const auto stableFrame = scene.frame_generation();
    ui.set_timed_cache(root, 0, 0, 0);
    require(scene.cache_generation() == retained && scene.frame_generation() == stableFrame,
        "An unchanged timed policy dirtied the scene");

    scene.registry().get<RenderLayer2DComponent>(root).visible = false;
    scene.mark_dirty(root);
    auto hidden = scene.build_render_plan(.4, retained);
    require(hidden.rebuildCachedLayer && hidden.cachedShapes.empty(),
        "Hiding a static subtree retained its paint");
    retained = scene.cache_generation();
    scene.destroy_entity_tree(root);
    require(scene.build_render_plan(.5, retained).rebuildCachedLayer,
        "Removing a static subtree did not invalidate its retained plan");
}

void playback_promotion_rebuilds_once()
{
    Renderer2DScene scene;
    Media2DHandle handle;
    handle.id = 1;
    handle.drawable = true;
    const auto media = scene.create_media({}, {100, 100}, handle);
    const auto initial = scene.build_render_plan(0, 0);
    require(contains(initial.cachedMedia, media), "The static player was not retained");
    const auto retained = scene.cache_generation();
    auto& player = scene.registry().get<Media2DComponent>(media);
    player.animated = true;
    player.playing = true;
    player.frameCount = 2;
    player.frameRate = 30;
    player.durationSeconds = 2.0 / 30;
    auto live = scene.build_render_plan(.01, retained);
    require(live.rebuildCachedLayer && contains(live.media, media) &&
        !contains(live.cachedMedia, media),
        "Starting animation retained a duplicate static player");
    const auto liveGeneration = scene.cache_generation();
    require(!scene.build_render_plan(.02, liveGeneration).rebuildCachedLayer,
        "Ongoing playback repeatedly invalidated the static plan");
}

void live_parents_invalidate_static_descendants()
{
    Renderer2DScene scene;
    UiBuilder ui(scene);
    const auto root = scene.create_shape({}, {200, 100}, {});
    const auto middle = scene.create_shape({}, {100, 50}, {});
    const auto child = scene.create_shape({}, {20, 20}, {});
    scene.registry().emplace<Parent2DComponent>(middle, root);
    scene.registry().emplace<Layout2DComponent>(middle).offset = {10, 10};
    scene.registry().emplace<Parent2DComponent>(child, middle);
    scene.registry().emplace<Layout2DComponent>(child).offset = {5, 5};
    ui.set_dynamic_cache(root, false);
    ui.set_dynamic_cache(middle, false);
    const auto initial = scene.build_render_plan(0, 0);
    require(contains(initial.cachedShapes, child), "The grandchild should remain static");
    auto retained = scene.cache_generation();

    scene.registry().get<Transform2DComponent>(root).position.x = 60;
    scene.mark_dirty(root);
    const auto moved = scene.build_render_plan(.1, retained);
    require(moved.rebuildCachedLayer && contains(moved.cachedShapes, child) &&
        scene.registry().get<Transform2DComponent>(child).position.x == 75,
        "Moving a live parent retained its static grandchild at the old position");
    retained = scene.cache_generation();
    scene.registry().get<RenderLayer2DComponent>(root).visible = false;
    scene.mark_dirty(root);
    const auto hidden = scene.build_render_plan(.2, retained);
    require(hidden.rebuildCachedLayer && hidden.cachedShapes.empty(),
        "Hiding a live parent retained a static grandchild");
}

void hidden_pages_reflow_when_input_becomes_visible()
{
    Renderer2DScene scene;
    const auto root = scene.create_shape({}, {500, 500}, {});
    scene.registry().emplace<InputTransparent2DComponent>(root);
    const auto page = scene.create_shape({}, {200, 100}, {});
    scene.registry().emplace<InputTransparent2DComponent>(page);
    scene.registry().emplace<Parent2DComponent>(page, root);
    scene.registry().emplace<Layout2DComponent>(page).offset = {10, 20};
    const auto child = scene.create_shape({}, {10, 10}, {});
    scene.registry().emplace<Parent2DComponent>(child, page);
    scene.registry().emplace<Layout2DComponent>(child).offset = {4, 6};
    scene.registry().get<RenderLayer2DComponent>(page).visible = false;
    scene.build_render_plan(0, 0);
    require(scene.registry().get<Transform2DComponent>(child).position == glm::vec2(14, 26),
        "Render layout no longer resolves hidden pages");

    scene.registry().get<Layout2DComponent>(page).offset = {120, 80};
    scene.mark_dirty(page);
    require(scene.entity_at({125, 87}) == entt::null && !scene.hit_test({125, 87}),
        "A hidden page accepted input");
    require(scene.registry().get<Transform2DComponent>(child).position == glm::vec2(14, 26),
        "Input needlessly reflowed a hidden page");
    scene.registry().get<RenderLayer2DComponent>(page).visible = true;
    scene.mark_dirty(page);
    require(scene.entity_at({125, 87}) == child && scene.hit_test({125, 87}),
        "Showing a page left its input geometry at the old location");
    require(scene.registry().get<Transform2DComponent>(child).position == glm::vec2(124, 86),
        "Visible input did not resolve the page and its child");
}

void visible_render_layout_matches_full_paint()
{
    Renderer2DScene scene;
    UiBuilder ui(scene);
    const auto root = scene.create_shape({}, {600, 400}, {});
    const auto visiblePage = scene.create_shape({}, {240, 180}, {});
    scene.registry().emplace<Parent2DComponent>(visiblePage, root);
    scene.registry().emplace<Layout2DComponent>(visiblePage).offset = {20, 30};
    const auto hiddenPage = scene.create_shape({}, {240, 180}, {});
    scene.registry().emplace<Parent2DComponent>(hiddenPage, root);
    scene.registry().emplace<Layout2DComponent>(hiddenPage).offset = {300, 40};
    scene.registry().get<RenderLayer2DComponent>(hiddenPage).visible = false;
    ui.set_dynamic_cache(hiddenPage);
    const auto hiddenChild = scene.create_shape({}, {20, 20}, {});
    scene.registry().emplace<Parent2DComponent>(hiddenChild, hiddenPage);
    scene.registry().emplace<Layout2DComponent>(hiddenChild).offset = {5, 7};
    const auto sibling = scene.create_shape({}, {40, 30}, {});
    scene.registry().emplace<Parent2DComponent>(sibling, root);
    scene.registry().emplace<Layout2DComponent>(sibling).offset = {10, 250};
    for (const auto page : {visiblePage, hiddenPage})
    {
        const auto label = scene.create_text("Page label", {}, 14);
        scene.registry().get<TextComponent>(label).bounds = {80, 18};
        scene.registry().emplace<Parent2DComponent>(label, page);
        scene.registry().emplace<Layout2DComponent>(label).offset = {8, 10};
    }
    const auto sameBatches = [](const auto& left, const auto& right) {
        if (left.size() != right.size()) return false;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            const auto& a = left[index];
            const auto& b = right[index];
            if (a.entity != b.entity || a.rect != b.rect || a.clipRect != b.clipRect ||
                a.layer != b.layer || a.order != b.order || a.flags != b.flags ||
                a.color0 != b.color0 || a.color1 != b.color1)
                return false;
        }
        return true;
    };
    const auto equivalentPaint = [&](double time) {
        Renderer2DRenderPlan visible;
        scene.build_visible_render_plan(visible, time, 0);
        const auto full = scene.build_render_plan(time, 0);
        require(visible.dynamicEntities == full.dynamicEntities &&
            sameBatches(visible.shapes, full.shapes) &&
            sameBatches(visible.cachedShapes, full.cachedShapes) &&
            sameBatches(visible.texts, full.texts) &&
            sameBatches(visible.cachedTexts, full.cachedTexts),
            "Deferred hidden layout changed visible paint or cache ownership");
        require(std::find(visible.dynamicEntities.begin(), visible.dynamicEntities.end(),
            entt::to_integral(hiddenChild)) != visible.dynamicEntities.end(),
            "Hidden layout deferral omitted dynamic ownership");
    };

    Renderer2DRenderPlan deferred;
    scene.build_visible_render_plan(deferred, 0, 0);
    require(scene.registry().get<Transform2DComponent>(hiddenChild).position == glm::vec2(0),
        "Visible rendering still resolved hidden-page geometry");
    equivalentPaint(0);
    scene.registry().get<Layout2DComponent>(hiddenPage).offset = {320, 80};
    scene.registry().get<RenderLayer2DComponent>(hiddenPage).visible = true;
    scene.mark_dirty(hiddenPage);
    equivalentPaint(.1);
    require(scene.registry().get<Transform2DComponent>(hiddenChild).position == glm::vec2(325, 87) &&
        scene.registry().get<Transform2DComponent>(sibling).position == glm::vec2(10, 250),
        "Showing a deferred page failed to reflow it or displaced a sibling");

    scene.registry().get<RenderLayer2DComponent>(visiblePage).visible = false;
    scene.registry().get<Layout2DComponent>(visiblePage).offset = {100, 120};
    scene.mark_dirty(visiblePage);
    equivalentPaint(.2);
    scene.registry().get<RenderLayer2DComponent>(visiblePage).visible = true;
    scene.mark_dirty(visiblePage);
    equivalentPaint(.3);
}

void glyph_hit_envelopes_preserve_overhang_and_gaps()
{
    Renderer2DScene scene;
    const auto parent = scene.create_entity();
    InteractiveVisual2DComponent stretch;
    stretch.scale = {2, 1.5};
    stretch.offset = {20, 30};
    scene.registry().emplace<InteractiveVisual2DComponent>(parent, stretch);
    const auto text = scene.create_text("two glyphs", {100, 100}, 14);
    scene.registry().emplace<Parent2DComponent>(text, parent);
    auto& value = scene.registry().get<TextComponent>(text);
    // Explicit bounds are deliberately smaller than the glyph overhangs.
    value.bounds = {20, 18};
    MSDFGlyph first;
    first.position = {-12, -4};
    first.size = {8, 18};
    MSDFGlyph second;
    second.position = {80, 0};
    second.size = {8, 18};
    value.glyphs = { first, second };
    const auto scaledPoint = [&](glm::vec2 point) {
        return point * stretch.scale + stretch.offset;
    };
    for (const auto point : {glm::vec2(90, 100), glm::vec2(184, 105)})
    {
        require(scene.entity_at(scaledPoint(point)) == text &&
            scene.hit_test(scaledPoint(point)),
            "The text envelope discarded a stretched glyph overhang");
    }
    require(scene.entity_at(scaledPoint({140, 105})) == entt::null &&
        !scene.hit_test(scaledPoint({140, 105})),
        "The coarse text envelope made a gap between glyphs interactive");
    require(scene.entity_at({1000, 1000}) == entt::null && !scene.hit_test({1000, 1000}),
        "A distant text label remained interactive");
}
}

int main()
{
    try
    {
        builder_handoffs_rebuild_retained_paint();
        playback_promotion_rebuilds_once();
        live_parents_invalidate_static_descendants();
        hidden_pages_reflow_when_input_becomes_visible();
        visible_render_layout_matches_full_paint();
        glyph_hit_envelopes_preserve_overhang_and_gaps();
        std::cout << "Scene ownership and glyph hit bounds passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

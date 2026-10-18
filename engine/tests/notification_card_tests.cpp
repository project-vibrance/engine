#include <vibranceUI/localisation/localisation.h>
#include <vibranceUI/notifications/card.h>
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/ui/builder.h>

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "notification card test failed: "
                << message << '\n';
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
    scale.logicalSize = { 428.0f, 400.0f };
    UiBuilder ui(scene, scale);
    const entt::entity root = ui.root();
    const Renderer2DFontAtlas fontAtlas;
    const Localisation localisation;

    const auto wrapped = ui_wrap_notification_message(
        "abcdefghijklmnopqrst",
        5u);
    bool activated = false;
    bool dismissed = false;
    bool optionsOpened = false;
    UiNotificationCardOptions options = {};
    options.parent = root;
    options.offset = { 16.0f, 44.0f };
    options.content.header = "Example";
    options.content.title = "Reusable notification";
    options.content.message = "Cards consume view data and callbacks.";
    options.content.elapsedText = "now";
    options.content.optionsLabel = "Options  v";
    options.callbacks.activate = [&activated] { activated = true; };
    options.callbacks.dismiss = [&dismissed] { dismissed = true; };
    options.callbacks.showOptions = [&optionsOpened] {
        optionsOpened = true;
    };
    options.groupSize = 3u;
    options.showOptions = true;

    UiNotificationCardHandle card = ui_create_notification_card(
        ui,
        fontAtlas,
        localisation,
        options);
    entt::registry& registry = scene.registry();
    bool passed = true;
    const std::string zwnj = "\xe2\x80\x8c";
    const std::string email = "artofnate.com is about to expire\n"
        "Renew artofnate.com to keep your site online.";
    const std::string emailPadding = "\n&#x20;" + zwnj + " " + zwnj + "  " + zwnj;
    passed &= expect(ui_normalise_notification_text(email + emailPadding) == email &&
        ui_supported_notification_text(fontAtlas, email + emailPadding, 14.0f) == email,
        "HTML email padding must disappear before glyph filtering or layout");
    passed &= expect(ui_normalise_notification_text("&#32;&zwnj;&nbsp;&#x200c;\n").empty() &&
        ui_normalise_notification_text("one\xc2\xa0\xe2\x80\xaf\xe3\x80\x80two") == "one two" &&
        ui_normalise_notification_text("one\r\n" + zwnj + " \n two") == "one\ntwo",
        "numeric entities and Unicode separators must normalise without empty padding rows");
    const std::string persian = "\xd9\x85\xdb\x8c" + zwnj + "\xd8\xb1\xd9\x88\xd9\x85";
    const std::string emoji = "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb";
    passed &= expect(ui_normalise_notification_text(persian + " " + emoji) == persian + " " + emoji &&
        ui_normalise_notification_text("&lt;literal&gt; &#x110000; &#nope;") ==
            "&lt;literal&gt; &#x110000; &#nope;",
        "padding cleanup must preserve intra-word joiners, emoji and non-padding entities");
    const auto paddingRows = ui_wrap_notification_message("Visible\n&#x20;" + zwnj + " " + zwnj);
    passed &= expect(paddingRows[0] == "Visible" && paddingRows[1].empty() && paddingRows[2].empty(),
        "invisible padding must not consume message rows or trigger overflow");
    UiNotificationCardContent twoRowTitle;
    twoRowTitle.title = "First\nSecond\nThird";
    twoRowTitle.message = "Body";
    const auto titleMetrics = ui_measure_notification_card(fontAtlas, twoRowTitle, false, false);
    passed &= expect(titleMetrics.size.y == kUiNotificationCardHeight + 18.0f,
        "a second title row must increase the card height independently of its body");
    for (const auto& palette : { ui_light_theme(), ui_dark_theme() })
    {
        auto themedOptions = options;
        themedOptions.theme = palette;
        const auto themedCard = ui_create_notification_card(ui, fontAtlas, localisation, themedOptions);
        const auto& style = registry.get<ShapeStyleComponent>(themedCard.surface);
        passed &= expect(style.color0 == renderer2d_hex_color(palette.surfaceElevated),
            "notification surface should use the selected theme");
        scene.destroy_entity_tree(themedCard.root);
    }
    passed &= expect(
        wrapped[0] == "abcde" && wrapped[1] == "fghij" &&
            wrapped[2] == "kl...",
        "UTF-8-safe wrapping should ellipsize overflow on the third row");
    passed &= expect(
        ui_measure_notification_card(
            fontAtlas,
            "Short message",
            false,
            false).size.y == kUiNotificationCardHeight &&
        ui_measure_notification_card(
            fontAtlas,
            "Short message",
            true,
            false).size.y == kUiNotificationActionCardHeight,
        "measurement should reserve action space only when requested");
    passed &= expect(
        card.root != entt::null && registry.valid(card.root) &&
            card.surface != entt::null && registry.valid(card.surface),
        "card creation should return valid retained entities");
    passed &= expect(
        registry.all_of<SystemBackdropComponent>(card.surface) &&
            registry.get<SystemBackdropComponent>(card.surface)
                .region.material == GlassMaterial::eSystemGlass,
        "the main card should retain its Windows Composition blur request");
    const Layout2DComponent& layout =
        registry.get<Layout2DComponent>(card.root);
    passed &= expect(
        close(layout.offset.x, 16.0f) && close(layout.offset.y, 44.0f) &&
            close(layout.size.x, options.metrics.size.x) &&
            close(layout.size.y, options.metrics.size.y + 14.0f),
        "card geometry should include two compact backing layers");
    passed &= expect(
        card.groupSize == 3u && card.optionsButton != entt::null &&
            card.dismissButton != entt::null,
        "group state and optional controls should be represented in the handle");

    registry.get<ButtonInputComponent>(card.root).onClick({});
    registry.get<ButtonInputComponent>(card.dismissButton).onClick({});
    registry.get<ButtonInputComponent>(card.optionsButton).onClick({});
    passed &= expect(
        activated && dismissed && optionsOpened,
        "card interactions should invoke only caller-owned callbacks");

    {
        auto actionOptions = options;
        actionOptions.showOptions = false;
        actionOptions.metrics = ui_measure_notification_card(fontAtlas, "A short message", true, false);
        int firstAction = 0, secondAction = 0;
        actionOptions.actions = { { "Not Now", [&] { ++firstAction; } },
            { "Restart", [&] { ++secondAction; } } };
        const auto actionable = ui_create_notification_card(ui, fontAtlas, localisation, actionOptions);
        scene.build_render_plan(0.0, 0u);
        passed &= expect(actionable.actionButtons.size() == 2u && actionable.optionsButton == entt::null,
            "two actions should appear as direct pill buttons rather than an options menu");
        for (const auto button : actionable.actionButtons)
        {
            const auto buttonRect = ui_entity_framebuffer_rect(registry, button);
            const auto surfaceRect = ui_entity_framebuffer_rect(registry, actionable.surface);
            passed &= expect(buttonRect.y >= surfaceRect.y + 60.0f &&
                buttonRect.y + buttonRect.w <= surfaceRect.y + surfaceRect.w &&
                registry.get<ShapeStyleComponent>(button).outlineWidth == 0.0f,
                "action pills must fit below the message and retain a neutral borderless paint");
        }
        registry.get<ButtonInputComponent>(actionable.actionButtons[0]).onClick({});
        registry.get<ButtonInputComponent>(actionable.actionButtons[1]).onClick({});
        passed &= expect(firstAction == 1 && secondAction == 1,
            "direct action pills must dispatch their own callback exactly once");
        scene.destroy_entity_tree(actionable.root);
    }

    for (const float dpi : { 1.0f, 1.5f, 2.0f })
    {
        for (const bool rtl : { false, true })
        {
        for (const double refreshRate : { 30.0, 60.0, 144.0 })
        {
            Renderer2DScene motionScene;
            motionScene.registry().ctx().insert_or_assign(UiLayoutDirectionComponent {
                rtl ? UiLayoutDirection::eRightToLeft : UiLayoutDirection::eLeftToRight });
            LayoutScale motionScale {};
            motionScale.logicalSize = { 428.0f, 400.0f };
            motionScale.factor = glm::vec2(dpi);
            UiBuilder motionUi(motionScene, motionScale);
            auto motionOptions = options;
            motionOptions.parent = motionUi.root();
            auto moving = ui_create_notification_card(motionUi, fontAtlas, localisation, motionOptions);
            moving.fanDurationSeconds = 0.48f;
            motionScene.build_render_plan(0.0, 0u);
            const auto resting = motionScene.resolved_shape_visual_state(moving.surface, 0.0);
            ui_prepare_notification_card_fan(motionUi, moving, { 428.0f, 0.0f }, 1.0, 0.1f);
            motionScene.build_render_plan(1.0, 0u);
            const auto arriving = motionScene.resolved_shape_visual_state(moving.surface, 1.0);
            passed &= expect(resting && arriving && close(arriving->rect.x - resting->rect.x, (rtl ? -1.0f : 1.0f) * 428.0f * dpi),
                "new card glass and sharp UI must begin off the leading screen edge at every DPI");
            bool overshot = false;
            for (int frame = 0; frame < 90; ++frame)
            {
                const double now = 1.0 + frame / refreshRate;
                ui_update_notification_card_motion(motionScene, moving, now);
                motionScene.build_render_plan(now, 0u);
                const auto state = motionScene.resolved_shape_visual_state(moving.surface, now);
                const auto offset = motionScene.registry().get<InteractiveVisual2DComponent>(moving.root).offset;
                passed &= expect(state && close(state->rect.x - resting->rect.x, offset.x),
                    "native glass must follow the same horizontal motion as its card's content");
                overshot = overshot || (rtl ? offset.x > 0.0f : offset.x < 0.0f);
            }
            const auto offset = motionScene.registry().get<InteractiveVisual2DComponent>(moving.root).offset;
            passed &= expect(overshot && offset == glm::vec2(0.0f) && moving.fanStartSeconds < 0.0,
                "arrival must settle with a small bounce and no residual position offset at all refresh rates");
        }
        }
    }

    ui_prepare_notification_card_fan(
        ui,
        card,
        { 0.0f, -20.0f },
        10.0,
        0.1f);
    const InteractiveVisual2DComponent& visual =
        registry.get<InteractiveVisual2DComponent>(card.root);
    passed &= expect(
        visual.offset == glm::vec2(0.0f, -20.0f) &&
            std::abs(card.fanStartSeconds - 10.1) <= 0.001,
        "fan preparation should retain transition state on the generic card");

    // Native backdrop geometry consumes this same resolved state after the
    // Vulkan render plan is built. Verify a transition on the card root is
    // inherited by its system-glass surface for both scale and fade.
    scene.build_render_plan(19.0, 0u);
    const auto restingSurface =
        scene.resolved_shape_visual_state(card.surface, 19.0);
    DisplayTransition2DComponent entrance = {};
    entrance.durationSeconds = 1.0f;
    entrance.fromOpacity = 0.0f;
    entrance.toOpacity = 1.0f;
    entrance.fromScale = { 0.94f, 0.94f };
    entrance.toScale = { 1.0f, 1.0f };
    entrance.removeWhenComplete = false;
    scene.play_display_transition(card.root, entrance, 20.0);
    scene.build_render_plan(20.0, 0u);
    const auto enteringSurface =
        scene.resolved_shape_visual_state(card.surface, 20.0);
    scene.build_render_plan(20.5, 0u);
    const auto halfwaySurface =
        scene.resolved_shape_visual_state(card.surface, 20.5);
    passed &= expect(
        restingSurface && enteringSurface && halfwaySurface &&
            close(enteringSurface->opacity, 0.0f) &&
            halfwaySurface->opacity > 0.0f &&
            halfwaySurface->opacity < 1.0f &&
            close(
                enteringSurface->rect.z,
                restingSurface->rect.z * 0.94f) &&
            close(
                enteringSurface->rect.w,
                restingSurface->rect.w * 0.94f) &&
            halfwaySurface->rect.z > enteringSurface->rect.z &&
            halfwaySurface->rect.z < restingSurface->rect.z,
        "the system-glass surface should inherit the card entrance scale and opacity");

    for (float dpi : { 1.0f, 1.5f, 1.75f, 3.0f, 5.0f })
    {
        Renderer2DScene layeredScene;
        LayoutScale layeredScale {};
        layeredScale.factor = glm::vec2(dpi);
        layeredScale.logicalSize = { 428.0f, 400.0f };
        UiBuilder layeredUi(layeredScene, layeredScale);
        auto layeredOptions = options;
        layeredOptions.parent = layeredUi.root();
        const auto layeredCard = ui_create_notification_card(layeredUi, fontAtlas,
            localisation, layeredOptions);
        auto& layeredRegistry = layeredScene.registry();
        const auto cutouts = layeredRegistry.view<ShapeCutout2DComponent>();
        passed &= expect(cutouts.size() == 2u,
            "each backing card must have a silhouette cutout instead of a rectangular strip");
        for (auto entity : cutouts)
        {
            const auto parent = layeredRegistry.get<Parent2DComponent>(entity).parent;
            passed &= expect(parent == layeredCard.root &&
                !layeredRegistry.all_of<Mask2DComponent>(parent),
                "backing cards must not be cropped to a straight horizontal strip");
        }
        DisplayTransition2DComponent scaleTransition {};
        scaleTransition.durationSeconds = 1.0f;
        scaleTransition.fromOpacity = 1.0f;
        scaleTransition.toOpacity = 1.0f;
        scaleTransition.fromScale = glm::vec2(0.8f);
        scaleTransition.toScale = glm::vec2(1.0f);
        scaleTransition.removeWhenComplete = false;
        layeredScene.play_display_transition(layeredCard.root, scaleTransition, 1.0);
        for (double now : { 1.0, 1.5, 2.0 })
        {
            const auto plan = layeredScene.build_render_plan(now, 0u);
            std::size_t shapeCutouts = 0u, blurCutouts = 0u;
            for (const auto& batch : plan.shapes)
            {
                if ((batch.flags & eRenderer2DStyleShapeCutout) == 0u) continue;
                ++shapeCutouts;
                const auto source = layeredRegistry.get<ShapeCutout2DComponent>(batch.entity).source;
                const auto sourceState = layeredScene.resolved_shape_visual_state(source, now);
                passed &= expect(sourceState && glm::length(batch.effect1 - sourceState->rect) < 0.01f,
                    "the curved cutout must follow the preceding card during scaled animation");
            }
            for (const auto& batch : plan.blurs)
                if ((batch.flags & eRenderer2DStyleShapeCutout) != 0u) ++blurCutouts;
            passed &= expect(shapeCutouts == 2u && blurCutouts == 2u,
                "both translucent fill and blur must exclude the preceding card");
        }
    }

    return passed ? 0 : 1;
}

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

    ui_prepare_notification_card_fan(
        ui,
        card,
        { 0.0f, -20.0f },
        10.0,
        0.1f);
    const VisualTransform2DComponent& visual =
        registry.get<VisualTransform2DComponent>(card.root);
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

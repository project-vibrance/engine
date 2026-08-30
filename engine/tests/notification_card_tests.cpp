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

    return passed ? 0 : 1;
}

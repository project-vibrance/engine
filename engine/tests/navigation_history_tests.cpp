#include <vibranceUI/ui/controls.h>

#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "navigation history test failed: " << message << '\n';
        }
        return condition;
    }
}

int main()
{
    Renderer2DScene scene;
    LayoutScale scale = {};
    scale.factor = { 1.0f, 1.0f };
    scale.contentScale = scale.factor;
    scale.logicalSize = { 640.0f, 480.0f };
    UiBuilder ui(scene, scale);
    const entt::entity root = ui.root();
    PointerInputEvent event = {};

    std::vector<std::size_t> visitedPages;
    UiIconButtonOptions backOptions = {};
    backOptions.useHistory = true;
    backOptions.historyDirection = UiNavigationDirection::eBack;
    backOptions.initialPage = 0u;
    backOptions.onNavigatePage = [&visitedPages](
        std::size_t page,
        const PointerInputEvent&) {
        visitedPages.push_back(page);
    };
    const UiControlHandle backButton = ui_create_icon_button(
        ui,
        root,
        UiAlignment::eTopLeft,
        { 8.0f, 8.0f },
        { 40.0f, 40.0f },
        2,
        0u,
        std::move(backOptions));

    entt::registry& registry = scene.registry();
    bool passed = true;
    const UiNavigationHistoryComponent* backHistory =
        registry.try_get<UiNavigationHistoryComponent>(backButton.root);
    passed &= expect(
        backHistory && backHistory->pages == std::vector<std::size_t> { 0u } &&
            backHistory->cursor == 0u,
        "a history-enabled icon button should own its initial page");
    passed &= expect(
        registry.try_get<UiNavClusterHistoryComponent>(backButton.root) != nullptr,
        "the nav-cluster component compatibility name should resolve the shared history");
    passed &= expect(
        !registry.get<ButtonInputComponent>(backButton.root).enabled,
        "a back button should start disabled when no previous page exists");

    passed &= expect(
        ui_navigation_push_history(scene, backButton.root, 1u, event),
        "pushing a page should succeed for a singular history owner");
    passed &= expect(
        registry.get<ButtonInputComponent>(backButton.root).enabled,
        "pushing a second page should enable the singular back button");
    passed &= expect(
        visitedPages == std::vector<std::size_t> { 1u },
        "pushing a page should invoke the page navigation callback");

    registry.get<ButtonInputComponent>(backButton.root).onClick(event);
    backHistory = registry.try_get<UiNavigationHistoryComponent>(backButton.root);
    passed &= expect(
        backHistory && backHistory->cursor == 0u &&
            visitedPages == std::vector<std::size_t> { 1u, 0u },
        "clicking the singular back button should visit the previous history entry");
    passed &= expect(
        !registry.get<ButtonInputComponent>(backButton.root).enabled,
        "the singular back button should disable again at the first entry");

    passed &= expect(
        ui_navigation_push_history(scene, backButton.root, 2u, event),
        "navigating after Back should append a replacement branch");
    backHistory = registry.try_get<UiNavigationHistoryComponent>(backButton.root);
    passed &= expect(
        backHistory && backHistory->pages == std::vector<std::size_t> { 0u, 2u } &&
            backHistory->cursor == 1u,
        "a new branch should discard the previous forward history");

    std::size_t forwardPage = 0u;
    UiIconButtonOptions forwardOptions = {};
    forwardOptions.useHistory = true;
    forwardOptions.historyDirection = UiNavigationDirection::eForward;
    forwardOptions.initialPage = 10u;
    forwardOptions.onNavigatePage = [&forwardPage](
        std::size_t page,
        const PointerInputEvent&) {
        forwardPage = page;
    };
    const UiControlHandle forwardButton = ui_create_icon_button(
        ui,
        root,
        UiAlignment::eTopRight,
        { -8.0f, 8.0f },
        { 40.0f, 40.0f },
        2,
        1u,
        std::move(forwardOptions));
    ui_navigation_push_history(scene, forwardButton.root, 11u, event);
    ui_navigation_go_back(scene, forwardButton.root, event);
    passed &= expect(
        registry.get<ButtonInputComponent>(forwardButton.root).enabled,
        "a forward-directed icon button should enable when forward history exists");
    registry.get<ButtonInputComponent>(forwardButton.root).onClick(event);
    passed &= expect(
        forwardPage == 11u,
        "clicking a forward-directed icon button should visit the next entry");

    return passed ? 0 : 1;
}

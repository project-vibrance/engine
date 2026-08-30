#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/page_stack.h>

#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "page stack test failed: " << message << '\n';
        }
        return condition;
    }

    bool visible(const Renderer2DScene& scene, entt::entity entity)
    {
        const RenderLayer2DComponent* layer =
            scene.registry().try_get<RenderLayer2DComponent>(entity);
        return layer && layer->visible;
    }
}

int main()
{
    enum class Page : std::size_t
    {
        eWelcome,
        eOptions,
        eSummary,
        eFinished
    };

    Renderer2DScene scene;
    LayoutScale scale = {};
    scale.factor = { 1.0f, 1.0f };
    scale.contentScale = scale.factor;
    scale.logicalSize = { 640.0f, 480.0f };
    UiBuilder ui(scene, scale);
    const entt::entity root = ui.root();

    const entt::entity welcome = ui_create_page_root(ui, root);
    const entt::entity options = ui_create_page_root(ui, root);
    const entt::entity summary = ui_create_page_root(ui, root);
    std::vector<UiPageId> activated;
    const UiPageStackHandle pages = ui_create_page_stack(
        scene,
        {
            { ui_page_id(Page::eWelcome), welcome },
            { ui_page_id(Page::eOptions), options },
            { ui_page_id(Page::eSummary), summary }
        },
        {
            .initialPage = ui_page_id(Page::eWelcome),
            .onPageActivated = [&activated](
                UiPageId page,
                const PointerInputEvent&) {
                activated.push_back(page);
            }
        });

    bool passed = true;
    passed &= expect(
        scene.registry().all_of<InputTransparent2DComponent>(welcome) &&
            scene.registry().get<Parent2DComponent>(welcome).parent == root,
        "page roots should fill their parent without intercepting empty-space input");
    passed &= expect(
        visible(scene, welcome) && !visible(scene, options) &&
            !visible(scene, summary),
        "creating a page stack should show only its initial page");
    passed &= expect(
        ui_page_stack_active_page(scene, pages) ==
            ui_page_id(Page::eWelcome) &&
            activated == std::vector<UiPageId> { ui_page_id(Page::eWelcome) },
        "the stack should expose and announce its initial page");

    PointerInputEvent event = {};
    const UiPageActivatedCallback navigate =
        ui_page_stack_navigation_callback(scene, pages);
    navigate(ui_page_id(Page::eSummary), event);
    passed &= expect(
        !visible(scene, welcome) && !visible(scene, options) &&
            visible(scene, summary),
        "the navigation callback should atomically switch visible roots");

    passed &= expect(
        !ui_page_stack_show(scene, pages, 999u, event) &&
            visible(scene, summary) &&
            ui_page_stack_active_page(scene, pages) ==
                ui_page_id(Page::eSummary),
        "an unknown page should leave the active page unchanged");

    const entt::entity finished = ui_create_page_root(ui, root);
    passed &= expect(
        ui_page_stack_add_page(
            scene,
            pages,
            { ui_page_id(Page::eFinished), finished }) &&
            !visible(scene, finished),
        "newly registered inactive pages should start hidden");
    passed &= expect(
        ui_page_stack_show(
            scene,
            pages,
            ui_page_id(Page::eFinished),
            event) &&
            visible(scene, finished) && !visible(scene, summary),
        "a page registered later should activate like an initial page");
    passed &= expect(
        !ui_page_stack_add_page(
            scene,
            pages,
            { ui_page_id(Page::eFinished), welcome }),
        "conflicting page IDs should be rejected");

    UiIconButtonOptions backOptions = {};
    backOptions.useHistory = true;
    backOptions.historyDirection = UiNavigationDirection::eBack;
    backOptions.initialPage = ui_page_id(Page::eWelcome);
    backOptions.onNavigatePage =
        ui_page_stack_navigation_callback(scene, pages);
    const UiControlHandle back = ui_create_icon_button(
        ui,
        root,
        UiAlignment::eTopLeft,
        { 8.0f, 8.0f },
        { 40.0f, 40.0f },
        5,
        0u,
        std::move(backOptions));

    ui_page_stack_show(scene, pages, ui_page_id(Page::eWelcome), event);
    ui_navigation_push_history(
        scene,
        back.root,
        ui_page_id(Page::eOptions),
        event);
    ui_navigation_push_history(
        scene,
        back.root,
        ui_page_id(Page::eSummary),
        event);
    scene.registry().get<ButtonInputComponent>(back.root).onClick(event);
    passed &= expect(
        ui_page_stack_active_page(scene, pages) ==
            ui_page_id(Page::eOptions) &&
            visible(scene, options) && !visible(scene, summary),
        "Back should reactivate the preceding page-stack entry");
    scene.registry().get<ButtonInputComponent>(back.root).onClick(event);
    passed &= expect(
        ui_page_stack_active_page(scene, pages) ==
            ui_page_id(Page::eWelcome) &&
            visible(scene, welcome) && !visible(scene, options),
        "Back should traverse every page in a multi-page flow");

    return passed ? 0 : 1;
}

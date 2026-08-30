#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <vector>

#include <entt/entt.hpp>

#include <vibranceUI/ui/builder.h>

using UiPageId = std::size_t;

template <typename PageEnum>
    requires std::is_enum_v<PageEnum>
constexpr UiPageId ui_page_id(PageEnum page) noexcept
{
    return static_cast<UiPageId>(page);
}

struct UiPageStackEntry
{
    // Every entity rendered for a page should descend from this root.
    UiPageId id = 0u;
    entt::entity root = entt::null;
};

using UiPageActivatedCallback =
    std::function<void(UiPageId, const PointerInputEvent&)>;

struct UiPageStackOptions
{
    UiPageId initialPage = 0u;
    UiPageActivatedCallback onPageActivated;
};

struct UiPageStackHandle
{
    // state is deliberately non-visual and may live outside every page root.
    entt::entity state = entt::null;

    explicit operator bool() const noexcept
    {
        return state != entt::null;
    }
};

struct UiPageStackComponent
{
    std::vector<UiPageStackEntry> pages;
    std::optional<UiPageId> activePage;
    UiPageActivatedCallback onPageActivated;
};

inline entt::entity ui_create_page_root(
    UiBuilder& ui,
    entt::entity parent,
    int32_t layer = 0,
    uint32_t order = 0u)
{
    // A page root paints nothing and cannot intercept input in gaps between
    // its controls. Hiding it also hides and disables its complete subtree.
    const entt::entity root = ui.layout_group(layer, order);
    ui.place(root).inside(parent).fill();
    return root;
}

inline UiPageStackComponent* ui_page_stack_component(
    Renderer2DScene& scene,
    UiPageStackHandle stack)
{
    entt::registry& registry = scene.registry();
    if (!stack || !registry.valid(stack.state))
    {
        return nullptr;
    }
    return registry.try_get<UiPageStackComponent>(stack.state);
}

inline const UiPageStackComponent* ui_page_stack_component(
    const Renderer2DScene& scene,
    UiPageStackHandle stack)
{
    const entt::registry& registry = scene.registry();
    if (!stack || !registry.valid(stack.state))
    {
        return nullptr;
    }
    return registry.try_get<UiPageStackComponent>(stack.state);
}

inline bool ui_page_stack_set_root_visible(
    Renderer2DScene& scene,
    entt::entity root,
    bool visible)
{
    entt::registry& registry = scene.registry();
    if (root == entt::null || !registry.valid(root))
    {
        return false;
    }

    RenderLayer2DComponent& layer =
        registry.get_or_emplace<RenderLayer2DComponent>(root);
    if (layer.visible != visible)
    {
        layer.visible = visible;
        scene.mark_dirty(root);
    }
    return true;
}

inline bool ui_page_stack_add_page(
    Renderer2DScene& scene,
    UiPageStackHandle stack,
    UiPageStackEntry page)
{
    UiPageStackComponent* component = ui_page_stack_component(scene, stack);
    if (!component ||
        page.root == entt::null ||
        !scene.registry().valid(page.root))
    {
        return false;
    }

    for (const UiPageStackEntry& registered : component->pages)
    {
        if (registered.id == page.id || registered.root == page.root)
        {
            // Re-registering the exact entry is harmless; conflicting IDs or
            // roots are rejected so visibility can never become ambiguous.
            return registered.id == page.id && registered.root == page.root;
        }
    }

    const bool visible =
        component->activePage && *component->activePage == page.id;
    ui_page_stack_set_root_visible(scene, page.root, visible);
    component->pages.push_back(page);
    return true;
}

inline bool ui_page_stack_show(
    Renderer2DScene& scene,
    UiPageStackHandle stack,
    UiPageId page,
    const PointerInputEvent& event)
{
    UiPageStackComponent* component = ui_page_stack_component(scene, stack);
    if (!component)
    {
        return false;
    }

    const auto requested = std::find_if(
        component->pages.begin(),
        component->pages.end(),
        [page](const UiPageStackEntry& entry) {
            return entry.id == page;
        });
    if (requested == component->pages.end() ||
        requested->root == entt::null ||
        !scene.registry().valid(requested->root))
    {
        // An unknown destination must not blank the currently visible page.
        return false;
    }

    for (const UiPageStackEntry& entry : component->pages)
    {
        ui_page_stack_set_root_visible(
            scene,
            entry.root,
            entry.id == page);
    }
    component->activePage = page;

    if (component->onPageActivated)
    {
        component->onPageActivated(page, event);
    }
    return true;
}

inline bool ui_page_stack_show(
    Renderer2DScene& scene,
    UiPageStackHandle stack,
    UiPageId page)
{
    return ui_page_stack_show(scene, stack, page, PointerInputEvent {});
}

inline UiPageStackHandle ui_create_page_stack(
    Renderer2DScene& scene,
    std::vector<UiPageStackEntry> pages,
    UiPageStackOptions options = {})
{
    UiPageStackHandle stack { scene.registry().create() };
    UiPageStackComponent component = {};
    component.onPageActivated = std::move(options.onPageActivated);
    scene.registry().emplace<UiPageStackComponent>(
        stack.state,
        std::move(component));

    for (UiPageStackEntry& page : pages)
    {
        ui_page_stack_add_page(scene, stack, page);
    }
    ui_page_stack_show(scene, stack, options.initialPage);
    return stack;
}

inline std::optional<UiPageId> ui_page_stack_active_page(
    const Renderer2DScene& scene,
    UiPageStackHandle stack)
{
    const UiPageStackComponent* component =
        ui_page_stack_component(scene, stack);
    return component ? component->activePage : std::nullopt;
}

inline UiPageActivatedCallback ui_page_stack_navigation_callback(
    Renderer2DScene& scene,
    UiPageStackHandle stack)
{
    // Renderer-owned callbacks have the same lifetime boundary as their scene.
    return [scenePtr = &scene, stack](
               UiPageId page,
               const PointerInputEvent& event) {
        ui_page_stack_show(*scenePtr, stack, page, event);
    };
}

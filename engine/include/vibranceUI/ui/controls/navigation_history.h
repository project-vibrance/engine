// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>

struct UiNavigationHistoryComponent
{

    std::vector<std::size_t> pages;
    std::size_t cursor = 0u;
    entt::entity backButton = entt::null;
    entt::entity forwardButton = entt::null;
    entt::entity backIcon = entt::null;
    entt::entity forwardIcon = entt::null;
    std::string iconTint = "#55585EFF";
    std::string disabledIconTint = "#B1B4BAFF";
    std::function<void(std::size_t, const PointerInputEvent&)> onNavigate;
};

inline void ui_update_navigation_history_visual(Renderer2DScene& scene, entt::entity entity)
{
    entt::registry& registry = scene.registry();
    UiNavigationHistoryComponent* history = registry.try_get<UiNavigationHistoryComponent>(entity);
    if (!history)
    {
        return;
    }

    const bool canGoBack = history->cursor > 0u && history->cursor < history->pages.size();
    const bool canGoForward = history->cursor + 1u < history->pages.size();
    if (ButtonInputComponent* back = registry.try_get<ButtonInputComponent>(history->backButton))
    {
        back->enabled = canGoBack;
    }
    if (ButtonInputComponent* forward = registry.try_get<ButtonInputComponent>(history->forwardButton))
    {
        forward->enabled = canGoForward;
    }
    if (history->backIcon != entt::null && registry.valid(history->backIcon))
    {
        ui_tint_icon_media(scene, history->backIcon, canGoBack ? history->iconTint : history->disabledIconTint);
        scene.mark_dirty(history->backIcon);
    }
    if (history->forwardIcon != entt::null && registry.valid(history->forwardIcon))
    {
        ui_tint_icon_media(scene, history->forwardIcon, canGoForward ? history->iconTint : history->disabledIconTint);
        scene.mark_dirty(history->forwardIcon);
    }
    ui_update_button_visual(scene, history->backButton);
    ui_update_button_visual(scene, history->forwardButton);
    scene.mark_dirty(entity);
}

inline bool ui_navigation_navigate_to_history(
    Renderer2DScene& scene,
    entt::entity entity,
    std::size_t cursor,
    const PointerInputEvent& event)
{
    UiNavigationHistoryComponent* history = scene.registry().try_get<UiNavigationHistoryComponent>(entity);
    if (!history || cursor >= history->pages.size())
    {
        return false;
    }

    history->cursor = cursor;
    ui_update_navigation_history_visual(scene, entity);
    if (history->onNavigate)
    {
        history->onNavigate(history->pages[history->cursor], event);
    }
    return true;
}

inline bool ui_navigation_go_back(Renderer2DScene& scene, entt::entity entity, const PointerInputEvent& event)
{
    const UiNavigationHistoryComponent* history = scene.registry().try_get<UiNavigationHistoryComponent>(entity);
    if (!history || history->cursor == 0u)
    {
        return false;
    }
    return ui_navigation_navigate_to_history(scene, entity, history->cursor - 1u, event);
}

inline bool ui_navigation_go_forward(Renderer2DScene& scene, entt::entity entity, const PointerInputEvent& event)
{
    const UiNavigationHistoryComponent* history = scene.registry().try_get<UiNavigationHistoryComponent>(entity);
    if (!history || history->cursor + 1u >= history->pages.size())
    {
        return false;
    }
    return ui_navigation_navigate_to_history(scene, entity, history->cursor + 1u, event);
}

inline bool ui_navigation_push_history(
    Renderer2DScene& scene,
    entt::entity entity,
    std::size_t page,
    const PointerInputEvent& event)
{
    UiNavigationHistoryComponent* history = scene.registry().try_get<UiNavigationHistoryComponent>(entity);
    if (!history)
    {
        return false;
    }

    if (history->pages.empty())
    {
        history->pages.push_back(page);
        history->cursor = 0u;
    }
    else if (history->cursor < history->pages.size() && history->pages[history->cursor] == page)
    {
        if (history->onNavigate)
        {
            history->onNavigate(page, event);
        }
        ui_update_navigation_history_visual(scene, entity);
        return true;
    }
    else
    {
        if (history->cursor + 1u < history->pages.size())
        {
            history->pages.erase(history->pages.begin() + static_cast<std::ptrdiff_t>(history->cursor + 1u), history->pages.end());
        }
        history->pages.push_back(page);
        history->cursor = history->pages.size() - 1u;
    }

    ui_update_navigation_history_visual(scene, entity);
    if (history->onNavigate)
    {
        history->onNavigate(page, event);
    }
    return true;
}


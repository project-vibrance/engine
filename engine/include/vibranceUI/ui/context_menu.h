#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/renderer/media2d.h>
#include <vibranceUI/ui/builder.h>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class UiContextMenuItemRole
{
    eAction,
    eHeading
};

struct UiContextMenuItem
{
    std::string id {};
    std::string label {};
    std::string shortcut {};
    Media2DHandle leadingIcon {};
    UiContextMenuItemRole role = UiContextMenuItemRole::eAction;
    bool enabled = true;
    bool destructive = false;
    bool separatorBefore = false;
    std::vector<UiContextMenuItem> children {};
    std::function<void()> onSelected {};
};

struct UiContextMenuMetrics
{
    float width = 300.0f;
    float rowHeight = 38.0f;
    float headingHeight = 30.0f;
    float rowGap = 1.0f;
    float separatorGap = 12.0f;
    glm::vec4 padding { 10.0f };
};

struct UiContextMenuRowPlacement
{
    float y = 0.0f;
    float height = 0.0f;
    std::optional<float> separatorY {};
};

struct UiContextMenuLayout
{
    std::vector<UiContextMenuRowPlacement> rows {};
    glm::vec2 size { 0.0f };
};

// Pure measurement is shared by transient hosts, embedded menus, and tests.
VIBRANCE_ENGINE_API UiContextMenuLayout ui_context_menu_layout(
    const std::vector<UiContextMenuItem>& items,
    const UiContextMenuMetrics& metrics = {});

struct UiContextMenuState
{
    std::string openSubmenuId {};
    std::function<void()> onStructureChanged {};
};

struct UiContextMenuOptions
{
    UiContextMenuMetrics metrics {};
    bool drawSurface = true;
    // A host can render the cascading child in its own native window while
    // retaining the same row/state behavior in the primary menu.
    bool buildInlineSubmenu = true;
    float cornerRadius = 18.0f;
    float submenuOverlap = 5.0f;
    float fontSize = 17.0f;
    float headingFontSize = 13.0f;
    float iconSize = 17.0f;
    float leadingInset = 13.0f;
    float trailingInset = 13.0f;
    float iconTextGap = 10.0f;
    glm::vec2 shadowOffset { 0.0f, 4.0f };
    float shadowBlurRadius = 18.0f;
    float shadowSpread = 1.0f;
    std::string surfaceTop = "rgba(241, 241, 238, 0.88)";
    std::string surfaceBottom = "rgba(211, 214, 218, 0.82)";
    std::string surfaceOutline = "rgba(255, 255, 255, 0.68)";
    std::string textColor = "#202124FF";
    std::string headingColor = "rgba(73, 76, 82, 0.56)";
    std::string disabledTextColor = "rgba(58, 60, 64, 0.36)";
    std::string destructiveTextColor = "#B3261EFF";
    std::string selectedTextColor = "#FFFFFFFF";
    std::string hoveredColor = "rgba(20, 112, 222, 0.96)";
    std::string pressedColor = "rgba(7, 91, 197, 1)";
    std::string separatorColor = "rgba(72, 76, 84, 0.18)";
    int32_t layer = 20;
    uint32_t order = 0u;
};

struct UiContextMenuHandle
{
    entt::entity root = entt::null;
    entt::entity submenuRoot = entt::null;
    std::vector<entt::entity> rows {};
    UiContextMenuLayout layout {};
};

// Compact desktop-popup preset shared by the tray menu and native context
// menu hosts. Callers may still replace metrics or colors after construction.
VIBRANCE_ENGINE_API UiContextMenuOptions ui_compact_context_menu_options(
    float menuWidth = 168.0f);

// Builds one menu and, unless disabled in the options, its selected first
// cascading child menu.
VIBRANCE_ENGINE_API UiContextMenuHandle ui_create_context_menu(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    const std::vector<UiContextMenuItem>& items,
    std::shared_ptr<UiContextMenuState> state = {},
    UiContextMenuOptions options = {});

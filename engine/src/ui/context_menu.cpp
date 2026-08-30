#include <vibranceUI/ui/context_menu.h>

#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/glass.h>
#include <vibranceUI/ui/stack_layout.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/surfaces.h>

#include <algorithm>
#include <utility>

namespace
{
    struct MenuLevelResult
    {
        entt::entity root = entt::null;
        std::vector<entt::entity> rows {};
        UiContextMenuLayout layout {};
    };

    ShapeStyleComponent menu_surface_style(
        const UiContextMenuOptions& options)
    {
        return make_frosted_panel_style(
            options.surfaceTop,
            options.surfaceBottom,
            options.surfaceOutline,
            0.8f,
            1.0f,
            22.0f,
            2u,
            0.94f);
    }

    TextStyleComponent menu_text_style(
        std::string_view color,
        float weight)
    {
        TextStyleComponent style = ui_macos26_text_style(color);
        style.fontWeight = weight;
        return style;
    }

    void set_text_color(
        Renderer2DScene& scene,
        entt::entity entity,
        std::string_view color)
    {
        if (entity == entt::null || !scene.registry().valid(entity))
        {
            return;
        }
        if (TextStyleComponent* style =
            scene.registry().try_get<TextStyleComponent>(entity))
        {
            style->set_color(color);
            scene.mark_dirty(entity);
        }
    }

    MenuLevelResult create_menu_level(
        UiBuilder& ui,
        const Renderer2DFontAtlas& fontAtlas,
        entt::entity parent,
        UiAlignment alignment,
        glm::vec2 offset,
        const std::vector<UiContextMenuItem>& items,
        const std::shared_ptr<UiContextMenuState>& state,
        const UiContextMenuOptions& options,
        bool controlsSubmenu)
    {
        MenuLevelResult result = {};
        result.layout = ui_context_menu_layout(items, options.metrics);

        UiSurfaceBlockOptions surface = {};
        surface.style = options.drawSurface ?
            menu_surface_style(options) :
            ui_clear_surface_style();
        surface.primitive = Renderer2DPrimitive::eSquircle;
        surface.cornerRadius = options.cornerRadius;
        surface.squircleAmount = 0.82f;
        surface.squirclePower = 4.2f;
        surface.layer = options.layer;
        surface.order = options.order;
        // The surface is the background of this level.  Marking it
        // always-on-top puts it in the renderer's final pass and covers the
        // ordinary-layer rows that belong above it.
        surface.alwaysOnTop = false;
        result.root = ui_create_surface_block(
            ui,
            parent,
            alignment,
            offset,
            result.layout.size,
            surface);
        if (options.drawSurface)
        {
            ui_apply_glass_material(
                ui,
                result.root,
                ui_system_glass_options(
                    22.0f,
                    1.06f,
                    { 0.98f, 0.985f, 1.0f, 0.035f }));
            ShadowComponent shadow = {};
            shadow.set_color("rgba(0, 0, 0, 0.34)");
            shadow.offset = scaled_offset(
                options.shadowOffset.x,
                options.shadowOffset.y,
                ui.scale());
            shadow.blurRadius = scaled_scalar(
                options.shadowBlurRadius,
                ui.scale());
            shadow.spread = scaled_scalar(
                options.shadowSpread,
                ui.scale());
            shadow.opacity = 1.0f;
            ui.registry().emplace_or_replace<ShadowComponent>(
                result.root,
                shadow);
        }

        UiSeparatorOptions separatorOptions = {};
        separatorOptions.color = options.separatorColor;
        separatorOptions.thickness = 1.0f;
        separatorOptions.inset = {
            std::max(options.metrics.padding.x, 8.0f),
            std::max(options.metrics.padding.z, 8.0f)
        };

        const float rowWidth = std::max(
            result.layout.size.x - options.metrics.padding.x -
                options.metrics.padding.z,
            1.0f);
        for (std::size_t index = 0u; index < items.size(); ++index)
        {
            const UiContextMenuItem& item = items[index];
            const UiContextMenuRowPlacement& placement =
                result.layout.rows[index];
            const uint32_t order = options.order +
                static_cast<uint32_t>(index) * 12u + 1u;
            if (placement.separatorY)
            {
                ui_create_separator(
                    ui,
                    result.root,
                    *placement.separatorY + options.metrics.padding.y,
                    options.layer + 1,
                    order,
                    separatorOptions);
            }

            if (item.role == UiContextMenuItemRole::eHeading)
            {
                ui.create_aligned_text(
                    item.label,
                    fontAtlas,
                    result.root,
                    UiAlignment::eTopLeft,
                    options.headingFontSize,
                    menu_text_style(options.headingColor, 620.0f),
                    options.layer + 2,
                    order + 1u,
                    {
                        options.metrics.padding.x + options.leadingInset,
                        options.metrics.padding.y + placement.y +
                            std::max(
                                (placement.height - options.headingFontSize) *
                                    0.5f,
                                0.0f)
                    });
                continue;
            }

            const bool submenuOpen = controlsSubmenu && state &&
                state->openSubmenuId == item.id && !item.children.empty();
            ShapeStyleComponent idle = make_solid_style(
                submenuOpen ? options.hoveredColor :
                    "rgba(0, 0, 0, 0)");
            ShapeStyleComponent hovered = make_solid_style(
                options.hoveredColor);
            ShapeStyleComponent pressed = make_solid_style(
                options.pressedColor);
            ShapeStyleComponent disabled = make_solid_style(
                "rgba(0, 0, 0, 0)");

            entt::entity row = ui.scene().create_shape(
                { 0.0f, 0.0f },
                scaled_size(rowWidth, placement.height, ui.scale()),
                item.enabled ? idle : disabled,
                Renderer2DPrimitive::eRoundedRectangle);
            ui.set_shape(row, std::min(9.0f, placement.height * 0.28f));
            ui.set_layer(row, options.layer + 2, order);
            ui.attach_aligned(
                row,
                result.root,
                UiAlignment::eTopLeft,
                scaled_offset(
                    options.metrics.padding.x,
                    options.metrics.padding.y + placement.y,
                    ui.scale()),
                scaled_size(rowWidth, placement.height, ui.scale()));

            // Idle menu rows are intentionally transparent. Give the row an
            // explicit hit surface so its entire width remains interactive;
            // otherwise only opaque descendants (the leading icon and text)
            // participate in picking, which makes the empty trailing half of
            // a row appear unresponsive.
            ui.registry().emplace<HitRegion2DComponent>(row);

            ButtonVisualComponent visual = {};
            visual.idle = idle;
            visual.hovered = hovered;
            visual.pressed = pressed;
            visual.disabled = disabled;
            visual.hasDisabled = true;
            ui.registry().emplace<ButtonVisualComponent>(row, visual);

            entt::entity icon = entt::null;
            float labelInset = options.leadingInset;
            if (item.leadingIcon.valid())
            {
                icon = ui.create_aligned_media(
                    item.leadingIcon,
                    row,
                    UiAlignment::eMiddleLeft,
                    { options.iconSize, options.iconSize },
                    options.layer + 3,
                    order,
                    { options.leadingInset, 0.0f });
                labelInset += options.iconSize + options.iconTextGap;
            }

            const std::string idleTextColor = !item.enabled ?
                options.disabledTextColor :
                (item.destructive ? options.destructiveTextColor :
                    options.textColor);
            const std::string initialTextColor = submenuOpen ?
                options.selectedTextColor : idleTextColor;
            entt::entity label = ui.create_aligned_text(
                item.label,
                fontAtlas,
                row,
                UiAlignment::eMiddleLeft,
                options.fontSize,
                menu_text_style(initialTextColor, 470.0f),
                options.layer + 3,
                order + 1u,
                { labelInset, 0.0f });

            std::string trailing = !item.children.empty() ?
                "›" : item.shortcut;
            if (!item.children.empty())
            {
                // Keep cascading indicators inside the guaranteed desktop
                // font set; legacy source encoding can otherwise become a
                // replacement question mark.
                trailing = ">";
            }
            entt::entity trailingLabel = entt::null;
            if (!trailing.empty())
            {
                trailingLabel = ui.create_aligned_text(
                    trailing,
                    fontAtlas,
                    row,
                    UiAlignment::eMiddleRight,
                    !item.children.empty() ?
                        options.fontSize + 4.0f : options.fontSize - 1.0f,
                    menu_text_style(initialTextColor, 470.0f),
                    options.layer + 3,
                    order + 2u,
                    { -options.trailingInset, 0.0f });
            }
            if (icon != entt::null)
            {
                ui_tint_icon_media(ui.scene(), icon, initialTextColor);
            }

            ButtonInputComponent input = {};
            input.enabled = item.enabled;
            input.onClick = [item, state, controlsSubmenu](
                const PointerInputEvent&) {
                if (!item.children.empty())
                {
                    if (controlsSubmenu && state &&
                        state->openSubmenuId != item.id)
                    {
                        state->openSubmenuId = item.id;
                        if (state->onStructureChanged)
                        {
                            state->onStructureChanged();
                        }
                    }
                    return;
                }
                if (item.onSelected)
                {
                    item.onSelected();
                }
            };
            Renderer2DScene* scene = &ui.scene();
            input.onHoverChanged = [
                scene,
                label,
                trailingLabel,
                icon,
                idleTextColor,
                selectedTextColor = options.selectedTextColor,
                submenuOpen,
                itemId = item.id,
                hasChildren = !item.children.empty(),
                state,
                controlsSubmenu](bool isHovered) {
                const std::string& color = isHovered || submenuOpen ?
                    selectedTextColor : idleTextColor;
                set_text_color(*scene, label, color);
                set_text_color(*scene, trailingLabel, color);
                if (icon != entt::null)
                {
                    ui_tint_icon_media(*scene, icon, color);
                }
                if (!isHovered || !controlsSubmenu || !state)
                {
                    return;
                }
                const std::string nextOpenId = hasChildren ?
                    itemId : std::string {};
                if (state->openSubmenuId == nextOpenId)
                {
                    return;
                }
                state->openSubmenuId = nextOpenId;
                if (state->onStructureChanged)
                {
                    state->onStructureChanged();
                }
            };
            ui.registry().emplace<ButtonInputComponent>(row, std::move(input));
            ui_update_button_visual(ui.scene(), row);
            result.rows.push_back(row);
        }
        return result;
    }
}

UiContextMenuLayout ui_context_menu_layout(
    const std::vector<UiContextMenuItem>& items,
    const UiContextMenuMetrics& metrics)
{
    UiContextMenuLayout result = {};
    result.rows.reserve(items.size());
    UiStackLayout stack({
        .axis = UiStackAxis::eVertical,
        .gap = std::max(metrics.rowGap, 0.0f)
    });
    for (std::size_t index = 0u; index < items.size(); ++index)
    {
        const UiContextMenuItem& item = items[index];
        const float height = item.role == UiContextMenuItemRole::eHeading ?
            std::max(metrics.headingHeight, 1.0f) :
            std::max(metrics.rowHeight, 1.0f);
        const float separatorGap = index > 0u && item.separatorBefore ?
            std::max(metrics.separatorGap, 0.0f) : 0.0f;
        const UiStackPlacement placement = stack.append(
            { std::max(metrics.width, 1.0f), height },
            separatorGap);
        UiContextMenuRowPlacement row = {};
        row.y = placement.offset.y;
        row.height = height;
        if (separatorGap > 0.0f)
        {
            row.separatorY = row.y - separatorGap * 0.5f;
        }
        result.rows.push_back(row);
    }
    result.size = {
        std::max(metrics.width, 1.0f),
        stack.content_size().y + std::max(metrics.padding.y, 0.0f) +
            std::max(metrics.padding.w, 0.0f)
    };
    return result;
}

UiContextMenuOptions ui_compact_context_menu_options(float menuWidth)
{
    UiContextMenuOptions options = {};
    options.metrics.width = std::max(menuWidth, 1.0f);
    options.metrics.rowHeight = 20.0f;
    options.metrics.headingHeight = 18.0f;
    options.metrics.rowGap = 2.0f;
    options.metrics.separatorGap = 4.0f;
    options.metrics.padding = glm::vec4(6.0f);
    options.cornerRadius = 14.0f;
    options.fontSize = 12.0f;
    options.headingFontSize = 10.0f;
    options.iconSize = 15.0f;
    options.leadingInset = 7.0f;
    options.trailingInset = 7.0f;
    options.iconTextGap = 7.0f;
    // Compact popups commonly live in a native window with only a six-pixel
    // transparent gutter. Keep the complete soft edge inside that host.
    options.shadowOffset = { 0.0f, 1.0f };
    options.shadowBlurRadius = 4.0f;
    options.shadowSpread = 0.0f;
    options.surfaceTop = "rgba(218, 220, 221, 0.78)";
    options.surfaceBottom = "rgba(181, 185, 188, 0.72)";
    options.surfaceOutline = "rgba(255, 255, 255, 0.56)";
    options.hoveredColor = "rgba(104, 108, 114, 0.34)";
    options.pressedColor = "rgba(82, 86, 92, 0.46)";
    options.separatorColor = "rgba(72, 76, 84, 0.18)";
    return options;
}

UiContextMenuHandle ui_create_context_menu(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    const std::vector<UiContextMenuItem>& items,
    std::shared_ptr<UiContextMenuState> state,
    UiContextMenuOptions options)
{
    UiContextMenuHandle handle = {};
    MenuLevelResult primary = create_menu_level(
        ui,
        fontAtlas,
        parent,
        alignment,
        offset,
        items,
        state,
        options,
        true);
    handle.root = primary.root;
    handle.rows = std::move(primary.rows);
    handle.layout = primary.layout;

    if (!options.buildInlineSubmenu || !state ||
        state->openSubmenuId.empty())
    {
        return handle;
    }
    const auto openItem = std::find_if(
        items.begin(),
        items.end(),
        [&state](const UiContextMenuItem& item) {
            return item.id == state->openSubmenuId &&
                !item.children.empty();
        });
    if (openItem == items.end())
    {
        return handle;
    }
    const std::size_t index = static_cast<std::size_t>(
        std::distance(items.begin(), openItem));
    UiContextMenuOptions submenuOptions = options;
    submenuOptions.metrics.width = std::max(
        options.metrics.width - 6.0f,
        180.0f);
    submenuOptions.layer += 10;
    submenuOptions.order += 1000u;
    const float submenuY = std::max(
        0.0f,
        primary.layout.rows[index].y + options.metrics.padding.y - 8.0f);
    MenuLevelResult submenu = create_menu_level(
        ui,
        fontAtlas,
        primary.root,
        UiAlignment::eTopLeft,
        {
            primary.layout.size.x - options.submenuOverlap,
            submenuY
        },
        openItem->children,
        state,
        submenuOptions,
        false);
    handle.submenuRoot = submenu.root;
    handle.rows.insert(
        handle.rows.end(),
        submenu.rows.begin(),
        submenu.rows.end());
    return handle;
}

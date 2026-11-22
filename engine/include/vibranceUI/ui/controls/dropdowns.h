// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/buttons.h>
#include <vibranceUI/ui/controls/common.h>

struct UiDropdownControlComponent
{

    std::vector<std::string> items;
    int selectedIndex = 0;
    bool open = false;
    std::size_t firstVisibleIndex = 0u;
    uint32_t visibleItemCount = 0u;
    entt::entity label = entt::null;
    entt::entity menuRoot = entt::null;
    std::vector<entt::entity> itemRows;
    std::vector<entt::entity> itemLabels;
    std::vector<entt::entity> checkIcons;
    std::size_t maxSelectedCharacters = 0u;
    std::size_t maxRowCharacters = 0u;
    float selectedFontSize = 13.5f;
    float selectedTextAvailableWidth = 0.0f;
    float rowFontSize = 12.5f;
    float rowTextAvailableWidth = 0.0f;
    std::function<void(int, std::string_view, const PointerInputEvent&)> onChanged;
};

struct UiDropdownOptions
{

    Media2DHandle chevronIcon {};
    Media2DHandle checkIcon {};
    int selectedIndex = 0;
    bool enabled = true;
    Renderer2DPrimitive buttonPrimitive = UiControlAppearance::buttonPrimitive;
    float buttonCornerRadius = UiControlAppearance::buttonCornerRadius;
    float buttonSquircleAmount = UiControlAppearance::buttonSquircleAmount;
    float buttonSquirclePower = UiControlAppearance::buttonSquirclePower;
    float buttonFontSize = UiControlAppearance::buttonFontSize;
    float buttonFontWeight = UiControlAppearance::buttonFontWeight;
    float buttonIconSize = 14.0f;

    glm::vec4 buttonPadding { 4.0f, 0.0f, 4.0f, 0.0f };
    float buttonTextIconGap = 18.0f;
    std::string buttonTextColor = "#1D1D1FFF";
    std::string buttonIconTint = "rgba(29, 29, 31, 0.85)";
    std::string buttonBackgroundColor = "rgba(231, 231, 234, 0.94)";
    std::string buttonHoveredBackgroundColor = "rgba(231, 231, 234, 0.94)";
    std::string buttonPressedBackgroundColor = "#FAFAFBFF";
    std::string buttonOutlineColor = "#00000000";
    std::string buttonHoveredOutlineColor = "#00000000";
    std::string buttonPressedOutlineColor = "#00000000";
    float buttonOutlineWidth = 0.0f;
    float buttonHoveredOutlineWidth = 0.0f;
    float buttonPressedOutlineWidth = 0.0f;
    float buttonOpacity = 1.0f;
    bool customButtonLabelPlacement = true;
    UiAlignment buttonLabelAlignment = UiAlignment::eMiddleLeft;
    glm::vec2 buttonLabelOffset { 8.0f, 0.0f };
    std::size_t maxSelectedCharacters = 0u;
    float rowHeight = UiControlAppearance::menuRowHeight;
    float menuGap = 4.0f;
    float menuWidth = 0.0f;
    uint32_t maxVisibleItems = 8u;
    float scrollStepRows = 1.0f;
    std::size_t maxRowCharacters = 0u;
    bool openAbove = false;
    std::function<void(int, std::string_view, const PointerInputEvent&)> onChanged;
    std::string backgroundColor = "rgba(231, 231, 234, 0.94)";
    std::string menuOutlineColor = "rgba(0, 0, 0, 0)";
    float menuOutlineWidth = 0.0f;
    std::string rowTextColor = "#1D1D1FFF";
    std::string rowHoveredTextColor = "#1D1D1FFF";
    std::string rowCheckTint = "#1D1D1FFF";
    std::string rowHoveredCheckTint = "#1D1D1FFF";
    std::string rowHoveredColor = "#00000000";
    std::string rowPressedColor = "#0068DDFF";
};

inline UiDropdownOptions ui_dropdown_options_from_theme(
    const UiTheme& theme,
    Media2DHandle chevronIcon = {},
    Media2DHandle checkIcon = {})
{

    UiDropdownOptions options = {};
    options.chevronIcon = chevronIcon;
    options.checkIcon = checkIcon;
    options.buttonTextColor = theme.text;
    options.buttonIconTint = theme.icon;
    options.buttonBackgroundColor = theme.surfaceMuted;
    options.buttonHoveredBackgroundColor = options.buttonBackgroundColor;
    options.buttonPressedBackgroundColor = theme.settingsPanel.cardSurface;
    options.backgroundColor = theme.surfaceMuted;
    options.rowHoveredColor = "#00000000";
    options.rowPressedColor = theme.accentPressed;
    options.rowTextColor = theme.text;
    options.rowHoveredTextColor = options.rowTextColor;
    options.rowCheckTint = theme.text;
    options.rowHoveredCheckTint = options.rowCheckTint;
    return options;
}

inline std::string ui_dropdown_fit_text(
    const Renderer2DFontAtlas& fontAtlas,
    std::string_view value,
    float fontSize,
    float availableWidth,
    std::size_t maxCharacters)
{

    return availableWidth > 0.0f ?
        ui_truncate_text_to_width_with_ellipsis(fontAtlas, value, fontSize, availableWidth, maxCharacters) :
        ui_truncate_text_with_ellipsis(value, maxCharacters);
}

inline void ui_refresh_dropdown_rows(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity entity)
{

    entt::registry& registry = scene.registry();
    UiDropdownControlComponent* dropdown = registry.try_get<UiDropdownControlComponent>(entity);
    if (!dropdown || dropdown->items.empty())
    {
        return;
    }

    const std::size_t rowCount = dropdown->itemRows.size();
    const std::size_t maxFirstVisible =
        dropdown->items.size() > rowCount ?
            dropdown->items.size() - rowCount :
            0u;
    dropdown->firstVisibleIndex = std::min(dropdown->firstVisibleIndex, maxFirstVisible);
    if (ScrollInputComponent* scroll = registry.try_get<ScrollInputComponent>(dropdown->menuRoot))
    {
        scroll->maxOffset = static_cast<float>(maxFirstVisible);
        scroll->offset = std::clamp(scroll->offset, scroll->minOffset, scroll->maxOffset);
    }

    for (std::size_t slot = 0u; slot < rowCount; ++slot)
    {
        const std::size_t itemIndex = dropdown->firstVisibleIndex + slot;
        const bool rowVisible = itemIndex < dropdown->items.size();
        const entt::entity row = dropdown->itemRows[slot];
        if (RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(row))
        {
            layer->visible = rowVisible;
        }
        if (ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(row))
        {
            button->enabled = rowVisible;
            button->hovered = false;
            button->leftPressed = false;
            button->rightPressed = false;
            if (button->onHoverChanged)
            {
                button->onHoverChanged(false);
            }
            ui_update_button_visual(scene, row);
        }
        scene.mark_dirty(row);

        if (slot < dropdown->itemLabels.size())
        {
            const entt::entity label = dropdown->itemLabels[slot];
            if (rowVisible)
            {
                set_text_entity(
                    scene,
                    fontAtlas,
                    label,
                    ui_dropdown_fit_text(
                        fontAtlas,
                        dropdown->items[itemIndex],
                        dropdown->rowFontSize,
                        dropdown->rowTextAvailableWidth,
                        dropdown->maxRowCharacters));
            }
            if (RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(label))
            {
                layer->visible = rowVisible;
            }
            scene.mark_dirty(label);
        }

        if (slot < dropdown->checkIcons.size())
        {
            const entt::entity check = dropdown->checkIcons[slot];
            if (check != entt::null && registry.valid(check))
            {
                if (RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(check))
                {
                    layer->visible = rowVisible && itemIndex == static_cast<std::size_t>(dropdown->selectedIndex);
                }
                scene.mark_dirty(check);
            }
        }
    }
}

inline void ui_set_dropdown_selected(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity entity,
    int selectedIndex)
{
    entt::registry& registry = scene.registry();
    UiDropdownControlComponent* dropdown = registry.try_get<UiDropdownControlComponent>(entity);
    if (!dropdown || dropdown->items.empty())
    {
        return;
    }

    dropdown->selectedIndex = std::clamp(selectedIndex, 0, static_cast<int>(dropdown->items.size()) - 1);
    if (!dropdown->itemRows.empty())
    {

        const std::size_t selected = static_cast<std::size_t>(dropdown->selectedIndex);
        const std::size_t visibleCount = dropdown->itemRows.size();
        if (selected < dropdown->firstVisibleIndex)
        {
            dropdown->firstVisibleIndex = selected;
        }
        else if (selected >= dropdown->firstVisibleIndex + visibleCount)
        {
            dropdown->firstVisibleIndex = selected - visibleCount + 1u;
        }
    }
    set_text_entity(
        scene,
        fontAtlas,
        dropdown->label,
        ui_dropdown_fit_text(
            fontAtlas,
            dropdown->items[static_cast<std::size_t>(dropdown->selectedIndex)],
            dropdown->selectedFontSize,
            dropdown->selectedTextAvailableWidth,
            dropdown->maxSelectedCharacters));

    ui_refresh_dropdown_rows(scene, fontAtlas, entity);
    scene.mark_dirty(entity);
}

inline void ui_set_dropdown_open(Renderer2DScene& scene, entt::entity entity, bool open)
{

    entt::registry& registry = scene.registry();
    UiDropdownControlComponent* dropdown = registry.try_get<UiDropdownControlComponent>(entity);
    if (!dropdown)
    {
        return;
    }

    dropdown->open = open;
    if (auto* outside = registry.try_get<OutsidePointerInputComponent>(entity))
        outside->enabled = open;
    if (dropdown->menuRoot != entt::null && registry.valid(dropdown->menuRoot))
    {
        if (RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(dropdown->menuRoot))
        {
            layer->visible = open;
        }
        scene.activate_dynamic(dropdown->menuRoot, 0.12f);
        scene.mark_dirty(dropdown->menuRoot);
    }
    auto refresh_entity = [&](entt::entity child) {
        if (child == entt::null || !registry.valid(child))
        {
            return;
        }
        scene.activate_dynamic(child, 0.12f);
        scene.mark_dirty(child);
    };
    for (entt::entity row : dropdown->itemRows)
    {
        if (!open)
        {
            if (ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(row))
            {
                button->hovered = false;
                button->leftPressed = false;
                button->rightPressed = false;
                if (button->onHoverChanged)
                {
                    button->onHoverChanged(false);
                }
                ui_update_button_visual(scene, row);
            }
        }
        refresh_entity(row);
    }
    for (entt::entity label : dropdown->itemLabels)
    {
        refresh_entity(label);
    }
    for (entt::entity check : dropdown->checkIcons)
    {
        refresh_entity(check);
    }
    scene.mark_dirty(entity);
}

inline void ui_close_other_dropdowns(Renderer2DScene& scene, entt::entity keepOpen = entt::null)
{

    auto view = scene.registry().view<UiDropdownControlComponent>();
    view.each([&](entt::entity entity, UiDropdownControlComponent& dropdown) {
        if (entity == keepOpen || !dropdown.open)
        {
            return;
        }
        ui_set_dropdown_open(scene, entity, false);
    });
}

inline UiControlHandle ui_create_dropdown(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    std::vector<std::string> items,
    int32_t layer,
    uint32_t order,
    UiDropdownOptions options = {})
{
    const int safeIndex = items.empty() ?
        0 :
        std::clamp(options.selectedIndex, 0, static_cast<int>(items.size()) - 1);
    const float menuWidth = options.menuWidth > 0.0f ? options.menuWidth : size.x;
    const uint32_t visibleItemCount = items.empty() ?
        0u :
        std::min<uint32_t>(
            static_cast<uint32_t>(items.size()),
            std::max(options.maxVisibleItems, 1u));
    const float menuHeight = options.rowHeight * static_cast<float>(visibleItemCount) + 8.0f;
    UiButtonOptions buttonOptions = {};
    buttonOptions.trailingIcon = options.chevronIcon;
    buttonOptions.enabled = options.enabled;
    buttonOptions.cornerRadius = options.buttonCornerRadius;
    buttonOptions.fontSize = options.buttonFontSize;
    buttonOptions.iconSize = options.buttonIconSize;
    buttonOptions.padding = options.buttonPadding;

    UiControlHandle handle = ui_create_aligned_text_button(
        ui,
        fontAtlas,
        parent,
        alignment,
        offset,
        items.empty() ? std::string() : items[static_cast<std::size_t>(safeIndex)],
        size,
        layer,
        order,
        buttonOptions);

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    if (ShapeComponent* buttonShape = registry.try_get<ShapeComponent>(handle.root))
    {
        buttonShape->primitive = options.buttonPrimitive;
        buttonShape->set_corner_radius(scaled_scalar(options.buttonCornerRadius, ui.scale()));
        if (options.buttonPrimitive == Renderer2DPrimitive::eSquircle)
        {
            buttonShape->squircleAmount = std::clamp(options.buttonSquircleAmount, 0.0f, 1.0f);
            buttonShape->squirclePower = std::clamp(options.buttonSquirclePower, 2.0f, 5.0f);
        }
        scene.mark_dirty(handle.root);
    }
    if (ButtonVisualComponent* visual = registry.try_get<ButtonVisualComponent>(handle.root))
    {
        visual->idle = ui_macos26_control_style(
            ui,
            options.buttonBackgroundColor,
            options.buttonOutlineColor,
            options.buttonOutlineWidth,
            options.buttonOpacity);
        visual->hovered = ui_macos26_control_style(
            ui,
            options.buttonHoveredBackgroundColor,
            options.buttonHoveredOutlineColor,
            options.buttonHoveredOutlineWidth,
            options.buttonOpacity);
        visual->pressed = ui_macos26_control_style(
            ui,
            options.buttonPressedBackgroundColor,
            options.buttonPressedOutlineColor,
            options.buttonPressedOutlineWidth,
            options.buttonOpacity);
        ui_update_button_visual(scene, handle.root);
    }
    if (TextStyleComponent* labelStyle = registry.try_get<TextStyleComponent>(handle.label))
    {
        *labelStyle = ui_macos26_text_style(options.buttonTextColor);
        if (options.buttonFontWeight > 0.0f)
        {
            labelStyle->set_font_weight(options.buttonFontWeight);
        }
        scene.mark_dirty(handle.label);
    }
    if (options.customButtonLabelPlacement)
    {
        if (Layout2DComponent* labelLayout = registry.try_get<Layout2DComponent>(handle.label))
        {
            labelLayout->anchorMin = ui_alignment_anchor(options.buttonLabelAlignment);
            labelLayout->anchorMax = labelLayout->anchorMin;
            labelLayout->pivot = ui_alignment_pivot(options.buttonLabelAlignment);
            labelLayout->offset = scaled_offset(options.buttonLabelOffset.x, options.buttonLabelOffset.y, ui.scale());
            if (options.buttonLabelAlignment == UiAlignment::eMiddleLeft &&
                handle.trailingIcon != entt::null &&
                registry.valid(handle.trailingIcon))
            {
                const float textIconGap = std::max(
                    options.buttonTextIconGap,
                    std::max(options.buttonPadding.x, options.buttonPadding.z) + 1.0f);
                const float reservedRight = options.buttonPadding.z + options.buttonIconSize + textIconGap;
                labelLayout->size.x = scaled_scalar(
                    std::max(size.x - options.buttonLabelOffset.x - reservedRight, 1.0f),
                    ui.scale());
            }
            scene.mark_dirty(handle.label);
        }
    }
    if (handle.trailingIcon != entt::null && registry.valid(handle.trailingIcon))
    {
        ui_tint_icon_media(scene, handle.trailingIcon, options.buttonIconTint);
        scene.mark_dirty(handle.trailingIcon);
    }
    UiDropdownControlComponent dropdown = {};
    dropdown.items = std::move(items);
    dropdown.selectedIndex = safeIndex;
    dropdown.visibleItemCount = visibleItemCount;
    dropdown.label = handle.label;
    dropdown.maxSelectedCharacters = options.maxSelectedCharacters;
    dropdown.maxRowCharacters = options.maxRowCharacters;
    dropdown.selectedFontSize = scaled_scalar(options.buttonFontSize, ui.scale());
    dropdown.rowFontSize = scaled_scalar(12.5f, ui.scale());
    dropdown.rowTextAvailableWidth = scaled_scalar(
        std::max(
            menuWidth - 8.0f - (options.checkIcon.valid() ? 26.0f : 10.0f) - 8.0f,
            1.0f),
        ui.scale());
    if (const Layout2DComponent* labelLayout = registry.try_get<Layout2DComponent>(handle.label))
    {
        dropdown.selectedTextAvailableWidth = labelLayout->size.x > 0.0f ?
            labelLayout->size.x :
            scaled_scalar(size.x - options.buttonPadding.x - options.buttonPadding.z, ui.scale());
    }
    dropdown.onChanged = std::move(options.onChanged);
    if (visibleItemCount > 0u && static_cast<std::size_t>(safeIndex) >= visibleItemCount)
    {
        dropdown.firstVisibleIndex = static_cast<std::size_t>(safeIndex) - visibleItemCount + 1u;
    }

    if (!dropdown.items.empty())
    {

        handle.menu = scene.create_shape(
            { 0.0f, 0.0f },
            scaled_size(menuWidth, menuHeight, ui.scale()),
            ui_macos26_control_style(ui, options.backgroundColor, options.menuOutlineColor, options.menuOutlineWidth, 1.0f),
            Renderer2DPrimitive::eSquircle);
        ui.set_shape(handle.menu, 20.0f);
        ui.set_layer(handle.menu, layer + 30, order + 300u, true);
        ui.attach_aligned(
            handle.menu,
            handle.root,
            options.openAbove ? UiAlignment::eBottomLeft : UiAlignment::eTopLeft,
            options.openAbove ?
                scaled_offset(-options.buttonPadding.x, -options.menuGap, ui.scale()) :
                scaled_offset(-options.buttonPadding.x, size.y + options.menuGap, ui.scale()),
            scaled_size(menuWidth, menuHeight, ui.scale()));
        ui.add_shadow(handle.menu, { 0.0f, 8.0f }, 18.0f, 0.24f);
        ui.set_dynamic_cache(handle.menu);
        dropdown.menuRoot = handle.menu;

        ButtonInputComponent menuInput = {};
        menuInput.enabled = options.enabled;
        registry.emplace<ButtonInputComponent>(handle.menu, std::move(menuInput));

        ButtonVisualComponent menuVisual = {};
        menuVisual.idle = ui_macos26_control_style(ui, options.backgroundColor, options.menuOutlineColor, options.menuOutlineWidth, 1.0f);
        menuVisual.hovered = menuVisual.idle;
        menuVisual.pressed = menuVisual.idle;
        registry.emplace<ButtonVisualComponent>(handle.menu, std::move(menuVisual));

        ScrollInputComponent menuScroll = {};

        menuScroll.enabled = dropdown.items.size() > static_cast<std::size_t>(visibleItemCount);
        menuScroll.step = std::max(options.scrollStepRows, 0.1f);
        menuScroll.offset = static_cast<float>(dropdown.firstVisibleIndex);
        menuScroll.maxOffset = dropdown.items.size() > static_cast<std::size_t>(visibleItemCount) ?
            static_cast<float>(dropdown.items.size() - static_cast<std::size_t>(visibleItemCount)) :
            0.0f;
        menuScroll.onScroll = [scenePtr = &scene, fontAtlasPtr = &fontAtlas, dropdownEntity = handle.root](const ScrollInputEvent& event) {
            UiDropdownControlComponent* captured = scenePtr->registry().try_get<UiDropdownControlComponent>(dropdownEntity);
            const ScrollInputComponent* scroll = scenePtr->registry().try_get<ScrollInputComponent>(event.target);
            if (!captured || !scroll)
            {
                return;
            }

            captured->firstVisibleIndex = static_cast<std::size_t>(
                std::clamp(
                    std::lround(scroll->offset),
                    0l,
                    static_cast<long>(std::max(scroll->maxOffset, 0.0f))));
            ui_refresh_dropdown_rows(*scenePtr, *fontAtlasPtr, dropdownEntity);
        };
        registry.emplace<ScrollInputComponent>(handle.menu, std::move(menuScroll));

        for (uint32_t i = 0; i < visibleItemCount; ++i)
        {

            entt::entity row = scene.create_shape(
                { 0.0f, 0.0f },
                scaled_size(menuWidth - 8.0f, options.rowHeight, ui.scale()),
                ui_macos26_control_style(ui, "#FFFFFF01", "#00000000", 0.0f, 1.0f),
                Renderer2DPrimitive::eSquircle);
            ui.set_shape(row, 20.0f);
            ui.set_layer(row, layer + 31, order + 310u + i, true);
            ui.attach_aligned(
                row,
                handle.menu,
                UiAlignment::eTopLeft,
                scaled_offset(4.0f, 4.0f + static_cast<float>(i) * options.rowHeight, ui.scale()),
                scaled_size(menuWidth - 8.0f, options.rowHeight, ui.scale()));

            ButtonInputComponent rowButton = {};
            rowButton.enabled = options.enabled;
            rowButton.onClick = [
                scenePtr = &scene,
                fontAtlasPtr = &fontAtlas,
                dropdownEntity = handle.root,
                slot = static_cast<std::size_t>(i)](const PointerInputEvent& event) {
                UiDropdownControlComponent* captured = scenePtr->registry().try_get<UiDropdownControlComponent>(dropdownEntity);
                if (!captured || captured->items.empty())
                {
                    return;
                }
                const std::size_t visibleIndex = captured->firstVisibleIndex + slot;
                if (visibleIndex >= captured->items.size())
                {
                    return;
                }
                const int itemIndex = static_cast<int>(visibleIndex);
                ui_set_dropdown_selected(*scenePtr, *fontAtlasPtr, dropdownEntity, itemIndex);
                ui_set_dropdown_open(*scenePtr, dropdownEntity, false);
                if (captured->onChanged)
                {
                    captured->onChanged(
                        itemIndex,
                        captured->items[static_cast<std::size_t>(itemIndex)],
                        event);
                }
            };
            registry.emplace<ButtonInputComponent>(row, std::move(rowButton));


            registry.emplace<HitRegion2DComponent>(row);
            ButtonVisualComponent rowVisual = {};
            rowVisual.idle = ui_macos26_control_style(ui, "#FFFFFF00", "#00000000", 0.0f, 1.0f);
            rowVisual.hovered = ui_macos26_control_style(ui, options.rowHoveredColor, "#00000000", 0.0f, 1.0f);
            rowVisual.pressed = ui_macos26_control_style(ui, options.rowPressedColor, "#00000000", 0.0f, 1.0f);
            registry.emplace<ButtonVisualComponent>(row, std::move(rowVisual));

            entt::entity check = entt::null;
            if (options.checkIcon.valid())
            {
                check = ui.create_aligned_media(
                    options.checkIcon,
                    row,
                    UiAlignment::eMiddleLeft,
                    { 12.0f, 12.0f },
                    layer + 33,
                    order + 330u + i,
                    { 8.0f, 0.0f });
                ui_tint_icon_media(scene, check, options.rowCheckTint);
                ui.set_layer(check, layer + 33, order + 330u + i, true);
                ui.set_dynamic_cache(check);
            }
            dropdown.checkIcons.push_back(check);

            entt::entity itemLabel = ui.create_aligned_text(
                "",
                fontAtlas,
                row,
                UiAlignment::eMiddleLeft,
                12.5f,
                ui_macos26_text_style(options.rowTextColor),
                layer + 34,
                order + 340u + i,
                { options.checkIcon.valid() ? 26.0f : 10.0f, 0.0f });
            ui.set_layer(itemLabel, layer + 34, order + 340u + i, true);
            ui.set_dynamic_cache(itemLabel);
            if (ButtonInputComponent* rowInput = registry.try_get<ButtonInputComponent>(row))
            {
                rowInput->onHoverChanged = [
                    scenePtr = &scene,
                    label = itemLabel,
                    check,
                    textColor = options.rowTextColor,
                    hoveredTextColor = options.rowHoveredTextColor,
                    checkTint = options.rowCheckTint,
                    hoveredCheckTint = options.rowHoveredCheckTint](bool hovered) {
                    entt::registry& capturedRegistry = scenePtr->registry();
                    if (label != entt::null && capturedRegistry.valid(label))
                    {
                        if (TextStyleComponent* labelStyle = capturedRegistry.try_get<TextStyleComponent>(label))
                        {
                            labelStyle->set_color(hovered ? hoveredTextColor : textColor);
                            scenePtr->mark_dirty(label);
                        }
                    }
                    if (check != entt::null && capturedRegistry.valid(check))
                    {
                        ui_tint_icon_media(*scenePtr, check, hovered ? hoveredCheckTint : checkTint);
                        scenePtr->mark_dirty(check);
                    }
                };
            }
            dropdown.itemRows.push_back(row);
            dropdown.itemLabels.push_back(itemLabel);
            ui.set_dynamic_cache(row);
            ui_update_button_visual(scene, row);
        }
    }

    registry.emplace<UiDropdownControlComponent>(handle.root, std::move(dropdown));
    const auto closeDropdown = [scenePtr = &scene, entity = handle.root] {
        ui_set_dropdown_open(*scenePtr, entity, false);
    };
    registry.emplace<OutsidePointerInputComponent>(handle.root,
        OutsidePointerInputComponent { false,
            [closeDropdown](const PointerInputEvent&) { closeDropdown(); }, closeDropdown });
    ui_set_dropdown_selected(scene, fontAtlas, handle.root, safeIndex);
    ui_set_dropdown_open(scene, handle.root, false);

    if (ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(handle.root))
    {

        button->onClick = [scenePtr = &ui.scene(), fontAtlasPtr = &fontAtlas, entity = handle.root](const PointerInputEvent& event) {
            UiDropdownControlComponent* captured = scenePtr->registry().try_get<UiDropdownControlComponent>(entity);
            if (!captured || captured->items.empty())
            {
                return;
            }
            const bool open = !captured->open;
            ui_close_other_dropdowns(*scenePtr, entity);
            ui_set_dropdown_open(*scenePtr, entity, open);
            (void)fontAtlasPtr;
            (void)event;
        };
    }
    return handle;
}

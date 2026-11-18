// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/ui/controls/common.h>

struct UiSearchFieldOptions
{

    Media2DHandle icon {};
    std::string placeholder = "Search";
    std::size_t maxBytes = 256u;
    std::string placeholderColor = "#707075E8";
    std::string textColor = "#1D1D1FFF";
    std::string iconTint {};
    std::string backgroundColor = "#E7E7EAF0";
    std::string backgroundBottomColor {};
    std::string focusedBackgroundColor = "#FFFFFFFF";
    std::string focusedBackgroundBottomColor {};
    std::string backgroundOutlineColor = "#00000000";
    std::string focusedOutlineColor = "#0D6FFFFF";
    float backgroundOutlineWidth = 0.0f;
    float focusedOutlineWidth = 1.4f;
    float backgroundOpacity = 0.96f;
    float focusedOpacity = 1.0f;
    float backdropBlurRadius = 0.0f;
    uint32_t backdropBlurPasses = 1u;
    float backdropBlurOpacity = 0.85f;
    UiGlassOptions glass {};
    std::string caretColor = "#0D6FFFFF";
    float caretWidth = 1.5f;
    float caretHeight = 16.0f;
    float textPaddingRight = 10.0f;
    std::optional<TextHorizontalAlignment2D> textAlignment {};
    std::function<void(const std::string&)> onChanged;
    std::function<void(const std::string&)> onSubmitted;
    bool readOnly = false;
    std::string selectionColour = "#3584E466";
};

struct UiSearchIndexEntry
{

    std::size_t id = 0u;
    float scrollOffset = 0.0f;
    Media2DHandle icon {};
    std::string title;
    std::string subtitle;
    std::string keywords;

    std::string iconTint;
};

struct UiSearchDestinationSpec
{

    std::size_t id = 0u;
    float scrollOffset = 0.0f;
    Media2DHandle icon {};
    Text title {};
    Text subtitle {};
    std::string keywords;
};

inline UiSearchIndexEntry ui_make_search_index_entry(
    const Localisation& localisation,
    UiSearchDestinationSpec spec)
{

    UiSearchIndexEntry entry = {};
    entry.id = spec.id;
    entry.scrollOffset = spec.scrollOffset;
    entry.icon = spec.icon;
    entry.title = localisation.resolve(spec.title);
    entry.subtitle = localisation.resolve(spec.subtitle);
    entry.keywords = std::move(spec.keywords);
    return entry;
}

struct UiSearchResultListComponent
{

    std::vector<UiSearchIndexEntry> entries;
    std::vector<std::size_t> matchedEntryIndices;
    std::vector<entt::entity> rows;
    std::vector<entt::entity> icons;
    std::vector<entt::entity> iconMedia;
    std::vector<entt::entity> iconFallbackLabels;
    std::vector<entt::entity> titleLabels;
    std::vector<entt::entity> subtitleLabels;
    std::vector<std::size_t> visibleEntryIndices;
    entt::entity menuRoot = entt::null;
    float width = 240.0f;
    float rowHeight = 44.0f;
    float verticalPadding = 5.0f;
    float textAvailableWidth = 0.0f;
    float titleFontSize = 13.5f;
    float subtitleFontSize = 12.0f;
    std::size_t firstVisibleIndex = 0u;
    std::size_t minQueryBytes = 2u;
    uint32_t maxResults = 5u;
    uint32_t maxVisibleResults = 5u;
    bool showNoResults = true;
    bool dismissOnSelected = true;
    std::string noResultsTitle = "No results";
    std::string noResultsSubtitle = "Try another search";
    std::size_t maxTitleCharacters = 0u;
    std::size_t maxSubtitleCharacters = 0u;
    std::function<void(const UiSearchIndexEntry&, const PointerInputEvent&)> onSelected;
};

struct UiSearchResultListOptions
{

    std::vector<UiSearchIndexEntry> entries;
    std::size_t minQueryBytes = 2u;
    uint32_t maxResults = 5u;
    uint32_t maxVisibleResults = 0u;
    float rowHeight = 46.0f;
    float menuGap = 6.0f;
    float verticalPadding = 5.0f;
    bool showNoResults = true;
    bool dismissOnSelected = true;
    std::string noResultsTitle = "No results";
    std::string noResultsSubtitle = "Try another search";
    std::string backgroundColor = "rgba(246, 246, 248, 0.94)";
    std::string backgroundBottomColor = "rgba(232, 234, 238, 0.88)";
    std::string outlineColor = "rgba(0, 0, 0, 0.14)";
    float outlineWidth = 0.9f;
    float cornerRadius = 16.0f;
    float backdropBlurRadius = 18.0f;
    uint32_t backdropBlurPasses = 2u;
    float backdropBlurOpacity = 0.9f;
    std::string rowBackgroundColor = "rgba(255, 255, 255, 0)";
    std::string rowHoveredBackgroundColor = "rgba(0, 113, 227, 1)";
    std::string rowPressedBackgroundColor = "rgba(0, 96, 202, 1)";
    std::string titleColor = "#1D1D1FFF";
    std::string subtitleColor = "#6F7278FF";
    std::string hoveredTitleColor = "#FFFFFFFF";
    std::string hoveredSubtitleColor = "rgba(255, 255, 255, 0.82)";
    float titleFontSize = 13.5f;
    float subtitleFontSize = 12.0f;
    float titleWeight = 590.0f;
    float subtitleWeight = 430.0f;
    std::size_t maxTitleCharacters = 0u;
    std::size_t maxSubtitleCharacters = 0u;
    float iconSize = 28.0f;
    std::string iconFallbackColor = "rgba(0, 0, 0, 0)";
    std::string iconFallbackTextColor = "#FFFFFFFF";
    float iconFallbackFontSize = 12.0f;
    glm::vec2 rowInset { 6.0f, 0.0f };
    glm::vec2 iconOffset { 14.0f, 0.0f };
    glm::vec2 textOffset { 52.0f, 7.0f };
    std::function<void(const UiSearchIndexEntry&, const PointerInputEvent&)> onSelected;
};

inline UiSearchFieldOptions ui_search_field_options_from_theme(
    const UiTheme& theme,
    Media2DHandle searchIcon = {},
    std::string placeholder = "Search")
{

    UiSearchFieldOptions options = {};
    options.icon = searchIcon;
    options.placeholder = std::move(placeholder);
    options.placeholderColor = theme.settingsPanel.searchPlaceholder;
    options.textColor = theme.text;
    options.iconTint = theme.icon;
    options.caretColor = theme.accent;
    options.backgroundColor = theme.settingsPanel.searchBackground;
    options.backgroundBottomColor = theme.settingsPanel.searchBackgroundBottom;
    options.focusedBackgroundColor = theme.settingsPanel.searchFocusedBackground;
    options.focusedBackgroundBottomColor = theme.settingsPanel.searchFocusedBackgroundBottom;
    options.backgroundOutlineColor = theme.settingsPanel.searchOutline;
    options.focusedOutlineColor = theme.accent;
    options.backgroundOutlineWidth = 1.4f;
    options.focusedOutlineWidth = 1.4f;
    options.backdropBlurRadius = 18.0f;
    options.backdropBlurPasses = 2u;
    options.backdropBlurOpacity = 0.92f;
    return options;
}

inline TextInputVisualComponent ui_macos26_search_visual(UiBuilder& ui)
{
    TextInputVisualComponent visual = {};
    visual.idle = ui_macos26_control_style(ui, "#E7E7EAF0", "#00000000", 0.0f, 0.96f);
    visual.focused = ui_macos26_control_style(ui, "#FFFFFFFF", "#0D6FFFFF", 1.4f, 1.0f);
    visual.disabled = ui_macos26_control_style(ui, "#ECECECA0", "#00000000", 0.0f, 1.0f);
    visual.valueText = ui_macos26_text_style("#1D1D1FFF");
    visual.placeholderText = ui_macos26_text_style("#707075E8");
    visual.caretStyle = make_solid_style("#007AFFFF", "#00000000", 0.0f, 1.0f);
    visual.hasDisabled = true;
    visual.maxDisplayCharacters = 36u;
    return visual;
}

inline char ui_search_lower_ascii(char value)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

inline std::string ui_normalise_search_text(std::string_view value)
{

    std::string result;
    result.reserve(value.size());
    bool pendingSpace = false;
    for (char c : value)
    {
        const unsigned char byte = static_cast<unsigned char>(c);
        if (std::isalnum(byte))
        {
            if (pendingSpace && !result.empty())
            {
                result.push_back(' ');
            }
            result.push_back(ui_search_lower_ascii(c));
            pendingSpace = false;
            continue;
        }
        pendingSpace = !result.empty();
    }
    return result;
}

inline bool ui_search_text_contains(std::string_view haystack, const std::string& query)
{
    if (query.empty())
    {
        return false;
    }
    return ui_normalise_search_text(haystack).find(query) != std::string::npos;
}

inline int ui_search_result_score(const UiSearchIndexEntry& entry, const std::string& query)
{

    if (query.empty())
    {
        return -1;
    }

    const std::string title = ui_normalise_search_text(entry.title);
    const std::string subtitle = ui_normalise_search_text(entry.subtitle);
    const std::string keywords = ui_normalise_search_text(entry.keywords);
    if (title == query)
    {
        return 1000;
    }
    if (title.rfind(query, 0u) == 0u)
    {
        return 900;
    }
    if (title.find(query) != std::string::npos)
    {
        return 800;
    }
    if (subtitle.find(query) != std::string::npos)
    {
        return 650;
    }
    if (keywords.find(query) != std::string::npos)
    {
        return 520;
    }
    return -1;
}

inline void ui_set_search_result_entity_visible(
    Renderer2DScene& scene,
    entt::entity entity,
    bool visible)
{
    if (entity == entt::null || !scene.registry().valid(entity))
    {
        return;
    }
    if (RenderLayer2DComponent* layer = scene.registry().try_get<RenderLayer2DComponent>(entity))
    {
        layer->visible = visible;
    }
    scene.mark_dirty(entity);
}

inline void ui_reset_search_result_row_hover(Renderer2DScene& scene, entt::entity row)
{
    if (row == entt::null || !scene.registry().valid(row))
    {
        return;
    }

    ButtonInputComponent* button = scene.registry().try_get<ButtonInputComponent>(row);
    if (!button)
    {
        return;
    }

    button->hovered = false;
    button->leftPressed = false;
    button->rightPressed = false;
    if (button->onHoverChanged)
    {
        button->onHoverChanged(false);
    }
    ui_update_button_visual(scene, row);
}

inline void ui_set_search_result_list_visible(
    Renderer2DScene& scene,
    entt::entity entity,
    bool visible)
{

    entt::registry& registry = scene.registry();
    UiSearchResultListComponent* list = registry.try_get<UiSearchResultListComponent>(entity);
    if (!list)
    {
        return;
    }

    ui_set_search_result_entity_visible(scene, list->menuRoot, visible);
    for (std::size_t i = 0; i < list->rows.size(); ++i)
    {
        const bool rowVisible = visible && i < list->visibleEntryIndices.size();
        const bool validEntry =
            rowVisible &&
            list->visibleEntryIndices[i] < list->entries.size();
        const bool hasMediaIcon =
            validEntry &&
            list->entries[list->visibleEntryIndices[i]].icon.valid();
        ui_set_search_result_entity_visible(scene, list->rows[i], rowVisible);
        ui_set_search_result_entity_visible(scene, list->icons[i], validEntry);
        ui_set_search_result_entity_visible(scene, list->iconMedia[i], validEntry && hasMediaIcon);
        ui_set_search_result_entity_visible(scene, list->iconFallbackLabels[i], validEntry && !hasMediaIcon);
        ui_set_search_result_entity_visible(scene, list->titleLabels[i], rowVisible);
        ui_set_search_result_entity_visible(scene, list->subtitleLabels[i], rowVisible);
        if (ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(list->rows[i]))
        {
            button->enabled = rowVisible;
            if (!rowVisible)
            {
                ui_reset_search_result_row_hover(scene, list->rows[i]);
            }
        }
    }
    scene.activate_dynamic(list->menuRoot, 0.12f);
}

inline void ui_resize_search_result_list(
    Renderer2DScene& scene,
    entt::entity entity,
    std::size_t visibleRows)
{
    UiSearchResultListComponent* list = scene.registry().try_get<UiSearchResultListComponent>(entity);
    if (!list || list->menuRoot == entt::null || !scene.registry().valid(list->menuRoot))
    {
        return;
    }

    const float height = list->verticalPadding * 2.0f + list->rowHeight * static_cast<float>(visibleRows);
    const glm::vec2 scaledSize = {
        list->width,
        height
    };
    if (ShapeComponent* shape = scene.registry().try_get<ShapeComponent>(list->menuRoot))
    {
        shape->size = scaledSize;
    }
    if (Layout2DComponent* layout = scene.registry().try_get<Layout2DComponent>(list->menuRoot))
    {
        layout->size = scaledSize;
    }
    scene.mark_dirty(list->menuRoot);
}

inline void ui_refresh_search_result_slots(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity entity)
{

    entt::registry& registry = scene.registry();
    UiSearchResultListComponent* list = registry.try_get<UiSearchResultListComponent>(entity);
    if (!list)
    {
        return;
    }


    scene.activate_dynamic(list->menuRoot, 0.35f);
    list->visibleEntryIndices.clear();
    const std::size_t slotCount = list->rows.size();
    if (slotCount == 0u || list->matchedEntryIndices.empty())
    {
        if (ScrollInputComponent* scroll = registry.try_get<ScrollInputComponent>(list->menuRoot))
        {
            scroll->offset = 0.0f;
            scroll->maxOffset = 0.0f;
        }
        ui_resize_search_result_list(scene, entity, 0u);
        ui_set_search_result_list_visible(scene, entity, false);
        return;
    }

    const std::size_t maxFirstVisible =
        list->matchedEntryIndices.size() > slotCount ?
            list->matchedEntryIndices.size() - slotCount :
            0u;

    list->firstVisibleIndex = std::min(list->firstVisibleIndex, maxFirstVisible);
    if (ScrollInputComponent* scroll = registry.try_get<ScrollInputComponent>(list->menuRoot))
    {
        scroll->maxOffset = static_cast<float>(maxFirstVisible);
        scroll->offset = std::clamp(scroll->offset, scroll->minOffset, scroll->maxOffset);
    }

    const std::size_t visibleCount = std::min(slotCount, list->matchedEntryIndices.size() - list->firstVisibleIndex);
    list->visibleEntryIndices.reserve(visibleCount);
    for (std::size_t slot = 0u; slot < visibleCount; ++slot)
    {
        list->visibleEntryIndices.push_back(list->matchedEntryIndices[list->firstVisibleIndex + slot]);
    }

    for (std::size_t slot = 0; slot < list->visibleEntryIndices.size(); ++slot)
    {
        const std::size_t entryIndex = list->visibleEntryIndices[slot];
        if (entryIndex >= list->entries.size())
        {
            continue;
        }
        const UiSearchIndexEntry& entry = list->entries[entryIndex];
        ui_set_search_result_entity_visible(scene, list->icons[slot], true);
        ui_set_search_result_entity_visible(scene, list->iconMedia[slot], entry.icon.valid());
        ui_set_search_result_entity_visible(scene, list->iconFallbackLabels[slot], !entry.icon.valid());
        if (entry.icon.valid())
        {
            if (Media2DComponent* media = registry.try_get<Media2DComponent>(list->iconMedia[slot]))
            {
                media->set_media(entry.icon);
                media->fit = Media2DFit::eCover;
                media->opacity = 1.0f;
                media->tintAsMask = false;
                media->set_tint(glm::vec4(1.0f));
                media->cornerRadius = 0.0f;
                scene.mark_dirty(list->iconMedia[slot]);
                if (!entry.iconTint.empty())
                    ui_tint_icon_media(scene, list->iconMedia[slot], entry.iconTint);
            }
        }
        else
        {
            std::string fallbackLetter = "?";
            if (!entry.title.empty())
            {
                fallbackLetter = std::string(1u, static_cast<char>(std::toupper(static_cast<unsigned char>(entry.title.front()))));
            }
            set_text_entity(scene, fontAtlas, list->iconFallbackLabels[slot], fallbackLetter);
        }
        set_text_entity(
            scene,
            fontAtlas,
            list->titleLabels[slot],
            ui_truncate_text_to_width_with_ellipsis(
                fontAtlas,
                entry.title,
                list->titleFontSize,
                list->textAvailableWidth,
                list->maxTitleCharacters));
        set_text_entity(
            scene,
            fontAtlas,
            list->subtitleLabels[slot],
            ui_truncate_text_to_width_with_ellipsis(
                fontAtlas,
                entry.subtitle,
                list->subtitleFontSize,
                list->textAvailableWidth,
                list->maxSubtitleCharacters));
        if (ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(list->rows[slot]))
        {
            button->enabled = true;
        }
    }

    ui_resize_search_result_list(scene, entity, list->visibleEntryIndices.size());
    ui_set_search_result_list_visible(scene, entity, !list->visibleEntryIndices.empty());
}

inline void ui_set_search_result_query(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity entity,
    std::string_view queryValue)
{

    entt::registry& registry = scene.registry();
    UiSearchResultListComponent* list = registry.try_get<UiSearchResultListComponent>(entity);
    if (!list)
    {
        return;
    }



    scene.activate_dynamic(list->menuRoot, 0.35f);
    list->visibleEntryIndices.clear();
    list->matchedEntryIndices.clear();
    list->firstVisibleIndex = 0u;
    if (ScrollInputComponent* scroll = registry.try_get<ScrollInputComponent>(list->menuRoot))
    {
        scroll->offset = 0.0f;
        scroll->maxOffset = 0.0f;
    }
    for (entt::entity row : list->rows)
    {
        ui_reset_search_result_row_hover(scene, row);
    }

    const std::string query = ui_normalise_search_text(queryValue);
    if (query.size() < list->minQueryBytes)
    {
        ui_resize_search_result_list(scene, entity, 0u);
        ui_set_search_result_list_visible(scene, entity, false);
        return;
    }

    std::vector<std::pair<int, std::size_t>> scored;
    scored.reserve(list->entries.size());
    for (std::size_t i = 0; i < list->entries.size(); ++i)
    {
        const int score = ui_search_result_score(list->entries[i], query);
        if (score >= 0)
        {
            scored.emplace_back(score, i);
        }
    }
    std::stable_sort(
        scored.begin(),
        scored.end(),
        [](const auto& lhs, const auto& rhs) {
            return lhs.first > rhs.first;
        });


    const std::size_t resultCount = std::min<std::size_t>(scored.size(), std::max<uint32_t>(list->maxResults, 1u));
    for (std::size_t i = 0; i < resultCount; ++i)
    {
        list->matchedEntryIndices.push_back(scored[i].second);
    }

    if (list->matchedEntryIndices.empty() && list->showNoResults && !list->rows.empty())
    {
        set_text_entity(
            scene,
            fontAtlas,
            list->titleLabels[0],
            ui_truncate_text_to_width_with_ellipsis(
                fontAtlas,
                list->noResultsTitle,
                list->titleFontSize,
                list->textAvailableWidth,
                list->maxTitleCharacters));
        set_text_entity(
            scene,
            fontAtlas,
            list->subtitleLabels[0],
            ui_truncate_text_to_width_with_ellipsis(
                fontAtlas,
                list->noResultsSubtitle,
                list->subtitleFontSize,
                list->textAvailableWidth,
                list->maxSubtitleCharacters));
        list->visibleEntryIndices.push_back(static_cast<std::size_t>(-1));
        ui_resize_search_result_list(scene, entity, 1u);
        ui_set_search_result_list_visible(scene, entity, true);
        ui_set_search_result_entity_visible(scene, list->icons[0], false);
        ui_set_search_result_entity_visible(scene, list->iconMedia[0], false);
        ui_set_search_result_entity_visible(scene, list->iconFallbackLabels[0], false);
        if (ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(list->rows[0]))
        {
            button->enabled = false;
            ui_reset_search_result_row_hover(scene, list->rows[0]);
        }
        return;
    }

    ui_refresh_search_result_slots(scene, fontAtlas, entity);
}

inline UiControlHandle ui_create_search_result_list(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiSearchResultListOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    if (parent == entt::null || !registry.valid(parent))
    {
        return handle;
    }

    const uint32_t maxResults = std::max(options.maxResults, 1u);
    const uint32_t rowSlots = options.maxVisibleResults == 0u ?
        maxResults :
        std::clamp(options.maxVisibleResults, 1u, maxResults);
    const float width = std::max(size.x, 1.0f);
    const float rowHeight = std::max(options.rowHeight, 1.0f);
    const float maxHeight = options.verticalPadding * 2.0f + rowHeight * static_cast<float>(rowSlots);
    const std::string backgroundBottom = options.backgroundBottomColor.empty() ?
        options.backgroundColor :
        options.backgroundBottomColor;
    const ShapeStyleComponent rootStyle = options.backdropBlurRadius > 0.0f ?
        ui_macos26_frosted_control_style(
            ui,
            options.backgroundColor,
            backgroundBottom,
            options.outlineColor,
            options.outlineWidth,
            1.0f,
            options.backdropBlurRadius,
            options.backdropBlurPasses,
            options.backdropBlurOpacity) :
        ui_macos26_control_style(
            ui,
            options.backgroundColor,
            options.outlineColor,
            options.outlineWidth,
            1.0f);

    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(width, maxHeight, ui.scale()),
        rootStyle,
        Renderer2DPrimitive::eSquircle);
    ui.set_shape(handle.root, options.cornerRadius);
    ui.set_shape_squircle(handle.root, 0.72f, 4.7f);
    ui.set_layer(handle.root, layer, order, true);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y + options.menuGap, ui.scale()),
        scaled_size(width, maxHeight, ui.scale()));
    scene.enable_mask(handle.root, false);
    ui.add_shadow(handle.root, { 0.0f, 8.0f }, 18.0f, 0.2f);
    ui.set_dynamic_cache(handle.root);

    UiSearchResultListComponent list = {};
    list.entries = std::move(options.entries);
    list.menuRoot = handle.root;
    list.width = scaled_scalar(width, ui.scale());
    list.rowHeight = scaled_scalar(rowHeight, ui.scale());
    list.verticalPadding = scaled_scalar(options.verticalPadding, ui.scale());
    list.titleFontSize = scaled_scalar(options.titleFontSize, ui.scale());
    list.subtitleFontSize = scaled_scalar(options.subtitleFontSize, ui.scale());
    list.textAvailableWidth = std::max(
        scaled_scalar(width - options.rowInset.x * 2.0f - options.textOffset.x - 10.0f, ui.scale()),
        0.0f);
    list.minQueryBytes = options.minQueryBytes;
    list.maxResults = maxResults;
    list.maxVisibleResults = rowSlots;
    list.showNoResults = options.showNoResults;
    list.dismissOnSelected = options.dismissOnSelected;
    list.noResultsTitle = std::move(options.noResultsTitle);
    list.noResultsSubtitle = std::move(options.noResultsSubtitle);
    list.maxTitleCharacters = options.maxTitleCharacters;
    list.maxSubtitleCharacters = options.maxSubtitleCharacters;
    list.onSelected = std::move(options.onSelected);

    TextStyleComponent titleStyle = ui_macos26_text_style(options.titleColor);
    titleStyle.set_font_weight(options.titleWeight);
    TextStyleComponent subtitleStyle = ui_macos26_text_style(options.subtitleColor);
    subtitleStyle.set_font_weight(options.subtitleWeight);

    Media2DHandle reusableIcon {};
    for (const UiSearchIndexEntry& entry : list.entries)
    {
        if (entry.icon.valid())
        {
            reusableIcon = entry.icon;
            break;
        }
    }

    for (uint32_t i = 0; i < rowSlots; ++i)
    {
        entt::entity row = scene.create_shape(
            { 0.0f, 0.0f },
            scaled_size(width - options.rowInset.x * 2.0f, rowHeight, ui.scale()),
            ui_macos26_control_style(ui, options.rowBackgroundColor, "#00000000", 0.0f, 1.0f),
            Renderer2DPrimitive::eSquircle);
        ui.set_shape(row, std::max(rowHeight * 0.35f, 8.0f));
        ui.set_shape_squircle(row, 0.68f, 4.6f);
        ui.set_layer(row, layer + 1, order + i, true);
        ui.attach_aligned(
            row,
            handle.root,
            UiAlignment::eTopLeft,
            scaled_offset(options.rowInset.x, options.verticalPadding + static_cast<float>(i) * rowHeight, ui.scale()),
            scaled_size(width - options.rowInset.x * 2.0f, rowHeight, ui.scale()));
        ui.set_dynamic_cache(row);

        ButtonInputComponent rowButton = {};
        rowButton.enabled = false;
        rowButton.onClick = [scenePtr = &scene, resultList = handle.root, slot = static_cast<std::size_t>(i)](const PointerInputEvent& event) {
            UiSearchResultListComponent* captured = scenePtr->registry().try_get<UiSearchResultListComponent>(resultList);
            if (!captured || slot >= captured->visibleEntryIndices.size())
            {
                return;
            }
            const std::size_t entryIndex = captured->visibleEntryIndices[slot];
            if (entryIndex >= captured->entries.size())
            {
                return;
            }
            auto callback = captured->onSelected;
            UiSearchIndexEntry entry = captured->entries[entryIndex];
            if (captured->dismissOnSelected)
                ui_set_search_result_list_visible(*scenePtr, resultList, false);
            if (callback)
            {
                callback(entry, event);
            }
        };
        registry.emplace<ButtonInputComponent>(row, std::move(rowButton));

        ButtonVisualComponent rowVisual = {};
        rowVisual.idle = ui_macos26_control_style(ui, options.rowBackgroundColor, "#00000000", 0.0f, 1.0f);
        rowVisual.hovered = ui_macos26_control_style(ui, options.rowHoveredBackgroundColor, "#00000000", 0.0f, 1.0f);
        rowVisual.pressed = ui_macos26_control_style(ui, options.rowPressedBackgroundColor, "#00000000", 0.0f, 1.0f);
        registry.emplace<ButtonVisualComponent>(row, std::move(rowVisual));

        entt::entity icon = scene.create_shape(
            { 0.0f, 0.0f },
            scaled_size(options.iconSize, options.iconSize, ui.scale()),
            ui_macos26_control_style(ui, options.iconFallbackColor, "#00000000", 0.0f, 1.0f),
            Renderer2DPrimitive::eSquircle);
        ui.set_shape(icon, options.iconSize * 0.3f);
        ui.set_shape_squircle(icon, 0.7f, 4.6f);
        ui.set_layer(icon, layer + 2, order + i, true);
        ui.attach_aligned(
            icon,
            row,
            UiAlignment::eMiddleLeft,
            options.iconOffset,
            scaled_size(options.iconSize, options.iconSize, ui.scale()));
        scene.enable_mask(icon, false);
        ui.set_dynamic_cache(icon);

        Media2DHandle initialIcon = reusableIcon;
        if (i < list.entries.size())
        {
            if (list.entries[i].icon.valid())
            {
                initialIcon = list.entries[i].icon;
            }
        }
        entt::entity iconMedia = entt::null;
        if (initialIcon.valid())
        {
            iconMedia = ui.create_aligned_media(
                initialIcon,
                icon,
                UiAlignment::eCenter,
                { options.iconSize, options.iconSize },
                layer + 3,
                order + i,
                glm::vec2(0.0f),
                Media2DFit::eCover);
            ui.set_layer(iconMedia, layer + 3, order + i, true);
            ui.set_dynamic_cache(iconMedia);
            if (Media2DComponent* media = registry.try_get<Media2DComponent>(iconMedia))
            {
                media->cornerRadius = 0.0f;
            }
        }

        std::string fallbackLetter = "?";
        if (i < list.entries.size() && !list.entries[i].title.empty())
        {
            fallbackLetter = std::string(1u, static_cast<char>(std::toupper(static_cast<unsigned char>(list.entries[i].title.front()))));
        }
        entt::entity fallbackText = ui.create_aligned_text(
            fallbackLetter,
            fontAtlas,
            icon,
            UiAlignment::eCenter,
            options.iconFallbackFontSize,
            ui_macos26_text_style(options.iconFallbackTextColor),
            layer + 4,
            order + i);
        ui.set_layer(fallbackText, layer + 4, order + i, true);
        ui.set_dynamic_cache(fallbackText);

        entt::entity title = ui.create_aligned_text(
            "",
            fontAtlas,
            row,
            UiAlignment::eTopLeft,
            options.titleFontSize,
            titleStyle,
            layer + 2,
            order + i,
            options.textOffset);
        ui.set_layer(title, layer + 2, order + i, true);
        ui.set_dynamic_cache(title);

        entt::entity subtitle = ui.create_aligned_text(
            "",
            fontAtlas,
            row,
            UiAlignment::eTopLeft,
            options.subtitleFontSize,
            subtitleStyle,
            layer + 2,
            order + rowSlots + i,
            { options.textOffset.x, options.textOffset.y + 18.0f });
        ui.set_layer(subtitle, layer + 2, order + rowSlots + i, true);
        ui.set_dynamic_cache(subtitle);

        if (ButtonInputComponent* rowInput = registry.try_get<ButtonInputComponent>(row))
        {
            rowInput->onHoverChanged = [
                scenePtr = &scene,
                title,
                subtitle,
                titleColor = options.titleColor,
                subtitleColor = options.subtitleColor,
                hoveredTitleColor = options.hoveredTitleColor,
                hoveredSubtitleColor = options.hoveredSubtitleColor](bool hovered) {
                entt::registry& capturedRegistry = scenePtr->registry();
                if (TextStyleComponent* style = capturedRegistry.try_get<TextStyleComponent>(title))
                {
                    style->set_color(hovered ? hoveredTitleColor : titleColor);
                    scenePtr->mark_dirty(title);
                }
                if (TextStyleComponent* style = capturedRegistry.try_get<TextStyleComponent>(subtitle))
                {
                    style->set_color(hovered ? hoveredSubtitleColor : subtitleColor);
                    scenePtr->mark_dirty(subtitle);
                }
            };
        }

        list.rows.push_back(row);
        list.icons.push_back(icon);
        list.iconMedia.push_back(iconMedia);
        list.iconFallbackLabels.push_back(fallbackText);
        list.titleLabels.push_back(title);
        list.subtitleLabels.push_back(subtitle);
    }

    registry.emplace<UiSearchResultListComponent>(handle.root, std::move(list));
    ScrollInputComponent scroll = {};
    scroll.step = 1.0f;
    scroll.maxOffset = 0.0f;
    scroll.onScroll = [scenePtr = &scene, fontAtlasPtr = &fontAtlas, resultList = handle.root](const ScrollInputEvent& event) {
        UiSearchResultListComponent* captured = scenePtr->registry().try_get<UiSearchResultListComponent>(resultList);
        const ScrollInputComponent* scrollInput = scenePtr->registry().try_get<ScrollInputComponent>(event.target);
        if (!captured || !scrollInput)
        {
            return;
        }

        captured->firstVisibleIndex = static_cast<std::size_t>(
            std::clamp(
                std::lround(scrollInput->offset),
                0l,
                static_cast<long>(std::max(scrollInput->maxOffset, 0.0f))));
        ui_refresh_search_result_slots(*scenePtr, *fontAtlasPtr, resultList);
    };
    registry.emplace<ScrollInputComponent>(handle.root, std::move(scroll));
    ui_set_search_result_list_visible(scene, handle.root, false);
    return handle;
}

inline UiControlHandle ui_create_search_field(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiSearchFieldOptions options = {})
{

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    const std::string iconTint = options.iconTint.empty() ? options.placeholderColor : options.iconTint;
    const std::string backgroundBottom = options.backgroundBottomColor.empty() ?
        options.backgroundColor :
        options.backgroundBottomColor;
    const std::string focusedBackgroundBottom = options.focusedBackgroundBottomColor.empty() ?
        options.focusedBackgroundColor :
        options.focusedBackgroundBottomColor;
    const bool frosted = options.glass.material == GlassMaterial::eOff &&
        options.backdropBlurRadius > 0.0f;
    const ShapeStyleComponent idleStyle = frosted ?
        ui_macos26_frosted_control_style(
            ui,
            options.backgroundColor,
            backgroundBottom,
            options.backgroundOutlineColor,
            options.backgroundOutlineWidth,
            options.backgroundOpacity,
            options.backdropBlurRadius,
            options.backdropBlurPasses,
            options.backdropBlurOpacity) :
        ui_macos26_control_style(
            ui,
            options.backgroundColor,
            options.backgroundOutlineColor,
            options.backgroundOutlineWidth,
            options.backgroundOpacity);
    const ShapeStyleComponent focusedStyle = frosted ?
        ui_macos26_frosted_control_style(
            ui,
            options.focusedBackgroundColor,
            focusedBackgroundBottom,
            options.focusedOutlineColor,
            options.focusedOutlineWidth,
            options.focusedOpacity,
            options.backdropBlurRadius,
            options.backdropBlurPasses,
            options.backdropBlurOpacity) :
        ui_macos26_control_style(
            ui,
            options.focusedBackgroundColor,
            options.focusedOutlineColor,
            options.focusedOutlineWidth,
            options.focusedOpacity);
    UiControlHandle handle = {};
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        idleStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, size.y * 0.5f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(handle.root, parent, alignment, scaled_offset(offset.x, offset.y, ui.scale()), scaled_size(size.x, size.y, ui.scale()));
    ui_apply_glass_material(ui, handle.root, options.glass);
    scene.enable_mask(handle.root, false);
    const float iconOffset = options.icon.valid() ? 28.0f : 10.0f;
    if (options.icon.valid())
    {
        handle.leadingIcon = ui.create_aligned_media(
            options.icon,
            handle.root,
            UiAlignment::eMiddleLeft,
            { 14.0f, 14.0f },
            layer + 1,
            order,
            { 9.0f, 0.0f });
        ui_tint_icon_media(scene, handle.leadingIcon, iconTint);
    }

    handle.label = ui.create_aligned_text(
        options.placeholder,
        fontAtlas,
        handle.root,
        UiAlignment::eMiddleLeft,
        14.0f,
        ui_macos26_text_style(options.placeholderColor),
        layer + 1,
        order + 1u,
        { iconOffset, 0.0f });

    handle.caret = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(options.caretWidth, options.caretHeight, ui.scale()),
        make_solid_style(options.caretColor, "#00000000", 0.0f, 1.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(handle.caret, layer + 2, order + 2u);
    ui.attach_aligned(
        handle.caret,
        handle.root,
        UiAlignment::eMiddleLeft,
        scaled_offset(iconOffset, 0.0f, ui.scale()),
        scaled_size(options.caretWidth, options.caretHeight, ui.scale()));
    if (RenderLayer2DComponent* caretLayer = registry.try_get<RenderLayer2DComponent>(handle.caret))
    {
        caretLayer->visible = false;
    }
    ui.set_dynamic_cache(handle.caret);

    TextInputComponent input = {};
    input.textEntity = handle.label;
    input.caretEntity = handle.caret;
    input.placeholder = std::move(options.placeholder);
    input.maxBytes = options.maxBytes;
    input.textPaddingLeft = scaled_scalar(iconOffset, ui.scale());
    input.textAlignment = options.textAlignment;
    input.textPaddingRight = scaled_scalar(options.textPaddingRight, ui.scale());
    input.caretWidth = scaled_scalar(options.caretWidth, ui.scale());
    input.caretHeight = scaled_scalar(options.caretHeight, ui.scale());
    input.onChanged = std::move(options.onChanged);
    input.onSubmitted = std::move(options.onSubmitted);
    input.readOnly = options.readOnly;
    registry.emplace<TextInputComponent>(handle.root, std::move(input));
    TextInputVisualComponent visual = ui_macos26_search_visual(ui);
    visual.idle = idleStyle;
    visual.focused = focusedStyle;
    visual.placeholderText = ui_macos26_text_style(options.placeholderColor);
    visual.valueText = ui_macos26_text_style(options.textColor);
    visual.caretStyle = make_solid_style(options.caretColor, "#00000000", 0.0f, 1.0f);
    visual.selectionStyle.set_color(options.selectionColour);
    registry.emplace<TextInputVisualComponent>(handle.root, std::move(visual));
    ui_update_text_input_visual(scene, fontAtlas, handle.root);
    return handle;
}


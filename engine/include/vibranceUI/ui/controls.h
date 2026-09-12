#pragma once

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <filesystem>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/renderer/media2d.h>
#include <vibranceUI/renderer/present_mode.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/ui/builder.h>
#include <vibranceUI/ui/glass.h>
#include <vibranceUI/ui/input.h>
#include <vibranceUI/ui/resources.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/surfaces.h>
#include <vibranceUI/ui/visuals.h>

struct UiControlHandle
{
    // Generic handle returned by most controls so callers can style or inspect parts
    entt::entity root = entt::null;
    entt::entity label = entt::null;
    entt::entity caret = entt::null;
    entt::entity leadingIcon = entt::null;
    entt::entity trailingIcon = entt::null;
    entt::entity knob = entt::null;
    entt::entity indicator = entt::null;
    entt::entity menu = entt::null;
};

inline std::string ui_truncate_text_with_ellipsis(std::string_view text, std::size_t maxCharacters)
{
    // Keep UTF-8 text valid while limiting the number of visible characters
    if (maxCharacters == 0u)
    {
        return std::string(text);
    }

    std::size_t characters = 0u;
    std::size_t byteEnd = 0u;
    for (std::size_t i = 0u; i < text.size() && characters < maxCharacters;)
    {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t width = 1u;
        if ((c & 0x80u) == 0u)
        {
            width = 1u;
        }
        else if ((c & 0xE0u) == 0xC0u && i + 1u < text.size())
        {
            width = 2u;
        }
        else if ((c & 0xF0u) == 0xE0u && i + 2u < text.size())
        {
            width = 3u;
        }
        else if ((c & 0xF8u) == 0xF0u && i + 3u < text.size())
        {
            width = 4u;
        }

        byteEnd = i + width;
        i += width;
        ++characters;
    }

    if (byteEnd >= text.size())
    {
        return std::string(text);
    }

    std::string result(text.substr(0u, byteEnd));
    result += "...";
    return result;
}

inline bool ui_string_ends_with_ellipsis(const std::string& value)
{
    return value.size() >= 3u && value.compare(value.size() - 3u, 3u, "...") == 0;
}

inline std::string ui_truncate_text_to_width_with_ellipsis(
    const Renderer2DFontAtlas& fontAtlas,
    std::string_view text,
    float fontSize,
    float availableWidth,
    std::size_t maxCharacters = 0u)
{
    // Prefer pixel-width clipping so labels stay inside their visual row
    if (availableWidth <= 0.0f)
    {
        return {};
    }

    std::string clipped = ui_truncate_text_with_ellipsis(text, maxCharacters);
    if (clipped.empty() || ui_text_width(fontAtlas, clipped, fontSize) <= availableWidth)
    {
        return clipped;
    }

    constexpr std::string_view ellipsis = "...";
    if (ui_text_width(fontAtlas, ellipsis, fontSize) > availableWidth)
    {
        return {};
    }

    if (ui_string_ends_with_ellipsis(clipped))
    {
        clipped.resize(clipped.size() - 3u);
    }

    std::size_t bestEnd = 0u;
    std::size_t next = ui_next_utf8_boundary(clipped, 0u);
    while (next > bestEnd && next <= clipped.size())
    {
        std::string candidate = clipped.substr(0u, next);
        candidate += ellipsis;
        if (ui_text_width(fontAtlas, candidate, fontSize) > availableWidth)
        {
            break;
        }

        bestEnd = next;
        if (next == clipped.size())
        {
            break;
        }
        next = ui_next_utf8_boundary(clipped, next);
    }

    if (bestEnd == 0u)
    {
        return std::string(ellipsis);
    }

    std::string result = clipped.substr(0u, bestEnd);
    result += ellipsis;
    return result;
}

struct UiButtonOptions
{
    // Shared text button options; icon padding is only used when an icon exists
    Media2DHandle leadingIcon {};
    Media2DHandle trailingIcon {};
    glm::vec4 padding { 12.0f, 4.0f, 12.0f, 4.0f };
    float cornerRadius = 10.0f;
    float fontSize = 13.0f;
    float iconSize = 14.0f;
    float leadingIconSize = 0.0f;
    float trailingIconSize = 0.0f;
    float iconTextGap = 6.0f;
    bool primary = false;
    bool enabled = true;
    bool tintLeadingIcon = true;
    bool tintTrailingIcon = true;
    std::string textColor {};
    std::string iconTint {};
    std::string backgroundColor {};
    std::string hoveredBackgroundColor {};
    std::string pressedBackgroundColor {};
    std::string disabledBackgroundColor {};
    std::string outlineColor {};
    std::string hoveredOutlineColor {};
    std::string pressedOutlineColor {};
    std::string disabledOutlineColor {};
    float outlineWidth = 0.8f;
    float hoveredOutlineWidth = 0.9f;
    float pressedOutlineWidth = 1.0f;
    float disabledOutlineWidth = 0.6f;
    UiGlassOptions glass {};
    // When set, dragging elastically stretches the button without changing
    // its layout position. Its icon, label, masks, and other descendants
    // inherit the same deformation.
    std::optional<StretchDynamicsOptions> stretchDynamics {};
    std::function<void(const PointerInputEvent&)> onClick;
};

struct UiSubmenuRowOptions
{
    // Responsive card row built on the standard text-button interaction model.
    UiButtonOptions button {};
    float horizontalInset = 8.0f;
    float height = 42.0f;
};

struct UiScrollViewportEntranceOptions
{
    // Only the border animates: it starts slightly oversized, fades in, and
    // settles onto the viewport. Content and scrolling remain fully visible.
    bool enabled = true;
    float durationSeconds = 0.44f;
    float delaySeconds = 0.0f;
    float startScale = 1.06f;
    float startOpacity = 0.0f;
    DisplayTransitionCurve2D curve = DisplayTransitionCurve2D::eEaseOut;
};

struct UiScrollViewportBuildContext
{
    // buildContent is invoked immediately. Add children to content using layer
    // (or a higher layer) and normal logical-pixel offsets.
    UiBuilder& ui;
    entt::entity content = entt::null;
    int32_t layer = 1;
    uint32_t order = 0u;
    glm::vec2 viewportSize { 0.0f };
};

struct UiScrollViewportOptions
{
    // contentHeight is the complete logical height, including vertical padding.
    float contentHeight = 0.0f;
    glm::vec4 contentPadding { 10.0f, 8.0f, 18.0f, 8.0f };
    float scrollStep = 42.0f;
    bool enabled = true;
    bool showScrollbar = true;
    bool scrollbarFadesWhenIdle = true;
    bool showEdgeFades = false;
    float cornerRadius = 8.0f;
    std::string backgroundColor = "rgba(255, 255, 255, 0.96)";
    std::string backgroundBottomColor {};
    std::string outlineColor = "#0A84FFFF";
    float outlineWidth = 1.8f;
    float opacity = 1.0f;
    float backdropBlurRadius = 0.0f;
    uint32_t backdropBlurPasses = 1u;
    float backdropBlurOpacity = 0.85f;
    int32_t layer = 1;
    uint32_t order = 0u;
    UiScrollBarOptions scrollbar {};
    UiScrollEdgeFadeOptions edgeFades {};
    UiScrollViewportEntranceOptions entrance {};
    std::function<void(const UiScrollViewportBuildContext&)> buildContent;
    std::function<void(const ScrollInputEvent&)> onScroll;
};

struct UiScrollViewportHandle
{
    // root is the positioned layout container; viewport owns surface paint,
    // clipping, and scrolling. border owns the entrance transition. Add app
    // content to content.
    entt::entity root = entt::null;
    entt::entity viewport = entt::null;
    entt::entity border = entt::null;
    entt::entity content = entt::null;
    UiScrollBarHandle scrollbar {};
    UiScrollViewHandle scrollView {};
};

enum class UiNavigationDirection : uint8_t
{
    eBack,
    eForward
};

struct UiIconButtonOptions
{
    // Circle or squircle icon-only button for toolbar, title-row, and navigation actions
    Media2DHandle icon {};
    bool enabled = true;
    float cornerRadius = 0.0f;
    float iconSize = 16.0f;
    std::string backgroundColor = "rgba(255, 255, 255, 0.90)";
    std::string backgroundBottomColor {};
    std::string hoveredBackgroundColor = "rgba(255, 255, 255, 1)";
    std::string pressedBackgroundColor = "rgba(233, 233, 235, 0.96)";
    std::string disabledBackgroundColor = "rgba(242, 242, 242, 0.50)";
    std::string outlineColor = "#00000018";
    float outlineWidth = 0.8f;
    float backdropBlurRadius = 0.0f;
    uint32_t backdropBlurPasses = 1u;
    float backdropBlurOpacity = 0.85f;
    UiGlassOptions glass {};
    std::optional<StretchDynamicsOptions> stretchDynamics {};
    std::string iconTint = "#55585EFF";
    std::string disabledIconTint = "#B1B4BAFF";
    std::function<void(const PointerInputEvent&)> onClick;
    // History mode owns onClick and enables/disables the button from its cursor.
    bool useHistory = false;
    UiNavigationDirection historyDirection = UiNavigationDirection::eBack;
    std::size_t initialPage = 0u;
    std::function<void(std::size_t, const PointerInputEvent&)> onNavigatePage;
};

struct UiSearchFieldOptions
{
    // Single-line search input with optional leading icon and focused styling
    Media2DHandle icon {};
    std::string placeholder = "Search";
    std::size_t maxBytes = 256u;
    std::string placeholderColor = "#707075E8";
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
    std::function<void(const std::string&)> onChanged;
};

struct UiSearchIndexEntry
{
    // One searchable destination; id is app-defined and scrollOffset is logical pixels
    std::size_t id = 0u;
    float scrollOffset = 0.0f;
    Media2DHandle icon {};
    std::string title;
    std::string subtitle;
    std::string keywords;
};

struct UiSearchDestinationSpec
{
    // Declarative source for one search result before localisation is resolved
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
    // Converts translatable search specs into runtime strings for filtering and display
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
    // Runtime state for the floating result list attached to a search input
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
    std::string noResultsTitle = "No results";
    std::string noResultsSubtitle = "Try another search";
    std::size_t maxTitleCharacters = 0u;
    std::size_t maxSubtitleCharacters = 0u;
    std::function<void(const UiSearchIndexEntry&, const PointerInputEvent&)> onSelected;
};

struct UiSearchResultListOptions
{
    // Search lists keep result drawing reusable while the app decides what a hit means
    std::vector<UiSearchIndexEntry> entries;
    std::size_t minQueryBytes = 2u;
    uint32_t maxResults = 5u;
    uint32_t maxVisibleResults = 0u;
    float rowHeight = 46.0f;
    float menuGap = 6.0f;
    float verticalPadding = 5.0f;
    bool showNoResults = true;
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

struct UiSwitchControlComponent
{
    bool checked = false;
    entt::entity knob = entt::null;
    glm::vec2 offOffset { 0.0f };
    glm::vec2 onOffset { 0.0f };
    glm::vec2 currentOffset { 0.0f };
    glm::vec2 animationStartOffset { 0.0f };
    glm::vec2 animationTargetOffset { 0.0f };
    double animationStartSeconds = 0.0;
    float animationDurationSeconds = 0.18f;
    bool animateKnob = true;
    bool animating = false;
    bool animationPendingStart = false;
    ShapeStyleComponent offIdle;
    ShapeStyleComponent offHovered;
    ShapeStyleComponent offPressed;
    ShapeStyleComponent onIdle;
    ShapeStyleComponent onHovered;
    ShapeStyleComponent onPressed;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

struct UiSwitchOptions
{
    bool checked = false;
    bool enabled = true;
    float minimumWidthRatio = 2.25f;
    float knobInset = 3.0f;
    float knobWidthRatio = 1.75f;
    float knobEdgeSoftness = 0.5f;
    bool knobSdfEdges = true;
    float animationDurationSeconds = 0.18f;
    bool animateKnob = true;
    std::string offColor = "#E2E2E4FF";
    std::string offHoveredColor = "#ECECEEFF";
    std::string offPressedColor = "#D6D6D9FF";
    std::string offOutlineColor = "#00000014";
    std::string onColor = "#007AFFFF";
    std::string onHoveredColor = "#1689FFFF";
    std::string onPressedColor = "#006DDEFF";
    std::string onOutlineColor = "#0068D8FF";
    std::string knobColor = "#FFFFFFFF";
    std::string knobOutlineColor = "#00000000";
    float trackOutlineWidth = 0.8f;
    float knobOutlineWidth = 0.0f;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

enum class UiSliderFillStyle : uint8_t
{
    eRoundedFill,
    eFlatFillClippedByTrack
};

struct UiSliderOptions
{
    // Reusable horizontal slider built on the engine slider input component
    float value = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float step = 0.0f;
    float snapPercentage = 0.0f;
    bool enabled = true;
    bool dimWhenDisabled = true;
    UiSliderFillStyle fillStyle = UiSliderFillStyle::eFlatFillClippedByTrack;
    float thumbDiameter = 22.0f;
    glm::vec2 thumbSize { 0.0f, 0.0f };
    // Values at or below the normal size disable hover expansion.
    float hoveredTrackHeight = 0.0f;
    float hoveredThumbScale = 1.0f;
    std::string trackColor = "#E2E2E4FF";
    std::string fillColor = "#007AFFFF";
    std::string thumbColor = "#FFFFFFFF";
    std::string outlineColor = "#00000014";
    std::string thumbOutlineColor = "#00000018";
    float outlineWidth = 0.8f;
    float thumbOutlineWidth = 0.8f;
    // Smooth scrubbing eases only the rendered fill and thumb, not the reported value
    bool smoothScrubbing = false;
    float visualSmoothingRate = 24.0f;
    std::function<void(const SliderInputEvent&)> onChanged;
    std::function<void(const SliderInputEvent&)> onCommitted;
    bool renderThumbShadow = true;
};

struct UiCircularProgressComponent
{
    // The root is the track; indicator owns the partial foreground arc.
    entt::entity indicator = entt::null;
    float value = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    glm::vec4 progressColor { 1.0f };
    glm::vec4 progressEndColor { 1.0f };
    bool progressUsesGradient = false;
    bool contentUsesProgressColor = false;
};

struct UiCircularProgressOptions
{
    // Supplying an end colour changes that arc from solid to an angular gradient.
    float value = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float thickness = 6.0f;
    float startAngleRadians = -1.57079632679f;
    bool clockwise = true;
    std::string trackColor = "#FFFFFF18";
    std::string trackEndColor {};
    std::string progressColor = "#6EE77CFF";
    std::string progressEndColor {};
    // Bound centre content inherits the solid progress colour or the complete
    // progress gradient. Call ui_apply_circular_progress_content_color after
    // creating the text, media, icon, or shape inside the progress root.
    bool contentUsesProgressColor = false;
    float opacity = 1.0f;
    float edgeSoftness = 0.8f;
};

struct UiRadioControlComponent
{
    bool checked = false;
    entt::entity outer = entt::null;
    entt::entity indicator = entt::null;
    entt::entity label = entt::null;
    std::string groupId;
    bool singleSelect = false;
    ShapeStyleComponent enabledIdle;
    ShapeStyleComponent enabledHovered;
    ShapeStyleComponent enabledPressed;
    ShapeStyleComponent disabled;
    ShapeStyleComponent enabledIndicator;
    ShapeStyleComponent disabledIndicator;
    TextStyleComponent enabledLabel;
    TextStyleComponent disabledLabel;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

struct UiRadioOptions
{
    // Use singleSelect with groupId when only one radio in a group may be active
    bool checked = false;
    bool enabled = true;
    bool singleSelect = false;
    std::string groupId;
    std::string enabledColor = "#0D6FFFFF";
    std::string hoveredColor = "#2A82FFFF";
    std::string pressedColor = "#0061D5FF";
    std::string disabledColor = "#B7BBC2FF";
    std::string outlineColor = "#00000014";
    std::string disabledOutlineColor = "#0000000F";
    std::string indicatorColor = "#FFFFFFFF";
    std::string disabledIndicatorColor = "#F7F7F7FF";
    std::string labelColor = "#1D1D1FFF";
    std::string disabledLabelColor = "#92969DFF";
    float diameter = 14.0f;
    float indicatorDiameter = 6.0f;
    float outlineWidth = 0.8f;
    float disabledOutlineWidth = 0.6f;
    float labelFontSize = 13.0f;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

struct UiCheckboxControlComponent
{
    bool checked = false;
    entt::entity box = entt::null;
    entt::entity indicator = entt::null;
    ShapeStyleComponent uncheckedStyle;
    ShapeStyleComponent checkedStyle;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

struct UiCheckboxOptions
{
    // Checkbox uses the same row hit-test pattern while only the square changes visually
    bool checked = false;
    bool enabled = true;
    Media2DHandle checkIcon {};
    std::string uncheckedColor = "#FFFFFFFF";
    std::string checkedColor = "#0D6FFFFF";
    std::string outlineColor = "#8D8D92FF";
    std::string checkTint = "#FFFFFFFF";
    std::string labelColor = "#1D1D1FFF";
    float labelFontSize = 13.0f;
    std::function<void(bool, const PointerInputEvent&)> onChanged;
};

struct UiDropdownControlComponent
{
    // Dropdown state lives on the button root so menu rows can update it
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
    // Dropdown draws the closed button and its floating menu as one reusable control
    Media2DHandle chevronIcon {};
    Media2DHandle checkIcon {};
    int selectedIndex = 0;
    bool enabled = true;
    Renderer2DPrimitive buttonPrimitive = Renderer2DPrimitive::eSquircle;
    float buttonCornerRadius = 18.0f;
    float buttonSquircleAmount = 0.74f;
    float buttonSquirclePower = 4.7f;
    float buttonFontSize = 13.5f;
    float buttonFontWeight = 460.0f;
    float buttonIconSize = 14.0f;
    // Padding controls the label inset; buttonTextIconGap controls label to chevron space
    glm::vec4 buttonPadding { 4.0f, 0.0f, 4.0f, 0.0f };
    float buttonTextIconGap = 18.0f;
    std::string buttonTextColor = "#1D1D1FFF";
    std::string buttonIconTint = "#1D1D1FFF";
    std::string buttonBackgroundColor = "#F2F2F4FF";
    std::string buttonHoveredBackgroundColor = "#EAEAECFF";
    std::string buttonPressedBackgroundColor = "#E2E2E4FF";
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
    float rowHeight = 24.0f;
    float menuGap = 4.0f;
    float menuWidth = 0.0f;
    uint32_t maxVisibleItems = 8u;
    float scrollStepRows = 1.0f;
    std::size_t maxRowCharacters = 0u;
    bool openAbove = false;
    std::function<void(int, std::string_view, const PointerInputEvent&)> onChanged;
    std::string backgroundColor = "rgba(176, 176, 176, 1)";
    std::string menuOutlineColor = "rgba(0, 0, 0, 0)";
    float menuOutlineWidth = 0.0f;
    std::string rowTextColor = "#1A1A1AFF";
    std::string rowHoveredTextColor = "#FFFFFFFF";
    std::string rowCheckTint = "#000000FF";
    std::string rowHoveredCheckTint = "#FFFFFFFF";
    std::string rowHoveredColor = "#0069E0FF";
    std::string rowPressedColor = "#0069E0FF";
};

struct UiNavClusterOptions
{
    // Back and forward cluster can be plain buttons or stateful history controls
    Media2DHandle backIcon {};
    Media2DHandle forwardIcon {};
    bool backEnabled = true;
    bool forwardEnabled = true;
    float cornerRadius = 0.0f;
    float iconSize = 16.0f;
    std::string backgroundColor = "rgba(255, 255, 255, 0.90)";
    std::string backgroundBottomColor {};
    float backgroundOpacity = 1.0f;
    std::string outlineColor = "#707075E8";
    float outlineWidth = 1.4f;
    float backdropBlurRadius = 0.0f;
    uint32_t backdropBlurPasses = 1u;
    float backdropBlurOpacity = 0.85f;
    UiGlassOptions glass {};
    std::string dividerColor = "rgba(0, 0, 0, 0.08)";
    std::string iconTint = "#55585EFF";
    std::string disabledIconTint = "#B1B4BAFF";
    std::function<void(const PointerInputEvent&)> onBack;
    std::function<void(const PointerInputEvent&)> onForward;
    bool useHistory = false;
    std::size_t initialPage = 0u;
    std::function<void(std::size_t, const PointerInputEvent&)> onNavigatePage;
};

struct UiSettingsShellOptions
{
    // Reusable two-pane settings shell with fixed chrome and scrollable content slots
    UiNavClusterOptions nav {};
    UiSearchFieldOptions search {};
    UiScrollEdgeFadeOptions sidebarEdgeFades {};
    UiScrollEdgeFadeOptions rightEdgeFades {};
    UiScrollBarOptions sidebarScrollbar {};
    UiScrollBarOptions rightScrollbar {};
    float panelHeight = 1.0f;
    glm::vec2 navOffset { 10.0f, 12.0f };
    glm::vec2 navSize { 96.0f, 40.0f };
    int32_t navLayer = 2;
    uint32_t navOrder = 0u;
    int32_t searchLayer = 6;
    uint32_t searchOrder = 1u;
    float sidebarStaticTop = 72.0f;
    float sidebarSearchHeight = 30.0f;
    glm::vec2 sidebarSearchMargin { 22.0f, 14.0f };
    float sidebarViewportTop = 0.0f;
    float sidebarBottom = 0.0f;
    glm::vec2 sidebarContentMargin { 22.0f, 14.0f };
    float sidebarContentInitialOffset = 72.0f;
    float sidebarContentHeight = 840.0f;
    float sidebarScrollStep = 34.0f;
    int32_t sidebarViewportLayer = 1;
    uint32_t sidebarViewportOrder = 0u;
    int32_t sidebarContentLayer = 1;
    uint32_t sidebarContentOrder = 1u;
    bool sidebarFades = true;
    bool sidebarScrollbarEnabled = true;
    bool fixedChromeDynamicCache = false;
    glm::vec4 rightViewportMargin { 32.0f, 72.0f, 28.0f, 24.0f };
    float rightScrollStep = 44.0f;
    int32_t rightViewportLayer = 1;
    uint32_t rightViewportOrder = 0u;
    bool rightFades = true;
    bool rightScrollbarEnabled = true;
    // Called by the shell after the right viewport scroll offset changes
    std::function<void(const ScrollInputEvent&)> onRightScroll;
};

struct UiSettingsShellHandle
{
    // Returned entities let the app fill scrollable areas without knowing shell internals
    UiControlHandle navCluster {};
    UiControlHandle search {};
    UiScrollViewHandle sidebar {};
    UiScrollViewHandle right {};
    entt::entity sidebarContent = entt::null;
    entt::entity rightViewport = entt::null;
    UiScrollBarHandle rightScrollbar {};
    float sidebarViewportHeight = 0.0f;
    float rightViewportHeight = 0.0f;
};

using UiSplitShellOptions = UiSettingsShellOptions;
using UiSplitShellHandle = UiSettingsShellHandle;

struct UiNavigationHistoryComponent
{
    // History is stored on either a nav cluster or a history-enabled icon button.
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

// Compatibility name retained for applications that inspect nav-cluster state.
using UiNavClusterHistoryComponent = UiNavigationHistoryComponent;

struct UiSidebarNavItemHandle
{
    // Exposes both glyph and media slots because a nav item may use either style
    entt::entity row = entt::null;
    entt::entity icon = entt::null;
    entt::entity glyph = entt::null;
    entt::entity mediaIcon = entt::null;
    entt::entity label = entt::null;
};

struct UiSidebarRichButtonHandle
{
    // Rich rows are useful for account cards, project pickers, or any two-line item
    entt::entity row = entt::null;
    entt::entity mediaFrame = entt::null;
    entt::entity media = entt::null;
    entt::entity avatar = entt::null;
    entt::entity avatarMedia = entt::null;
    entt::entity title = entt::null;
    entt::entity subtitle = entt::null;
};

struct UiSplitShellRuntimeState
{
    // Optional state bucket for apps that rebuild pages inside a split shell
    entt::entity titleText = entt::null;
    entt::entity navCluster = entt::null;
    UiSidebarRichButtonHandle featuredRow {};
    entt::entity rightViewport = entt::null;
    entt::entity rightContent = entt::null;
    UiScrollBarHandle rightScrollbar {};
    float rightViewportHeight = 0.0f;
    float rightContentInitialOffset = 0.0f;
    float pendingRightScrollOffset = -1.0f;
    std::size_t activeIndex = 0u;
    std::vector<UiSidebarNavItemHandle> sidebarItems;
};

struct UiSidebarNavItemOptions
{
    // Sidebar nav item accepts either a glyph or a media icon
    std::string glyph;
    Media2DHandle mediaIcon {};
    std::string iconColor = "#0A84FFFF";
    bool selected = false;
    float rowHeight = 36.0f;
    float rowCornerRadius = 9.0f;
    float iconSize = 26.0f;
    float iconCornerRadius = 7.0f;
    UiAlignment labelAlignment = UiAlignment::eMiddleLeft;
    glm::vec2 iconOffset { 8.0f, 0.0f };
    glm::vec2 labelOffset { 44.0f, 0.0f };
    float labelFontSize = 15.0f;
    std::size_t maxLabelCharacters = 0u;
    float glyphFontSize = 11.0f;
    glm::vec2 mediaIconSize { 16.0f, 16.0f };
    Media2DFit mediaIconFit = Media2DFit::eContain;
    bool mediaIconUsesContainer = true;
    bool showIconBackground = true;
    std::string selectedRowColor = "rgba(0, 113, 227, 1)";
    std::string selectedHoveredRowColor = "rgba(0, 103, 207, 1)";
    std::string selectedPressedRowColor = "rgba(0, 92, 190, 1)";
    std::string hoveredRowColor = "rgba(255, 255, 255, 0.55)";
    std::string pressedRowColor = "rgba(230, 230, 232, 0.75)";
    std::string clearRowColor = "rgba(255, 255, 255, 0.002)";
    std::string selectedIconColor = "#111111FF";
    std::string iconOutlineColor = "rgba(255, 255, 255, 0.34)";
    float iconOutlineWidth = 0.6f;
    std::string selectedLabelColor = "#FFFFFFFF";
    std::string labelColor = "#2C2D30FF";
    float selectedLabelWeight = 590.0f;
    float labelWeight = 450.0f;
    std::function<void(const PointerInputEvent&)> onClick;
};

struct UiSidebarRichButtonOptions
{
    // Rich sidebar row for any title and subtitle item, not just account/profile rows
    Media2DHandle media {};
    Media2DHandle avatar {};
    std::string title;
    std::string subtitle;
    bool selected = false;
    bool showMedia = true;
    float rowHeight = 56.0f;
    float rowCornerRadius = 12.0f;
    float mediaRadius = 0.0f;
    glm::vec2 mediaCenter { 28.0f, 28.0f };
    std::string mediaBackgroundColor {};
    std::string mediaOutlineColor {};
    Media2DFit mediaFit = Media2DFit::eCover;
    float avatarRadius = 23.0f;
    glm::vec2 avatarCenter { 28.0f, 28.0f };
    glm::vec2 textOffset { 70.0f, 10.0f };
    float titleFontSize = 16.0f;
    float subtitleFontSize = 13.0f;
    std::size_t maxTitleCharacters = 0u;
    std::size_t maxSubtitleCharacters = 0u;
    std::string selectedRowColor = "rgba(0, 113, 227, 1)";
    std::string selectedHoveredRowColor = "rgba(0, 103, 207, 1)";
    std::string selectedPressedRowColor = "rgba(0, 92, 190, 1)";
    std::string hoveredRowColor = "rgba(255, 255, 255, 0.55)";
    std::string pressedRowColor = "rgba(230, 230, 232, 0.75)";
    std::string clearRowColor = "rgba(255, 255, 255, 0.002)";
    std::string avatarBackgroundColor = "rgba(222, 166, 130, 1)";
    std::string avatarOutlineColor = "#FFFFFF96";
    std::string titleColor = "#2C2D30FF";
    std::string subtitleColor = "#6F7278FF";
    std::string selectedTitleColor = "#FFFFFFFF";
    std::string selectedSubtitleColor = "rgba(255, 255, 255, 0.82)";
    float titleWeight = 700.0f;
    float subtitleWeight = 430.0f;
    std::function<void(const PointerInputEvent&)> onClick;
};

struct UiCommonIconSet
{
    // Common controls use these icons, but callers decide where assets live
    Media2DHandle search {};
    Media2DHandle check {};
    Media2DHandle chevronLeft {};
    Media2DHandle chevronRight {};
    Media2DHandle chevronDown {};
    Media2DHandle chevronUp {};
};

struct UiThemePreviewHandle
{
    entt::entity root = entt::null;
    entt::entity label = entt::null;
};

struct UiThemePreviewOptions
{
    // Small theme thumbnail that can be reused in settings, pickers, or galleries
    UiTheme theme {};
    std::string label;
    bool selected = false;
    bool dark = false;
    glm::vec2 size { 88.0f, 58.0f };
    glm::vec2 labelOffset { 22.0f, 63.0f };
    float cornerRadius = 9.0f;
    float labelFontSize = 13.0f;
    float selectedOutlineWidth = 2.6f;
    float outlineWidth = 0.8f;
    int32_t layer = 3;
    uint32_t order = 0u;
};

inline Media2DLoadOptions ui_svg_icon_load_options(uint32_t rasterSize = 128u)
{
    // SVG icons are rasterised once and then tinted by individual controls
    Media2DLoadOptions options = {};
    options.rasterWidth = rasterSize;
    options.rasterHeight = rasterSize;
    options.premultiplyAlpha = true;
    return options;
}

template <typename EngineLike>
inline UiCommonIconSet ui_load_common_icon_set(
    EngineLike& engine,
    const std::filesystem::path& assetRoot = {},
    const Media2DLoadOptions& loadOptions = ui_svg_icon_load_options())
{
    // Keeps standard control icon paths in one place while staying engine-instance agnostic
    if (assetRoot.empty())
    {
        return {};
    }
    const std::filesystem::path svgRoot = assetRoot / "svg" / "static";
    UiCommonIconSet icons = {};
    icons.search = engine.load_media_2d(svgRoot / "icons" / "search.svg", loadOptions);
    icons.check = engine.load_media_2d(svgRoot / "icons" / "check.svg", loadOptions);
    icons.chevronLeft = engine.load_media_2d(svgRoot / "arrows" / "chevron_left.svg", loadOptions);
    icons.chevronRight = engine.load_media_2d(svgRoot / "arrows" / "chevron_right.svg", loadOptions);
    icons.chevronDown = engine.load_media_2d(svgRoot / "arrows" / "chevron_down.svg", loadOptions);
    icons.chevronUp = engine.load_media_2d(svgRoot / "arrows" / "chevron_up.svg", loadOptions);
    return icons;
}

template <typename EngineLike>
inline UiCommonIconSet ui_load_common_icon_set(
    EngineLike& engine,
    const UiResourceDirectories& resources,
    const Media2DLoadOptions& loadOptions = ui_svg_icon_load_options())
{
    const std::filesystem::path svgRoot = std::filesystem::path("svg") / "static";
    const auto loadLayeredIcon = [&engine, &resources, &loadOptions](
        const std::filesystem::path& relativePath) {
        Media2DHandle handle = {};
        for (const std::filesystem::path& path : resources.asset_candidates(relativePath))
        {
            handle = engine.load_media_2d(path, loadOptions);
            if (handle.drawable)
            {
                return handle;
            }
        }
        return Media2DHandle {};
    };
    UiCommonIconSet icons = {};
    icons.search = loadLayeredIcon(svgRoot / "icons" / "search.svg");
    icons.check = loadLayeredIcon(svgRoot / "icons" / "check.svg");
    icons.chevronLeft = loadLayeredIcon(svgRoot / "arrows" / "chevron_left.svg");
    icons.chevronRight = loadLayeredIcon(svgRoot / "arrows" / "chevron_right.svg");
    icons.chevronDown = loadLayeredIcon(svgRoot / "arrows" / "chevron_down.svg");
    icons.chevronUp = loadLayeredIcon(svgRoot / "arrows" / "chevron_up.svg");
    return icons;
}

inline void ui_apply_sidebar_nav_palette(const UiTheme& theme, UiSidebarNavItemOptions& options)
{
    // Shared sidebar row palette for any navigation list, not just settings
    options.selectedRowColor = theme.settingsPanel.sidebarRowSelected;
    options.selectedHoveredRowColor = theme.settingsPanel.sidebarRowSelectedHover;
    options.selectedPressedRowColor = theme.settingsPanel.sidebarRowSelectedPressed;
    options.hoveredRowColor = theme.settingsPanel.sidebarRowHover;
    options.pressedRowColor = theme.settingsPanel.sidebarRowPressed;
    options.labelColor = theme.settingsPanel.sidebarLabel;
    options.selectedLabelColor = theme.settingsPanel.sidebarSelectedLabel;
}

inline void ui_apply_rich_sidebar_row_palette(const UiTheme& theme, UiSidebarRichButtonOptions& options)
{
    // Rich rows reuse the same navigation states with separate title and subtitle colours
    options.selectedRowColor = theme.settingsPanel.sidebarRowSelected;
    options.selectedHoveredRowColor = theme.settingsPanel.sidebarRowSelectedHover;
    options.selectedPressedRowColor = theme.settingsPanel.sidebarRowSelectedPressed;
    options.hoveredRowColor = theme.settingsPanel.sidebarRowHover;
    options.pressedRowColor = theme.settingsPanel.sidebarRowPressed;
    options.titleColor = theme.settingsPanel.sidebarLabel;
    options.subtitleColor = theme.settingsPanel.sidebarSubLabel;
    options.selectedTitleColor = theme.settingsPanel.sidebarSelectedLabel;
    options.selectedSubtitleColor = "rgba(255, 255, 255, 0.82)";
    options.mediaBackgroundColor = theme.settingsPanel.avatarBackground;
}

inline UiDropdownOptions ui_dropdown_options_from_theme(
    const UiTheme& theme,
    Media2DHandle chevronIcon = {},
    Media2DHandle checkIcon = {})
{
    // Default dropdown styling follows the active theme while keeping menu behaviour configurable
    UiDropdownOptions options = {};
    options.chevronIcon = chevronIcon;
    options.checkIcon = checkIcon;
    options.buttonTextColor = theme.text;
    options.buttonIconTint = theme.icon;
    options.buttonBackgroundColor = theme.surfaceMuted;
    options.buttonHoveredBackgroundColor = theme.surfaceElevated;
    options.buttonPressedBackgroundColor = theme.settingsPanel.cardSurface;
    options.backgroundColor = theme.surfaceMuted;
    options.rowHoveredColor = theme.accent;
    options.rowPressedColor = theme.accentPressed;
    options.rowTextColor = theme.text;
    options.rowHoveredTextColor = theme.settingsPanel.sidebarSelectedLabel;
    options.rowCheckTint = theme.text;
    options.rowHoveredCheckTint = theme.settingsPanel.sidebarSelectedLabel;
    return options;
}

inline UiSwitchOptions ui_switch_options_from_theme(const UiTheme& theme, bool checked = false)
{
    // Switch colours are centralised so examples and real panels match
    UiSwitchOptions options = {};
    options.checked = checked;
    options.offColor = theme.surfaceMuted;
    options.offHoveredColor = theme.surfaceElevated;
    options.offPressedColor = theme.settingsPanel.cardSurface;
    options.offOutlineColor = theme.settingsPanel.cardOutline;
    options.onColor = theme.accent;
    options.onHoveredColor = theme.accentHover;
    options.onPressedColor = theme.accentPressed;
    options.onOutlineColor = theme.accentPressed;
    return options;
}

inline UiSliderOptions ui_slider_options_from_theme(const UiTheme& theme, float value = 0.0f)
{
    const UiSwitchOptions switchOptions = ui_switch_options_from_theme(theme, false);
    UiSliderOptions options = {};
    options.value = value;
    options.fillStyle = UiSliderFillStyle::eFlatFillClippedByTrack;
    options.trackColor = switchOptions.offColor;
    options.fillColor = switchOptions.onColor;
    options.thumbColor = switchOptions.knobColor;
    options.outlineColor = switchOptions.offOutlineColor;
    options.thumbOutlineColor = switchOptions.knobOutlineColor;
    options.outlineWidth = switchOptions.trackOutlineWidth;
    options.thumbOutlineWidth = switchOptions.knobOutlineWidth;
    return options;
}

inline UiSearchFieldOptions ui_search_field_options_from_theme(
    const UiTheme& theme,
    Media2DHandle searchIcon = {},
    std::string placeholder = "Search")
{
    // Search field preset covers both idle and focused chrome
    UiSearchFieldOptions options = {};
    options.icon = searchIcon;
    options.placeholder = std::move(placeholder);
    options.placeholderColor = theme.settingsPanel.searchPlaceholder;
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

inline UiNavClusterOptions ui_nav_cluster_options_from_theme(
    const UiTheme& theme,
    Media2DHandle backIcon = {},
    Media2DHandle forwardIcon = {})
{
    // Navigation clusters are generic back and forward controls with optional history
    UiNavClusterOptions options = {};
    options.backIcon = backIcon;
    options.forwardIcon = forwardIcon;
    options.iconSize = 17.0f;
    options.backgroundColor = theme.settingsPanel.navBackground;
    options.backgroundBottomColor = theme.settingsPanel.navBackgroundBottom;
    options.outlineColor = theme.settingsPanel.searchOutline;
    options.outlineWidth = 1.4f;
    options.backdropBlurRadius = 18.0f;
    options.backdropBlurPasses = 2u;
    options.backdropBlurOpacity = 0.9f;
    options.dividerColor = theme.settingsPanel.navDivider;
    options.iconTint = theme.settingsPanel.navIcon;
    options.disabledIconTint = theme.settingsPanel.navIconDisabled;
    return options;
}

inline UiScrollBarOptions ui_scrollbar_options_from_theme(
    const UiTheme& theme,
    float right,
    float top,
    float bottom,
    uint32_t order)
{
    // Thin fading scrollbar used by scroll views and split panels
    UiScrollBarOptions options = {};
    options.width = 4.0f;
    options.right = right;
    options.top = top;
    options.bottom = bottom;
    options.minThumbHeight = 42.0f;
    options.thumbColor = theme.settingsPanel.scrollbarThumb;
    options.thumbOutlineColor = theme.settingsPanel.scrollbarThumbOutline;
    options.layer = 7;
    options.order = order;
    return options;
}

inline UiThemePreviewHandle ui_create_theme_preview(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    glm::vec2 offset,
    const UiThemePreviewOptions& options)
{
    UiThemePreviewHandle handle = {};
    ShapeStyleComponent frameStyle = make_solid_style(
        options.dark ? "#0E1324FF" : "#F8FBFFFF",
        options.selected ? options.theme.accent : "rgba(0, 0, 0, 0.12)",
        scaled_scalar(options.selected ? options.selectedOutlineWidth : options.outlineWidth, ui.scale()),
        1.0f);
    handle.root = ui_create_fixed_block(
        ui,
        parent,
        offset,
        options.size,
        frameStyle,
        options.cornerRadius,
        options.layer,
        options.order);

    ShapeStyleComponent stripStyle = make_gradient_style(
        options.dark ? "#1B3A8AFF" : "#7CC7FFFF",
        options.dark ? "#071023FF" : "#E7F7FFFF",
        "rgba(0, 0, 0, 0)",
        0.0f,
        1.0f);
    ui_create_fixed_block(
        ui,
        handle.root,
        { 5.0f, 7.0f },
        { std::max(1.0f, options.size.x - 10.0f), 24.0f },
        stripStyle,
        5.0f,
        options.layer + 1,
        options.order);
    ui_create_fixed_block(
        ui,
        handle.root,
        { 8.0f, 11.0f },
        { 44.0f, 11.0f },
        make_solid_style(
            options.dark ? "#006BFFFF" : "#85D4FFFF",
            "rgba(0, 0, 0, 0.12)",
            0.4f,
            1.0f),
        3.0f,
        options.layer + 2,
        options.order);
    ui_create_dot(ui, handle.root, { 28.0f, 42.0f }, 4.0f, "#FF635DFF", options.layer + 2, options.order);
    ui_create_dot(ui, handle.root, { 44.0f, 42.0f }, 4.0f, "#FFCC00FF", options.layer + 2, options.order + 1u);
    ui_create_dot(ui, handle.root, { 60.0f, 42.0f }, 4.0f, "#34C759FF", options.layer + 2, options.order + 2u);

    handle.label = ui.create_aligned_text(
        options.label,
        fontAtlas,
        parent,
        UiAlignment::eTopLeft,
        options.labelFontSize,
        options.theme.text_style(
            options.selected ? options.theme.settingsPanel.selectedPreviewLabel : options.theme.settingsPanel.previewLabel,
            options.selected ? 620.0f : 440.0f),
        options.layer + 1,
        options.order + 10u,
        offset + options.labelOffset);
    return handle;
}

inline TextStyleComponent ui_macos26_text_style(std::string_view color = "#1D1D1FFF")
{
    TextStyleComponent style = {};
    style.set_color(color);
    style.shadowColor = glm::vec4(0.0f);
    style.effectColor = glm::vec4(0.0f);
    return style;
}

inline ShapeStyleComponent ui_macos26_control_style(
    UiBuilder& ui,
    std::string_view fill,
    std::string_view outline = "#00000018",
    float outlineWidth = 0.8f,
    float opacity = 1.0f)
{
    return make_solid_style(fill, outline, scaled_scalar(outlineWidth, ui.scale()), opacity);
}

inline ShapeStyleComponent ui_macos26_frosted_control_style(
    UiBuilder& ui,
    std::string_view topColor,
    std::string_view bottomColor,
    std::string_view outline = "#00000018",
    float outlineWidth = 0.8f,
    float opacity = 1.0f,
    float backdropBlurRadius = 16.0f,
    uint32_t backdropBlurPasses = 2u,
    float backdropBlurOpacity = 0.85f)
{
    return make_frosted_panel_style(
        topColor,
        bottomColor,
        outline,
        scaled_scalar(outlineWidth, ui.scale()),
        opacity,
        scaled_scalar(backdropBlurRadius, ui.scale()),
        backdropBlurPasses,
        backdropBlurOpacity);
}

inline ButtonVisualComponent ui_macos26_button_visual(UiBuilder& ui, bool primary = false)
{
    ButtonVisualComponent visual = {};
    if (primary)
    {
        visual.idle = ui_macos26_control_style(ui, "#007AFFFF", "#0068D8FF", 0.8f, 1.0f);
        visual.hovered = ui_macos26_control_style(ui, "#1387FFFF", "#006FE8FF", 0.9f, 1.0f);
        visual.pressed = ui_macos26_control_style(ui, "#0068DDFF", "#0057BDFF", 1.0f, 1.0f);
        visual.disabled = ui_macos26_control_style(ui, "#BFC7D0A8", "#00000010", 0.6f, 1.0f);
    }
    else
    {
        visual.idle = ui_macos26_control_style(ui, "#F8F8F8F0", "#0000001F", 0.8f, 1.0f);
        visual.hovered = ui_macos26_control_style(ui, "#FFFFFFFF", "#0000002A", 0.9f, 1.0f);
        visual.pressed = ui_macos26_control_style(ui, "#E9E9EBF5", "#00000030", 1.0f, 1.0f);
        visual.disabled = ui_macos26_control_style(ui, "#F2F2F280", "#00000010", 0.6f, 1.0f);
    }
    visual.hasDisabled = true;
    return visual;
}

inline ButtonVisualComponent ui_button_visual_from_options(
    UiBuilder& ui,
    const UiButtonOptions& options)
{
    ButtonVisualComponent visual = ui_macos26_button_visual(ui, options.primary);
    const auto style = [&ui](const std::string& fill, const std::string& outline, float outlineWidth, const ShapeStyleComponent& fallback) {
        if (fill.empty() && outline.empty())
        {
            return fallback;
        }
        return make_solid_style(
            fill.empty() ? fallback.color0 : renderer2d_hex_color(fill),
            outline.empty() ? fallback.outlineColor : renderer2d_hex_color(outline, glm::vec4(0.0f)),
            scaled_scalar(outlineWidth, ui.scale()),
            fallback.opacity);
    };

    visual.idle = style(options.backgroundColor, options.outlineColor, options.outlineWidth, visual.idle);
    visual.hovered = style(options.hoveredBackgroundColor, options.hoveredOutlineColor, options.hoveredOutlineWidth, visual.hovered);
    visual.pressed = style(options.pressedBackgroundColor, options.pressedOutlineColor, options.pressedOutlineWidth, visual.pressed);
    visual.disabled = style(options.disabledBackgroundColor, options.disabledOutlineColor, options.disabledOutlineWidth, visual.disabled);
    visual.hasDisabled = true;
    return visual;
}

inline ButtonVisualComponent ui_transparent_button_visual()
{
    // Preserve the hit area without painting hover, press, or disabled states
    ButtonVisualComponent visual = {};
    visual.idle = ui_clear_surface_style(0.0f);
    visual.hovered = ui_clear_surface_style(0.0f);
    visual.pressed = ui_clear_surface_style(0.0f);
    visual.disabled = ui_clear_surface_style(0.0f);
    visual.hasDisabled = true;
    return visual;
}

inline ShapeStyleComponent ui_icon_button_style(
    UiBuilder& ui,
    const UiIconButtonOptions& options,
    std::string_view fill)
{
    const std::string bottom = options.backgroundBottomColor.empty() ?
        std::string(fill) :
        options.backgroundBottomColor;
    if (options.glass.material == GlassMaterial::eOff &&
        options.backdropBlurRadius > 0.0f)
    {
        return ui_macos26_frosted_control_style(
            ui,
            fill,
            bottom,
            options.outlineColor,
            options.outlineWidth,
            1.0f,
            options.backdropBlurRadius,
            options.backdropBlurPasses,
            options.backdropBlurOpacity);
    }
    return ui_macos26_control_style(ui, fill, options.outlineColor, options.outlineWidth, 1.0f);
}

inline TextStyleComponent ui_sidebar_nav_label_style(
    const UiSidebarNavItemOptions& options,
    bool selected)
{
    TextStyleComponent style = ui_macos26_text_style(selected ? options.selectedLabelColor : options.labelColor);
    style.set_font_weight(selected ? options.selectedLabelWeight : options.labelWeight);
    return style;
}

inline ShapeStyleComponent ui_sidebar_nav_row_style(
    const UiSidebarNavItemOptions& options,
    bool selected)
{
    return selected ?
        make_solid_style(options.selectedRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f) :
        make_solid_style(options.clearRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f);
}

inline ButtonVisualComponent ui_sidebar_nav_visual(
    const UiSidebarNavItemOptions& options,
    bool selected)
{
    ButtonVisualComponent visual = {};
    visual.idle = ui_sidebar_nav_row_style(options, selected);
    visual.hovered = selected ?
        make_solid_style(options.selectedHoveredRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f) :
        make_solid_style(options.hoveredRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f);
    visual.pressed = selected ?
        make_solid_style(options.selectedPressedRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f) :
        make_solid_style(options.pressedRowColor, "rgba(0, 0, 0, 0)", 0.0f, 1.0f);
    return visual;
}

inline void ui_apply_sidebar_nav_item_state(
    Renderer2DScene& scene,
    const UiSidebarNavItemHandle& handle,
    bool selected,
    UiSidebarNavItemOptions options = {})
{
    entt::registry& registry = scene.registry();
    options.selected = selected;
    if (ShapeStyleComponent* rowStyle = registry.try_get<ShapeStyleComponent>(handle.row))
    {
        *rowStyle = ui_sidebar_nav_row_style(options, selected);
    }
    if (ButtonVisualComponent* visual = registry.try_get<ButtonVisualComponent>(handle.row))
    {
        *visual = ui_sidebar_nav_visual(options, selected);
    }
    if (handle.icon != entt::null && registry.valid(handle.icon))
    {
        if (ShapeStyleComponent* iconStyle = registry.try_get<ShapeStyleComponent>(handle.icon))
        {
            *iconStyle = make_solid_style(
                options.showIconBackground ? (selected ? options.selectedIconColor : options.iconColor) : "rgba(255, 255, 255, 0.002)",
                options.showIconBackground ? options.iconOutlineColor : "rgba(0, 0, 0, 0)",
                options.showIconBackground ? iconStyle->outlineWidth : 0.0f,
                1.0f);
        }
    }
    if (TextStyleComponent* labelStyle = registry.try_get<TextStyleComponent>(handle.label))
    {
        *labelStyle = ui_sidebar_nav_label_style(options, selected);
    }
    ui_update_button_visual(scene, handle.row);
    if (handle.icon != entt::null && registry.valid(handle.icon))
    {
        scene.mark_dirty(handle.icon);
    }
    scene.mark_dirty(handle.label);
}

inline TextStyleComponent ui_sidebar_rich_button_text_style(
    std::string_view color,
    float weight)
{
    TextStyleComponent style = ui_macos26_text_style(color);
    style.set_font_weight(weight);
    return style;
}

inline void ui_apply_sidebar_rich_button_state(
    Renderer2DScene& scene,
    const UiSidebarRichButtonHandle& handle,
    bool selected,
    UiSidebarRichButtonOptions options = {})
{
    entt::registry& registry = scene.registry();
    options.selected = selected;
    UiSidebarNavItemOptions navOptions = {};
    navOptions.selectedRowColor = options.selectedRowColor;
    navOptions.selectedHoveredRowColor = options.selectedHoveredRowColor;
    navOptions.selectedPressedRowColor = options.selectedPressedRowColor;
    navOptions.hoveredRowColor = options.hoveredRowColor;
    navOptions.pressedRowColor = options.pressedRowColor;
    navOptions.clearRowColor = options.clearRowColor;

    if (ShapeStyleComponent* rowStyle = registry.try_get<ShapeStyleComponent>(handle.row))
    {
        *rowStyle = ui_sidebar_nav_row_style(navOptions, selected);
    }
    if (ButtonVisualComponent* visual = registry.try_get<ButtonVisualComponent>(handle.row))
    {
        *visual = ui_sidebar_nav_visual(navOptions, selected);
    }
    if (TextStyleComponent* titleStyle = registry.try_get<TextStyleComponent>(handle.title))
    {
        *titleStyle = ui_sidebar_rich_button_text_style(
            selected ? options.selectedTitleColor : options.titleColor,
            options.titleWeight);
        scene.mark_dirty(handle.title);
    }
    if (TextStyleComponent* subtitleStyle = registry.try_get<TextStyleComponent>(handle.subtitle))
    {
        *subtitleStyle = ui_sidebar_rich_button_text_style(
            selected ? options.selectedSubtitleColor : options.subtitleColor,
            options.subtitleWeight);
        scene.mark_dirty(handle.subtitle);
    }
    ui_update_button_visual(scene, handle.row);
    scene.mark_dirty(handle.row);
}

inline UiSidebarRichButtonHandle ui_create_sidebar_rich_button(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    float y,
    int32_t layer,
    uint32_t order,
    UiSidebarRichButtonOptions options = {})
{
    // Build a selectable sidebar row with optional media and two aligned text lines
    UiSidebarRichButtonHandle handle = {};
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return handle;
    }

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiSidebarNavItemOptions navOptions = {};
    navOptions.selectedRowColor = options.selectedRowColor;
    navOptions.selectedHoveredRowColor = options.selectedHoveredRowColor;
    navOptions.selectedPressedRowColor = options.selectedPressedRowColor;
    navOptions.hoveredRowColor = options.hoveredRowColor;
    navOptions.pressedRowColor = options.pressedRowColor;
    navOptions.clearRowColor = options.clearRowColor;

    handle.row = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(1.0f, options.rowHeight, ui.scale()),
        ui_sidebar_nav_row_style(navOptions, options.selected),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.row, options.rowCornerRadius);
    ui.set_layer(handle.row, layer, order);
    ui.attach_stretch(
        handle.row,
        parent,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        glm::vec4(0.0f),
        scaled_offset(0.0f, y, ui.scale()),
        scaled_size(0.0f, options.rowHeight, ui.scale()));

    ButtonInputComponent input = {};
    input.onClick = std::move(options.onClick);
    registry.emplace<ButtonInputComponent>(handle.row, std::move(input));
    registry.emplace<ButtonVisualComponent>(handle.row, ui_sidebar_nav_visual(navOptions, options.selected));

    const bool usesGenericMedia = options.media.valid();
    const Media2DHandle mediaHandle = usesGenericMedia ? options.media : options.avatar;
    const float mediaRadius = usesGenericMedia && options.mediaRadius > 0.0f ? options.mediaRadius : options.avatarRadius;
    const glm::vec2 mediaCenter = usesGenericMedia ? options.mediaCenter : options.avatarCenter;
    const std::string mediaBackground = usesGenericMedia && !options.mediaBackgroundColor.empty() ?
        options.mediaBackgroundColor :
        options.avatarBackgroundColor;
    const std::string mediaOutline = usesGenericMedia && !options.mediaOutlineColor.empty() ?
        options.mediaOutlineColor :
        options.avatarOutlineColor;

    if (options.showMedia)
    {
        handle.mediaFrame = ui_create_dot(
            ui,
            handle.row,
            mediaCenter,
            mediaRadius,
            mediaBackground,
            layer + 1,
            order,
            mediaOutline,
            1.0f);
        handle.avatar = handle.mediaFrame;
        scene.enable_mask(handle.mediaFrame, false);
        if (mediaHandle.valid())
        {
            handle.media = ui.create_aligned_media(
                mediaHandle,
                handle.mediaFrame,
                UiAlignment::eCenter,
                { mediaRadius * 2.0f, mediaRadius * 2.0f },
                layer + 2,
                order,
                glm::vec2(0.0f),
                options.mediaFit);
            handle.avatarMedia = handle.media;
        }
    }

    handle.title = ui.create_aligned_text(
        ui_truncate_text_with_ellipsis(options.title, options.maxTitleCharacters),
        fontAtlas,
        handle.row,
        UiAlignment::eTopLeft,
        options.titleFontSize,
        ui_sidebar_rich_button_text_style(
            options.selected ? options.selectedTitleColor : options.titleColor,
            options.titleWeight),
        layer + 1,
        order + 1u,
        options.textOffset);
    handle.subtitle = ui.create_aligned_text(
        ui_truncate_text_with_ellipsis(options.subtitle, options.maxSubtitleCharacters),
        fontAtlas,
        handle.row,
        UiAlignment::eTopLeft,
        options.subtitleFontSize,
        ui_sidebar_rich_button_text_style(
            options.selected ? options.selectedSubtitleColor : options.subtitleColor,
            options.subtitleWeight),
        layer + 1,
        order + 2u,
        { options.textOffset.x, options.textOffset.y + 22.0f });

    ui_update_button_visual(scene, handle.row);
    return handle;
}

inline UiSidebarNavItemHandle ui_create_sidebar_nav_item(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    float y,
    std::string label,
    int32_t layer,
    uint32_t order,
    UiSidebarNavItemOptions options = {})
{
    UiSidebarNavItemHandle handle = {};
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return handle;
    }

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    handle.row = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(1.0f, options.rowHeight, ui.scale()),
        ui_sidebar_nav_row_style(options, options.selected),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.row, options.rowCornerRadius);
    ui.set_layer(handle.row, layer, order);
    ui.attach_stretch(
        handle.row,
        parent,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        glm::vec4(0.0f),
        scaled_offset(0.0f, y, ui.scale()),
        scaled_size(0.0f, options.rowHeight, ui.scale()));

    ButtonInputComponent input = {};
    input.onClick = std::move(options.onClick);
    registry.emplace<ButtonInputComponent>(handle.row, std::move(input));
    registry.emplace<ButtonVisualComponent>(handle.row, ui_sidebar_nav_visual(options, options.selected));

    const bool directMediaIcon = options.mediaIcon.valid() && !options.mediaIconUsesContainer;
    if (!directMediaIcon)
    {
        handle.icon = scene.create_shape(
            { 0.0f, 0.0f },
            scaled_size(options.iconSize, options.iconSize, ui.scale()),
            make_solid_style(
                options.showIconBackground ? (options.selected ? options.selectedIconColor : options.iconColor) : "rgba(255, 255, 255, 0.002)",
                options.showIconBackground ? options.iconOutlineColor : "rgba(0, 0, 0, 0)",
                options.showIconBackground ? scaled_scalar(options.iconOutlineWidth, ui.scale()) : 0.0f,
                1.0f),
            Renderer2DPrimitive::eRoundedRectangle);
        ui.set_shape(handle.icon, options.iconCornerRadius);
        ui.set_layer(handle.icon, layer + 1, order);
        ui.attach_aligned(
            handle.icon,
            handle.row,
            UiAlignment::eMiddleLeft,
            scaled_offset(options.iconOffset.x, options.iconOffset.y, ui.scale()),
            scaled_size(options.iconSize, options.iconSize, ui.scale()));
    }

    if (options.mediaIcon.valid())
    {
        const glm::vec2 directOffset {
            options.iconOffset.x + (options.iconSize - options.mediaIconSize.x) * 0.5f,
            options.iconOffset.y
        };
        handle.mediaIcon = ui.create_aligned_media(
            options.mediaIcon,
            directMediaIcon ? handle.row : handle.icon,
            directMediaIcon ? UiAlignment::eMiddleLeft : UiAlignment::eCenter,
            options.mediaIconSize,
            layer + 2,
            order,
            directMediaIcon ? directOffset : glm::vec2(0.0f),
            options.mediaIconFit);
    }
    else if (!options.glyph.empty())
    {
        TextStyleComponent glyphStyle = ui_macos26_text_style("#FFFFFFFF");
        glyphStyle.set_font_weight(700.0f);
        handle.glyph = ui.create_aligned_text(
            options.glyph,
            fontAtlas,
            handle.icon,
            UiAlignment::eCenter,
            options.glyphFontSize,
            glyphStyle,
            layer + 2,
            order);
    }

    handle.label = ui.create_aligned_text(
        ui_truncate_text_with_ellipsis(label, options.maxLabelCharacters),
        fontAtlas,
        handle.row,
        options.labelAlignment,
        options.labelFontSize,
        ui_sidebar_nav_label_style(options, options.selected),
        layer + 2,
        order + 1u,
        options.labelOffset);
    ui_update_button_visual(scene, handle.row);
    return handle;
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

inline void ui_tint_icon_media(Renderer2DScene& scene, entt::entity entity, std::string_view color)
{
    ui_set_media_mask_tint(scene, entity, color);
}

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

// Nav-cluster names remain source-compatible while the implementation is shared
// with singular history-enabled icon buttons.
inline void ui_update_nav_cluster_history_visual(Renderer2DScene& scene, entt::entity entity)
{
    ui_update_navigation_history_visual(scene, entity);
}

inline bool ui_nav_cluster_navigate_to_history(
    Renderer2DScene& scene,
    entt::entity entity,
    std::size_t cursor,
    const PointerInputEvent& event)
{
    return ui_navigation_navigate_to_history(scene, entity, cursor, event);
}

inline bool ui_nav_cluster_go_back(Renderer2DScene& scene, entt::entity entity, const PointerInputEvent& event)
{
    return ui_navigation_go_back(scene, entity, event);
}

inline bool ui_nav_cluster_go_forward(Renderer2DScene& scene, entt::entity entity, const PointerInputEvent& event)
{
    return ui_navigation_go_forward(scene, entity, event);
}

inline bool ui_nav_cluster_push_history(
    Renderer2DScene& scene,
    entt::entity entity,
    std::size_t page,
    const PointerInputEvent& event)
{
    return ui_navigation_push_history(scene, entity, page, event);
}

inline float ui_control_ease_out(float t)
{
    t = std::clamp(t, 0.0f, 1.0f);
    const float inverse = 1.0f - t;
    return 1.0f - inverse * inverse * inverse;
}

inline glm::vec2 ui_switch_target_offset(const UiSwitchControlComponent& control)
{
    return control.checked ? control.onOffset : control.offOffset;
}

inline void ui_apply_switch_knob_offset(
    Renderer2DScene& scene,
    UiSwitchControlComponent& control,
    glm::vec2 offset)
{
    entt::registry& registry = scene.registry();
    if (control.knob == entt::null || !registry.valid(control.knob))
    {
        control.currentOffset = offset;
        return;
    }

    if (Layout2DComponent* layout = registry.try_get<Layout2DComponent>(control.knob))
    {
        layout->anchorMin = ui_alignment_anchor(UiAlignment::eMiddleLeft);
        layout->anchorMax = layout->anchorMin;
        layout->pivot = ui_alignment_pivot(UiAlignment::eMiddleLeft);
        layout->offset = offset;
    }
    control.currentOffset = offset;
    scene.mark_dirty(control.knob);
}

inline void ui_start_switch_knob_animation(Renderer2DScene& scene, entt::entity entity)
{
    entt::registry& registry = scene.registry();
    UiSwitchControlComponent* control = registry.try_get<UiSwitchControlComponent>(entity);
    if (!control)
    {
        return;
    }

    glm::vec2 startOffset = control->currentOffset;
    if (control->knob != entt::null && registry.valid(control->knob))
    {
        if (const Layout2DComponent* layout = registry.try_get<Layout2DComponent>(control->knob))
        {
            startOffset = layout->offset;
        }
    }

    const glm::vec2 targetOffset = ui_switch_target_offset(*control);
    if (!control->animateKnob || control->animationDurationSeconds <= 0.001f)
    {
        control->animating = false;
        control->animationPendingStart = false;
        ui_apply_switch_knob_offset(scene, *control, targetOffset);
        scene.mark_dirty(entity);
        return;
    }

    control->animationStartOffset = startOffset;
    control->animationTargetOffset = targetOffset;
    control->animating = true;
    control->animationPendingStart = true;
    scene.activate_dynamic(entity, control->animationDurationSeconds + 0.08f);
    if (control->knob != entt::null && registry.valid(control->knob))
    {
        scene.activate_dynamic(control->knob, control->animationDurationSeconds + 0.08f);
        scene.mark_dirty(control->knob);
    }
    scene.mark_dirty(entity);
}

inline UiControlHandle ui_create_text_button(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    std::string label,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiButtonOptions options = {})
{
    // Text button lays out optional icons only when handles are provided
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    ButtonVisualComponent visual = ui_button_visual_from_options(ui, options);
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        options.enabled ? visual.idle : visual.disabled,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, options.cornerRadius);
    ui.set_layer(handle.root, layer, order);
    ui.set_padding(handle.root, options.padding.x, options.padding.y, options.padding.z, options.padding.w);
    ui_apply_glass_material(ui, handle.root, options.glass);

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onClick = std::move(options.onClick);
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, visual);
    // Transparent buttons still own their complete authored rectangle. Without
    // an explicit hit region, picking falls through anywhere that is not
    // covered by a label or icon.
    registry.emplace<HitRegion2DComponent>(handle.root);
    if (options.stretchDynamics)
    {
        ui_enable_stretch_dynamics(
            scene,
            handle.root,
            std::move(*options.stretchDynamics));
    }

    const std::string textColor = !options.textColor.empty() ? options.textColor : (options.primary ? "#FFFFFFFF" : "#1D1D1FFF");
    const std::string iconTint = !options.iconTint.empty() ? options.iconTint : textColor;
    const float iconGap = std::max(options.iconTextGap, 0.0f);
    const float leadingIconSize = options.leadingIconSize > 0.0f ?
        options.leadingIconSize : options.iconSize;
    const float trailingIconSize = options.trailingIconSize > 0.0f ?
        options.trailingIconSize : options.iconSize;
    glm::vec2 labelOffset { 0.0f };
    UiAlignment labelAlignment = UiAlignment::eCenter;
    if (options.leadingIcon.valid())
    {
        handle.leadingIcon = ui.create_aligned_media(
            options.leadingIcon,
            handle.root,
            UiAlignment::eMiddleLeft,
            { leadingIconSize, leadingIconSize },
            layer + 1,
            order,
            { options.padding.x, 0.0f });
        if (options.tintLeadingIcon)
        {
            ui_tint_icon_media(scene, handle.leadingIcon, iconTint);
        }
        labelAlignment = UiAlignment::eMiddleLeft;
        labelOffset.x = options.padding.x + leadingIconSize + iconGap;
    }
    if (options.trailingIcon.valid())
    {
        handle.trailingIcon = ui.create_aligned_media(
            options.trailingIcon,
            handle.root,
            UiAlignment::eMiddleRight,
            { trailingIconSize, trailingIconSize },
            layer + 1,
            order + 1u,
            { -options.padding.z, 0.0f });
        if (options.tintTrailingIcon)
        {
            ui_tint_icon_media(scene, handle.trailingIcon, iconTint);
        }
        if (labelAlignment == UiAlignment::eCenter)
        {
            labelOffset.x = -((trailingIconSize + iconGap) * 0.5f);
        }
    }

    handle.label = ui.create_aligned_text(
        std::move(label),
        fontAtlas,
        handle.root,
        labelAlignment,
        options.fontSize,
        ui_macos26_text_style(textColor),
        layer + 1,
        order + 2u,
        labelOffset);
    ui_update_button_visual(scene, handle.root);
    return handle;
}

inline UiControlHandle ui_create_icon_button(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiIconButtonOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    const float cornerRadius = options.cornerRadius > 0.0f ?
        options.cornerRadius :
        std::min(size.x, size.y) * 0.5f;

    ButtonVisualComponent visual = {};
    visual.idle = ui_icon_button_style(ui, options, options.backgroundColor);
    visual.hovered = ui_icon_button_style(ui, options, options.hoveredBackgroundColor);
    visual.pressed = ui_icon_button_style(ui, options, options.pressedBackgroundColor);
    visual.disabled = ui_icon_button_style(ui, options, options.disabledBackgroundColor);
    visual.hasDisabled = true;

    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        options.enabled ? visual.idle : visual.disabled,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, cornerRadius);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaled_size(size.x, size.y, ui.scale()));
    ui_apply_glass_material(ui, handle.root, options.glass);

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onClick = std::move(options.onClick);
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, visual);
    if (options.stretchDynamics)
    {
        ui_enable_stretch_dynamics(
            scene,
            handle.root,
            std::move(*options.stretchDynamics));
    }

    if (options.icon.valid())
    {
        handle.leadingIcon = ui.create_aligned_media(
            options.icon,
            handle.root,
            UiAlignment::eCenter,
            { options.iconSize, options.iconSize },
            layer + 1,
            order);
        ui_tint_icon_media(
            scene,
            handle.leadingIcon,
            options.enabled ? options.iconTint : options.disabledIconTint);
    }

    if (options.useHistory)
    {
        UiNavigationHistoryComponent history = {};
        history.pages.push_back(options.initialPage);
        history.cursor = 0u;
        history.iconTint = options.iconTint;
        history.disabledIconTint = options.disabledIconTint;
        history.onNavigate = std::move(options.onNavigatePage);
        if (options.historyDirection == UiNavigationDirection::eBack)
        {
            history.backButton = handle.root;
            history.backIcon = handle.leadingIcon;
        }
        else
        {
            history.forwardButton = handle.root;
            history.forwardIcon = handle.leadingIcon;
        }
        registry.emplace<UiNavigationHistoryComponent>(handle.root, std::move(history));

        if (ButtonInputComponent* input = registry.try_get<ButtonInputComponent>(handle.root))
        {
            const UiNavigationDirection direction = options.historyDirection;
            input->onClick = [scenePtr = &scene, root = handle.root, direction](const PointerInputEvent& event) {
                if (direction == UiNavigationDirection::eBack)
                {
                    ui_navigation_go_back(*scenePtr, root, event);
                }
                else
                {
                    ui_navigation_go_forward(*scenePtr, root, event);
                }
            };
        }
        ui_update_navigation_history_visual(scene, handle.root);
    }
    ui_update_button_visual(scene, handle.root);
    return handle;
}

inline UiControlHandle ui_create_aligned_text_button(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    std::string label,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiButtonOptions options = {})
{
    UiControlHandle handle = ui_create_text_button(
        ui,
        fontAtlas,
        std::move(label),
        size,
        layer,
        order,
        std::move(options));
    ui.attach_aligned(handle.root, parent, alignment, scaled_offset(offset.x, offset.y, ui.scale()), scaled_size(size.x, size.y, ui.scale()));
    return handle;
}

inline UiScrollViewportHandle ui_create_scroll_viewport(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    UiScrollViewportOptions options = {})
{
    // This is the application-facing scroll control. The lower-level scroll
    // view remains available for shells that need to assemble their own chrome.
    UiScrollViewportHandle handle = {};
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return handle;
    }

    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    const glm::vec2 logicalSize = glm::max(size, glm::vec2(1.0f));
    const float contentHeight = options.contentHeight > 0.0f ?
        options.contentHeight : logicalSize.y;

    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(logicalSize.x, logicalSize.y, ui.scale()),
        ui_clear_surface_style(0.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(handle.root, options.layer, options.order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaled_size(logicalSize.x, logicalSize.y, ui.scale()));

    UiScrollViewOptions scroll = {};
    scroll.viewportHeight = logicalSize.y;
    scroll.contentHeight = std::max(contentHeight, 1.0f);
    scroll.contentMargin = options.contentPadding;
    scroll.scrollStep = std::max(options.scrollStep, 1.0f);
    scroll.viewportLayer = options.layer;
    scroll.viewportOrder = options.order + 1u;
    scroll.contentLayer = options.layer + 1;
    scroll.contentOrder = options.order;
    scroll.createScrollbar = options.showScrollbar;
    scroll.createEdgeFades = options.showEdgeFades;
    scroll.edgeFadesAttachToViewport = true;
    scroll.scrollbar = options.scrollbar;
    scroll.scrollbar.layer = options.layer + 2;
    scroll.scrollbar.order = options.order;
    scroll.scrollbar.fadeWhenIdle = options.scrollbarFadesWhenIdle;
    scroll.edgeFades = options.edgeFades;
    scroll.edgeFades.layer = options.layer + 2;
    scroll.edgeFades.order = options.order + 10u;
    scroll.onScroll = std::move(options.onScroll);

    handle.scrollView = ui_create_scroll_view(ui, handle.root, scroll);
    handle.viewport = handle.scrollView.viewport;
    handle.content = handle.scrollView.content;
    handle.scrollbar = handle.scrollView.scrollbar;
    if (handle.viewport == entt::null || !registry.valid(handle.viewport))
    {
        return handle;
    }

    const std::string bottomColor = options.backgroundBottomColor.empty() ?
        options.backgroundColor : options.backgroundBottomColor;
    ShapeStyleComponent surfaceStyle =
        options.backdropBlurRadius > 0.0f || !options.backgroundBottomColor.empty() ?
        ui_macos26_frosted_control_style(
            ui,
            options.backgroundColor,
            bottomColor,
            "rgba(0, 0, 0, 0)",
            0.0f,
            options.opacity,
            options.backdropBlurRadius,
            options.backdropBlurPasses,
            options.backdropBlurOpacity) :
        ui_macos26_control_style(
            ui,
            options.backgroundColor,
            "rgba(0, 0, 0, 0)",
            0.0f,
            options.opacity);
    surfaceStyle.edgeSoftness = scaled_scalar(0.7f, ui.scale());
    registry.emplace_or_replace<ShapeStyleComponent>(handle.viewport, surfaceStyle);
    ui.set_shape(handle.viewport, std::max(options.cornerRadius, 0.0f));
    if (ShapeComponent* shape = registry.try_get<ShapeComponent>(handle.viewport))
    {
        shape->primitive = Renderer2DPrimitive::eRoundedRectangle;
    }

    ShapeStyleComponent borderStyle = make_solid_style(
        "rgba(0, 0, 0, 0)",
        options.outlineColor,
        scaled_scalar(std::max(options.outlineWidth, 0.0f), ui.scale()),
        options.opacity);
    borderStyle.edgeSoftness = scaled_scalar(0.7f, ui.scale());
    handle.border = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(logicalSize.x, logicalSize.y, ui.scale()),
        borderStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.border, std::max(options.cornerRadius, 0.0f));
    ui.set_layer(handle.border, options.layer + 3, options.order);
    ui.attach_fill(handle.border, handle.root);
    // This is paint-only chrome above the content and scrollbar.
    registry.emplace<InputTransparent2DComponent>(handle.border);

    if (ScrollInputComponent* input = registry.try_get<ScrollInputComponent>(handle.viewport))
    {
        input->enabled = options.enabled;
    }

    if (options.buildContent && handle.content != entt::null && registry.valid(handle.content))
    {
        const UiScrollViewportBuildContext context {
            ui,
            handle.content,
            options.layer + 1,
            options.order,
            logicalSize
        };
        options.buildContent(context);
    }

    if (options.entrance.enabled && options.entrance.durationSeconds > 0.0f)
    {
        DisplayTransition2DComponent transition = {};
        transition.inheritToChildren = false;
        transition.durationSeconds = options.entrance.durationSeconds;
        transition.fromOpacity = std::clamp(options.entrance.startOpacity, 0.0f, 1.0f);
        transition.toOpacity = 1.0f;
        transition.fromScale = glm::vec2(std::max(options.entrance.startScale, 0.0f));
        transition.toScale = glm::vec2(1.0f);
        transition.curve = options.entrance.curve;
        transition.set_delay(options.entrance.delaySeconds);
        // The renderer establishes the clock epoch on the first frame, so a
        // control can start an entrance animation during a build callback.
        scene.play_display_transition(handle.border, transition, 0.0);
    }

    scene.mark_dirty(handle.viewport);
    scene.mark_dirty(handle.border);
    return handle;
}

inline UiControlHandle ui_create_submenu_row(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    float y,
    std::string label,
    int32_t layer,
    uint32_t order,
    UiSubmenuRowOptions options = {})
{
    if (parent == entt::null || !ui.registry().valid(parent))
    {
        return {};
    }

    const float height = std::max(options.height, 1.0f);
    const float horizontalInset = std::max(options.horizontalInset, 0.0f);
    UiControlHandle handle = ui_create_text_button(
        ui,
        fontAtlas,
        std::move(label),
        { 1.0f, height },
        layer,
        order,
        std::move(options.button));
    ui.attach_stretch(
        handle.root,
        parent,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        scaled_edges(
            horizontalInset,
            0.0f,
            horizontalInset,
            0.0f,
            ui.scale()),
        scaled_offset(0.0f, y, ui.scale()),
        scaled_size(0.0f, height, ui.scale()));
    return handle;
}

inline char ui_search_lower_ascii(char value)
{
    return static_cast<char>(std::tolower(static_cast<unsigned char>(value)));
}

inline std::string ui_normalise_search_text(std::string_view value)
{
    // ASCII normalisation keeps search cheap and deterministic for menu labels
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
    // Higher scores favour exact and title matches before subtitles or keyword bags
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
    // Visibility is applied to every slot because result rows are reused between queries
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
    // Refresh maps the current matched entry window into the fixed row slots
    entt::registry& registry = scene.registry();
    UiSearchResultListComponent* list = registry.try_get<UiSearchResultListComponent>(entity);
    if (!list)
    {
        return;
    }

    // Detach the reusable result subtree before remapping its visible rows.
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
    // Scroll offset is stored as a row index rather than pixels for predictable snapping
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
    // New queries reset hover and scroll state so stale rows cannot remain interactive
    entt::registry& registry = scene.registry();
    UiSearchResultListComponent* list = registry.try_get<UiSearchResultListComponent>(entity);
    if (!list)
    {
        return;
    }

    // Wake the result subtree before query processing mutates visibility and
    // labels, keeping unrelated window chrome in its existing static cache.
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

    // Cap the stored matches so huge settings indexes do not create unnecessary UI work
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
    // Search field is a specialised text input with icon, placeholder, caret and focus style
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
    input.textPaddingRight = scaled_scalar(options.textPaddingRight, ui.scale());
    input.caretWidth = scaled_scalar(options.caretWidth, ui.scale());
    input.caretHeight = scaled_scalar(options.caretHeight, ui.scale());
    input.onChanged = std::move(options.onChanged);
    registry.emplace<TextInputComponent>(handle.root, std::move(input));
    TextInputVisualComponent visual = ui_macos26_search_visual(ui);
    visual.idle = idleStyle;
    visual.focused = focusedStyle;
    visual.placeholderText = ui_macos26_text_style(options.placeholderColor);
    visual.caretStyle = make_solid_style(options.caretColor, "#00000000", 0.0f, 1.0f);
    registry.emplace<TextInputVisualComponent>(handle.root, std::move(visual));
    ui_update_text_input_visual(scene, fontAtlas, handle.root);
    return handle;
}

inline UiControlHandle ui_create_nav_cluster(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiNavClusterOptions options = {})
{
    (void)fontAtlas;
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    const glm::vec2 safeSize {
        std::max(size.x, 2.0f),
        std::max(size.y, 2.0f)
    };
    const float halfWidth = safeSize.x * 0.5f;
    const float cornerRadius = options.cornerRadius > 0.0f ? options.cornerRadius : safeSize.y * 0.5f;
    const std::string backgroundBottom = options.backgroundBottomColor.empty() ?
        options.backgroundColor :
        options.backgroundBottomColor;
    const ShapeStyleComponent rootStyle =
        options.glass.material == GlassMaterial::eOff &&
        options.backdropBlurRadius > 0.0f ?
        ui_macos26_frosted_control_style(
            ui,
            options.backgroundColor,
            backgroundBottom,
            options.outlineColor,
            options.outlineWidth,
            options.backgroundOpacity,
            options.backdropBlurRadius,
            options.backdropBlurPasses,
            options.backdropBlurOpacity) :
        ui_macos26_control_style(
            ui,
            options.backgroundColor,
            options.outlineColor,
            options.outlineWidth,
            options.backgroundOpacity);

    UiControlHandle handle = {};
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(safeSize.x, safeSize.y, ui.scale()),
        rootStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, cornerRadius);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaled_size(safeSize.x, safeSize.y, ui.scale()));
    ui_apply_glass_material(ui, handle.root, options.glass);
    scene.enable_mask(handle.root, false);
    auto make_button_visual = [&ui]() {
        ButtonVisualComponent visual = {};
        visual.idle = ui_macos26_control_style(ui, "#FFFFFF00", "#00000000", 0.0f, 1.0f);
        visual.hovered = ui_macos26_control_style(ui, "#0000000A", "#00000000", 0.0f, 1.0f);
        visual.pressed = ui_macos26_control_style(ui, "#00000018", "#00000000", 0.0f, 1.0f);
        visual.disabled = ui_macos26_control_style(ui, "#FFFFFF00", "#00000000", 0.0f, 1.0f);
        visual.hasDisabled = true;
        return visual;
    };

    handle.label = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(halfWidth, safeSize.y, ui.scale()),
        ui_macos26_control_style(ui, "#FFFFFF00", "#00000000", 0.0f, 1.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(handle.label, layer + 1, order);
    ui.attach_aligned(
        handle.label,
        handle.root,
        UiAlignment::eTopLeft,
        glm::vec2(0.0f),
        scaled_size(halfWidth, safeSize.y, ui.scale()));

    handle.caret = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(halfWidth, safeSize.y, ui.scale()),
        ui_macos26_control_style(ui, "#FFFFFF00", "#00000000", 0.0f, 1.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(handle.caret, layer + 1, order + 1u);
    ui.attach_aligned(
        handle.caret,
        handle.root,
        UiAlignment::eTopLeft,
        scaled_offset(halfWidth, 0.0f, ui.scale()),
        scaled_size(halfWidth, safeSize.y, ui.scale()));

    ButtonInputComponent backButton = {};
    backButton.enabled = options.backEnabled;
    backButton.onClick = std::move(options.onBack);
    registry.emplace<ButtonInputComponent>(handle.label, std::move(backButton));
    registry.emplace<ButtonVisualComponent>(handle.label, make_button_visual());

    ButtonInputComponent forwardButton = {};
    forwardButton.enabled = options.forwardEnabled;
    forwardButton.onClick = std::move(options.onForward);
    registry.emplace<ButtonInputComponent>(handle.caret, std::move(forwardButton));
    registry.emplace<ButtonVisualComponent>(handle.caret, make_button_visual());

    handle.indicator = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(1.0f, std::max(safeSize.y - 20.0f, 1.0f), ui.scale()),
        make_solid_style(options.dividerColor, "#00000000", 0.0f, 1.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(handle.indicator, layer + 2, order);
    ui.attach_aligned(
        handle.indicator,
        handle.root,
        UiAlignment::eCenter,
        glm::vec2(0.0f),
        scaled_size(1.0f, std::max(safeSize.y - 20.0f, 1.0f), ui.scale()));

    if (options.backIcon.valid())
    {
        handle.leadingIcon = ui.create_aligned_media(
            options.backIcon,
            handle.label,
            UiAlignment::eCenter,
            { options.iconSize, options.iconSize },
            layer + 3,
            order);
        ui_tint_icon_media(
            scene,
            handle.leadingIcon,
            options.backEnabled ? options.iconTint : options.disabledIconTint);
    }

    if (options.forwardIcon.valid())
    {
        handle.trailingIcon = ui.create_aligned_media(
            options.forwardIcon,
            handle.caret,
            UiAlignment::eCenter,
            { options.iconSize, options.iconSize },
            layer + 3,
            order + 1u);
        ui_tint_icon_media(
            scene,
            handle.trailingIcon,
            options.forwardEnabled ? options.iconTint : options.disabledIconTint);
    }

    if (options.useHistory)
    {
        UiNavigationHistoryComponent history = {};
        history.pages.push_back(options.initialPage);
        history.cursor = 0u;
        history.backButton = handle.label;
        history.forwardButton = handle.caret;
        history.backIcon = handle.leadingIcon;
        history.forwardIcon = handle.trailingIcon;
        history.iconTint = options.iconTint;
        history.disabledIconTint = options.disabledIconTint;
        history.onNavigate = std::move(options.onNavigatePage);
        registry.emplace<UiNavigationHistoryComponent>(handle.root, std::move(history));

        if (ButtonInputComponent* back = registry.try_get<ButtonInputComponent>(handle.label))
        {
            back->onClick = [scenePtr = &scene, root = handle.root](const PointerInputEvent& event) {
                ui_navigation_go_back(*scenePtr, root, event);
            };
        }
        if (ButtonInputComponent* forward = registry.try_get<ButtonInputComponent>(handle.caret))
        {
            forward->onClick = [scenePtr = &scene, root = handle.root](const PointerInputEvent& event) {
                ui_navigation_go_forward(*scenePtr, root, event);
            };
        }
        ui_update_navigation_history_visual(scene, handle.root);
    }

    ui_update_button_visual(scene, handle.label);
    ui_update_button_visual(scene, handle.caret);
    return handle;
}

inline UiSettingsShellHandle ui_create_settings_shell(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity leftSection,
    entt::entity rightSection,
    UiSettingsShellOptions options = {})
{
    // Settings shell wires chrome, left scroll view, right scroll view, fades and scrollbars
    UiSettingsShellHandle handle = {};
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    if (leftSection == entt::null || rightSection == entt::null ||
        !registry.valid(leftSection) || !registry.valid(rightSection))
    {
        return handle;
    }

    handle.navCluster = ui_create_nav_cluster(
        ui,
        fontAtlas,
        rightSection,
        UiAlignment::eTopLeft,
        options.navOffset,
        options.navSize,
        options.navLayer,
        options.navOrder,
        options.nav);
    if (options.fixedChromeDynamicCache && handle.navCluster.root != entt::null && registry.valid(handle.navCluster.root))
    {
        ui.set_dynamic_cache(handle.navCluster.root);
    }

    handle.search = ui_create_search_field(
        ui,
        fontAtlas,
        leftSection,
        UiAlignment::eTopLeft,
        { options.sidebarSearchMargin.x, options.sidebarStaticTop },
        { 1.0f, options.sidebarSearchHeight },
        options.searchLayer,
        options.searchOrder,
        options.search);
    if (options.fixedChromeDynamicCache && handle.search.root != entt::null && registry.valid(handle.search.root))
    {
        ui.set_dynamic_cache(handle.search.root);
    }
    ui.attach_stretch(
        handle.search.root,
        leftSection,
        { 0.0f, 0.0f },
        { 1.0f, 0.0f },
        { 0.0f, 0.0f },
        scaled_edges(
            options.sidebarSearchMargin.x,
            0.0f,
            options.sidebarSearchMargin.y,
            0.0f,
            ui.scale()),
        scaled_offset(0.0f, options.sidebarStaticTop, ui.scale()),
        scaled_size(0.0f, options.sidebarSearchHeight, ui.scale()));

    const float sidebarViewportHeight = std::max(
        1.0f,
        options.panelHeight - options.sidebarViewportTop - options.sidebarBottom);
    handle.sidebarViewportHeight = scaled_scalar(sidebarViewportHeight, ui.scale());
    UiScrollViewOptions sidebarScroll = {};
    sidebarScroll.viewportMargin = {
        0.0f,
        options.sidebarViewportTop,
        0.0f,
        options.sidebarBottom
    };
    sidebarScroll.contentMargin = {
        options.sidebarContentMargin.x,
        0.0f,
        options.sidebarContentMargin.y,
        0.0f
    };
    sidebarScroll.viewportHeight = sidebarViewportHeight;
    sidebarScroll.contentHeight = options.sidebarContentHeight;
    sidebarScroll.contentInitialOffset = options.sidebarContentInitialOffset;
    sidebarScroll.scrollStep = options.sidebarScrollStep;
    sidebarScroll.viewportLayer = options.sidebarViewportLayer;
    sidebarScroll.viewportOrder = options.sidebarViewportOrder;
    sidebarScroll.contentLayer = options.sidebarContentLayer;
    sidebarScroll.contentOrder = options.sidebarContentOrder;
    sidebarScroll.createScrollbar = options.sidebarScrollbarEnabled;
    sidebarScroll.createEdgeFades = options.sidebarFades;
    sidebarScroll.edgeFadeParent = leftSection;
    sidebarScroll.scrollbar = options.sidebarScrollbar;
    sidebarScroll.edgeFades = options.sidebarEdgeFades;
    if (options.fixedChromeDynamicCache)
    {
        sidebarScroll.edgeFades.dynamicCache = true;
    }
    handle.sidebar = ui_create_scroll_view(ui, leftSection, sidebarScroll);
    handle.sidebarContent = handle.sidebar.content;

    const float rightViewportHeight = std::max(
        1.0f,
        options.panelHeight - options.rightViewportMargin.y - options.rightViewportMargin.w);
    handle.rightViewportHeight = scaled_scalar(rightViewportHeight, ui.scale());

    UiScrollViewOptions rightScroll = {};
    rightScroll.viewportMargin = options.rightViewportMargin;
    rightScroll.viewportHeight = rightViewportHeight;
    rightScroll.scrollStep = options.rightScrollStep;
    rightScroll.viewportLayer = options.rightViewportLayer;
    rightScroll.viewportOrder = options.rightViewportOrder;
    rightScroll.createContent = false;
    rightScroll.createScrollbar = options.rightScrollbarEnabled;
    rightScroll.createEdgeFades = options.rightFades;
    rightScroll.edgeFadeParent = rightSection;
    rightScroll.scrollbar = options.rightScrollbar;
    rightScroll.edgeFades = options.rightEdgeFades;
    rightScroll.onScroll = std::move(options.onRightScroll);
    if (options.fixedChromeDynamicCache)
    {
        rightScroll.edgeFades.dynamicCache = true;
    }
    handle.right = ui_create_scroll_view(ui, rightSection, rightScroll);
    handle.rightViewport = handle.right.viewport;
    handle.rightScrollbar = handle.right.scrollbar;
    return handle;
}

inline UiSplitShellHandle ui_create_split_shell(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity leftSection,
    entt::entity rightSection,
    UiSplitShellOptions options = {})
{
    // Neutral name for the same reusable two-pane chrome and scroll-view builder
    return ui_create_settings_shell(
        ui,
        fontAtlas,
        leftSection,
        rightSection,
        std::move(options));
}

inline void ui_update_switch_visual(Renderer2DScene& scene, entt::entity entity)
{
    entt::registry& registry = scene.registry();
    UiSwitchControlComponent* control = registry.try_get<UiSwitchControlComponent>(entity);
    ButtonVisualComponent* visual = registry.try_get<ButtonVisualComponent>(entity);
    if (!control || !visual)
    {
        return;
    }

    visual->idle = control->checked ? control->onIdle : control->offIdle;
    visual->hovered = control->checked ? control->onHovered : control->offHovered;
    visual->pressed = control->checked ? control->onPressed : control->offPressed;
    ui_update_button_visual(scene, entity);

    if (control->knob != entt::null && registry.valid(control->knob))
    {
        if (!control->animating)
        {
            ui_apply_switch_knob_offset(scene, *control, ui_switch_target_offset(*control));
        }
        scene.mark_dirty(control->knob);
    }
}

inline void ui_update_switch_animations(Renderer2DScene& scene, double currentTimeSeconds)
{
    entt::registry& registry = scene.registry();
    auto view = registry.view<UiSwitchControlComponent>();
    view.each([&](entt::entity entity, UiSwitchControlComponent& control) {
        if (!control.animating)
        {
            return;
        }

        if (control.animationPendingStart)
        {
            control.animationStartSeconds = currentTimeSeconds;
            control.animationPendingStart = false;
        }

        const float duration = std::max(control.animationDurationSeconds, 0.001f);
        const float progress = static_cast<float>(
            std::clamp((currentTimeSeconds - control.animationStartSeconds) / static_cast<double>(duration), 0.0, 1.0));
        const float eased = ui_control_ease_out(progress);
        const glm::vec2 offset = glm::mix(control.animationStartOffset, control.animationTargetOffset, eased);
        ui_apply_switch_knob_offset(scene, control, offset);

        if (progress >= 1.0f)
        {
            control.animating = false;
            control.animationPendingStart = false;
            ui_apply_switch_knob_offset(scene, control, ui_switch_target_offset(control));
        }
        else
        {
            scene.activate_dynamic(entity, 1.0f / 30.0f);
            if (control.knob != entt::null && registry.valid(control.knob))
            {
                scene.activate_dynamic(control.knob, 1.0f / 30.0f);
            }
        }
    });
}

inline UiControlHandle ui_create_slider(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiSliderOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    const float trackHeight = std::max(size.y, 4.0f);
    const glm::vec2 requestedThumbSize = options.thumbSize.x > 0.0f && options.thumbSize.y > 0.0f ?
        options.thumbSize :
        glm::vec2(options.thumbDiameter);
    const glm::vec2 thumbSize {
        std::max(requestedThumbSize.x, trackHeight),
        std::max(requestedThumbSize.y, trackHeight)
    };
    const float hoveredTrackHeight = std::max(options.hoveredTrackHeight, trackHeight);
    const glm::vec2 hoveredThumbSize = glm::max(
        thumbSize * std::max(options.hoveredThumbScale, 1.0f),
        glm::vec2(hoveredTrackHeight));
    const float initialNormalized = options.maxValue > options.minValue ?
        std::clamp((options.value - options.minValue) / (options.maxValue - options.minValue), 0.0f, 1.0f) :
        0.0f;
    const bool flatFill = options.fillStyle == UiSliderFillStyle::eFlatFillClippedByTrack;

    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, trackHeight, ui.scale()),
        ui_macos26_control_style(ui, options.trackColor, options.outlineColor, options.outlineWidth, 1.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, trackHeight * 0.5f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaled_size(size.x, trackHeight, ui.scale()));

    entt::entity fillMask = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, trackHeight, ui.scale()),
        ui_clear_surface_style(0.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(fillMask, trackHeight * 0.5f);
    ui.set_layer(fillMask, layer + 1, order);
    ui.attach_aligned(
        fillMask,
        handle.root,
        UiAlignment::eCentre,
        glm::vec2(0.0f),
        scaled_size(size.x, trackHeight, ui.scale()));
    scene.enable_mask(fillMask, false);

    handle.indicator = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(std::max(size.x * initialNormalized, 0.0f), trackHeight, ui.scale()),
        ui_macos26_control_style(ui, options.fillColor, "#00000000", 0.0f, 1.0f),
        flatFill ? Renderer2DPrimitive::eRectangle : Renderer2DPrimitive::eRoundedRectangle);
    if (!flatFill)
    {
        ui.set_shape(handle.indicator, trackHeight * 0.5f);
    }
    ui.set_layer(handle.indicator, layer + 1, order);
    ui.attach_layout(
        handle.indicator,
        fillMask,
        { 0.0f, 0.5f },
        { 0.0f, 0.5f },
        glm::vec2(0.0f),
        scaled_size(std::max(size.x * initialNormalized, 0.0f), trackHeight, ui.scale()));

    handle.knob = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(thumbSize.x, thumbSize.y, ui.scale()),
        ui_macos26_control_style(ui, options.thumbColor, options.thumbOutlineColor, options.thumbOutlineWidth, 1.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.knob, thumbSize.y * 0.5f);
    ui.set_layer(handle.knob, layer + 2, order);
    ui.attach_layout(
        handle.knob,
        handle.root,
        { 0.0f, 0.5f },
        { 0.5f, 0.5f },
        scaled_offset(size.x * initialNormalized, 0.0f, ui.scale()),
        scaled_size(thumbSize.x, thumbSize.y, ui.scale()));
        
    if (options.renderThumbShadow)
    {
        ui.add_shadow(
            handle.knob,
            { 0, 0 },
            4.0f,
            0.24f);
    }

    SliderInputComponent slider = {};
    slider.maskEntity = fillMask;
    slider.fillEntity = handle.indicator;
    slider.thumbEntity = handle.knob;
    slider.enabled = options.enabled;
    slider.dimWhenDisabled = options.dimWhenDisabled;
    slider.minValue = std::min(options.minValue, options.maxValue);
    slider.maxValue = std::max(options.minValue, options.maxValue);
    slider.value = std::clamp(options.value, slider.minValue, slider.maxValue);
    slider.visualValue = slider.value;
    slider.step = std::max(options.step, 0.0f);
    const float rawSnapPercentage = options.snapPercentage > 1.0f ?
        options.snapPercentage / 100.0f :
        options.snapPercentage;
    slider.normalisedStep = std::clamp(rawSnapPercentage, 0.0f, 1.0f);
    slider.trackHeight = scaled_scalar(trackHeight, ui.scale());
    slider.hoveredTrackHeight = scaled_scalar(hoveredTrackHeight, ui.scale());
    slider.thumbSize = scaled_size(thumbSize.x, thumbSize.y, ui.scale());
    slider.hoveredThumbSize = scaled_size(
        hoveredThumbSize.x,
        hoveredThumbSize.y,
        ui.scale());
    slider.smoothScrubbing = options.smoothScrubbing;
    slider.visualSmoothingRate = std::max(options.visualSmoothingRate, 0.0f);
    slider.onChanged = std::move(options.onChanged);
    slider.onCommitted = std::move(options.onCommitted);
    registry.emplace<SliderInputComponent>(handle.root, std::move(slider));
    registry.emplace<DisabledVisualComponent>(handle.root, DisabledVisualComponent { options.dimWhenDisabled, false });
    return handle;
}

inline float ui_circular_progress_normalized_value(
    float value,
    float minValue,
    float maxValue)
{
    const float low = std::min(minValue, maxValue);
    const float high = std::max(minValue, maxValue);
    return high > low ? std::clamp((value - low) / (high - low), 0.0f, 1.0f) : 0.0f;
}

inline bool ui_set_circular_progress(
    Renderer2DScene& scene,
    entt::entity root,
    float value)
{
    entt::registry& registry = scene.registry();
    UiCircularProgressComponent* control =
        registry.try_get<UiCircularProgressComponent>(root);
    if (!control || control->indicator == entt::null ||
        !registry.valid(control->indicator))
    {
        return false;
    }

    const float low = std::min(control->minValue, control->maxValue);
    const float high = std::max(control->minValue, control->maxValue);
    control->value = std::clamp(value, low, high);
    ShapeComponent* indicator =
        registry.try_get<ShapeComponent>(control->indicator);
    if (!indicator)
    {
        return false;
    }

    const float normalizedValue = ui_circular_progress_normalized_value(
        control->value,
        control->minValue,
        control->maxValue);
    if (indicator->arcProgress == normalizedValue)
    {
        return false;
    }

    indicator->arcProgress = normalizedValue;
    scene.mark_dirty(control->indicator);
    return true;
}

inline bool ui_apply_circular_progress_content_color(
    Renderer2DScene& scene,
    entt::entity root,
    entt::entity content,
    bool mediaAsMask = false)
{
    entt::registry& registry = scene.registry();
    const UiCircularProgressComponent* control =
        registry.try_get<UiCircularProgressComponent>(root);
    if (!control || !control->contentUsesProgressColor ||
        content == entt::null || !registry.valid(content))
    {
        return false;
    }

    const glm::vec4 startColor = control->progressColor;
    const glm::vec4 endColor = control->progressEndColor;
    bool applied = false;
    if (TextStyleComponent* text =
        registry.try_get<TextStyleComponent>(content))
    {
        if (control->progressUsesGradient)
        {
            text->set_gradient(
                startColor,
                endColor,
                { 0.0f, 0.0f },
                { 1.0f, 0.0f });
        }
        else
        {
            text->set_color(startColor);
        }
        applied = true;
    }
    if (ShapeStyleComponent* shape =
        registry.try_get<ShapeStyleComponent>(content))
    {
        shape->fill = control->progressUsesGradient ?
            Renderer2DFill::eLinearGradient :
            Renderer2DFill::eSolid;
        shape->color0 = startColor;
        shape->color1 = control->progressUsesGradient ? endColor : startColor;
        shape->gradientStart = { 0.0f, 0.0f };
        shape->gradientEnd = { 1.0f, 0.0f };
        applied = true;
    }
    if (Media2DComponent* media =
        registry.try_get<Media2DComponent>(content))
    {
        if (control->progressUsesGradient)
        {
            media->set_tint_gradient(
                startColor,
                endColor,
                { 0.0f, 0.0f },
                { 1.0f, 0.0f });
        }
        else
        {
            media->set_tint(startColor);
        }
        if (mediaAsMask)
        {
            media->set_tint_as_mask(true);
        }
        applied = true;
    }

    if (applied)
    {
        scene.mark_dirty(content);
    }
    return applied;
}

inline UiControlHandle ui_create_circular_progress(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    float diameter,
    int32_t layer,
    uint32_t order,
    UiCircularProgressOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    const float safeDiameter = std::max(diameter, 1.0f);
    const float safeThickness = std::clamp(options.thickness, 0.0f, safeDiameter);
    const glm::vec2 scaledDiameter =
        scaled_size(safeDiameter, safeDiameter, ui.scale());
    const float scaledThickness = scaled_scalar(safeThickness, ui.scale());
    const float scaledSoftness = scaled_scalar(
        std::max(options.edgeSoftness, 0.0f),
        ui.scale());

    auto arcStyle = [&](std::string_view color, std::string_view endColor) {
        ShapeStyleComponent style = endColor.empty() ?
            make_solid_style(color, "#00000000", 0.0f, options.opacity) :
            make_gradient_style(color, endColor, "#00000000", 0.0f, options.opacity);
        style.edgeSoftness = scaledSoftness;
        return style;
    };
    auto configureArc = [&](entt::entity entity, float progress) {
        if (ShapeComponent* shape = registry.try_get<ShapeComponent>(entity))
        {
            shape->arcProgress = std::clamp(progress, 0.0f, 1.0f);
            shape->arcThickness = scaledThickness;
            shape->arcStartAngleRadians = options.startAngleRadians;
            shape->arcClockwise = options.clockwise;
        }
    };

    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaledDiameter,
        arcStyle(options.trackColor, options.trackEndColor),
        Renderer2DPrimitive::eCircularProgress);
    configureArc(handle.root, 1.0f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(
        handle.root,
        parent,
        alignment,
        scaled_offset(offset.x, offset.y, ui.scale()),
        scaledDiameter);

    handle.indicator = scene.create_shape(
        { 0.0f, 0.0f },
        scaledDiameter,
        arcStyle(options.progressColor, options.progressEndColor),
        Renderer2DPrimitive::eCircularProgress);
    configureArc(
        handle.indicator,
        ui_circular_progress_normalized_value(
            options.value,
            options.minValue,
            options.maxValue));
    ui.set_layer(handle.indicator, layer + 1, order);
    ui.attach_aligned(
        handle.indicator,
        handle.root,
        UiAlignment::eCenter,
        glm::vec2(0.0f),
        scaledDiameter);

    const float low = std::min(options.minValue, options.maxValue);
    const float high = std::max(options.minValue, options.maxValue);
    const glm::vec4 progressColor =
        renderer2d_hex_color(options.progressColor);
    const bool progressUsesGradient = !options.progressEndColor.empty();
    const glm::vec4 progressEndColor = progressUsesGradient ?
        renderer2d_hex_color(options.progressEndColor) :
        progressColor;
    registry.emplace<UiCircularProgressComponent>(
        handle.root,
        UiCircularProgressComponent {
            handle.indicator,
            std::clamp(options.value, low, high),
            low,
            high,
            progressColor,
            progressEndColor,
            progressUsesGradient,
            options.contentUsesProgressColor
        });
    return handle;
}

inline UiControlHandle ui_create_switch(
    UiBuilder& ui,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiSwitchOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    const glm::vec2 trackSize {
        std::max(size.x, size.y * std::max(options.minimumWidthRatio, 1.0f)),
        size.y
    };
    const float knobInset = std::clamp(options.knobInset, 1.0f, std::max(trackSize.y * 0.45f, 1.0f));
    const float knobHeight = std::max(trackSize.y - knobInset * 2.0f, 2.0f);
    const float knobWidth = std::clamp(
        knobHeight * std::max(options.knobWidthRatio, 1.0f),
        knobHeight,
        std::max(trackSize.x - knobInset * 2.0f, knobHeight));
    UiControlHandle handle = {};
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(trackSize.x, trackSize.y, ui.scale()),
        ui_macos26_control_style(ui, options.offColor, options.offOutlineColor, options.trackOutlineWidth, 1.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, trackSize.y * 0.5f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(handle.root, parent, alignment, scaled_offset(offset.x, offset.y, ui.scale()), scaled_size(trackSize.x, trackSize.y, ui.scale()));

    handle.knob = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(knobWidth, knobHeight, ui.scale()),
        ui_macos26_control_style(ui, options.knobColor, options.knobOutlineColor, options.knobOutlineWidth, 1.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    if (ShapeStyleComponent* knobStyle = registry.try_get<ShapeStyleComponent>(handle.knob))
    {
        knobStyle->edgeSoftness = scaled_scalar(options.knobEdgeSoftness, ui.scale());
    }
    ui.set_shape(handle.knob, knobHeight * 0.5f, options.knobSdfEdges);
    ui.set_layer(handle.knob, layer + 1, order);
    ui.attach_aligned(handle.knob, handle.root, UiAlignment::eMiddleLeft, scaled_offset(knobInset, 0.0f, ui.scale()), scaled_size(knobWidth, knobHeight, ui.scale()));

    UiSwitchControlComponent control = {};
    control.checked = options.checked;
    control.knob = handle.knob;
    control.offOffset = scaled_offset(knobInset, 0.0f, ui.scale());
    control.onOffset = scaled_offset(trackSize.x - knobInset - knobWidth, 0.0f, ui.scale());
    control.currentOffset = control.checked ? control.onOffset : control.offOffset;
    control.animationDurationSeconds = std::max(options.animationDurationSeconds, 0.0f);
    control.animateKnob = options.animateKnob;
    control.offIdle = ui_macos26_control_style(ui, options.offColor, options.offOutlineColor, options.trackOutlineWidth, 1.0f);
    control.offHovered = ui_macos26_control_style(ui, options.offHoveredColor, options.offOutlineColor, options.trackOutlineWidth, 1.0f);
    control.offPressed = ui_macos26_control_style(ui, options.offPressedColor, options.offOutlineColor, options.trackOutlineWidth, 1.0f);
    control.onIdle = ui_macos26_control_style(ui, options.onColor, options.onOutlineColor, options.trackOutlineWidth, 1.0f);
    control.onHovered = ui_macos26_control_style(ui, options.onHoveredColor, options.onOutlineColor, options.trackOutlineWidth, 1.0f);
    control.onPressed = ui_macos26_control_style(ui, options.onPressedColor, options.onOutlineColor, options.trackOutlineWidth, 1.0f);
    control.onChanged = std::move(options.onChanged);
    registry.emplace<UiSwitchControlComponent>(handle.root, std::move(control));

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onClick = [scenePtr = &scene, entity = handle.root](const PointerInputEvent& event) {
        entt::registry& capturedRegistry = scenePtr->registry();
        UiSwitchControlComponent* captured = capturedRegistry.try_get<UiSwitchControlComponent>(entity);
        if (!captured)
        {
            return;
        }
        captured->checked = !captured->checked;
        const bool checked = captured->checked;
        auto callback = captured->onChanged;
        ui_start_switch_knob_animation(*scenePtr, entity);
        ui_update_switch_visual(*scenePtr, entity);
        if (callback)
        {
            callback(checked, event);
        }
    };
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, ui_macos26_button_visual(ui, false));
    ui_update_switch_visual(scene, handle.root);
    return handle;
}

inline void ui_update_radio_visual(
    Renderer2DScene& scene,
    entt::entity entity)
{
    entt::registry& registry = scene.registry();
    UiRadioControlComponent* control =
        registry.try_get<UiRadioControlComponent>(entity);
    ButtonInputComponent* button =
        registry.try_get<ButtonInputComponent>(entity);
    if (!control || !button)
    {
        return;
    }

    const bool enabled = button->enabled;
    if (control->outer != entt::null && registry.valid(control->outer))
    {
        if (ShapeStyleComponent* style =
                registry.try_get<ShapeStyleComponent>(control->outer))
        {
            if (!enabled)
            {
                *style = control->disabled;
            }
            else if (button->leftPressed || button->rightPressed)
            {
                *style = control->enabledPressed;
            }
            else if (button->hovered)
            {
                *style = control->enabledHovered;
            }
            else
            {
                *style = control->enabledIdle;
            }
        }
        scene.mark_dirty(control->outer);
    }

    if (control->indicator != entt::null &&
        registry.valid(control->indicator))
    {
        if (ShapeStyleComponent* style =
                registry.try_get<ShapeStyleComponent>(control->indicator))
        {
            *style = enabled ?
                control->enabledIndicator :
                control->disabledIndicator;
            style->opacity = control->checked ? 1.0f : 0.0f;
        }
        scene.mark_dirty(control->indicator);
    }

    if (control->label != entt::null && registry.valid(control->label))
    {
        if (TextStyleComponent* style =
                registry.try_get<TextStyleComponent>(control->label))
        {
            *style = enabled ? control->enabledLabel : control->disabledLabel;
        }
        scene.mark_dirty(control->label);
    }
}

inline void ui_set_radio_checked(
    Renderer2DScene& scene,
    entt::entity entity,
    bool checked)
{
    entt::registry& registry = scene.registry();
    UiRadioControlComponent* control = registry.try_get<UiRadioControlComponent>(entity);
    if (!control)
    {
        return;
    }

    control->checked = checked;
    if (control->indicator != entt::null && registry.valid(control->indicator))
    {
        if (ShapeStyleComponent* indicatorStyle = registry.try_get<ShapeStyleComponent>(control->indicator))
        {
            indicatorStyle->opacity = checked ? 1.0f : 0.0f;
        }
        scene.mark_dirty(control->indicator);
    }
    ui_update_radio_visual(scene, entity);
    scene.mark_dirty(entity);
}

inline void ui_set_radio_enabled(
    Renderer2DScene& scene,
    entt::entity entity,
    bool enabled)
{
    ButtonInputComponent* button =
        scene.registry().try_get<ButtonInputComponent>(entity);
    if (!button)
    {
        return;
    }

    button->enabled = enabled;
    if (!enabled)
    {
        button->hovered = false;
        button->leftPressed = false;
        button->rightPressed = false;
    }
    ui_update_radio_visual(scene, entity);
    ui_update_button_visual(scene, entity);
}

inline void ui_set_checkbox_checked(
    Renderer2DScene& scene,
    entt::entity entity,
    bool checked)
{
    entt::registry& registry = scene.registry();
    UiCheckboxControlComponent* control = registry.try_get<UiCheckboxControlComponent>(entity);
    if (!control)
    {
        return;
    }

    control->checked = checked;
    if (control->box != entt::null && registry.valid(control->box))
    {
        if (ShapeStyleComponent* boxStyle = registry.try_get<ShapeStyleComponent>(control->box))
        {
            *boxStyle = checked ? control->checkedStyle : control->uncheckedStyle;
        }
        scene.mark_dirty(control->box);
    }
    if (control->indicator != entt::null && registry.valid(control->indicator))
    {
        if (RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(control->indicator))
        {
            layer->visible = checked;
        }
        if (ShapeStyleComponent* indicatorStyle = registry.try_get<ShapeStyleComponent>(control->indicator))
        {
            indicatorStyle->opacity = checked ? 1.0f : 0.0f;
        }
        scene.mark_dirty(control->indicator);
    }
    scene.mark_dirty(entity);
}

inline UiControlHandle ui_create_radio(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    std::string label,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiRadioOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        ui_clear_surface_style(0.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, 6.0f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(handle.root, parent, alignment, scaled_offset(offset.x, offset.y, ui.scale()), scaled_size(size.x, size.y, ui.scale()));

    const float diameter = std::max(options.diameter, 8.0f);
    const float indicatorDiameter = std::clamp(
        options.indicatorDiameter,
        2.0f,
        diameter - 2.0f);
    const ShapeStyleComponent enabledIdle = ui_macos26_control_style(
        ui,
        options.enabledColor,
        options.outlineColor,
        options.outlineWidth,
        1.0f);
    const ShapeStyleComponent enabledHovered = ui_macos26_control_style(
        ui,
        options.hoveredColor,
        options.outlineColor,
        options.outlineWidth,
        1.0f);
    const ShapeStyleComponent enabledPressed = ui_macos26_control_style(
        ui,
        options.pressedColor,
        options.outlineColor,
        options.outlineWidth,
        1.0f);
    const ShapeStyleComponent disabled = ui_macos26_control_style(
        ui,
        options.disabledColor,
        options.disabledOutlineColor,
        options.disabledOutlineWidth,
        1.0f);

    entt::entity outer = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(diameter, diameter, ui.scale()),
        options.enabled ? enabledIdle : disabled,
        Renderer2DPrimitive::eEllipse);
    ui.set_layer(outer, layer + 1, order);
    ui.attach_aligned(
        outer,
        handle.root,
        UiAlignment::eMiddleLeft,
        scaled_offset(0.0f, 0.0f, ui.scale()),
        scaled_size(diameter, diameter, ui.scale()));

    handle.indicator = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(indicatorDiameter, indicatorDiameter, ui.scale()),
        make_solid_style(
            options.enabled ?
                options.indicatorColor :
                options.disabledIndicatorColor,
            "#00000000",
            0.0f,
            options.checked ? 1.0f : 0.0f),
        Renderer2DPrimitive::eEllipse);
    ui.set_layer(handle.indicator, layer + 2, order);
    ui.attach_aligned(
        handle.indicator,
        outer,
        UiAlignment::eCenter,
        glm::vec2(0.0f),
        scaled_size(indicatorDiameter, indicatorDiameter, ui.scale()));

    const TextStyleComponent enabledLabel =
        ui_macos26_text_style(options.labelColor);
    const TextStyleComponent disabledLabel =
        ui_macos26_text_style(options.disabledLabelColor);

    handle.label = ui.create_aligned_text(
        std::move(label),
        fontAtlas,
        handle.root,
        UiAlignment::eMiddleLeft,
        options.labelFontSize,
        options.enabled ? enabledLabel : disabledLabel,
        layer + 1,
        order + 1u,
        { diameter + 8.0f, 0.0f });

    UiRadioControlComponent control = {};
    control.checked = options.checked;
    control.outer = outer;
    control.indicator = handle.indicator;
    control.label = handle.label;
    control.groupId = options.groupId;
    if (options.singleSelect && control.groupId.empty())
    {
        control.groupId = "parent:" + std::to_string(static_cast<uint32_t>(entt::to_integral(parent)));
    }
    control.singleSelect = options.singleSelect;
    control.enabledIdle = enabledIdle;
    control.enabledHovered = enabledHovered;
    control.enabledPressed = enabledPressed;
    control.disabled = disabled;
    control.enabledIndicator = make_solid_style(
        options.indicatorColor,
        "#00000000",
        0.0f,
        options.checked ? 1.0f : 0.0f);
    control.disabledIndicator = make_solid_style(
        options.disabledIndicatorColor,
        "#00000000",
        0.0f,
        options.checked ? 1.0f : 0.0f);
    control.enabledLabel = enabledLabel;
    control.disabledLabel = disabledLabel;
    control.onChanged = std::move(options.onChanged);
    registry.emplace<UiRadioControlComponent>(handle.root, std::move(control));

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onPress = [scenePtr = &scene, entity = handle.root](
                         const PointerInputEvent&) {
        ui_update_radio_visual(*scenePtr, entity);
    };
    button.onRelease = [scenePtr = &scene, entity = handle.root](
                           const PointerInputEvent&) {
        ui_update_radio_visual(*scenePtr, entity);
    };
    button.onHoverChanged = [scenePtr = &scene, entity = handle.root](
                                bool) {
        ui_update_radio_visual(*scenePtr, entity);
    };
    button.onClick = [scenePtr = &scene, entity = handle.root](const PointerInputEvent& event) {
        entt::registry& capturedRegistry = scenePtr->registry();
        UiRadioControlComponent* captured = capturedRegistry.try_get<UiRadioControlComponent>(entity);
        if (!captured)
        {
            return;
        }
        if (captured->singleSelect && captured->checked)
        {
            return;
        }

        const bool nextChecked = captured->singleSelect ? true : !captured->checked;
        if (captured->singleSelect)
        {
            const std::string groupId = captured->groupId;
            auto view = capturedRegistry.view<UiRadioControlComponent>();
            view.each([&](entt::entity otherEntity, UiRadioControlComponent& other) {
                if (otherEntity == entity || !other.singleSelect || other.groupId != groupId || !other.checked)
                {
                    return;
                }

                auto callback = other.onChanged;
                ui_set_radio_checked(*scenePtr, otherEntity, false);
                if (callback)
                {
                    callback(false, event);
                }
            });
        }

        auto callback = captured->onChanged;
        ui_set_radio_checked(*scenePtr, entity, nextChecked);
        if (callback)
        {
            callback(nextChecked, event);
        }
    };
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, ui_transparent_button_visual());
    ui_update_radio_visual(scene, handle.root);
    return handle;
}

inline UiControlHandle ui_create_checkbox(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    UiAlignment alignment,
    glm::vec2 offset,
    std::string label,
    glm::vec2 size,
    int32_t layer,
    uint32_t order,
    UiCheckboxOptions options = {})
{
    Renderer2DScene& scene = ui.scene();
    entt::registry& registry = ui.registry();
    UiControlHandle handle = {};
    handle.root = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(size.x, size.y, ui.scale()),
        ui_clear_surface_style(0.0f),
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.root, 6.0f);
    ui.set_layer(handle.root, layer, order);
    ui.attach_aligned(handle.root, parent, alignment, scaled_offset(offset.x, offset.y, ui.scale()), scaled_size(size.x, size.y, ui.scale()));

    const ShapeStyleComponent uncheckedStyle = ui_macos26_control_style(
        ui,
        options.uncheckedColor,
        options.outlineColor,
        1.0f,
        1.0f);
    const ShapeStyleComponent checkedStyle = ui_macos26_control_style(
        ui,
        options.checkedColor,
        options.checkedColor,
        1.0f,
        1.0f);

    handle.caret = scene.create_shape(
        { 0.0f, 0.0f },
        scaled_size(16.0f, 16.0f, ui.scale()),
        options.checked ? checkedStyle : uncheckedStyle,
        Renderer2DPrimitive::eRoundedRectangle);
    ui.set_shape(handle.caret, 5.0f);
    ui.set_layer(handle.caret, layer + 1, order);
    ui.attach_aligned(handle.caret, handle.root, UiAlignment::eMiddleLeft, glm::vec2(0.0f), scaled_size(16.0f, 16.0f, ui.scale()));

    if (options.checkIcon.valid())
    {
        handle.indicator = ui.create_aligned_media(
            options.checkIcon,
            handle.caret,
            UiAlignment::eCenter,
            { 12.0f, 12.0f },
            layer + 2,
            order);
        ui_tint_icon_media(scene, handle.indicator, options.checkTint);
    }
    else
    {
        handle.indicator = scene.create_shape(
            { 0.0f, 0.0f },
            scaled_size(8.0f, 8.0f, ui.scale()),
            make_solid_style(options.checkTint, "#00000000", 0.0f, options.checked ? 1.0f : 0.0f),
            Renderer2DPrimitive::eSquircle);
        ui.set_shape(handle.indicator, 3.0f);
        ui.set_layer(handle.indicator, layer + 2, order);
        ui.attach_aligned(handle.indicator, handle.caret, UiAlignment::eCenter, glm::vec2(0.0f), scaled_size(8.0f, 8.0f, ui.scale()));
    }

    if (RenderLayer2DComponent* indicatorLayer = registry.try_get<RenderLayer2DComponent>(handle.indicator))
    {
        indicatorLayer->visible = options.checked;
    }

    handle.label = ui.create_aligned_text(
        std::move(label),
        fontAtlas,
        handle.root,
        UiAlignment::eMiddleLeft,
        options.labelFontSize,
        ui_macos26_text_style(options.labelColor),
        layer + 1,
        order + 1u,
        { 24.0f, 0.0f });

    UiCheckboxControlComponent control = {};
    control.checked = options.checked;
    control.box = handle.caret;
    control.indicator = handle.indicator;
    control.uncheckedStyle = uncheckedStyle;
    control.checkedStyle = checkedStyle;
    control.onChanged = std::move(options.onChanged);
    registry.emplace<UiCheckboxControlComponent>(handle.root, std::move(control));

    ButtonInputComponent button = {};
    button.enabled = options.enabled;
    button.onClick = [scenePtr = &scene, entity = handle.root](const PointerInputEvent& event) {
        UiCheckboxControlComponent* captured = scenePtr->registry().try_get<UiCheckboxControlComponent>(entity);
        if (!captured)
        {
            return;
        }

        const bool nextChecked = !captured->checked;
        auto callback = captured->onChanged;
        ui_set_checkbox_checked(*scenePtr, entity, nextChecked);
        if (callback)
        {
            callback(nextChecked, event);
        }
    };
    registry.emplace<ButtonInputComponent>(handle.root, std::move(button));
    registry.emplace<ButtonVisualComponent>(handle.root, ui_transparent_button_visual());
    return handle;
}

inline std::string ui_dropdown_fit_text(
    const Renderer2DFontAtlas& fontAtlas,
    std::string_view value,
    float fontSize,
    float availableWidth,
    std::size_t maxCharacters)
{
    // Dropdown labels prefer measured clipping, then fall back to character count
    return availableWidth > 0.0f ?
        ui_truncate_text_to_width_with_ellipsis(fontAtlas, value, fontSize, availableWidth, maxCharacters) :
        ui_truncate_text_with_ellipsis(value, maxCharacters);
}

inline void ui_refresh_dropdown_rows(
    Renderer2DScene& scene,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity entity)
{
    // Dropdown rows are virtualised so long menus reuse a small visible set
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
        // Keep the selected item inside the visible menu window after programmatic changes
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
    // Opening only toggles the menu subtree; the closed button remains interactive
    entt::registry& registry = scene.registry();
    UiDropdownControlComponent* dropdown = registry.try_get<UiDropdownControlComponent>(entity);
    if (!dropdown)
    {
        return;
    }

    dropdown->open = open;
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
    // Only one floating dropdown should own hover and scroll at a time
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
        // The menu is created as top-most chrome so it can overlap neighbouring panels
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
        // Dropdown scrolling advances by rows rather than pixels
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
            // Each row slot is rebound to a different item as firstVisibleIndex changes
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
            // The idle row is intentionally transparent, but its whitespace is
            // part of the option and must react exactly like its text.
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
    ui_set_dropdown_selected(scene, fontAtlas, handle.root, safeIndex);
    ui_set_dropdown_open(scene, handle.root, false);

    if (ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(handle.root))
    {
        // The closed button toggles the floating menu and closes any sibling dropdown
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

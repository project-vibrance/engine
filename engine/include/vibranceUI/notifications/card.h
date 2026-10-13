#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/renderer/renderer2d_components.h>

#include <glm/vec2.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

class Localisation;
class Renderer2DFontAtlas;
class Renderer2DScene;
class UiBuilder;

inline constexpr float kUiNotificationCardHeight = 72.0f;
inline constexpr float kUiNotificationTwoLineCardHeight = 90.0f;
inline constexpr float kUiNotificationThreeLineCardHeight = 108.0f;
inline constexpr float kUiNotificationActionCardHeight = 104.0f;
inline constexpr float kUiNotificationTwoLineActionCardHeight = 122.0f;
inline constexpr float kUiNotificationThreeLineActionCardHeight = 140.0f;

struct UiNotificationCardMetrics
{
    glm::vec2 size { 376.0f, kUiNotificationCardHeight };
    float cornerRadius = 24.0f;
    float iconSize = 36.0f;
    float contentLeft = 64.0f;
};

struct UiNotificationCardAction
{
    std::string label;
    std::function<void()> invoke;
};

// Presentation-only values. Callers decide how IDs, persistence, permissions,
// elapsed-time formatting, and actions are represented in their own model.
struct UiNotificationCardContent
{
    std::string header;
    std::string title;
    std::string message;
    std::string elapsedText;
    std::string optionsLabel;
};

struct UiNotificationCardCallbacks
{
    std::function<void()> activate;
    std::function<void()> dismiss;
    std::function<void()> showOptions;
};

struct UiNotificationCardOptions
{
    UiTheme theme = ui_light_theme();
    entt::entity parent = entt::null;
    glm::vec2 offset {};
    UiNotificationCardContent content {};
    UiNotificationCardCallbacks callbacks {};
    UiNotificationCardMetrics metrics {};
    Media2DHandle iconMedia {};
    Media2DHandle dismissIcon {};
    // Up to two actions can appear directly in the card's bottom row.
    std::vector<UiNotificationCardAction> actions;
    std::size_t groupSize = 1u;
    float layerOffset = 7.0f;
    std::size_t maximumLayers = 2u;
    bool showOptions = false;
    std::uint32_t order = 0u;
};

struct UiNotificationCardHandle
{
    std::size_t groupSize = 1u;
    glm::vec2 fanStartOffset { 0.0f };
    double fanStartSeconds = -1.0;
    float fanDurationSeconds = 0.42f;
    entt::entity root = entt::null;
    entt::entity surface = entt::null;
    entt::entity icon = entt::null;
    entt::entity timestampText = entt::null;
    entt::entity dismissButton = entt::null;
    entt::entity optionsButton = entt::null;
    std::vector<entt::entity> actionButtons;
    bool dismissVisible = false;
};

VIBRANCE_ENGINE_API std::string ui_supported_notification_text(
    const Renderer2DFontAtlas& fontAtlas,
    const std::string& value,
    float fontSize);

VIBRANCE_ENGINE_API std::array<std::string, 3u>
ui_wrap_notification_message(
    std::string_view message,
    std::size_t maximumLineCharacters = 46u);

VIBRANCE_ENGINE_API UiNotificationCardMetrics ui_measure_notification_card(
    const Renderer2DFontAtlas& fontAtlas,
    std::string_view message,
    bool hasOptions,
    bool hasIcon);

// Builds a complete retained card without depending on an application model.
// Empty callbacks simply disable their corresponding interactions.
VIBRANCE_ENGINE_API UiNotificationCardHandle ui_create_notification_card(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    const Localisation& localisation,
    const UiNotificationCardOptions& options);

VIBRANCE_ENGINE_API void ui_prepare_notification_card_fan(
    UiBuilder& ui,
    UiNotificationCardHandle& card,
    glm::vec2 startOffset,
    double currentTimeSeconds,
    float delaySeconds = 0.0f);

// Advances arrival, reflow or fan motion on the same visual transform used
// by sharp UI and native backdrop regions. Offsets are authored in UI units.
VIBRANCE_ENGINE_API void ui_update_notification_card_motion(
    Renderer2DScene& scene,
    UiNotificationCardHandle& card,
    double currentTimeSeconds);

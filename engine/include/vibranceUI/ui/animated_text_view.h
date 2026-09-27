#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/ui/builder.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

class Engine;

struct UiAnimatedTextTransition
{
    float delaySeconds = 0.0f;
    float durationSeconds = 0.26f;
    float fromOpacity = 0.18f;
    float fromBlurRadius = 10.0f;
    glm::vec2 fromScale { 0.86f, 1.14f };
    float springResponse = 0.24f;
    float springDamping = 0.78f;
    float springInitialVelocity = 0.015f;
    // Changed characters are staggered from right to left. Timers, counters,
    // scores, and similar values therefore update least-significant digits
    // first without requiring separate view implementations.
    float characterStaggerSeconds = 0.0f;
};

// Retained character slots allow only changed glyphs to animate. The value is
// intentionally generic: timers, clocks, counters, scores, and status codes
// can all use the same view.
struct VIBRANCE_ENGINE_API UiAnimatedCharacterTextView
{
    entt::entity root = entt::null;
    std::vector<entt::entity> characters {};
    std::string value {};
    bool proportionalSpacing = false;
    float characterSpacing = 0.0f;
    float punctuationBaselineOffset = 0.0f;
    bool centrePunctuationInCell = false;

    void reset();
    bool valid(const Renderer2DScene& scene) const;
};

struct UiAnimatedCharacterTextOptions
{
    UiAlignment alignment = UiAlignment::eCenter;
    glm::vec2 offset { 0.0f };
    float fontSize = 16.0f;
    std::size_t characterCount = 1u;
    float cellWidthFactor = 0.58f;
    float lineHeightFactor = 1.22f;
    // Fixed cells suit counters whose columns must never move. Proportional
    // spacing uses the font's shaped glyph positions while retaining one
    // entity per character for staggered transitions.
    bool proportionalSpacing = false;
    float characterSpacing = 0.0f;
    // Positive values move punctuation down towards the digit baseline.
    float punctuationBaselineOffset = 0.0f;
    // Keeps punctuation optically centred while letters and digits share the
    // shaped run's baseline.
    bool centrePunctuationInCell = false;
    int32_t layer = 0;
    std::uint32_t order = 0u;
    TextStyleComponent style {};
    bool hideWhitespace = true;
};

VIBRANCE_ENGINE_API void ui_set_animated_text(
    Engine& engine,
    entt::entity entity,
    std::string value,
    double currentTimeSeconds,
    bool animated,
    const UiAnimatedTextTransition& transition = {});

VIBRANCE_ENGINE_API UiAnimatedCharacterTextView
ui_create_animated_character_text(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    std::string value,
    const UiAnimatedCharacterTextOptions& options);

VIBRANCE_ENGINE_API void ui_update_animated_character_text(
    Engine& engine,
    UiAnimatedCharacterTextView& view,
    std::string value,
    double currentTimeSeconds,
    bool animated,
    bool animateDigitsOnly = false,
    const UiAnimatedTextTransition& transition = {});

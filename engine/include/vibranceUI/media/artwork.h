#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/ui/layout.h>

namespace media_ui
{
    struct MediaArtworkPresentation
    {
        glm::vec2 maximumLogicalSize { 112.0f, 66.0f };
        float cornerRadius = 16.0f;
        float edgeSoftness = 1.0f;
        float transitionDuration = 0.58f;
        float revealBlurRadius = 8.0f;
        // Lift only detected black matte/background pixels so dark artwork
        // remains distinguishable from a dark host without washing out color.
        bool autoLiftBlackBackground = true;
        bool synchroniseParentGridColumn = false;
    };

    struct VIBRANCE_ENGINE_API MediaArtworkAnimationState
    {
        Media2DHandle pendingMedia {};
        glm::vec2 outgoingSize { 0.0f };
        glm::vec2 incomingSize { 0.0f };
        double startSeconds = -1.0;
        float scaledRevealBlurRadius = 0.0f;
        bool swapped = false;

        void reset();
        bool active() const;
    };

    struct MediaArtworkPlaybackPresentation
    {
        float pausedScale = 0.88f;
        // Pause feedback is an alpha dim by default. A negative brightness
        // reads as a destructive darkening and collapses shadow detail.
        float pausedOpacity = 0.72f;
        float pausedBrightness = 0.0f;
        float transitionDuration = 0.40f;
        float overshootScale = 0.025f;
    };

    struct VIBRANCE_ENGINE_API MediaArtworkPlaybackAnimationState
    {
        double startSeconds = -1.0;
        float startScale = 1.0f;
        float scale = 1.0f;
        float startBrightness = 0.0f;
        float brightness = 0.0f;
        float startOpacity = 1.0f;
        float opacity = 1.0f;
        bool initialised = false;
        bool targetPlaying = false;

        void reset();
        bool active() const;
    };

    VIBRANCE_ENGINE_API MediaArtworkPresentation
    make_media_artwork_presentation(
        glm::vec2 maximumLogicalSize,
        bool synchroniseParentGridColumn = false);
    VIBRANCE_ENGINE_API glm::vec2 media_artwork_size(
        const Media2DHandle& media,
        const MediaArtworkPresentation& presentation,
        const LayoutScale& scale);
    VIBRANCE_ENGINE_API void apply_media_artwork_presentation(
        Media2DComponent& media,
        const MediaArtworkPresentation& presentation,
        const LayoutScale& scale);
    VIBRANCE_ENGINE_API bool begin_media_artwork_transition(
        Renderer2DScene& scene,
        entt::entity entity,
        MediaArtworkAnimationState& state,
        const Media2DHandle& incomingMedia,
        const MediaArtworkPresentation& presentation,
        const LayoutScale& scale,
        double currentTimeSeconds);
    VIBRANCE_ENGINE_API void update_media_artwork_transition(
        Renderer2DScene& scene,
        entt::entity entity,
        MediaArtworkAnimationState& state,
        const MediaArtworkPresentation& presentation,
        double currentTimeSeconds);
    VIBRANCE_ENGINE_API void update_media_artwork_playback_animation(
        MediaArtworkPlaybackAnimationState& state,
        const MediaArtworkPlaybackPresentation& presentation,
        bool playing,
        double currentTimeSeconds);
    VIBRANCE_ENGINE_API void apply_media_artwork_playback_animation(
        Renderer2DScene& scene,
        entt::entity entity,
        const MediaArtworkPlaybackAnimationState& state);
}

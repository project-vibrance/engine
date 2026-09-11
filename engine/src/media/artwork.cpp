#include <vibranceUI/media/artwork.h>
#include <vibranceUI/media/presentation.h>

#include <algorithm>
#include <cmath>

namespace media_ui
{
    namespace
    {
        float smooth_step(float value)
        {
            const float t = std::clamp(value, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        }

        glm::vec4 artwork_cover_uv(
            const Media2DComponent& media,
            glm::vec2 targetSize)
        {
            if (media.sourcePixelSize.x == 0u ||
                media.sourcePixelSize.y == 0u ||
                targetSize.x <= 0.0f || targetSize.y <= 0.0f)
            {
                return { 0.0f, 0.0f, 1.0f, 1.0f };
            }

            const float sourceAspect =
                static_cast<float>(media.sourcePixelSize.x) /
                static_cast<float>(media.sourcePixelSize.y);
            const float targetAspect = targetSize.x / targetSize.y;
            glm::vec4 uv { 0.0f, 0.0f, 1.0f, 1.0f };
            if (targetAspect > sourceAspect)
            {
                const float visibleHeight = std::clamp(
                    sourceAspect / targetAspect,
                    0.0f,
                    1.0f);
                const float inset = (1.0f - visibleHeight) * 0.5f;
                uv.y = inset;
                uv.w = 1.0f - inset;
            }
            else if (targetAspect < sourceAspect)
            {
                const float visibleWidth = std::clamp(
                    targetAspect / sourceAspect,
                    0.0f,
                    1.0f);
                const float inset = (1.0f - visibleWidth) * 0.5f;
                uv.x = inset;
                uv.z = 1.0f - inset;
            }
            return uv;
        }

        glm::vec2 artwork_entity_size(
            entt::registry& registry,
            entt::entity entity,
            const Media2DComponent& media)
        {
            if (const Layout2DComponent* layout =
                registry.try_get<Layout2DComponent>(entity))
            {
                return layout->size;
            }
            return media.size;
        }

        void synchronise_grid_column(
            Renderer2DScene& scene,
            entt::registry& registry,
            entt::entity entity,
            float width)
        {
            const Parent2DComponent* hierarchy =
                registry.try_get<Parent2DComponent>(entity);
            if (!hierarchy || hierarchy->parent == entt::null ||
                !registry.valid(hierarchy->parent))
            {
                return;
            }
            Grid2DComponent* grid =
                registry.try_get<Grid2DComponent>(hierarchy->parent);
            if (!grid || grid->columns.empty() ||
                std::abs(grid->columns.front().value - width) <= 0.01f)
            {
                return;
            }
            grid->columns.front() = GridTrack2D::pixels(width);
            scene.mark_dirty(hierarchy->parent);
        }
    }

    void MediaArtworkAnimationState::reset()
    {
        pendingMedia = {};
        outgoingSize = glm::vec2(0.0f);
        incomingSize = glm::vec2(0.0f);
        startSeconds = -1.0;
        scaledRevealBlurRadius = 0.0f;
        swapped = false;
    }

    bool MediaArtworkAnimationState::active() const
    {
        return startSeconds >= 0.0 && pendingMedia.valid();
    }

    void MediaArtworkPlaybackAnimationState::reset()
    {
        startSeconds = -1.0;
        startScale = 1.0f;
        scale = 1.0f;
        startBrightness = 0.0f;
        brightness = 0.0f;
        startOpacity = 1.0f;
        opacity = 1.0f;
        initialised = false;
        targetPlaying = false;
    }

    bool MediaArtworkPlaybackAnimationState::active() const
    {
        return startSeconds >= 0.0;
    }

    MediaArtworkPresentation make_media_artwork_presentation(
        glm::vec2 maximumLogicalSize,
        bool synchroniseParentGridColumn)
    {
        MediaArtworkPresentation presentation = {};
        presentation.maximumLogicalSize = glm::max(
            maximumLogicalSize,
            glm::vec2(1.0f));
        presentation.cornerRadius = 22.0f *
            (presentation.maximumLogicalSize.y / 66.0f);
        presentation.synchroniseParentGridColumn =
            synchroniseParentGridColumn;
        return presentation;
    }

    glm::vec2 media_artwork_size(
        const Media2DHandle& media,
        const MediaArtworkPresentation& presentation,
        const LayoutScale& scale)
    {
        const glm::vec2 logicalSize = supported_media_size(
            media,
            presentation.maximumLogicalSize);
        return scaled_size(logicalSize.x, logicalSize.y, scale);
    }

    void apply_media_artwork_presentation(
        Media2DComponent& media,
        const MediaArtworkPresentation& presentation,
        const LayoutScale& scale)
    {
        media.primitive = Renderer2DPrimitive::eSquircle;
        media.cornerRadius = scaled_scalar(
            presentation.cornerRadius,
            scale);
        media.edgeSoftness = scaled_scalar(
            presentation.edgeSoftness,
            scale);
        media.contrast = 1.0f;
        media.brightness = 0.0f;
        media.blurRadius = 0.0f;
        media.set_auto_black_lift(
            presentation.autoLiftBlackBackground);
        media.fit = Media2DFit::eCover;
        media.uvRect = { 0.0f, 0.0f, 1.0f, 1.0f };
    }

    bool begin_media_artwork_transition(
        Renderer2DScene& scene,
        entt::entity entity,
        MediaArtworkAnimationState& state,
        const Media2DHandle& incomingMedia,
        const MediaArtworkPresentation& presentation,
        const LayoutScale& scale,
        double currentTimeSeconds)
    {
        entt::registry& registry = scene.registry();
        if (!incomingMedia.valid() || entity == entt::null ||
            !registry.valid(entity))
        {
            state.reset();
            return false;
        }

        Media2DComponent* media =
            registry.try_get<Media2DComponent>(entity);
        if (!media)
        {
            state.reset();
            return false;
        }
        if (state.active() && state.pendingMedia.id == incomingMedia.id)
        {
            return false;
        }
        if (media->mediaId == incomingMedia.id)
        {
            state.reset();
            return false;
        }

        state.pendingMedia = incomingMedia;
        state.outgoingSize = artwork_entity_size(registry, entity, *media);
        state.incomingSize = media_artwork_size(
            incomingMedia,
            presentation,
            scale);
        state.startSeconds = currentTimeSeconds;
        state.scaledRevealBlurRadius = scaled_scalar(
            presentation.revealBlurRadius,
            scale);
        state.swapped = false;
        media->blurRadius = 0.0f;
        scene.activate_dynamic(
            entity,
            static_cast<double>(presentation.transitionDuration) + 0.08);
        scene.mark_dirty(entity);
        return true;
    }

    void update_media_artwork_transition(
        Renderer2DScene& scene,
        entt::entity entity,
        MediaArtworkAnimationState& state,
        const MediaArtworkPresentation& presentation,
        double currentTimeSeconds)
    {
        if (!state.active())
        {
            return;
        }

        entt::registry& registry = scene.registry();
        if (entity == entt::null || !registry.valid(entity))
        {
            state.reset();
            return;
        }
        Media2DComponent* media =
            registry.try_get<Media2DComponent>(entity);
        if (!media)
        {
            state.reset();
            return;
        }

        const float duration = std::max(
            presentation.transitionDuration,
            0.001f);
        const float progress = std::clamp(
            static_cast<float>(
                (currentTimeSeconds - state.startSeconds) /
                static_cast<double>(duration)),
            0.0f,
            1.0f);
        constexpr float halfPi = 1.57079632679f;
        constexpr float turnPoint = 0.40f;
        glm::vec2 artworkSize = state.outgoingSize;
        float yaw = 0.0f;
        float blurRadius = 0.0f;
        if (progress < turnPoint)
        {
            yaw = halfPi * smooth_step(progress / turnPoint);
        }
        else
        {
            if (!state.swapped)
            {
                media->set_media(state.pendingMedia);
                state.swapped = true;
            }
            artworkSize = state.incomingSize;
            const float phase = std::clamp(
                (progress - turnPoint) / (1.0f - turnPoint), 0.0f, 1.0f);
            const float remaining = 1.0f - phase;
            // Rigid yaw with a small angular settle; never stretch the face.
            yaw = -halfPi * remaining * remaining * remaining +
                0.08f * std::sin(3.14159265359f * smooth_step(phase));
            blurRadius = state.scaledRevealBlurRadius * remaining * remaining;
        }
        artworkSize = glm::max(artworkSize, glm::vec2(1.0f));
        if (Layout2DComponent* layout = registry.try_get<Layout2DComponent>(entity))
        {
            layout->size = artworkSize;
            layout->offset.x = 0.0f;
        }
        media->size = artworkSize;
        media->uvRect = artwork_cover_uv(*media, artworkSize);
        media->fit = Media2DFit::eStretch;
        media->blurRadius = blurRadius;
        if (presentation.synchroniseParentGridColumn)
        {
            synchronise_grid_column(
                scene,
                registry,
                entity,
                state.incomingSize.x);
        }
        if (Transform2DComponent* transform =
            registry.try_get<Transform2DComponent>(entity))
        {
            transform->rotation3DRadians = { 0.0f, yaw };
            // Orthographic projection preserves height and avoids near-edge magnification.
            transform->perspective = 0.0f;
        }
        scene.mark_dirty(entity);

        if (progress < 1.0f)
        {
            return;
        }

        if (Layout2DComponent* layout =
            registry.try_get<Layout2DComponent>(entity))
        {
            layout->size = state.incomingSize;
            layout->offset.x = 0.0f;
        }
        media->size = state.incomingSize;
        media->uvRect = { 0.0f, 0.0f, 1.0f, 1.0f };
        media->fit = Media2DFit::eCover;
        media->blurRadius = 0.0f;
        if (presentation.synchroniseParentGridColumn)
        {
            synchronise_grid_column(
                scene,
                registry,
                entity,
                state.incomingSize.x);
        }
        if (auto* transform = registry.try_get<Transform2DComponent>(entity))
        {
            transform->rotation3DRadians = glm::vec2(0.0f);
            transform->perspective = 0.0f;
        }
        state.reset();
        scene.mark_dirty(entity);
    }

    void update_media_artwork_playback_animation(
        MediaArtworkPlaybackAnimationState& state,
        const MediaArtworkPlaybackPresentation& presentation,
        bool playing,
        double currentTimeSeconds)
    {
        if (!state.initialised)
        {
            state.initialised = true;
            state.targetPlaying = playing;
            state.scale = playing ? 1.0f : presentation.pausedScale;
            state.brightness = playing ?
                0.0f : presentation.pausedBrightness;
            state.opacity = playing ?
                1.0f : std::clamp(
                    presentation.pausedOpacity,
                    0.0f,
                    1.0f);
            return;
        }

        if (playing != state.targetPlaying)
        {
            state.targetPlaying = playing;
            state.startScale = state.scale;
            state.startBrightness = state.brightness;
            state.startOpacity = state.opacity;
            state.startSeconds = currentTimeSeconds;
        }
        if (!state.active())
        {
            return;
        }

        const float elapsed = std::max(
            static_cast<float>(currentTimeSeconds - state.startSeconds),
            0.0f);
        const float duration = std::max(
            presentation.transitionDuration,
            0.001f);
        const float progress = std::clamp(
            elapsed / duration,
            0.0f,
            1.0f);
        const float remaining = 1.0f - progress;
        const float eased = 1.0f - remaining * remaining * remaining;
        const float targetScale = state.targetPlaying ?
            1.0f : presentation.pausedScale;
        const float targetBrightness = state.targetPlaying ?
            0.0f : presentation.pausedBrightness;
        const float targetOpacity = state.targetPlaying ?
            1.0f : std::clamp(presentation.pausedOpacity, 0.0f, 1.0f);
        state.scale = glm::mix(state.startScale, targetScale, eased);
        state.brightness = glm::mix(
            state.startBrightness,
            targetBrightness,
            eased);
        state.opacity = glm::mix(
            state.startOpacity,
            targetOpacity,
            eased);

        // The ease-out and overshoot are one continuous curve. The lobe has
        // zero velocity at both ends, so it rises gently after the initial
        // acceleration and returns cleanly to the target without a seam.
        constexpr float pi = 3.14159265359f;
        if (state.targetPlaying)
        {
            const float overshootWave = std::sin(
                pi * smooth_step(progress));
            state.scale +=
                std::max(presentation.overshootScale, 0.0f) *
                overshootWave;
        }

        if (progress >= 1.0f)
        {
            state.scale = targetScale;
            state.brightness = targetBrightness;
            state.opacity = targetOpacity;
            state.startSeconds = -1.0;
        }
    }

    void apply_media_artwork_playback_animation(
        Renderer2DScene& scene,
        entt::entity entity,
        const MediaArtworkPlaybackAnimationState& state)
    {
        entt::registry& registry = scene.registry();
        if (entity == entt::null || !registry.valid(entity))
        {
            return;
        }

        bool changed = false;
        const glm::vec2 scale(state.scale);
        if (registry.all_of<Layout2DComponent>(entity))
        {
            VisualTransform2DComponent* visual =
                registry.try_get<VisualTransform2DComponent>(entity);
            if (!visual)
            {
                visual = &registry.emplace<VisualTransform2DComponent>(entity);
            }
            if (glm::length(visual->scale - scale) > 0.001f)
            {
                visual->scale = scale;
                changed = true;
            }
            visual->pivot = glm::vec2(0.5f);
        }
        else if (Transform2DComponent* transform =
            registry.try_get<Transform2DComponent>(entity))
        {
            if (glm::length(transform->scale - scale) > 0.001f ||
                glm::length(transform->origin - glm::vec2(0.5f)) > 0.001f)
            {
                transform->scale = scale;
                transform->origin = glm::vec2(0.5f);
                changed = true;
            }
        }

        if (Media2DComponent* media =
            registry.try_get<Media2DComponent>(entity))
        {
            if (std::abs(media->brightness - state.brightness) > 0.001f)
            {
                media->brightness = state.brightness;
                changed = true;
            }
            if (std::abs(media->opacity - state.opacity) > 0.001f)
            {
                media->opacity = state.opacity;
                changed = true;
            }
        }
        if (changed)
        {
            scene.activate_dynamic(entity, 0.12);
            scene.mark_dirty(entity);
        }
    }
}

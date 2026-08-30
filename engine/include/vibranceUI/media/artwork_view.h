#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/media/artwork.h>
#include <vibranceUI/renderer/media2d.h>

#include <cstdint>
#include <filesystem>

class Engine;

namespace media_ui
{
    // Engine-local media source keyed by a provider revision. The descriptor
    // can be shared process-wide while each renderer uploads its own handle.
    class VIBRANCE_ENGINE_API RevisionedMediaSlot
    {
    public:
        bool update(
            Engine& engine,
            const std::filesystem::path& path,
            std::uint64_t revision,
            const Media2DLoadOptions& options = {});
        void reset();

        const Media2DHandle& handle() const;
        std::uint64_t revision() const;

    private:
        std::filesystem::path loadedPath {};
        std::uint64_t loadedRevision = 0u;
        Media2DHandle loadedMedia {};
    };

    // Retained artwork presentation for one renderer/scene. It owns the
    // entity-specific transition state so feature controllers do not need to
    // retain and reset parallel raw handles and animation structures.
    class VIBRANCE_ENGINE_API MediaArtworkView
    {
    public:
        void bind(
            entt::entity entity,
            MediaArtworkPresentation presentation = {});
        void reset();

        entt::entity entity() const;
        bool valid(const Renderer2DScene& scene) const;
        MediaArtworkPresentation& presentation();
        const MediaArtworkPresentation& presentation() const;

        bool transition_to(
            Renderer2DScene& scene,
            const Media2DHandle& media,
            const LayoutScale& scale,
            double currentTimeSeconds);
        void tick(Renderer2DScene& scene, double currentTimeSeconds);

    private:
        entt::entity artworkEntity = entt::null;
        MediaArtworkPresentation artworkPresentation {};
        MediaArtworkAnimationState transitionState {};
    };
}

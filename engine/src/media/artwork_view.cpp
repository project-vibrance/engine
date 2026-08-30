#include <vibranceUI/media/artwork_view.h>
#include <vibranceUI/renderer/renderer.h>

#include <utility>

namespace media_ui
{
    bool RevisionedMediaSlot::update(
        Engine& engine,
        const std::filesystem::path& path,
        std::uint64_t revision,
        const Media2DLoadOptions& options)
    {
        if (path.empty() || revision == 0u)
        {
            const bool changed = loadedMedia.valid() || loadedRevision != 0u;
            reset();
            return changed;
        }
        if (loadedRevision == revision && loadedPath == path)
        {
            return false;
        }

        const Media2DHandle next = engine.load_media_2d(path, options);
        const bool changed = next.id != loadedMedia.id;
        loadedPath = path;
        loadedRevision = revision;
        loadedMedia = next;
        return changed;
    }

    void RevisionedMediaSlot::reset()
    {
        loadedPath.clear();
        loadedRevision = 0u;
        loadedMedia = {};
    }

    const Media2DHandle& RevisionedMediaSlot::handle() const
    {
        return loadedMedia;
    }

    std::uint64_t RevisionedMediaSlot::revision() const
    {
        return loadedRevision;
    }

    void MediaArtworkView::bind(
        entt::entity entity,
        MediaArtworkPresentation presentation)
    {
        artworkEntity = entity;
        artworkPresentation = std::move(presentation);
        transitionState.reset();
    }

    void MediaArtworkView::reset()
    {
        artworkEntity = entt::null;
        transitionState.reset();
    }

    entt::entity MediaArtworkView::entity() const
    {
        return artworkEntity;
    }

    bool MediaArtworkView::valid(const Renderer2DScene& scene) const
    {
        return artworkEntity != entt::null &&
            scene.registry().valid(artworkEntity);
    }

    MediaArtworkPresentation& MediaArtworkView::presentation()
    {
        return artworkPresentation;
    }

    const MediaArtworkPresentation& MediaArtworkView::presentation() const
    {
        return artworkPresentation;
    }

    bool MediaArtworkView::transition_to(
        Renderer2DScene& scene,
        const Media2DHandle& media,
        const LayoutScale& scale,
        double currentTimeSeconds)
    {
        return begin_media_artwork_transition(
            scene,
            artworkEntity,
            transitionState,
            media,
            artworkPresentation,
            scale,
            currentTimeSeconds);
    }

    void MediaArtworkView::tick(
        Renderer2DScene& scene,
        double currentTimeSeconds)
    {
        update_media_artwork_transition(
            scene,
            artworkEntity,
            transitionState,
            artworkPresentation,
            currentTimeSeconds);
    }
}

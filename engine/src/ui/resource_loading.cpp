#include <vibranceUI/ui/resource_loading.h>

#include <cstddef>
#include <vector>

#include <vibranceUI/core/logger.h>
#include <vibranceUI/renderer/renderer.h>

Media2DHandle ui_load_layered_media(
    Engine& engine,
    const UiResourceDirectories& resources,
    const std::filesystem::path& relativePath,
    const Media2DLoadOptions& options,
    Logger* logger)
{
    const std::vector<std::filesystem::path> candidates =
        resources.asset_candidates(relativePath);
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        Media2DHandle handle = engine.load_media_2d(candidates[index], options);
        if (!handle.drawable)
        {
            continue;
        }
        if (index > 0 && logger)
        {
            logger->warning(
                "Using packaged fallback after an override failed to decode: " +
                candidates[index].string());
        }
        return handle;
    }
    return {};
}

AudioClipHandle ui_load_layered_audio(
    Engine& engine,
    const UiResourceDirectories& resources,
    const std::filesystem::path& relativePath,
    Logger* logger)
{
    const std::vector<std::filesystem::path> candidates =
        resources.asset_candidates(relativePath);
    for (std::size_t index = 0; index < candidates.size(); ++index)
    {
        AudioClipHandle handle = engine.load_audio_clip(candidates[index]);
        if (!handle.valid())
        {
            continue;
        }
        if (index > 0 && logger)
        {
            logger->warning(
                "Using packaged audio fallback after an override failed to decode: " +
                candidates[index].string());
        }
        return handle;
    }
    return {};
}

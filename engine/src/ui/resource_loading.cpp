#include <vibranceUI/core/path_utf8.h>
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
    const LogOperation diagnosticOperation("resources: ui_load_layered_media");
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
                vibrance::path_utf8(candidates[index]));
        }
        if (logger) logger->debug("Resource loaded: " + vibrance::path_utf8(candidates[index]));
        return handle;
    }
    if (logger) logger->error("No resource candidate could be loaded: " + vibrance::path_utf8(relativePath));
    return {};
}

AudioClipHandle ui_load_layered_audio(
    Engine& engine,
    const UiResourceDirectories& resources,
    const std::filesystem::path& relativePath,
    Logger* logger)
{
    const LogOperation diagnosticOperation("resources: ui_load_layered_audio");
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
                vibrance::path_utf8(candidates[index]));
        }
        if (logger) logger->debug("Resource loaded: " + vibrance::path_utf8(candidates[index]));
        return handle;
    }
    if (logger) logger->error("No resource candidate could be loaded: " + vibrance::path_utf8(relativePath));
    return {};
}

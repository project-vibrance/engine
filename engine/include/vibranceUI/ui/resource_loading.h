#pragma once

#include <filesystem>

#include <vibranceUI/audio/audio.h>
#include <vibranceUI/export.h>
#include <vibranceUI/renderer/media2d.h>
#include <vibranceUI/ui/resources.h>

class Engine;
class Logger;

// Loads the first valid user override or packaged fallback. A corrupt override
// therefore cannot prevent a packaged asset from loading.
VIBRANCE_ENGINE_API Media2DHandle ui_load_layered_media(
    Engine& engine,
    const UiResourceDirectories& resources,
    const std::filesystem::path& relativePath,
    const Media2DLoadOptions& options = {},
    Logger* logger = nullptr);

VIBRANCE_ENGINE_API AudioClipHandle ui_load_layered_audio(
    Engine& engine,
    const UiResourceDirectories& resources,
    const std::filesystem::path& relativePath,
    Logger* logger = nullptr);

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <filesystem>

#include <vibranceUI/audio/audio.h>
#include <vibranceUI/export.h>
#include <vibranceUI/renderer/media2d.h>
#include <vibranceUI/ui/resources.h>

class Engine;
class Logger;



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

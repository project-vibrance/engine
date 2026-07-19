#pragma once

#include <vibranceUI/export.h>

struct EngineManifest
{
    const char* name;
    const char* version;
    const char* suffix;
    const char* full_version;
    const char* lastUpdated;
};

// The returned object and its strings have static lifetime in the engine DLL.
[[nodiscard]] VIBRANCE_ENGINE_API const EngineManifest& vibrance_engine_manifest() noexcept;

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

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


[[nodiscard]] VIBRANCE_ENGINE_API const EngineManifest& vibrance_engine_manifest() noexcept;

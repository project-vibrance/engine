#pragma once
#include <vibranceUI/export.h>
// Returns 0 for unsupported hardware, otherwise the native Metal API generation.
VIBRANCE_ENGINE_API unsigned vibrance_metal_version() noexcept;

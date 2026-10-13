#pragma once
#include <vibranceUI/export.h>
// Returns 0 for unsupported hardware, otherwise the selected native renderer version (Metal 3).
VIBRANCE_ENGINE_API unsigned vibrance_metal_version() noexcept;

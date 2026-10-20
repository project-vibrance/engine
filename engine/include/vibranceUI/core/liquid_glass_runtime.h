#pragma once

#include <vibranceUI/export.h>

namespace vibrance
{
// Cached off the render loop. True only after the current app's replacement
// runtime was staged and an older pinned compositor runtime was identified.
// A Windows sign-out is needed to release that runtime. Other hosts return false.
[[nodiscard]] VIBRANCE_ENGINE_API bool liquid_glass_runtime_restart_required() noexcept;
}

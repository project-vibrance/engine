#pragma once
#include <vibranceUI/display/placement.h>

// True when another process has a visible foreground window covering this
// monitor. Maximised work-area windows and desktop shell surfaces do not count.
// Currently implemented on Windows; unsupported platforms return false.
// The application decides whether to hide, pause or keep its own windows visible.
VIBRANCE_ENGINE_API bool foreground_application_is_fullscreen_on_monitor(
    const DisplayMonitorTarget& monitor);

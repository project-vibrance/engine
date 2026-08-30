#pragma once

#include <vibranceUI/display/placement.h>
#include <vibranceUI/export.h>

#include <filesystem>
#include <string>

// Each feature should own a storage file so one interface cannot overwrite
// another interface's monitor selection.
struct DisplayPreferenceStorage
{
    std::filesystem::path path;
    std::string modeKey = "display";
    std::string monitorKey = "monitor";
    bool migrateLegacyAnyToMonitor = false;
};

VIBRANCE_ENGINE_API DisplayTargetPreference load_display_target_preference(
    const DisplayPreferenceStorage& storage,
    DisplayTargetPreference fallback = {});
VIBRANCE_ENGINE_API bool save_display_target_preference(
    const DisplayPreferenceStorage& storage,
    const DisplayTargetPreference& preference);

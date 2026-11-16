// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/display/placement.h>
#include <vibranceUI/export.h>

#include <filesystem>
#include <string>



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

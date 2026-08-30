#pragma once

#include "vibranceUI/export.h"

#include <filesystem>
#include <optional>
#include <string>

struct NativeFolderDialogOptions
{
    std::string title = "Select a folder";
    std::filesystem::path initialDirectory {};
    // HWND on Windows. Other platforms currently ignore the parent handle.
    void* parentWindow = nullptr;
};

// Opens the platform folder picker and returns no value when the user cancels.
VIBRANCE_ENGINE_API std::optional<std::filesystem::path>
show_native_folder_dialog(const NativeFolderDialogOptions& options = {});

// Returns the conventional machine-wide application directory for the host.
// Windows: Program Files, macOS: /Applications, Linux: /opt.
VIBRANCE_ENGINE_API std::filesystem::path
native_program_files_directory();

VIBRANCE_ENGINE_API bool native_process_is_elevated();

// Checks write access against the closest existing ancestor. Installers can
// defer UAC/sudo until they actually write instead of elevating folder browse.
VIBRANCE_ENGINE_API bool native_path_requires_elevation(
    const std::filesystem::path& path);

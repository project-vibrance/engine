// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vibranceUI/export.h"

#include <filesystem>
#include <optional>
#include <string>
#include <vector>

struct NativeFolderDialogOptions
{
    std::string title = "Select a folder";
    std::filesystem::path initialDirectory {};

    void* parentWindow = nullptr;
};


VIBRANCE_ENGINE_API std::optional<std::filesystem::path>
show_native_folder_dialog(const NativeFolderDialogOptions& options = {});



VIBRANCE_ENGINE_API std::filesystem::path
native_program_files_directory();

VIBRANCE_ENGINE_API bool native_process_is_elevated();



VIBRANCE_ENGINE_API bool native_path_requires_elevation(
    const std::filesystem::path& path);

struct NativeFileDialogFilter
{
    std::string name;
    std::vector<std::string> extensions;
};

struct NativeFileDialogOptions
{
    std::string title = "Select a file";
    std::filesystem::path initialDirectory;
    std::vector<NativeFileDialogFilter> filters;
    void* parentWindow = nullptr;
    bool allowAllFiles = false;
};

VIBRANCE_ENGINE_API std::optional<std::filesystem::path> show_native_file_dialog(
    const NativeFileDialogOptions& options, std::string& error);
VIBRANCE_ENGINE_API bool native_open_directory(const std::filesystem::path& directory,
    std::string& error, bool createIfMissing = false);

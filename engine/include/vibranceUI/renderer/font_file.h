// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/export.h>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace font_files
{
struct FaceNames
{
    std::string family;
    std::string style;
    std::string displayName;
};

VIBRANCE_ENGINE_API std::optional<std::vector<unsigned char>> read_font(
    const std::filesystem::path& source, std::string& error);
VIBRANCE_ENGINE_API bool validate_font_file(const std::filesystem::path& source, std::string& error);
VIBRANCE_ENGINE_API std::vector<FaceNames> font_face_names(const std::filesystem::path& source, std::string& error);
VIBRANCE_ENGINE_API std::string font_display_name(const std::filesystem::path& source);
}

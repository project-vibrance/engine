// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include "vibranceUI/export.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>



VIBRANCE_ENGINE_API std::vector<unsigned char> read_binary_file(
    const std::filesystem::path& path);
VIBRANCE_ENGINE_API std::string read_text_file(
    const std::filesystem::path& path);






VIBRANCE_ENGINE_API bool write_file_atomically(
    const std::filesystem::path& path, std::string_view bytes);

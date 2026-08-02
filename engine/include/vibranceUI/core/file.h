#pragma once
#include "vibranceUI/export.h"
#include <filesystem>
#include <string>
#include <vector>

// Silent resource helpers for callers that implement layered fallback and
// therefore expect an unreadable candidate to return an empty result.
VIBRANCE_ENGINE_API std::vector<unsigned char> read_binary_file(
    const std::filesystem::path& path);
VIBRANCE_ENGINE_API std::string read_text_file(
    const std::filesystem::path& path);

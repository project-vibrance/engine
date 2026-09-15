#pragma once
#include "vibranceUI/export.h"
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

// Silent resource helpers for callers that implement layered fallback and
// therefore expect an unreadable candidate to return an empty result.
VIBRANCE_ENGINE_API std::vector<unsigned char> read_binary_file(
    const std::filesystem::path& path);
VIBRANCE_ENGINE_API std::string read_text_file(
    const std::filesystem::path& path);

// Creates parent directories and replaces a complete document only after its
// temporary file has closed successfully. Failed writes preserve the old file.
// Bytes may contain NULs. Concurrent writers use separate temporary files;
// the last successful replacement wins. This is not a power-loss durability
// guarantee or a secure credential store.
VIBRANCE_ENGINE_API bool write_file_atomically(
    const std::filesystem::path& path, std::string_view bytes);

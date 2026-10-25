#pragma once

#include <filesystem>
#include <string>

namespace vibrance
{
// Filesystem::path::string() uses the Windows ANSI/OEM code page. Diagnostics
// and UTF-8 resource names must not depend on the user's regional settings.
inline std::string path_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}

inline std::string generic_path_utf8(const std::filesystem::path& path)
{
    const auto encoded = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(encoded.data()), encoded.size());
}
}

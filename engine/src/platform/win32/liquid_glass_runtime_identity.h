#pragma once

#include "liquid_glass_dwm_identity.h"
#include <filesystem>
#include <fstream>
#include <string_view>

namespace vibrance::win32
{
inline constexpr int liquidRuntimeRestartRequired = 25;
inline constexpr std::array<const wchar_t*, 7> liquidRuntimeNames {
    L"vibrance_liquid_dwm_extension.dll", L"liquid_glass_win32.cso",
    L"liquid_glass_ui_win32.cso", L"liquid_glass_fullscreen.cso",
    L"liquid_glass_blur_horizontal.cso", L"liquid_glass_blur_vertical.cso",
    L"liquid_glass_compare.cso"
};

inline bool liquid_runtime_key_valid(std::wstring_view key) noexcept
{
    if (key.size() != 64) return false;
    for (const wchar_t character : key)
        if (!(character >= L'0' && character <= L'9') &&
            !(character >= L'a' && character <= L'f')) return false;
    return true;
}

struct LiquidRuntimeBundleStamp
{
    struct File
    {
        std::filesystem::file_time_type modified {};
        std::uintmax_t size = 0;
        bool operator==(const File&) const = default;
    };
    std::array<File, liquidRuntimeNames.size()> files {};
    bool complete = false;
    bool operator==(const LiquidRuntimeBundleStamp&) const = default;
};

inline LiquidRuntimeBundleStamp liquid_runtime_bundle_stamp(const std::filesystem::path& directory)
{
    LiquidRuntimeBundleStamp stamp;
    for (std::size_t index = 0; index < liquidRuntimeNames.size(); ++index)
    {
        std::error_code error;
        const auto path = directory / liquidRuntimeNames[index];
        stamp.files[index].modified = std::filesystem::last_write_time(path, error);
        if (error) return stamp;
        stamp.files[index].size = std::filesystem::file_size(path, error);
        if (error) return stamp;
    }
    stamp.complete = true;
    return stamp;
}

inline std::wstring liquid_runtime_key(const std::filesystem::path& extension)
{
    std::uint8_t hashes[liquidRuntimeNames.size()][32] {}, digest[32] {};
    for (std::size_t index = 0; index < liquidRuntimeNames.size(); ++index)
    {
        const auto path = index == 0 ? extension : extension.parent_path() / liquidRuntimeNames[index];
        if (!file_sha256(path.wstring(), hashes[index])) return {};
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0) return {};
    DWORD objectLength = 0, returned = 0;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
        reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &returned, 0) >= 0;
    std::vector<std::uint8_t> object(objectLength);
    ok = ok && BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) >= 0;
    ok = ok && BCryptHashData(hash, &hashes[0][0], sizeof(hashes), 0) >= 0 &&
        BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return {};
    std::wstring result;
    for (const auto byte : digest)
    {
        result += L"0123456789abcdef"[byte >> 4];
        result += L"0123456789abcdef"[byte & 15];
    }
    return result;
}

struct LiquidRuntimeStatus
{
    DWORD compositorPid = 0;
    std::wstring requestedKey, activeKey;
    std::filesystem::path stagedExtension, activeExtension;
};

inline LiquidRuntimeStatus read_liquid_runtime_status(const std::filesystem::path& directory)
{
    std::wifstream stream(directory / L"runtime-status.txt");
    std::wstring version, pid;
    LiquidRuntimeStatus result;
    if (!std::getline(stream, version) || version != L"VIBRANCE_LIQUID_RUNTIME_STATUS_V1" ||
        !std::getline(stream, pid) || !std::getline(stream, result.requestedKey) ||
        !std::getline(stream, result.activeKey)) return {};
    std::wstring staged, active;
    if (!std::getline(stream, staged) || !std::getline(stream, active)) return {};
    try { result.compositorPid = static_cast<DWORD>(std::stoul(pid)); }
    catch (...) { return {}; }
    result.stagedExtension = staged;
    result.activeExtension = active;
    return result;
}

inline bool write_liquid_runtime_status(const std::filesystem::path& directory,
    const LiquidRuntimeStatus& status)
{
    const auto target = directory / L"runtime-status.txt";
    auto temporary = target;
    temporary += L".pending-" + std::to_wstring(GetCurrentProcessId());
    {
        std::wofstream stream(temporary, std::ios::trunc);
        stream << L"VIBRANCE_LIQUID_RUNTIME_STATUS_V1\n" << status.compositorPid << L'\n' <<
            status.requestedKey << L'\n' << status.activeKey << L'\n' <<
            status.stagedExtension.wstring() << L'\n' << status.activeExtension.wstring() << L'\n';
        stream.close();
        if (!stream) { DeleteFileW(temporary.c_str()); return false; }
    }
    const bool result = MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING) != FALSE;
    if (!result) DeleteFileW(temporary.c_str());
    return result;
}

inline bool liquid_runtime_restart_required(const LiquidRuntimeStatus& status,
    DWORD compositorPid, const std::wstring& requestedKey) noexcept
{
    return compositorPid != 0 && status.compositorPid == compositorPid &&
        liquid_runtime_key_valid(requestedKey) && status.requestedKey == requestedKey &&
        liquid_runtime_key_valid(status.activeKey) && status.activeKey != requestedKey &&
        !status.stagedExtension.empty();
}
}

#pragma once

#include "../../engine/src/platform/win32/liquid_glass_dwm_identity.h"
#include <aclapi.h>
#include <sddl.h>

#include <filesystem>
#include <system_error>

namespace vibrance::win32
{
// DWM runs as a Window Manager virtual account, rather than the interactive
// user or SYSTEM. Private AppData ACLs otherwise prevent it loading this bundle.
inline bool grant_dwm_runtime_read_access(std::wstring path)
{
    PSID windowManager = nullptr;
    if (!ConvertStringSidToSidW(L"S-1-5-90-0", &windowManager)) return false;
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    PACL existing = nullptr;
    DWORD result = GetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT,
        DACL_SECURITY_INFORMATION, nullptr, nullptr, &existing, nullptr, &descriptor);
    PACL updated = nullptr;
    if (result == ERROR_SUCCESS && existing)
    {
        EXPLICIT_ACCESSW access {};
        access.grfAccessPermissions = FILE_GENERIC_READ | FILE_GENERIC_EXECUTE;
        access.grfAccessMode = GRANT_ACCESS;
        access.grfInheritance = NO_INHERITANCE;
        access.Trustee.TrusteeForm = TRUSTEE_IS_SID;
        access.Trustee.TrusteeType = TRUSTEE_IS_GROUP;
        access.Trustee.ptstrName = static_cast<LPWSTR>(windowManager);
        result = SetEntriesInAclW(1, &access, existing, &updated);
        if (result == ERROR_SUCCESS)
            result = SetNamedSecurityInfoW(path.data(), SE_FILE_OBJECT,
                DACL_SECURITY_INFORMATION, nullptr, nullptr, updated, nullptr);
    }
    // A null DACL already permits access. Preserve it rather than replacing
    // it with a DACL that unexpectedly removes the creator's permissions.
    if (updated) LocalFree(updated);
    if (descriptor) LocalFree(descriptor);
    LocalFree(windowManager);
    return result == ERROR_SUCCESS;
}

// DWM pins the extension for its lifetime. Load an immutable bundle outside
// build/install directories so rebuilding and packaging never overwrite it.
inline std::wstring cache_liquid_runtime(const std::wstring& extension,
                                        const std::wstring& cacheRoot)
{
    namespace fs = std::filesystem;
    constexpr std::array<const wchar_t*, 7> names {
        L"vibrance_liquid_dwm_extension.dll", L"liquid_glass_win32.cso",
        L"liquid_glass_ui_win32.cso", L"liquid_glass_fullscreen.cso",
        L"liquid_glass_blur_horizontal.cso", L"liquid_glass_blur_vertical.cso",
        L"liquid_glass_compare.cso"
    };
    std::uint8_t fingerprints[names.size()][32] {};
    const auto sourceDirectory = fs::absolute(extension).parent_path();
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        const auto source = index == 0 ? fs::absolute(extension) :
            sourceDirectory / names[index];
        if (!file_sha256(source.wstring(), fingerprints[index])) return {};
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    std::uint8_t bundleHash[32] {};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM,
            nullptr, 0) < 0) return {};
    const bool hashed = BCryptHash(algorithm, nullptr, 0,
        &fingerprints[0][0], sizeof(fingerprints), bundleHash,
        sizeof(bundleHash)) >= 0;
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!hashed) return {};
    std::wstring version;
    for (const auto byte : bundleHash)
    {
        version += L"0123456789abcdef"[byte >> 4];
        version += L"0123456789abcdef"[byte & 15];
    }
    const auto directory = fs::absolute(cacheRoot) / version;
    const auto validBundle = [&] {
        for (std::size_t index = 0; index < names.size(); ++index)
        {
            std::uint8_t digest[32] {};
            if (!file_sha256((directory / names[index]).wstring(), digest) ||
                std::memcmp(digest, fingerprints[index], sizeof(digest)) != 0)
                return false;
        }
        return true;
    };
    const auto grantBundleAccess = [&](const fs::path& bundle) {
        if (!grant_dwm_runtime_read_access(bundle.wstring())) return false;
        for (const auto* name : names)
            if (!grant_dwm_runtime_read_access((bundle / name).wstring())) return false;
        return true;
    };
    // Repair permissions on caches produced by earlier brokers as well.
    if (validBundle()) return grantBundleAccess(directory) ?
        (directory / names[0]).wstring() : std::wstring {};

    // Publish the complete, verified bundle atomically. A concurrent broker
    // may win the rename; only reuse its directory after checking every file.
    const auto staged = fs::absolute(cacheRoot) / (version + L".staging-" +
        std::to_wstring(GetCurrentProcessId()) + L"-" +
        std::to_wstring(GetTickCount64()));
    std::error_code error;
    fs::create_directories(staged.parent_path(), error);
    if (error || !fs::create_directory(staged, error)) return {};
    struct StagingCleanup
    {
        fs::path path;
        ~StagingCleanup()
        {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    } cleanup { staged };
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        const auto source = index == 0 ? fs::absolute(extension) :
            sourceDirectory / names[index];
        if (!fs::copy_file(source, staged / names[index], error)) return {};
        std::uint8_t digest[32] {};
        if (!file_sha256((staged / names[index]).wstring(), digest) ||
            std::memcmp(digest, fingerprints[index], sizeof(digest)) != 0)
            return {};
    }
    if (!grantBundleAccess(staged)) return {};
    fs::rename(staged, directory, error);
    return validBundle() && grantBundleAccess(directory) ?
        (directory / names[0]).wstring() : std::wstring {};
}
}

#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace vibrance::win32
{
inline bool file_sha256(const std::wstring& path, std::uint8_t (&digest)[32])
{
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    DWORD objectLength = 0;
    DWORD returned = 0;
    std::vector<std::uint8_t> object;
    bool success = BCryptOpenAlgorithmProvider(&algorithm,
        BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (success)
    {
        success = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH,
            reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
            &returned, 0) >= 0 && objectLength > 0;
    }
    if (success)
    {
        object.resize(objectLength);
        success = BCryptCreateHash(algorithm, &hash, object.data(),
            objectLength, nullptr, 0, 0) >= 0;
    }
    std::array<std::uint8_t, 65536> buffer {};
    DWORD read = 0;
    while (success)
    {
        if (!ReadFile(file, buffer.data(),
                static_cast<DWORD>(buffer.size()), &read, nullptr))
        {
            success = false;
            break;
        }
        if (read == 0)
        {
            break;
        }
        success = BCryptHashData(hash, buffer.data(), read, 0) >= 0;
    }
    if (success)
    {
        success = BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    }
    if (hash)
    {
        BCryptDestroyHash(hash);
    }
    if (algorithm)
    {
        BCryptCloseAlgorithmProvider(algorithm, 0);
    }
    CloseHandle(file);
    return success;
}

inline std::wstring module_path(HMODULE module)
{
    std::wstring path(32768, L'\0');
    const DWORD length = GetModuleFileNameW(module, path.data(),
        static_cast<DWORD>(path.size()));
    if (length == 0 || length >= path.size())
    {
        return {};
    }
    path.resize(length);
    return path;
}
}

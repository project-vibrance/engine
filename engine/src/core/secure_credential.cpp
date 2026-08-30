#include "vibranceUI/core/secure_credential.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>
#elif defined(__APPLE__)
#include <Security/Security.h>
#endif

namespace
{
constexpr std::array<unsigned char, 8u> oauthMagic {
    'V', 'O', 'A', 'U', 'T', 'H', '1', 0
};
constexpr std::size_t maximumCredentialBytes = 2400u;
constexpr std::size_t maximumFieldBytes = 1800u;

SecureCredentialResult make_result(
    SecureCredentialStatus status,
    std::string error = {})
{
    return { status, std::move(error) };
}

bool valid_key_part(std::string_view value)
{
    return !value.empty() && value.size() <= 512u &&
        value.find('\0') == std::string_view::npos;
}

std::string target_name(std::string_view service, std::string_view account)
{
    std::string target(service);
    target.push_back('/');
    target.append(account);
    return target;
}

#if defined(_WIN32)
std::optional<std::wstring> utf8_to_wide(std::string_view value)
{
    if (value.empty())
    {
        return std::wstring {};
    }
    const int required = MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        value.data(),
        static_cast<int>(value.size()),
        nullptr,
        0);
    if (required <= 0)
    {
        return std::nullopt;
    }
    std::wstring result(static_cast<std::size_t>(required), L'\0');
    if (MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            required) != required)
    {
        return std::nullopt;
    }
    return result;
}

std::string windows_error(DWORD code)
{
    wchar_t* message = nullptr;
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER |
            FORMAT_MESSAGE_FROM_SYSTEM |
            FORMAT_MESSAGE_IGNORE_INSERTS,
        nullptr,
        code,
        0,
        reinterpret_cast<wchar_t*>(&message),
        0,
        nullptr);
    std::string result = "Windows credential error " +
        std::to_string(code) + ".";
    if (length && message)
    {
        const int bytes = WideCharToMultiByte(
            CP_UTF8,
            0,
            message,
            static_cast<int>(length),
            nullptr,
            0,
            nullptr,
            nullptr);
        if (bytes > 0)
        {
            std::string detail(static_cast<std::size_t>(bytes), '\0');
            WideCharToMultiByte(
                CP_UTF8,
                0,
                message,
                static_cast<int>(length),
                detail.data(),
                bytes,
                nullptr,
                nullptr);
            while (!detail.empty() &&
                (detail.back() == '\r' || detail.back() == '\n'))
            {
                detail.pop_back();
            }
            result += " " + detail;
        }
    }
    if (message)
    {
        LocalFree(message);
    }
    return result;
}

bool windows_credential_provider_unavailable(DWORD code)
{
    // Credential Manager is tied to the interactive user's logon session.
    // Service, sandboxed, and some remote test sessions can expose the API but
    // have no vault available to service a request.
    return code == ERROR_NO_SUCH_LOGON_SESSION;
}
#endif

void append_u32(std::string& output, std::uint32_t value)
{
    for (unsigned shift = 0u; shift < 32u; shift += 8u)
    {
        output.push_back(static_cast<char>((value >> shift) & 0xffu));
    }
}

void append_u64(std::string& output, std::uint64_t value)
{
    for (unsigned shift = 0u; shift < 64u; shift += 8u)
    {
        output.push_back(static_cast<char>((value >> shift) & 0xffu));
    }
}

bool append_field(std::string& output, std::string_view value)
{
    if (value.size() > maximumFieldBytes ||
        value.size() > std::numeric_limits<std::uint32_t>::max())
    {
        return false;
    }
    append_u32(output, static_cast<std::uint32_t>(value.size()));
    output.append(value);
    return output.size() <= maximumCredentialBytes;
}

bool read_u32(
    std::string_view input,
    std::size_t& offset,
    std::uint32_t& value)
{
    if (input.size() - std::min(input.size(), offset) < 4u)
    {
        return false;
    }
    value = 0u;
    for (unsigned index = 0u; index < 4u; ++index)
    {
        value |= static_cast<std::uint32_t>(
            static_cast<unsigned char>(input[offset++])) << (index * 8u);
    }
    return true;
}

bool read_u64(
    std::string_view input,
    std::size_t& offset,
    std::uint64_t& value)
{
    if (input.size() - std::min(input.size(), offset) < 8u)
    {
        return false;
    }
    value = 0u;
    for (unsigned index = 0u; index < 8u; ++index)
    {
        value |= static_cast<std::uint64_t>(
            static_cast<unsigned char>(input[offset++])) << (index * 8u);
    }
    return true;
}

bool read_field(
    std::string_view input,
    std::size_t& offset,
    std::string& value)
{
    std::uint32_t length = 0u;
    if (!read_u32(input, offset, length) ||
        length > maximumFieldBytes ||
        input.size() - std::min(input.size(), offset) < length)
    {
        return false;
    }
    value.assign(input.substr(offset, length));
    offset += length;
    return true;
}

std::optional<std::string> serialise_oauth(
    const SecureOAuthCredential& credential)
{
    if (credential.accessToken.empty() || credential.refreshToken.empty() ||
        credential.expiresAtUnixSeconds <= 0)
    {
        return std::nullopt;
    }
    std::string data;
    data.reserve(
        40u + credential.accessToken.size() +
        credential.refreshToken.size() + credential.scopes.size());
    data.append(
        reinterpret_cast<const char*>(oauthMagic.data()),
        oauthMagic.size());
    append_u32(data, static_cast<std::uint32_t>(credential.tokenType));
    append_u64(
        data,
        static_cast<std::uint64_t>(credential.expiresAtUnixSeconds));
    if (!append_field(data, credential.accessToken) ||
        !append_field(data, credential.refreshToken) ||
        !append_field(data, credential.scopes))
    {
        secure_clear_string(data);
        return std::nullopt;
    }
    return data;
}

std::optional<SecureOAuthCredential> deserialise_oauth(
    std::string_view data)
{
    if (data.size() < oauthMagic.size() + 12u ||
        std::memcmp(data.data(), oauthMagic.data(), oauthMagic.size()) != 0)
    {
        return std::nullopt;
    }
    std::size_t offset = oauthMagic.size();
    std::uint32_t tokenType = 0u;
    std::uint64_t expiry = 0u;
    SecureOAuthCredential credential;
    if (!read_u32(data, offset, tokenType) ||
        !read_u64(data, offset, expiry) ||
        expiry > static_cast<std::uint64_t>(
            std::numeric_limits<std::int64_t>::max()) ||
        !read_field(data, offset, credential.accessToken) ||
        !read_field(data, offset, credential.refreshToken) ||
        !read_field(data, offset, credential.scopes) ||
        offset != data.size())
    {
        secure_clear_string(credential.accessToken);
        secure_clear_string(credential.refreshToken);
        return std::nullopt;
    }
    credential.tokenType = static_cast<std::int32_t>(tokenType);
    credential.expiresAtUnixSeconds = static_cast<std::int64_t>(expiry);
    if (credential.accessToken.empty() || credential.refreshToken.empty() ||
        credential.expiresAtUnixSeconds <= 0)
    {
        secure_clear_string(credential.accessToken);
        secure_clear_string(credential.refreshToken);
        return std::nullopt;
    }
    return credential;
}
}

SecureCredentialResult secure_credential_store(
    std::string_view service,
    std::string_view account,
    std::string_view secret)
{
    if (!valid_key_part(service) || !valid_key_part(account) ||
        secret.empty() || secret.size() > maximumCredentialBytes)
    {
        return make_result(
            SecureCredentialStatus::eInvalidArgument,
            "The secure credential key or payload is invalid.");
    }

#if defined(_WIN32)
    const std::string target = target_name(service, account);
    const std::optional<std::wstring> wideTarget = utf8_to_wide(target);
    const std::optional<std::wstring> wideAccount = utf8_to_wide(account);
    if (!wideTarget || !wideAccount)
    {
        return make_result(
            SecureCredentialStatus::eInvalidArgument,
            "The secure credential key is not valid UTF-8.");
    }

    CREDENTIALW credential {};
    credential.Type = CRED_TYPE_GENERIC;
    credential.TargetName = const_cast<wchar_t*>(wideTarget->c_str());
    credential.CredentialBlobSize = static_cast<DWORD>(secret.size());
    credential.CredentialBlob = reinterpret_cast<LPBYTE>(
        const_cast<char*>(secret.data()));
    credential.Persist = CRED_PERSIST_LOCAL_MACHINE;
    credential.UserName = const_cast<wchar_t*>(wideAccount->c_str());
    if (!CredWriteW(&credential, 0u))
    {
        const DWORD error = GetLastError();
        return make_result(
            windows_credential_provider_unavailable(error) ?
                SecureCredentialStatus::eUnavailable :
                SecureCredentialStatus::ePlatformError,
            windows_error(error));
    }
    return make_result(SecureCredentialStatus::eSuccess);
#elif defined(__APPLE__)
    const std::string target = target_name(service, account);
    UInt32 passwordLength = 0u;
    void* passwordData = nullptr;
    SecKeychainItemRef item = nullptr;
    OSStatus status = SecKeychainFindGenericPassword(
        nullptr,
        static_cast<UInt32>(target.size()),
        target.data(),
        static_cast<UInt32>(account.size()),
        account.data(),
        &passwordLength,
        &passwordData,
        &item);
    if (passwordData)
    {
        SecKeychainItemFreeContent(nullptr, passwordData);
    }
    if (status == errSecSuccess && item)
    {
        status = SecKeychainItemModifyAttributesAndData(
            item,
            nullptr,
            static_cast<UInt32>(secret.size()),
            secret.data());
        CFRelease(item);
    }
    else if (status == errSecItemNotFound)
    {
        status = SecKeychainAddGenericPassword(
            nullptr,
            static_cast<UInt32>(target.size()),
            target.data(),
            static_cast<UInt32>(account.size()),
            account.data(),
            static_cast<UInt32>(secret.size()),
            secret.data(),
            nullptr);
    }
    if (status != errSecSuccess)
    {
        return make_result(
            SecureCredentialStatus::ePlatformError,
            "macOS Keychain rejected the credential operation.");
    }
    return make_result(SecureCredentialStatus::eSuccess);
#else
    return make_result(
        SecureCredentialStatus::eUnavailable,
        "No secure credential provider is available on this platform.");
#endif
}

std::optional<std::string> secure_credential_load(
    std::string_view service,
    std::string_view account,
    SecureCredentialResult* result)
{
    auto publish = [result](SecureCredentialResult value) {
        if (result)
        {
            *result = std::move(value);
        }
    };
    if (!valid_key_part(service) || !valid_key_part(account))
    {
        publish(make_result(
            SecureCredentialStatus::eInvalidArgument,
            "The secure credential key is invalid."));
        return std::nullopt;
    }

#if defined(_WIN32)
    const std::optional<std::wstring> wideTarget = utf8_to_wide(
        target_name(service, account));
    if (!wideTarget)
    {
        publish(make_result(
            SecureCredentialStatus::eInvalidArgument,
            "The secure credential key is not valid UTF-8."));
        return std::nullopt;
    }
    PCREDENTIALW credential = nullptr;
    if (!CredReadW(
            wideTarget->c_str(),
            CRED_TYPE_GENERIC,
            0u,
            &credential))
    {
        const DWORD error = GetLastError();
        publish(make_result(
            error == ERROR_NOT_FOUND ?
                SecureCredentialStatus::eNotFound :
                windows_credential_provider_unavailable(error) ?
                    SecureCredentialStatus::eUnavailable :
                    SecureCredentialStatus::ePlatformError,
            error == ERROR_NOT_FOUND ?
                "The credential was not found." : windows_error(error)));
        return std::nullopt;
    }
    if (!credential || !credential->CredentialBlob ||
        credential->CredentialBlobSize == 0u ||
        credential->CredentialBlobSize > maximumCredentialBytes)
    {
        if (credential)
        {
            CredFree(credential);
        }
        publish(make_result(
            SecureCredentialStatus::eInvalidData,
            "The stored credential is empty or invalid."));
        return std::nullopt;
    }
    std::string secret(
        reinterpret_cast<const char*>(credential->CredentialBlob),
        credential->CredentialBlobSize);
    CredFree(credential);
    publish(make_result(SecureCredentialStatus::eSuccess));
    return secret;
#elif defined(__APPLE__)
    const std::string target = target_name(service, account);
    UInt32 passwordLength = 0u;
    void* passwordData = nullptr;
    const OSStatus status = SecKeychainFindGenericPassword(
        nullptr,
        static_cast<UInt32>(target.size()),
        target.data(),
        static_cast<UInt32>(account.size()),
        account.data(),
        &passwordLength,
        &passwordData,
        nullptr);
    if (status != errSecSuccess || !passwordData || passwordLength == 0u)
    {
        if (passwordData)
        {
            SecKeychainItemFreeContent(nullptr, passwordData);
        }
        publish(make_result(
            status == errSecItemNotFound ?
                SecureCredentialStatus::eNotFound :
                SecureCredentialStatus::ePlatformError,
            status == errSecItemNotFound ?
                "The credential was not found." :
                "macOS Keychain rejected the credential operation."));
        return std::nullopt;
    }
    std::string secret(
        static_cast<const char*>(passwordData),
        passwordLength);
    SecKeychainItemFreeContent(nullptr, passwordData);
    publish(make_result(SecureCredentialStatus::eSuccess));
    return secret;
#else
    publish(make_result(
        SecureCredentialStatus::eUnavailable,
        "No secure credential provider is available on this platform."));
    return std::nullopt;
#endif
}

SecureCredentialResult secure_credential_erase(
    std::string_view service,
    std::string_view account)
{
    if (!valid_key_part(service) || !valid_key_part(account))
    {
        return make_result(
            SecureCredentialStatus::eInvalidArgument,
            "The secure credential key is invalid.");
    }
#if defined(_WIN32)
    const std::optional<std::wstring> wideTarget = utf8_to_wide(
        target_name(service, account));
    if (!wideTarget)
    {
        return make_result(
            SecureCredentialStatus::eInvalidArgument,
            "The secure credential key is not valid UTF-8.");
    }
    if (!CredDeleteW(wideTarget->c_str(), CRED_TYPE_GENERIC, 0u))
    {
        const DWORD error = GetLastError();
        if (error != ERROR_NOT_FOUND)
        {
            return make_result(
                windows_credential_provider_unavailable(error) ?
                    SecureCredentialStatus::eUnavailable :
                    SecureCredentialStatus::ePlatformError,
                windows_error(error));
        }
    }
    return make_result(SecureCredentialStatus::eSuccess);
#elif defined(__APPLE__)
    const std::string target = target_name(service, account);
    SecKeychainItemRef item = nullptr;
    const OSStatus findStatus = SecKeychainFindGenericPassword(
        nullptr,
        static_cast<UInt32>(target.size()),
        target.data(),
        static_cast<UInt32>(account.size()),
        account.data(),
        nullptr,
        nullptr,
        &item);
    if (findStatus == errSecItemNotFound)
    {
        return make_result(SecureCredentialStatus::eSuccess);
    }
    if (findStatus != errSecSuccess || !item)
    {
        return make_result(
            SecureCredentialStatus::ePlatformError,
            "macOS Keychain rejected the credential operation.");
    }
    const OSStatus deleteStatus = SecKeychainItemDelete(item);
    CFRelease(item);
    return deleteStatus == errSecSuccess ?
        make_result(SecureCredentialStatus::eSuccess) :
        make_result(
            SecureCredentialStatus::ePlatformError,
            "macOS Keychain rejected the credential operation.");
#else
    return make_result(
        SecureCredentialStatus::eUnavailable,
        "No secure credential provider is available on this platform.");
#endif
}

void secure_clear_string(std::string& value) noexcept
{
    if (!value.empty())
    {
#if defined(_WIN32)
        SecureZeroMemory(value.data(), value.size());
#else
        volatile char* bytes = value.data();
        for (std::size_t index = 0u; index < value.size(); ++index)
        {
            bytes[index] = 0;
        }
#endif
    }
    value.clear();
    value.shrink_to_fit();
}

SecureCredentialResult secure_oauth_credential_store(
    std::string_view service,
    std::string_view account,
    const SecureOAuthCredential& credential)
{
    std::optional<std::string> serialised = serialise_oauth(credential);
    if (!serialised)
    {
        return make_result(
            SecureCredentialStatus::eInvalidArgument,
            "The OAuth credential record is incomplete or too large.");
    }
    SecureCredentialResult result = secure_credential_store(
        service,
        account,
        *serialised);
    secure_clear_string(*serialised);
    return result;
}

std::optional<SecureOAuthCredential> secure_oauth_credential_load(
    std::string_view service,
    std::string_view account,
    SecureCredentialResult* result)
{
    SecureCredentialResult loadResult;
    std::optional<std::string> data = secure_credential_load(
        service,
        account,
        &loadResult);
    if (!data)
    {
        if (result)
        {
            *result = std::move(loadResult);
        }
        return std::nullopt;
    }
    std::optional<SecureOAuthCredential> credential =
        deserialise_oauth(*data);
    secure_clear_string(*data);
    if (!credential)
    {
        loadResult = make_result(
            SecureCredentialStatus::eInvalidData,
            "The stored OAuth credential record is invalid.");
    }
    if (result)
    {
        *result = std::move(loadResult);
    }
    return credential;
}

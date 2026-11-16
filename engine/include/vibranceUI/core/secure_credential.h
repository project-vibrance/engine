// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "vibranceUI/export.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

enum class SecureCredentialStatus
{
    eSuccess,
    eNotFound,
    eUnavailable,
    eInvalidArgument,
    ePlatformError,
    eInvalidData
};

struct SecureCredentialResult
{
    SecureCredentialStatus status = SecureCredentialStatus::eUnavailable;
    std::string error {};

    bool successful() const noexcept
    {
        return status == SecureCredentialStatus::eSuccess;
    }
};




VIBRANCE_ENGINE_API SecureCredentialResult secure_credential_store(
    std::string_view service,
    std::string_view account,
    std::string_view secret);

VIBRANCE_ENGINE_API std::optional<std::string> secure_credential_load(
    std::string_view service,
    std::string_view account,
    SecureCredentialResult* result = nullptr);

VIBRANCE_ENGINE_API SecureCredentialResult secure_credential_erase(
    std::string_view service,
    std::string_view account);



VIBRANCE_ENGINE_API void secure_clear_string(std::string& value) noexcept;




struct SecureOAuthCredential
{
    std::int32_t tokenType = 0;
    std::int64_t expiresAtUnixSeconds = 0;
    std::string accessToken {};
    std::string refreshToken {};
    std::string scopes {};
};

VIBRANCE_ENGINE_API SecureCredentialResult secure_oauth_credential_store(
    std::string_view service,
    std::string_view account,
    const SecureOAuthCredential& credential);

VIBRANCE_ENGINE_API std::optional<SecureOAuthCredential>
secure_oauth_credential_load(
    std::string_view service,
    std::string_view account,
    SecureCredentialResult* result = nullptr);

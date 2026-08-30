#include <vibranceUI/core/secure_credential.h>

#include <chrono>
#include <iostream>
#include <string>

int main()
{
    constexpr std::string_view service =
        "com.vibrance.engine.tests.secure-credential";
    const std::string account = "roundtrip-" + std::to_string(
        std::chrono::steady_clock::now().time_since_epoch().count());

    SecureOAuthCredential expected;
    expected.tokenType = 1;
    expected.expiresAtUnixSeconds = 2000000000;
    expected.accessToken = "test-access-token";
    expected.refreshToken = "test-refresh-token";
    expected.scopes = "identify";

    const SecureCredentialResult stored = secure_oauth_credential_store(
        service,
        account,
        expected);
    if (stored.status == SecureCredentialStatus::eUnavailable)
    {
        std::cout << "Secure credential provider unavailable; fail-closed path passed.\n";
        return 0;
    }
    if (!stored.successful())
    {
        std::cerr << stored.error << '\n';
        return 1;
    }

    SecureCredentialResult loadedResult;
    std::optional<SecureOAuthCredential> loaded =
        secure_oauth_credential_load(service, account, &loadedResult);
    if (!loaded || !loadedResult.successful() ||
        loaded->tokenType != expected.tokenType ||
        loaded->expiresAtUnixSeconds != expected.expiresAtUnixSeconds ||
        loaded->accessToken != expected.accessToken ||
        loaded->refreshToken != expected.refreshToken ||
        loaded->scopes != expected.scopes)
    {
        secure_credential_erase(service, account);
        std::cerr << "Secure OAuth credential roundtrip failed.\n";
        return 2;
    }

    secure_clear_string(loaded->accessToken);
    secure_clear_string(loaded->refreshToken);
    const SecureCredentialResult erased = secure_credential_erase(
        service,
        account);
    SecureCredentialResult missingResult;
    const auto missing = secure_oauth_credential_load(
        service,
        account,
        &missingResult);
    if (!erased.successful() || missing ||
        missingResult.status != SecureCredentialStatus::eNotFound)
    {
        std::cerr << "Secure credential erase failed.\n";
        return 3;
    }
    return 0;
}

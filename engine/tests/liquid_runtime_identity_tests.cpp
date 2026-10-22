#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../src/platform/win32/liquid_glass_runtime_identity.h"
#include <iostream>

int main(int argc, char** argv)
{
    using namespace vibrance::win32;
    unsigned failures = 0;
    const auto check = [&](bool value, const char* message) {
        if (!value) { ++failures; std::cerr << message << '\n'; }
    };
    const std::wstring desired(64, L'a'), active(64, L'b');
    check(liquid_runtime_key_valid(desired) && liquid_runtime_key_valid(std::wstring(64, L'0')),
        "Lowercase SHA-256 fingerprints must be accepted");
    check(!liquid_runtime_key_valid(L"short") && !liquid_runtime_key_valid(std::wstring(64, L'z')) &&
        !liquid_runtime_key_valid(std::wstring(64, L'A')),
        "Malformed fingerprints must not be treated as runtime identities");
    LiquidRuntimeStatus report { 123, desired, active, L"staged/extension.dll", L"active/extension.dll" };
    check(liquid_runtime_restart_required(report, 123, desired),
        "A verified staged replacement must identify the older pinned runtime");
    check(!liquid_runtime_restart_required(report, 124, desired),
        "An earlier compositor's report must not request sign-out");
    check(!liquid_runtime_restart_required(report, 0, desired),
        "A missing compositor is not a stale runtime");
    check(!liquid_runtime_restart_required(report, 123, active),
        "Another app generation's report must not request sign-out");
    report.activeKey = desired;
    check(!liquid_runtime_restart_required(report, 123, desired),
        "A matching loaded generation must not request sign-out");
    report.activeKey.clear();
    check(!liquid_runtime_restart_required(report, 123, desired),
        "Unavailable active identity is not proof of an outdated runtime");
    report.activeKey = L"short";
    check(!liquid_runtime_restart_required(report, 123, desired),
        "A short active fingerprint must not request sign-out");
    report.activeKey = std::wstring(64, L'z');
    check(!liquid_runtime_restart_required(report, 123, desired),
        "A non-hexadecimal active fingerprint must not request sign-out");
    report.activeKey = active;
    report.stagedExtension.clear();
    check(!liquid_runtime_restart_required(report, 123, desired),
        "An unstaged update must not request sign-out");
    if (argc > 1)
    {
        const std::filesystem::path directory(argv[1]);
        std::filesystem::create_directories(directory);
        for (const auto* name : liquidRuntimeNames)
        { std::ofstream file(directory / name, std::ios::binary); file << "original"; }
        const auto originalStamp = liquid_runtime_bundle_stamp(directory);
        check(originalStamp.complete && originalStamp == liquid_runtime_bundle_stamp(directory),
            "An unchanged installed bundle must retain its cached fingerprint");
        { std::ofstream file(directory / liquidRuntimeNames[1], std::ios::binary); file << "replacement shader"; }
        check(liquid_runtime_bundle_stamp(directory) != originalStamp,
            "An in-place shader update must invalidate the running app's cached runtime identity");
        report.stagedExtension = L"staged/extension.dll";
        check(write_liquid_runtime_status(directory, report), "Writing a complete runtime report failed");
        const auto restored = read_liquid_runtime_status(directory);
        check(restored.compositorPid == report.compositorPid && restored.requestedKey == desired &&
            restored.activeKey == active && restored.stagedExtension == report.stagedExtension &&
            restored.activeExtension == report.activeExtension, "Runtime status did not round-trip");
        { std::wofstream stream(directory / L"runtime-status.txt"); stream << L"invalid\n"; }
        check(!liquid_runtime_restart_required(read_liquid_runtime_status(directory), 123, desired),
            "A malformed status report must not request sign-out");
    }
    return failures ? 1 : 0;
}

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "../src/platform/win32/media_process_routing.h"

#include <algorithm>
#include <array>
#include <cwctype>
#include <iostream>
#include <string_view>

namespace
{
bool expect(bool condition, std::string_view message)
{
    if (!condition) std::cerr << message << '\n';
    return condition;
}

bool equal_identity(std::wstring_view left, std::wstring_view right)
{
    return left.size() == right.size() && std::equal(left.begin(), left.end(),
        right.begin(), [](wchar_t a, wchar_t b) { return std::towlower(a) == std::towlower(b); });
}
}

int main()
{
    using namespace vibrance::win32;
    bool passed = true;
    const auto matches = [](std::wstring_view source, std::wstring_view registered,
        std::wstring_view app, std::wstring_view executable, std::wstring_view package) {
        return media_process_identity_matches(source, registered, app, executable,
            package, equal_identity);
    };
    passed &= expect(matches(L"Example.Player_publisher!Player", {},
        L"Example.Player_publisher!AudioService", L"C:\\Apps\\UnexpectedName.exe",
        L"Example.Player_publisher"),
        "A separately launched rendering helper must match its source package");
    passed &= expect(!matches(L"Example.Player_publisher!Player", {},
        L"Other.Player_publisher!AudioService", L"C:\\Apps\\UnexpectedName.exe",
        L"Other.Player_publisher"),
        "An unrelated package must not match because it also has a rendering helper");
    passed &= expect(!matches(L"Example.Player_publisher!Player", {}, {},
        L"C:\\Other\\Player.exe", L"Example.Player_otherpublisher"),
        "Matching package names with different publishers must remain separate");
    passed &= expect(matches(L"Desktop.Player", L"C:\\Apps\\Player.exe", {},
        L"c:\\apps\\player.EXE", {}),
        "Registered desktop identities must resolve their executable");
    passed &= expect(matches(L"Player.exe", {}, {}, L"C:\\Apps\\Player.exe", {}),
        "Desktop executable sources must remain supported");
    passed &= expect(matches(L"C:\\Apps\\Player.exe", {}, {},
        L"C:\\Apps\\Player.exe", {}), "Executable paths must remain supported");
    passed &= expect(media_process_identity_matches(L"Player.exe", {},
        L"Example.Player_publisher!Worker", L"C:\\Apps\\RenderService.exe",
        L"Example.Player_publisher", equal_identity, L"Example.Player_publisher"),
        "Executable sources must recognise helpers using the metadata owner's package identity");
    passed &= expect(!matches({}, {}, {}, {}, {}), "An empty source must not match");
    passed &= expect(!matches(L"Unresolved.Player", {}, {}, {}, {}),
        "Unavailable process identity must remain unresolved");

    struct Process
    {
        std::uint32_t id, parent;
        bool source;
    };
    const std::array<Process, 6> processes {{
        { 10u, 1u, true }, { 11u, 10u, false }, { 12u, 11u, false },
        { 20u, 1u, false }, { 30u, 31u, false }, { 31u, 30u, false }
    }};
    const auto find = [&](std::uint32_t id) {
        return std::find_if(processes.begin(), processes.end(),
            [id](const Process& process) { return process.id == id; });
    };
    const auto belongs = [&](std::uint32_t id) {
        return media_audio_process_matches_source(id, [&](std::uint32_t candidate) {
            const auto process = find(candidate);
            return process != processes.end() && process->source;
        }, [&](std::uint32_t candidate) {
            const auto process = find(candidate);
            return process == processes.end() ? 0u : process->parent;
        });
    };
    passed &= expect(belongs(12u), "Nested desktop rendering children must match the app");
    passed &= expect(!belongs(20u), "Sibling apps sharing a launcher must remain separate");
    passed &= expect(!belongs(30u), "Cyclic process ancestry must terminate");
    passed &= expect(!belongs(0u), "System audio must not become a target");

    MediaAudioProcessSelection playing;
    playing.consider(10u, false, 10u);
    playing.consider(12u, true, 10u);
    passed &= expect(playing.process_id() == 12u,
        "An active renderer must outrank the inactive metadata process");
    MediaAudioProcessSelection paused;
    paused.consider(10u, false, 12u);
    paused.consider(12u, false, 12u);
    passed &= expect(paused.process_id() == 12u,
        "Pausing must retain the previous renderer while its session remains available");
    MediaAudioProcessSelection resumed;
    resumed.consider(12u, false, 12u);
    resumed.consider(13u, true, 12u);
    passed &= expect(resumed.process_id() == 13u,
        "An app moving playback to another helper must retarget capture");
    MediaAudioProcessSelection multiple;
    multiple.consider(13u, true, 12u);
    multiple.consider(12u, true, 12u);
    multiple.consider(14u, true, 12u);
    passed &= expect(multiple.process_id() == 12u,
        "Enumeration order must not replace an existing active renderer");
    MediaAudioProcessSelection unresolved;
    unresolved.consider(0u, true);
    passed &= expect(unresolved.process_id() == 0u,
        "Missing render sessions must not broaden capture to a process root or system mix");
    return passed ? 0 : 1;
}

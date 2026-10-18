#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <winrt/base.h>
#include "../src/platform/win32/playback_quality_probe.h"
#include <atomic>
#include <iostream>

int main()
{
    WNDCLASSW windowClass {};
    windowClass.lpfnWndProc = DefWindowProcW;
    windowClass.hInstance = GetModuleHandleW(nullptr);
    windowClass.lpszClassName = L"VibrancePlaybackQualityFixture";
    if (!RegisterClassW(&windowClass)) return 1;
    const HWND window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        windowClass.lpszClassName, L"Vibrance playback quality test", WS_OVERLAPPED | WS_CAPTION,
        120, 120, 360, 180, nullptr, nullptr, windowClass.hInstance, nullptr);
    if (!window) return 1;
    const HWND playback = CreateWindowW(L"BUTTON", L"Playback", WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
        10, 10, 320, 120, window, nullptr, windowClass.hInstance, nullptr);
    const auto label = [&](const wchar_t* text, int y) {
        return CreateWindowW(L"STATIC", text, WS_CHILD | WS_VISIBLE,
            15, y, 160, 20, playback, nullptr, windowClass.hInstance, nullptr);
    };
    label(L"Current track", 20);
    label(L"Current artist", 42);
    const HWND lossless = label(L"Lossless", 64);
    CreateWindowW(L"BUTTON", L"Pause", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        210, 30, 80, 30, playback, nullptr, windowClass.hInstance, nullptr);
    ShowWindow(window, SW_SHOWNOACTIVATE);
    UpdateWindow(window);
    int failures = 0;
    {
        std::atomic<unsigned> ineligibleWindowQueries { 0u };
        Win32PlaybackQualityProbe probe([&](const auto& metadata) {
            if (media_quality_detection::normalise(metadata.title).empty() ||
                media_quality_detection::normalise(metadata.artist).empty())
                ++ineligibleWindowQueries;
            return window;
        });
        Win32PlaybackQualityProbe::Request request { GetCurrentProcessId(),
            L"Fixture", L"Current track", L"Current artist", true };
        const auto waitFor = [&](MediaPlaybackQuality expected, const char* message) {
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(6);
            while (std::chrono::steady_clock::now() < deadline)
            {
                MSG event;
                while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&event);
                    DispatchMessageW(&event);
                }
                if (probe.refresh(request) == expected) return;
                Sleep(10);
            }
            std::cerr << message << '\n';
            ++failures;
        };
        waitFor(MediaPlaybackQuality::eLossless,
            "The real UI Automation cache must detect a matching playback badge");
        const auto expectIdentitySkipped = [&](const wchar_t* title, const wchar_t* artist) {
            request.title = title;
            request.artist = artist;
            if (probe.refresh(request) != MediaPlaybackQuality::eUnknown) ++failures;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(1200);
            const auto before = ineligibleWindowQueries.load();
            while (std::chrono::steady_clock::now() < deadline)
            {
                MSG event;
                while (PeekMessageW(&event, nullptr, 0, 0, PM_REMOVE))
                {
                    TranslateMessage(&event);
                    DispatchMessageW(&event);
                }
                if (probe.refresh(request) != MediaPlaybackQuality::eUnknown) ++failures;
                Sleep(10);
            }
            if (ineligibleWindowQueries.load() != before)
            {
                std::cerr << "Empty normalised metadata must not resolve a window or request a UIA snapshot\n";
                ++failures;
            }
        };
        expectIdentitySkipped(L"", L"Current artist");
        expectIdentitySkipped(L"Current track", L" \t\r\n");
        request.title = L"Current track";
        request.artist = L"Current artist";
        waitFor(MediaPlaybackQuality::eLossless,
            "Restoring eligible metadata must promptly resume native quality detection");
        ShowWindow(playback, SW_HIDE);
        waitFor(MediaPlaybackQuality::eUnknown,
            "An offscreen ancestor must exclude all of its cached playback evidence");
        ShowWindow(playback, SW_SHOWNA);
        waitFor(MediaPlaybackQuality::eLossless,
            "Restoring a visible playback subtree must resume native quality detection");
        const HWND atmos = label(L"Dolby Atmos", 86);
        waitFor(MediaPlaybackQuality::eDolbyAtmos,
            "The native probe must prefer Atmos when both badges are exposed");
        ShowWindow(atmos, SW_HIDE);
        waitFor(MediaPlaybackQuality::eLossless,
            "The native probe must discard hidden Atmos evidence");
        request.title = L"Next track";
        if (probe.refresh(request) != MediaPlaybackQuality::eUnknown)
        {
            std::cerr << "A track change must clear the old badge synchronously\n";
            ++failures;
        }
        request.title = L"Current track";
        waitFor(MediaPlaybackQuality::eLossless, "Returning to matching metadata must refresh the badge");
        request.album = L"Another release";
        if (probe.refresh(request) != MediaPlaybackQuality::eUnknown)
        {
            std::cerr << "Changing the release must clear the previous quality evidence\n";
            ++failures;
        }
        request.durationMilliseconds = 180000;
        if (probe.refresh(request) != MediaPlaybackQuality::eUnknown) ++failures;
        waitFor(MediaPlaybackQuality::eLossless, "Updated release metadata must get fresh playback evidence");
        DestroyWindow(lossless);
        waitFor(MediaPlaybackQuality::eUnknown, "Removing a playback badge must clear the native result");
        request.active = false;
        if (probe.refresh(request) != MediaPlaybackQuality::eUnknown) ++failures;
    }
    DestroyWindow(window);
    UnregisterClassW(windowClass.lpszClassName, windowClass.hInstance);
    return failures ? 1 : 0;
}

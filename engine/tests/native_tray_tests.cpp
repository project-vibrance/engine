#include <vibranceUI/platform/native_tray.h>
#if defined(_WIN32)
#include <windows.h>
#include <iostream>
#include <string_view>

int main()
{
    NativeTrayOptions options;
    options.registerWithShell = false;
    options.menu = { { 90000, "First" }, { 27, "Disabled", false },
        { 0, {}, false, true }, { 42, "Last" } };
    NativeTray tray(options);
    if (!tray.initialise()) return 1;
    HWND window = nullptr;
    EnumWindows([](HWND candidate, LPARAM parameter) -> BOOL {
        DWORD process = 0;
        GetWindowThreadProcessId(candidate, &process);
        wchar_t name[64] {};
        if (process == GetCurrentProcessId() && GetClassNameW(candidate, name, 64) &&
            std::wstring_view(name) == L"VibranceNativeTrayWindow")
        {
            *reinterpret_cast<HWND*>(parameter) = candidate;
            return FALSE;
        }
        return TRUE;
    }, reinterpret_cast<LPARAM>(&window));
    if (!window) return 1;
    // Native menu indexes map back to arbitrary application command IDs.
    SendMessageW(window, WM_COMMAND, 1, 0);
    const auto first = tray.take_event();
    if (first.kind != NativeTrayEventKind::eCommand || first.command != 90000) return 1;
    for (const WPARAM index : { 0, 2, 3, 5 }) SendMessageW(window, WM_COMMAND, index, 0);
    if (tray.take_event().kind != NativeTrayEventKind::eNone) return 1;
    SendMessageW(window, WM_COMMAND, 4, 0);
    const auto last = tray.take_event();
    if (last.kind != NativeTrayEventKind::eCommand || last.command != 42) return 1;
    tray.shutdown();
    if (tray.available() || tray.take_event().kind != NativeTrayEventKind::eNone) return 1;
    return tray.initialise() ? 0 : 1;
}
#endif

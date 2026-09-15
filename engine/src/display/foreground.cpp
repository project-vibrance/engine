#include <vibranceUI/display/foreground.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <string_view>
#endif

bool foreground_application_is_fullscreen_on_monitor(
    const DisplayMonitorTarget& monitor)
{
#if defined(_WIN32)
    const HWND foreground = GetForegroundWindow();
    if (!foreground || !IsWindowVisible(foreground) ||
        IsIconic(foreground))
    {
        return false;
    }

    DWORD processId = 0u;
    GetWindowThreadProcessId(foreground, &processId);
    if (processId == 0u || processId == GetCurrentProcessId())
    {
        return false;
    }

    std::array<wchar_t, 64u> className {};
    const int classNameLength = GetClassNameW(
        foreground,
        className.data(),
        static_cast<int>(className.size()));
    const std::wstring_view windowClass(
        className.data(),
        classNameLength > 0 ? static_cast<std::size_t>(classNameLength) : 0u);
    if (windowClass == L"Progman" || windowClass == L"WorkerW" ||
        windowClass == L"Shell_TrayWnd" ||
        windowClass == L"Shell_SecondaryTrayWnd")
    {
        return false;
    }

    RECT windowBounds {};
    if (!GetWindowRect(foreground, &windowBounds))
    {
        return false;
    }
    const HMONITOR foregroundMonitor = MonitorFromWindow(
        foreground,
        MONITOR_DEFAULTTONULL);
    MONITORINFO monitorInfo {};
    monitorInfo.cbSize = sizeof(monitorInfo);
    if (!foregroundMonitor ||
        !GetMonitorInfoW(foregroundMonitor, &monitorInfo))
    {
        return false;
    }

    // A game can change resolution before the display tracker refreshes.
    // Identify the monitor from its origin, not its cached width and height.
    const POINT monitorPoint { monitor.position.x, monitor.position.y };
    const HMONITOR selectedMonitor = MonitorFromPoint(
        monitorPoint, MONITOR_DEFAULTTONULL);
    const RECT& actualMonitorBounds = monitorInfo.rcMonitor;
    if (selectedMonitor != foregroundMonitor)
    {
        return false;
    }

    // A small tolerance accommodates borderless windows whose frame differs by
    // one physical pixel because of DPI rounding.
    constexpr LONG fullscreenTolerance = 2;
    return windowBounds.left <= actualMonitorBounds.left + fullscreenTolerance &&
        windowBounds.top <= actualMonitorBounds.top + fullscreenTolerance &&
        windowBounds.right >= actualMonitorBounds.right - fullscreenTolerance &&
        windowBounds.bottom >= actualMonitorBounds.bottom - fullscreenTolerance;
#else
    (void)monitor;
    return false;
#endif
}


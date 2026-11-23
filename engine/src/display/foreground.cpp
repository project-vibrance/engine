// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/display/foreground.h>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <string_view>
#elif defined(VIBRANCE_HAS_XCB)
#include <GLFW/glfw3.h>
#include <xcb/xcb.h>
#include <unistd.h>
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <span>

namespace
{
template<typename T>
using XReply = std::unique_ptr<T, decltype(&std::free)>;

struct X11Foreground
{
    int screenIndex = 0;
    std::unique_ptr<xcb_connection_t, decltype(&xcb_disconnect)> connection {
        xcb_connect(nullptr, &screenIndex), xcb_disconnect};
    xcb_window_t root = XCB_WINDOW_NONE;
    xcb_atom_t active = XCB_ATOM_NONE;
    xcb_atom_t state = XCB_ATOM_NONE;
    xcb_atom_t fullscreen = XCB_ATOM_NONE;
    xcb_atom_t pid = XCB_ATOM_NONE;

    X11Foreground()
    {
        if (!connection || xcb_connection_has_error(connection.get())) return;
        auto screen = xcb_setup_roots_iterator(xcb_get_setup(connection.get()));
        for (int index = 0; screen.rem && index < screenIndex; ++index) xcb_screen_next(&screen);
        if (!screen.rem) return;
        root = screen.data->root;
        active = atom("_NET_ACTIVE_WINDOW");
        state = atom("_NET_WM_STATE");
        fullscreen = atom("_NET_WM_STATE_FULLSCREEN");
        pid = atom("_NET_WM_PID");
    }

    xcb_atom_t atom(const char* name) const
    {
        const auto request = xcb_intern_atom(connection.get(), true, std::strlen(name), name);
        XReply<xcb_intern_atom_reply_t> reply(xcb_intern_atom_reply(connection.get(), request, nullptr), std::free);
        return reply ? reply->atom : xcb_atom_t(XCB_ATOM_NONE);
    }

    XReply<xcb_get_property_reply_t> property(xcb_window_t window, xcb_atom_t key, xcb_atom_t type,
        std::uint32_t maximum) const
    {
        const auto request = xcb_get_property(connection.get(), false, window, key, type, 0, maximum);
        return {xcb_get_property_reply(connection.get(), request, nullptr), std::free};
    }
};

std::span<const std::uint32_t> property_values(const XReply<xcb_get_property_reply_t>& reply)
{
    if (!reply || reply->format != 32) return {};
    return {static_cast<const std::uint32_t*>(xcb_get_property_value(reply.get())), reply->value_len};
}
}
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



    const POINT monitorPoint { monitor.position.x, monitor.position.y };
    const HMONITOR selectedMonitor = MonitorFromPoint(
        monitorPoint, MONITOR_DEFAULTTONULL);
    const RECT& actualMonitorBounds = monitorInfo.rcMonitor;
    if (selectedMonitor != foregroundMonitor)
    {
        return false;
    }



    constexpr LONG fullscreenTolerance = 2;
    return windowBounds.left <= actualMonitorBounds.left + fullscreenTolerance &&
        windowBounds.top <= actualMonitorBounds.top + fullscreenTolerance &&
        windowBounds.right >= actualMonitorBounds.right - fullscreenTolerance &&
        windowBounds.bottom >= actualMonitorBounds.bottom - fullscreenTolerance;
#elif defined(VIBRANCE_HAS_XCB)
#if defined(GLFW_PLATFORM_X11)
    if (glfwGetPlatform() != GLFW_PLATFORM_X11) return false;
#endif
    static thread_local X11Foreground x11;
    if (!x11.root || !x11.active || !x11.state || !x11.fullscreen) return false;
    const auto activeReply = x11.property(x11.root, x11.active, XCB_ATOM_WINDOW, 1);
    const auto active = property_values(activeReply);
    if (active.empty() || !active.front()) return false;
    const auto window = active.front();
    const auto pidReply = x11.property(window, x11.pid, XCB_ATOM_CARDINAL, 1);
    const auto pid = property_values(pidReply);
    if (!pid.empty() && pid.front() == static_cast<std::uint32_t>(getpid())) return false;
    const auto stateReply = x11.property(window, x11.state, XCB_ATOM_ATOM, 128);
    const auto states = property_values(stateReply);
    if (std::find(states.begin(), states.end(), x11.fullscreen) == states.end()) return false;

    auto* connection = x11.connection.get();
    const auto geometryRequest = xcb_get_geometry(connection, window);
    const auto positionRequest = xcb_translate_coordinates(connection, window, x11.root, 0, 0);
    const XReply<xcb_get_geometry_reply_t> geometry(xcb_get_geometry_reply(connection, geometryRequest, nullptr), std::free);
    const XReply<xcb_translate_coordinates_reply_t> position(
        xcb_translate_coordinates_reply(connection, positionRequest, nullptr), std::free);
    constexpr int tolerance = 2;
    return geometry && position && position->same_screen &&
        position->dst_x <= monitor.position.x + tolerance &&
        position->dst_y <= monitor.position.y + tolerance &&
        position->dst_x + geometry->width >= monitor.position.x + monitor.size.x - tolerance &&
        position->dst_y + geometry->height >= monitor.position.y + monitor.size.y - tolerance;
#else
    (void)monitor;
    return false;
#endif
}


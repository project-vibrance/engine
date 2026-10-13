#pragma once

#include <windows.h>
#include <wtsapi32.h>
#include "liquid_glass_dwm_windows10_lifecycle_policy.h"

namespace vibrance::win32
{
inline LiquidDwmWindows10Session liquid_windows10_session() noexcept
{
    using Session = LiquidDwmWindows10Session;
    DWORD session = 0u;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return Session::unknown;
    const auto fallback = [&] {
        if (WTSGetActiveConsoleSessionId() == session) return Session::console;
        return GetSystemMetrics(SM_REMOTESESSION) ? Session::remote : Session::unknown;
    };
    LPWSTR buffer = nullptr;
    DWORD bytes = 0u;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session,
            WTSConnectState, &buffer, &bytes))
        // The monitor separately honours disconnect/lock notifications when
        // this virtual account lacks permission to query WTS metadata.
        return GetLastError() == ERROR_ACCESS_DENIED ? fallback() : Session::unknown;
    const bool active = buffer && bytes >= sizeof(WTS_CONNECTSTATE_CLASS) &&
        *reinterpret_cast<const WTS_CONNECTSTATE_CLASS*>(buffer) == WTSActive;
    WTSFreeMemory(buffer);
    if (!active) return Session::inactive;
    buffer = nullptr;
    if (!WTSQuerySessionInformationW(WTS_CURRENT_SERVER_HANDLE, session,
            WTSClientProtocolType, &buffer, &bytes))
        return GetLastError() == ERROR_ACCESS_DENIED ? fallback() : Session::unknown;
    const USHORT protocol = buffer && bytes >= sizeof(USHORT) ?
        *reinterpret_cast<const USHORT*>(buffer) : 0xffffu;
    WTSFreeMemory(buffer);
    if (protocol == 2u) return Session::remote;
    if (protocol == 0u && WTSGetActiveConsoleSessionId() == session) return Session::console;
    return Session::unknown;
}
}

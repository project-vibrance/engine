// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <windows.h>
#include <wtsapi32.h>
#include <span>

namespace vibrance::win32
{
inline DWORD liquid_session_compositor(std::span<const WTS_PROCESS_INFOW> processes,
    DWORD session) noexcept
{
    DWORD compositor = 0;
    for (const auto& process : processes)
    {
        if (process.SessionId != session || !process.pProcessName ||
            _wcsicmp(process.pProcessName, L"dwm.exe") != 0 || !process.ProcessId) continue;
        if (compositor && compositor != process.ProcessId) return 0;
        compositor = process.ProcessId;
    }
    return compositor;
}

inline DWORD liquid_session_compositor() noexcept
{
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return 0;
    PWTS_PROCESS_INFOW processes = nullptr;
    DWORD count = 0;


    if (!WTSEnumerateProcessesW(WTS_CURRENT_SERVER_HANDLE, 0, 1, &processes, &count)) return 0;
    const auto compositor = liquid_session_compositor({ processes, count }, session);
    WTSFreeMemory(processes);
    return compositor;
}
}

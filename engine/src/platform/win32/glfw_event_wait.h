// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cmath>
#include <limits>

#ifndef CREATE_WAITABLE_TIMER_HIGH_RESOLUTION
#define CREATE_WAITABLE_TIMER_HIGH_RESOLUTION 0x00000002
#endif

namespace vibrance::win32
{
class GlfwEventWaitTimer
{
public:
    GlfwEventWaitTimer() : timer_(CreateWaitableTimerExW(nullptr, nullptr,
        CREATE_WAITABLE_TIMER_HIGH_RESOLUTION, TIMER_MODIFY_STATE | SYNCHRONIZE))
    {
        if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }



    explicit GlfwEventWaitTimer(HANDLE timer) : timer_(timer) {}
    ~GlfwEventWaitTimer()
    {
        if (timer_) CloseHandle(timer_);
    }
    GlfwEventWaitTimer(const GlfwEventWaitTimer&) = delete;
    GlfwEventWaitTimer& operator=(const GlfwEventWaitTimer&) = delete;

    bool wait(double seconds)
    {
        if (!timer_) return false;
        LARGE_INTEGER due {};
        const long double ticks = std::ceil(static_cast<long double>(seconds) * 10000000.0L);
        due.QuadPart = -static_cast<LONGLONG>(std::clamp(ticks, 1.0L,
            static_cast<long double>(std::numeric_limits<LONGLONG>::max() / 2)));
        if (!SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE)) return false;
        const DWORD result = MsgWaitForMultipleObjectsEx(1u, &timer_, INFINITE,
            QS_ALLINPUT, MWMO_INPUTAVAILABLE);


        CancelWaitableTimer(timer_);
        return result != WAIT_FAILED;
    }

private:
    HANDLE timer_ = nullptr;
};

inline DWORD glfw_fallback_wait_milliseconds(double seconds)
{
    const double maximum = static_cast<double>(INFINITE - 1u);
    return static_cast<DWORD>(std::clamp(std::ceil(seconds * 1000.0), 1.0, maximum));
}

inline void wait_for_glfw_events(GlfwEventWaitTimer& timer, double seconds)
{
    if (!timer.wait(seconds))
    {



        MsgWaitForMultipleObjectsEx(0u, nullptr,
            glfw_fallback_wait_milliseconds(seconds), QS_ALLINPUT, MWMO_INPUTAVAILABLE);
    }
}
}
#endif

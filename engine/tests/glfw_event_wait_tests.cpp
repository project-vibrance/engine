#include <vibranceUI/glfw/window.h>
#include <vibranceUI/core/logger.h>
#include "../src/platform/win32/glfw_event_wait.h"

#include <chrono>
#include <iostream>
#include <limits>
#include <thread>

namespace
{
constexpr UINT testMessage = WM_APP + 117u;
WNDPROC originalProcedure = nullptr;
unsigned dispatched = 0u;
LRESULT CALLBACK test_window_procedure(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
{
    if (message == testMessage) { ++dispatched; return 0; }
    return CallWindowProcW(originalProcedure, window, message, wParam, lParam);
}
}

int main()
{
    using namespace vibrance::win32;
    using Clock = std::chrono::steady_clock;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    LoggerOutputOptions logs;
    logs.directory = std::filesystem::current_path() / "event-wait-logs";
    logs.filePrefix = "event_wait_test";
    Logger::configure_output(std::move(logs));
    GlfwWindowCreateInfo options;
    options.width = 64; options.height = 64;
    options.name = "Vibrance event wait test";
    options.focusOnShow = false;
    options.showInTaskbar = false;
    options.showInAltTab = false;
    auto* window = build_glfw_window(options);
    if (!window) return 77;
    set_glfw_window_visible(window, false);
    HWND native = static_cast<HWND>(glfw_native_window_handle(window));
    originalProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(native, GWLP_WNDPROC,
        reinterpret_cast<LONG_PTR>(&test_window_procedure)));
    poll_glfw_events();

    // Poll-only calls must dispatch already queued input without sleeping.
    for (const double timeout : { 0.0, -1.0, std::numeric_limits<double>::quiet_NaN() })
    {
        const auto before = dispatched;
        PostMessageW(native, testMessage, 0u, 0u);
        wait_glfw_events(timeout);
        check(dispatched == before + 1u, "Poll-only waits must retain GLFW message dispatch");
    }

    // A queue query marks existing input as seen. INPUTAVAILABLE must still
    // wake for it, instead of ignoring the message until the timer expires.
    PostMessageW(native, testMessage, 0u, 0u);
    MSG queued {};
    PeekMessageW(&queued, native, testMessage, testMessage, PM_NOREMOVE);
    auto start = Clock::now();
    const auto beforeQueued = dispatched;
    wait_glfw_events(1.0);
    check(dispatched == beforeQueued + 1u && Clock::now() - start < std::chrono::milliseconds(800),
        "Already seen queued input must interrupt a positive wait and be dispatched");

    const auto beforePosted = dispatched;
    std::thread poster([native] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        PostMessageW(native, testMessage, 0u, 0u);
    });
    start = Clock::now();
    while (dispatched == beforePosted && Clock::now() - start < std::chrono::seconds(2))
        wait_glfw_events(1.0);
    poster.join();
    check(dispatched == beforePosted + 1u && Clock::now() - start < std::chrono::milliseconds(800),
        "New input must interrupt a positive wait without waiting for its deadline");

    // Emulate fractional frame remainders. GLFW's millisecond truncation used
    // to issue thousands of immediate polls here. Bound wake-ups, without a
    // CPU-percentage assertion or an assumption about timer overshoot.
    unsigned calls = 0u;
    start = Clock::now();
    const auto duration = std::chrono::milliseconds(60);
    while (Clock::now() - start < duration && calls <= 1000u)
    {
        wait_glfw_events(0.00025);
        ++calls;
    }
    check(calls <= 1000u, "Positive fractional waits must not poll repeatedly before their deadline");
    std::cout << "Fractional waits in 60 ms: " << calls << '\n';

    check(glfw_fallback_wait_milliseconds(0.000001) == 1u &&
        glfw_fallback_wait_milliseconds(0.00101) == 2u &&
        glfw_fallback_wait_milliseconds(std::numeric_limits<double>::max()) == INFINITE - 1u,
        "Timer failure must use bounded millisecond waits rounded upwards");
    for (const bool regularTimer : { false, true })
    {
        GlfwEventWaitTimer timer(regularTimer ? CreateWaitableTimerW(nullptr, FALSE, nullptr) : nullptr);
        calls = 0u;
        start = Clock::now();
        while (Clock::now() - start < duration && calls <= 1000u)
        {
            wait_for_glfw_events(timer, 0.00025);
            poll_glfw_events();
            ++calls;
        }
        check(calls <= 1000u, "Regular or unavailable timers must not reintroduce fractional polling");
    }
    DWORD beforeHandles = 0u, afterHandles = 0u;
    GetProcessHandleCount(GetCurrentProcess(), &beforeHandles);
    for (unsigned index = 0u; index < 32u; ++index) { GlfwEventWaitTimer timer; }
    GetProcessHandleCount(GetCurrentProcess(), &afterHandles);
    check(afterHandles <= beforeHandles, "Per-thread event wait timers must release their kernel handles");

    SetWindowLongPtrW(native, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(originalProcedure));
    destroy_glfw_window(window);
    terminate_glfw();
    return passed ? 0 : 1;
}

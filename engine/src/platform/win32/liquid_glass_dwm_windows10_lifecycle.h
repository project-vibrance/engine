#pragma once

// Included inside the extension's anonymous namespace. This observer uses the
// existing Windows 10 worker thread and owns no display or rendering device.
bool retire_windows10_gpu_resources() noexcept
{
    std::unique_lock<std::mutex> lock(rendererMutex, std::try_to_lock);
    if (!lock.owns_lock()) return false;
    reset_renderers();
    displayBackgrounds = {};
    captureBackgrounds = {};
    ddaBackgrounds = {};
    reset_ui();
    reset_cpu_ui();
    // Tags contain CPU identity snapshots, not retained GPU resources. DWM
    // can reuse their native entries after a display or session transition;
    // keep them subject to the normal identity and live-ownership checks.
    status(236);
    return true;
}

struct Windows10LifecycleMonitor
{
    LiquidDwmWindows10RenderGate gate;
    LiquidDwmWindows10SessionPolicy sessionPolicy;
    ComPtr<IDXGIFactory1> factory;
    HWND window = nullptr;
    DWORD sessionId = 0xffffffffu;
    std::atomic<std::uint64_t> retiredGeneration { 0u };
    LiquidDwmWindows10Session previousSession = LiquidDwmWindows10Session::unknown;
    bool sessionKnown = false, wasReady = false;

    void suspend() noexcept
    {
        gate.invalidate();
        if (readyEvent) ResetEvent(readyEvent);
        status(234);
    }

    static LRESULT CALLBACK window_proc(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
    {
        auto* monitor = reinterpret_cast<Windows10LifecycleMonitor*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            monitor = static_cast<Windows10LifecycleMonitor*>(reinterpret_cast<CREATESTRUCTW*>(lparam)->lpCreateParams);
            SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(monitor));
        }
        if (monitor && message == WM_WTSSESSION_CHANGE && static_cast<DWORD>(lparam) == monitor->sessionId)
        {
            monitor->sessionPolicy.notify(static_cast<std::uint32_t>(wparam));
            monitor->suspend();
        }
        if (monitor && (message == WM_DISPLAYCHANGE ||
                message == WM_DEVICECHANGE || message == WM_POWERBROADCAST)) monitor->suspend();
        if (monitor && message == WM_TIMER) { monitor->sample(); return 0; }
        return DefWindowProcW(hwnd, message, wparam, lparam);
    }

    void stop() noexcept
    {
        suspend();
        if (window)
        {
            KillTimer(window, 1u);
            WTSUnRegisterSessionNotification(window);
            DestroyWindow(window);
            window = nullptr;
        }
        factory.Reset();
    }

    bool initialise() noexcept
    {
        if (!ProcessIdToSessionId(GetCurrentProcessId(), &sessionId))
        { detail(237, GetLastError()); return false; }
        constexpr wchar_t className[] = L"VibranceWindows10GlassLifecycle";
        WNDCLASSEXW type { sizeof(type) };
        type.lpfnWndProc = &window_proc;
        type.hInstance = extensionModule;
        type.lpszClassName = className;
        if (!RegisterClassExW(&type) && GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        { detail(237, GetLastError()); return false; }
        // A hidden top-level window receives display broadcasts; a message-only
        // window would miss them. It never creates a visible surface.
        window = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, className, L"", WS_POPUP,
            0, 0, 0, 0, nullptr, nullptr, extensionModule, this);
        if (!window) { detail(237, GetLastError()); return false; }
        const bool registered = WTSRegisterSessionNotification(window, NOTIFY_FOR_ALL_SESSIONS) ||
            WTSRegisterSessionNotification(window, NOTIFY_FOR_THIS_SESSION);
        if (!registered || !SetTimer(window, 1u, 250u, nullptr))
        { detail(237, GetLastError()); stop(); return false; }
        const HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
        if (FAILED(result)) { detail(237, result); stop(); return false; }
        sample();
        return true;
    }

    void sample() noexcept
    {
        auto sampledGeneration = gate.generation();
        const auto session = liquid_windows10_session();
        if (!sessionKnown || session != previousSession)
        {
            suspend();
            sampledGeneration = gate.generation();
            sessionKnown = true;
            previousSession = session;
        }
        if (!factory || !factory->IsCurrent())
        {
            suspend();
            factory.Reset();
            const HRESULT result = CreateDXGIFactory1(IID_PPV_ARGS(&factory));
            if (FAILED(result)) detail(237, result);
            sampledGeneration = gate.generation();
        }
        const bool currentFactory = factory && factory->IsCurrent();
        detail(238, static_cast<LONG>(session));
        detail(239, sessionPolicy.blocked() ? 1 : 0);
        detail(240, currentFactory ? 1 : 0);
        gate.observe(GetTickCount64(), sampledGeneration, sessionPolicy.allows(session) && currentFactory);
        const auto current = gate.generation();
        if (retiredGeneration.load(std::memory_order_acquire) != current && retire_windows10_gpu_resources())
            retiredGeneration.store(current, std::memory_order_release);
        const bool ready = retiredGeneration.load(std::memory_order_acquire) == current && gate.allows(GetTickCount64());
        if (readyEvent)
        {
            if (ready) SetEvent(readyEvent);
            else ResetEvent(readyEvent);
        }
        detail(229, ready ? 1 : 0);
        if (ready && !wasReady) status(235);
        wasReady = ready;
    }

    void run() noexcept
    {
        MSG message {};
        while (GetMessageW(&message, nullptr, 0u, 0u) > 0)
        {
            TranslateMessage(&message);
            DispatchMessageW(&message);
        }
        stop();
        (void)retire_windows10_gpu_resources();
    }
};
Windows10LifecycleMonitor windows10Lifecycle;

bool windows10_optical_render_ready() noexcept
{
    // The observer admits both console and RDP connections after settling.
    // Requiring the physical console here would permanently reject RDP.
    const bool ready = windows10Lifecycle.retiredGeneration.load(std::memory_order_acquire) == windows10Lifecycle.gate.generation() &&
        windows10Lifecycle.gate.allows(GetTickCount64());
    if (!ready && readyEvent) ResetEvent(readyEvent);
    return ready;
}

void windows10_suspend_optical_render(HRESULT reason) noexcept
{
    detail(233, reason);
    status(231);
    windows10Lifecycle.suspend();
}

bool windows10_device_matches(ID3D11Device* first, ID3D11Device* second) noexcept
{
    if (!first || !second) return false;
    ComPtr<IUnknown> firstIdentity, secondIdentity;
    return SUCCEEDED(first->QueryInterface(IID_PPV_ARGS(&firstIdentity))) &&
        SUCCEEDED(second->QueryInterface(IID_PPV_ARGS(&secondIdentity))) &&
        firstIdentity.Get() == secondIdentity.Get();
}

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/glfw/panel_window.h>
#include <windows.h>
#include <commctrl.h>
#include <dwmapi.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace
{
struct CursorRestore
{
    POINT original {};
    bool valid = GetCursorPos(&original) != FALSE;
    ~CursorRestore() { if (valid) SetCursorPos(original.x, original.y); }
};

struct MouseRelease
{
    HWND window;
    ~MouseRelease() { SendMessageW(window, WM_LBUTTONUP, 0, 0); }
};

LRESULT CALLBACK count_moves(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR reference)
{
    if (message == WM_WINDOWPOSCHANGED) ++*reinterpret_cast<unsigned*>(reference);
    return DefSubclassProc(window, message, wParam, lParam);
}

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct BackdropWindow
{
    HBRUSH brush = CreateSolidBrush(RGB(17, 31, 49));
    HWND handle = nullptr;
    ~BackdropWindow()
    {
        if (handle) DestroyWindow(handle);
        UnregisterClassW(L"VibranceDragBackdrop", GetModuleHandleW(nullptr));
        if (brush) DeleteObject(brush);
    }
    void open(RECT work)
    {
        WNDCLASSW type {};
        type.lpfnWndProc = DefWindowProcW;
        type.hInstance = GetModuleHandleW(nullptr);
        type.hbrBackground = brush;
        type.lpszClassName = L"VibranceDragBackdrop";
        require(RegisterClassW(&type) != 0, "Cannot register the drag backdrop");
        handle = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TOPMOST,
            type.lpszClassName, L"Vibrance drag backdrop", WS_POPUP | WS_VISIBLE,
            work.left + 20, work.top + 20, 860, 460, nullptr, nullptr, type.hInstance, nullptr);
        require(handle != nullptr, "Cannot create the drag backdrop");
        UpdateWindow(handle);
    }
    void set_colour(COLORREF colour)
    {
        HBRUSH next = CreateSolidBrush(colour);
        require(next != nullptr, "Cannot update the drag backdrop");
        SetClassLongPtrW(handle, GCLP_HBRBACKGROUND, reinterpret_cast<LONG_PTR>(next));
        DeleteObject(brush);
        brush = next;
        InvalidateRect(handle, nullptr, TRUE);
        UpdateWindow(handle);
    }
};

COLORREF window_colour(HWND window, POINT point)
{
    require(ClientToScreen(window, &point) != FALSE, "Cannot locate the drag window on screen");
    DwmFlush();
    const HDC desktop = GetDC(nullptr);
    const COLORREF colour = desktop ? GetPixel(desktop, point.x, point.y) : CLR_INVALID;
    if (desktop) ReleaseDC(nullptr, desktop);
    return colour;
}

COLORREF centre_colour(HWND window)
{
    RECT rect {};
    require(GetClientRect(window, &rect) != FALSE, "Cannot read the drag window size");
    return window_colour(window, { (rect.right - rect.left) / 2, (rect.bottom - rect.top) / 2 });
}

bool same_colour(COLORREF actual, COLORREF expected)
{
    return actual != CLR_INVALID &&
        std::abs(int(GetRValue(actual)) - int(GetRValue(expected))) <= 4 &&
        std::abs(int(GetGValue(actual)) - int(GetGValue(expected))) <= 4 &&
        std::abs(int(GetBValue(actual)) - int(GetBValue(expected))) <= 4;
}

struct BoundsChanges
{
    unsigned placements = 0;
    unsigned sizes = 0;
    UINT flags = 0;
};

LRESULT CALLBACK count_bounds(HWND window, UINT message, WPARAM wParam,
    LPARAM lParam, UINT_PTR, DWORD_PTR reference)
{
    auto& changes = *reinterpret_cast<BoundsChanges*>(reference);
    if (message == WM_WINDOWPOSCHANGING)
    {
        ++changes.placements;
        changes.flags = reinterpret_cast<WINDOWPOS*>(lParam)->flags;
    }
    if (message == WM_SIZE) ++changes.sizes;
    return DefSubclassProc(window, message, wParam, lParam);
}

int retained_resize(bool composition = true)
{
    auto* logger = Logger::fetch_logger();
    logger->set_mode(true);
    logger->set_vulkan_renderer_logging_mode(true);
    for (bool transparent : { true, false })
    {
        GlfwPanelWindowHostOptions options;
        options.title = "Vibrance retained resize test";
        options.width = 640;
        options.height = 320;
        options.focusOnShow = false;
        options.showInTaskbar = options.showInAltTab = false;
        options.transparentFramebuffer = transparent;
        options.presentationBackend = composition ? PresentationBackend::eWindowsCompositionD3D11 : PresentationBackend::eNative;
        options.enableAudio = false;
        options.msaaSamples = 1;
        options.targetFrameRate = 60;
        options.makeTemplateOptions = [](Engine&) {
            GlfwPanelWindowTemplateOptions panel;
            panel.trafficLights.visible = false;
            panel.panelShadow.reset();
            panel.panelStyle = make_solid_style("#32363CD9");
            return panel;
        };
        GlfwPanelWindowHost host(options);
        if (!host.open()) return 77;
        require(host.engine()->presentation_backend() == options.presentationBackend,
            "Retained resizing must exercise the requested presentation backend");
        const auto window = static_cast<HWND>(glfw_native_window_handle(host.window()));
        EnableWindow(window, FALSE);
        for (unsigned frame = 0; frame < 12; ++frame) host.tick();
        logger->flush_file_output();
        const auto logOffset = std::filesystem::file_size(logger->output_path());
        BoundsChanges changes;
        require(SetWindowSubclass(window, count_bounds, 2,
            reinterpret_cast<DWORD_PTR>(&changes)) != FALSE, "Cannot observe resize placements");
        int startX = 0, startY = 0;
        glfw_window_position(host.window(), startX, startY);
        for (unsigned step = 0; step < 24; ++step)
        {
            const int dx = 1 + static_cast<int>(step % 12);
            const int dy = 1 + static_cast<int>((step * 3) % 11);
            const int x = startX - dx, y = startY - dy;
            const int width = 640 + dx, height = 320 + dy;
            changes = {};
            set_glfw_window_bounds(host.window(), x, y, width, height);
            require(changes.placements == 1 && changes.sizes == 1,
                "Moving a resize edge must apply position and size in one native placement");
            require((changes.flags & SWP_NOCOPYBITS) != 0,
                "Resizing must leave retained composition pixels to the compositor");
            int actualX = 0, actualY = 0, actualWidth = 0, actualHeight = 0;
            glfw_window_position(host.window(), actualX, actualY);
            glfw_window_size(host.window(), actualWidth, actualHeight);
            require(actualX == x && actualY == y && actualWidth == width && actualHeight == height,
                "Atomic resizing must preserve the requested client bounds");
            changes = {};
            set_glfw_window_bounds(host.window(), x, y, width, height);
            require(changes.placements == 0 && changes.sizes == 0,
                "An unchanged resize pointer must not invalidate the native window");
            for (unsigned frame = 0; frame < 3; ++frame) host.tick();
            require(host.engine()->render_width() == static_cast<unsigned>(width) &&
                host.engine()->render_height() == static_cast<unsigned>(height),
                "Retained presentation must render at the new client dimensions");
            require(host.engine()->presentation_backend() == options.presentationBackend,
                "Repeated resizing must preserve the active composition presenter");
        }
        logger->flush_file_output();
        if (composition && logger->is_vulkan_renderer_logging_enabled())
        {
            std::ifstream log(logger->output_path(), std::ios::binary);
            log.seekg(static_cast<std::streamoff>(logOffset));
            const std::string messages((std::istreambuf_iterator<char>(log)), {});
            require(messages.find("Rebuilding swapchain.") == std::string::npos,
                "Composition resizing must retain the unused native Vulkan swapchain");
        }
        RemoveWindowSubclass(window, count_bounds, 2);
    }
    std::cout << "Retained resize geometry and presentation verified at 60 fps\n";
    return 0;
}

int transparent_moves(bool composition, bool backdrop = false)
{
    GlfwPanelWindowHostOptions options;
    options.title = "Vibrance transparent drag test";
    options.width = 640;
    options.height = 320;
    options.focusOnShow = false;
    options.showInTaskbar = options.showInAltTab = false;
    options.transparentFramebuffer = true;
    options.presentationBackend = composition ? PresentationBackend::eWindowsCompositionD3D11 : PresentationBackend::eNative;
    options.alwaysOnTop = true;
    options.enableAudio = false;
    options.msaaSamples = 1;
    unsigned draws = 0;
    std::string panelColour = "#E53266D9";
    std::string markerColour = "#FFFFFFFF";
    bool showBackdrop = false;
    options.makeTemplateOptions = [&](Engine&) {
        GlfwPanelWindowTemplateOptions panel;
        panel.title.clear();
        panel.trafficLights.visible = false;
        panel.topDragHeight = 72;
        panel.panelShadow.reset();
        panel.panelStyle = make_solid_style(panelColour, "#00000000", 0, 1);
        if (showBackdrop)
        {
            auto layer = UiPanelBackgroundLayer::fill(*panel.panelStyle);
            SystemBackdropRegion region;
            region.material = GlassMaterial::eSystemGlass;
            region.blurRadius = 18;
            region.saturation = 1;
            region.tint = { .1f, .3f, .8f, .1f };
            layer.systemBackdrop = region;
            panel.panelBackgroundLayers = { layer };
            panel.panelBackgroundLayersReplacePanelFill = true;
        }
        panel.buildContent = [&](GlfwPanelWindowTemplateContext& context) {
            const auto colour = context.windowSize.x >= 700 ? "#32E566FF" : "#E5BB32FF";
            const auto indicator = context.scene.create_shape({ 80, 100 }, { 40, 40 }, make_solid_style(colour));
            const auto marker = context.scene.create_shape({ 200, 80 }, { 4, 120 }, make_solid_style(markerColour));
            const auto horizontal = context.scene.create_shape({ 80, 240 }, { 140, 4 }, make_solid_style(markerColour));
            context.ui.set_layer(indicator, 10, 0);
            context.ui.set_layer(marker, 10, 1);
            context.ui.set_layer(horizontal, 10, 2);
        };
        panel.beforeDraw = [&](Engine&) { ++draws; };
        return panel;
    };
    GlfwPanelWindowHost host(options);
    if (!host.open()) return 77;
    require(host.engine()->presentation_backend() == options.presentationBackend,
        "Transparent movement must exercise the requested presenter");
    HWND window = static_cast<HWND>(glfw_native_window_handle(host.window()));
    EnableWindow(window, FALSE);
    MONITORINFO monitor { sizeof(monitor) };
    require(GetMonitorInfoW(MonitorFromWindow(window, MONITOR_DEFAULTTONEAREST), &monitor) != FALSE,
        "Cannot find the drag test monitor");
    if (monitor.rcWork.right - monitor.rcWork.left < 900 || monitor.rcWork.bottom - monitor.rcWork.top < 500) return 77;
    BackdropWindow background;
    background.open(monitor.rcWork);
    set_glfw_window_position(host.window(), monitor.rcWork.left + 70, monitor.rcWork.top + 70);
    set_glfw_window_visible(host.window(), true);
    SetWindowPos(window, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    for (unsigned frame = 0; frame < (backdrop ? 60u : 20u); ++frame)
    {
        poll_glfw_events();
        host.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const COLORREF rawBaseline = centre_colour(window);
    if (backdrop)
    {
        showBackdrop = true;
        host.panel()->set_template_options(options.makeTemplateOptions(*host.engine()));
        for (unsigned frame = 0; frame < 40; ++frame)
        {
            poll_glfw_events(); host.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    const COLORREF baseline = centre_colour(window);
    if (baseline == CLR_INVALID) return 77;
    if (!backdrop)
        require(GetRValue(baseline) > GetGValue(baseline) + 30 && GetRValue(baseline) > GetBValue(baseline) + 30,
            "The transparent drag fixture must first display its pink retained surface");
    else require(!same_colour(baseline, RGB(17, 31, 49)),
        "The native backdrop must be visible before testing live resizing");
    const auto fixedMarker = window_colour(window, { 201, 110 });
    require(GetRValue(fixedMarker) > 240 && GetGValue(fixedMarker) > 240,
        "The resize marker must be visible before testing live presentation");
    require(same_colour(window_colour(window, { 120, 241 }), fixedMarker),
        "The horizontal resize marker must be visible before testing live presentation");
    const auto generation = host.engine()->renderer2d_scene().frame_generation();
    const unsigned originalDraws = draws;
    int startX = 0, startY = 0;
    glfw_window_position(host.window(), startX, startY);
    for (unsigned step = 1; step <= (backdrop ? 0u : 8u); ++step)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(22));
        set_glfw_window_position(host.window(), startX + int(step * 8), startY + int(step * 2));
        host.engine()->draw();
        int movedX = 0, movedY = 0;
        glfw_window_position(host.window(), movedX, movedY);
        require(movedX == startX + int(step * 8) && movedY == startY + int(step * 2),
            "The visible transparent fixture must follow the drag");
        const COLORREF actual = centre_colour(window);
        if (!same_colour(actual, baseline))
            throw std::runtime_error("Transparent panel disappeared during drag: baseline=" +
                std::to_string(baseline) + ", actual=" + std::to_string(actual) + ", step=" + std::to_string(step));
    }
    require(draws == originalDraws && host.engine()->renderer2d_scene().frame_generation() == generation,
        "Moving a transparent panel must retain its rendered pixels without rebuilding the UI");
    for (unsigned frame = 0; frame < 5; ++frame) { poll_glfw_events(); host.tick(); }
    require(same_colour(centre_colour(window), baseline), "The transparent panel must remain visible after release");
    for (unsigned step = 1; step <= 48; ++step)
    {
        const unsigned phase = (step - 1) % 24 + 1;
        const int growth = phase <= 12 ? int(phase) : 24 - int(phase);
        set_glfw_window_size(host.window(), 640 + (step <= 24 ? growth * 9 : 0),
            320 + (step > 24 ? growth * 3 : 0));
        for (unsigned sample = 0; sample < 4; ++sample)
        {
            poll_glfw_events(); host.tick();
            const auto resized = centre_colour(window);
            if (!same_colour(resized, baseline))
                throw std::runtime_error("Transparent panel disappeared during live resize: baseline=" +
                    std::to_string(baseline) + ", actual=" + std::to_string(resized) + ", step=" +
                    std::to_string(step) + ", sample=" + std::to_string(sample));
            const auto marker = window_colour(window, { 201, 110 });
            if (!same_colour(marker, fixedMarker))
                throw std::runtime_error("Retained panel pixels moved while resizing: baseline=" +
                    std::to_string(fixedMarker) + ", actual=" + std::to_string(marker) + ", step=" +
                    std::to_string(step) + ", sample=" + std::to_string(sample));
            require(same_colour(window_colour(window, { 120, 241 }), fixedMarker),
                "Retained panel details must stay in place while resizing the window height");
            if (sample == 3)
            {
                RECT bounds {};
                require(GetClientRect(window, &bounds) != FALSE, "Cannot read resized panel bounds");
                const auto right = window_colour(window, { bounds.right - 60, bounds.bottom / 2 });
                const auto bottom = window_colour(window, { bounds.right / 2, bounds.bottom - 55 });
                if (!same_colour(right, baseline) || !same_colour(bottom, baseline))
                    throw std::runtime_error("The resized background must reach both updated window edges: baseline=" +
                        std::to_string(baseline) + ", right=" + std::to_string(right) + ", bottom=" +
                        std::to_string(bottom) + ", step=" + std::to_string(step));
            }
        }
        if (step == 12)
        {
            const auto indicator = window_colour(window, { 100, 120 });
            require(GetGValue(indicator) > GetRValue(indicator) + 60,
                "Window contents must update while continuously resizing the outer window");
        }
    }
    require(host.engine()->presentation_backend() == options.presentationBackend,
        "Live resizing must retain the requested presenter");
    if (backdrop)
    {
        showBackdrop = false;
        host.panel()->set_template_options(options.makeTemplateOptions(*host.engine()));
        set_glfw_window_size(host.window(), 650, 330);
        for (unsigned frame = 0; frame < 20; ++frame)
        {
            poll_glfw_events(); host.tick();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        require(same_colour(centre_colour(window), rawBaseline),
            "Removing a native backdrop during resizing must clear the retained material");
    }
    if (!backdrop)
    {
        background.set_colour(RGB(17, 131, 49));
        const auto blended = centre_colour(window);
        require(GetGValue(blended) > GetGValue(baseline) + 8,
            "The retained window must preserve its transparency after live resizing");
    }
    panelColour = "#3266E5D9";
    markerColour = "#3266E5FF";
    host.panel()->set_template_options(options.makeTemplateOptions(*host.engine()));
    for (unsigned frame = 0; frame < 20; ++frame)
    {
        poll_glfw_events(); host.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto repainted = window_colour(window, { 201, 110 });
    require(GetBValue(repainted) > GetRValue(repainted) + 60,
        "Resized windows must display newly rendered content after the retained surface");
    host.close();
    std::cout << "Transparent surface retained during movement and live resizing; new frames remain visible\n";
    return 0;
}

int run()
{
    CursorRestore cursor;
    if (!cursor.valid) return 77;
    LoggerOutputOptions logs;
    logs.directory = std::filesystem::current_path() / "panel-drag-logs";
    logs.filePrefix = "panel_drag_test";
    Logger::configure_output(std::move(logs));
    unsigned draws = 0u, nativeMoves = 0u;
    GlfwPanelWindowHostOptions options;
    options.title = "Vibrance window drag test";
    options.width = 640; options.height = 320;
    options.focusOnShow = false;
    options.showInTaskbar = false; options.showInAltTab = false;
    options.transparentFramebuffer = false; options.msaaSamples = 1u;
    options.makeTemplateOptions = [&](Engine&) {
        GlfwPanelWindowTemplateOptions panel;
        panel.title.clear(); panel.trafficLights.visible = false;
        panel.topDragHeight = 72.0f;
        panel.buildContent = [](GlfwPanelWindowTemplateContext&) {};
        panel.beforeDraw = [&](Engine&) { ++draws; };
        return panel;
    };
    GlfwPanelWindowHost host(options);
    if (!host.open()) return 77;
    set_glfw_window_visible(host.window(), false);
    const HWND window = static_cast<HWND>(glfw_native_window_handle(host.window()));
    MouseRelease release { window };
    POINT anchor { 200, 35 };
    ClientToScreen(window, &anchor);
    if (!SetCursorPos(anchor.x, anchor.y)) return 77;
    poll_glfw_events();
    SendMessageW(window, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(200, 35));
    int startX = 0, startY = 0;
    glfw_window_position(host.window(), startX, startY);
    const auto generation = host.engine()->renderer2d_scene().frame_generation();
    require(SetWindowSubclass(window, count_moves, 1u,
        reinterpret_cast<DWORD_PTR>(&nativeMoves)) != FALSE, "Cannot observe window placement");

    unsigned verified = 0u;
    for (unsigned step = 1u; step <= 32u; ++step)
    {


        const double deadline = glfw_time_seconds() + 0.02;
        while (glfw_time_seconds() < deadline)
            wait_glfw_events(deadline - glfw_time_seconds());
        const int dx = static_cast<int>(step), dy = static_cast<int>(step / 4u);
        require(SetCursorPos(anchor.x + dx, anchor.y + dy) != FALSE, "Cannot position test cursor");
        POINT local { anchor.x + dx, anchor.y + dy };
        ScreenToClient(window, &local);
        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(local.x, local.y));
        int x = 0, y = 0;
        glfw_window_position(host.window(), x, y);
        POINT actual {};
        GetCursorPos(&actual);


        if (actual.x != anchor.x + dx || actual.y != anchor.y + dy) continue;
        require(x == startX + dx && y == startY + dy,
            "Window movement must follow input before any render/UI tick");
        ++verified;
        const auto beforeDuplicate = nativeMoves;
        ScreenToClient(window, &actual);
        SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(actual.x, actual.y));
        require(nativeMoves == beforeDuplicate, "Duplicate motion must retain the native window position");
    }
    require(verified > 0u, "No controlled cursor samples could be verified");
    require(draws == 0u && host.engine()->renderer2d_scene().frame_generation() == generation,
        "Dragging retained pixels must not request rendering or dirty the scene");



    SetCursorPos(anchor.x + 48, anchor.y + 8);
    POINT finalLocal { anchor.x + 48, anchor.y + 8 };
    ScreenToClient(window, &finalLocal);
    SendMessageW(window, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(finalLocal.x, finalLocal.y));
    SendMessageW(window, WM_LBUTTONUP, 0, MAKELPARAM(finalLocal.x, finalLocal.y));
    int releasedX = 0, releasedY = 0;
    glfw_window_position(host.window(), releasedX, releasedY);
    POINT finalCursor {};
    GetCursorPos(&finalCursor);
    if (finalCursor.x == anchor.x + 48 && finalCursor.y == anchor.y + 8)
        require(releasedX == startX + 48 && releasedY == startY + 8,
            "Release must apply the last coalesced pointer sample");
    SetCursorPos(anchor.x, anchor.y);
    SendMessageW(window, WM_MOUSEMOVE, 0, MAKELPARAM(200, 35));
    int x = 0, y = 0;
    glfw_window_position(host.window(), x, y);
    require(x == releasedX && y == releasedY, "Mouse release must finish the window drag");
    RemoveWindowSubclass(window, count_moves, 1u);
    std::cout << "Verified immediate moves: " << verified << "; renderer draws: " << draws << '\n';
    return 0;
}
}

int main(int argc, char** argv)
{
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--retained-resize") return retained_resize();
        if (argc == 2 && std::string_view(argv[1]) == "--native-resize") return retained_resize(false);
        if (argc == 2 && std::string_view(argv[1]) == "--transparent") return transparent_moves(true);
        if (argc == 2 && std::string_view(argv[1]) == "--transparent-native") return transparent_moves(false);
        if (argc == 2 && std::string_view(argv[1]) == "--transparent-backdrop") return transparent_moves(true, true);
        return run();
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

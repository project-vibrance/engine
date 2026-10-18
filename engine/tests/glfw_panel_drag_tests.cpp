#include <vibranceUI/glfw/panel_window.h>
#include <windows.h>
#include <commctrl.h>
#include <iostream>
#include <stdexcept>

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
        // A new display opportunity, with no panel or renderer tick. The
        // motion handler coalesces faster input to the monitor's cadence.
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
        // A user or another desktop process can move the cursor during the
        // probe. Verify the samples for which the requested point was retained.
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

    // Release immediately after another sample, before the movement deadline.
    // The final pointer location must not be left behind by coalescing.
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

int main()
{
    try { return run(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

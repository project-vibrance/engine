#include <vibranceUI/glfw/window.h>
#include <vibranceUI/core/logger.h>
#include <vibranceUI/ui/controls.h>
#include <algorithm>
#include <sstream>
#include <unordered_map>
#include <utility>

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#include <GLFW/glfw3native.h>
#endif

namespace
{
    GLFWmonitor* monitor_for_window(GLFWwindow* window)
    {
        if (!window)
        {
            return nullptr;
        }

        int windowX = 0;
        int windowY = 0;
        int windowWidth = 0;
        int windowHeight = 0;
        glfwGetWindowPos(window, &windowX, &windowY);
        glfwGetWindowSize(window, &windowWidth, &windowHeight);

        int monitorCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        GLFWmonitor* bestMonitor = nullptr;
        int bestArea = -1;
        for (int i = 0; monitors && i < monitorCount; ++i)
        {
            int monitorX = 0;
            int monitorY = 0;
            int monitorWidth = 0;
            int monitorHeight = 0;
            glfwGetMonitorWorkarea(
                monitors[i],
                &monitorX,
                &monitorY,
                &monitorWidth,
                &monitorHeight);
            const int overlapWidth = std::max(
                0,
                std::min(windowX + windowWidth, monitorX + monitorWidth) -
                    std::max(windowX, monitorX));
            const int overlapHeight = std::max(
                0,
                std::min(windowY + windowHeight, monitorY + monitorHeight) -
                    std::max(windowY, monitorY));
            const int overlapArea = overlapWidth * overlapHeight;
            if (overlapArea > bestArea)
            {
                bestArea = overlapArea;
                bestMonitor = monitors[i];
            }
        }
        return bestMonitor ? bestMonitor : glfwGetPrimaryMonitor();
    }

    void log_window_features(GLFWwindow* window, const GlfwWindowCreateInfo& createInfo)
    {
        // Report compositor-related hints so transparency problems are easier to diagnose
        Logger* logger = Logger::fetch_logger();

#ifdef GLFW_TRANSPARENT_FRAMEBUFFER
        if (createInfo.transparentFramebuffer)
        {
            std::stringstream transparencyLine;
            transparencyLine << "GLFW transparent framebuffer attribute is "
                << (glfwGetWindowAttrib(window, GLFW_TRANSPARENT_FRAMEBUFFER) == GLFW_TRUE ? "enabled" : "disabled")
                << ".";
            logger->print(transparencyLine.str());
        }
#else
        if (createInfo.transparentFramebuffer)
        {
            logger->print("GLFW transparent framebuffer hint is not available in this GLFW build.");
        }
#endif
    }
}

GLFWwindow* build_glfw_window(const GlfwWindowCreateInfo& createInfo)
{
    Logger* logger = Logger::fetch_logger();

    // GLFW stays inside this backend layer so the engine can remain platform-agnostic
    if (glfwInit() != GLFW_TRUE)
    {
        logger->print("Failed to initialise GLFW.");
        return nullptr;
    }

    glfwDefaultWindowHints();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#ifdef GLFW_SCALE_TO_MONITOR
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, GLFW_TRUE);
#endif
#ifdef GLFW_SCALE_FRAMEBUFFER
    glfwWindowHint(GLFW_SCALE_FRAMEBUFFER, GLFW_TRUE);
#endif

    if (!createInfo.decorated)
    {
        glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
    }
    if (createInfo.alwaysOnTop)
    {
        glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
    }

    if (createInfo.transparentFramebuffer)
    {
        glfwWindowHint(GLFW_ALPHA_BITS, 8);
#ifdef GLFW_TRANSPARENT_FRAMEBUFFER
        glfwWindowHint(GLFW_TRANSPARENT_FRAMEBUFFER, GLFW_TRUE);
#endif
    }

    GLFWwindow* window = glfwCreateWindow(createInfo.width, createInfo.height, createInfo.name, nullptr, nullptr);
    if (window)
    {
        set_glfw_window_always_on_top(
            window,
            createInfo.alwaysOnTop);
        log_window_features(window, createInfo);

        std::stringstream line;
        line << "GLFW window for \"" << createInfo.name << "\" initialised successfully with dimensions "
            << createInfo.width << "x" << createInfo.height << ".";
        logger->print(line.str());
    }
    else
    {
        logger->print("Failed to create GLFW window");
        glfwTerminate();
    }

    return window;
}

GLFWwindow* build_glfw_window(int width, int height, const char* name, bool transparent)
{
    GlfwWindowCreateInfo createInfo = {};
    createInfo.width = width;
    createInfo.height = height;
    createInfo.name = name;
    createInfo.transparentFramebuffer = transparent;
    return build_glfw_window(createInfo);
}

void destroy_glfw_window(GLFWwindow* window)
{
    if (window)
    {
        glfwDestroyWindow(window);
    }
}

void terminate_glfw()
{
    glfwTerminate();
}

std::vector<GlfwMonitorInfo> glfw_connected_monitors()
{
    std::vector<GlfwMonitorInfo> result;
    if (glfwInit() != GLFW_TRUE)
    {
        return result;
    }
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    std::unordered_map<std::string, std::uint32_t> idOccurrences;
    result.reserve(static_cast<std::size_t>(std::max(count, 0)));
    for (int index = 0; monitors && index < count; ++index)
    {
        GLFWmonitor* monitor = monitors[index];
        const GLFWvidmode* videoMode = glfwGetVideoMode(monitor);
        if (!monitor || !videoMode || videoMode->width <= 0 ||
            videoMode->height <= 0)
        {
            continue;
        }
        GlfwMonitorInfo info = {};
        info.handle = monitor;
        info.primary = monitor == primary;
        if (const char* name = glfwGetMonitorName(monitor))
        {
            info.name = name;
        }
        glfwGetMonitorPos(
            monitor,
            &info.position.x,
            &info.position.y);
        info.size = { videoMode->width, videoMode->height };
        std::string baseId = glfw_monitor_identifier(monitor);
        if (baseId.empty())
        {
            int physicalWidth = 0;
            int physicalHeight = 0;
            glfwGetMonitorPhysicalSize(
                monitor,
                &physicalWidth,
                &physicalHeight);
            baseId = "display:" + info.name + ":" +
                std::to_string(physicalWidth) + "x" +
                std::to_string(physicalHeight);
        }
        const std::uint32_t occurrence = idOccurrences[baseId]++;
        info.id = occurrence == 0u ?
            std::move(baseId) :
            baseId + "#" + std::to_string(occurrence + 1u);
        result.push_back(std::move(info));
    }
    return result;
}

#if !defined(__APPLE__)
std::string glfw_monitor_identifier(GLFWmonitor* monitor)
{
    if (!monitor)
    {
        return {};
    }
#if defined(_WIN32)
    if (const char* device = glfwGetWin32Monitor(monitor);
        device && device[0] != '\0')
    {
        return "windows:" + std::string(device);
    }
#endif
    return {};
}
#endif

bool glfw_screen_cursor_position(
    GLFWwindow* referenceWindow,
    glm::ivec2& position)
{
#if defined(_WIN32)
    POINT point {};
    if (GetCursorPos(&point))
    {
        position = { point.x, point.y };
        return true;
    }
#endif
    if (!referenceWindow)
    {
        return false;
    }
    int windowX = 0;
    int windowY = 0;
    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetWindowPos(referenceWindow, &windowX, &windowY);
    glfwGetCursorPos(referenceWindow, &cursorX, &cursorY);
    position = {
        windowX + static_cast<int>(cursorX),
        windowY + static_cast<int>(cursorY)
    };
    return true;
}

int vibrance_glfw_create_surface(void* instance, void* userData, void* surfaceOut)
{
    return glfwCreateWindowSurface(
        static_cast<VkInstance>(instance),
        static_cast<GLFWwindow*>(userData),
        nullptr,
        static_cast<VkSurfaceKHR*>(surfaceOut));
}

const char** glfw_required_instance_extensions(uint32_t& count)
{
    count = 0u;
    return glfwGetRequiredInstanceExtensions(&count);
}

bool glfw_framebuffer_size(GLFWwindow* window, int& width, int& height)
{
    width = 0;
    height = 0;
    if (!window)
    {
        return false;
    }
    glfwGetFramebufferSize(window, &width, &height);
    return width > 0 && height > 0;
}

bool glfw_window_size(GLFWwindow* window, int& width, int& height)
{
    width = 0;
    height = 0;
    if (!window)
    {
        return false;
    }
    glfwGetWindowSize(window, &width, &height);
    return width > 0 && height > 0;
}

bool glfw_window_position(GLFWwindow* window, int& x, int& y)
{
    x = 0;
    y = 0;
    if (!window)
    {
        return false;
    }
    glfwGetWindowPos(window, &x, &y);
    return true;
}

bool glfw_cursor_window_point(GLFWwindow* window, glm::vec2& point)
{
    point = glm::vec2(0.0f);
    if (!window)
    {
        return false;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    point = { static_cast<float>(cursorX), static_cast<float>(cursorY) };
    return true;
}

bool glfw_window_focused(GLFWwindow* window)
{
    return window && glfwGetWindowAttrib(window, GLFW_FOCUSED) == GLFW_TRUE;
}

bool glfw_window_hovered(GLFWwindow* window)
{
#ifdef GLFW_HOVERED
    return window && glfwGetWindowAttrib(window, GLFW_HOVERED) == GLFW_TRUE;
#else
    if (!window)
    {
        return false;
    }

    int width = 0;
    int height = 0;
    if (!glfw_window_size(window, width, height))
    {
        return false;
    }

    glm::vec2 point(0.0f);
    if (!glfw_cursor_window_point(window, point))
    {
        return false;
    }
    return point.x >= 0.0f && point.y >= 0.0f &&
        point.x < static_cast<float>(width) &&
        point.y < static_cast<float>(height);
#endif
}

glm::vec2 glfw_content_scale(GLFWwindow* window)
{
    float xscale = 1.0f;
    float yscale = 1.0f;
    if (window)
    {
        glfwGetWindowContentScale(window, &xscale, &yscale);
    }
    return {
        std::max(xscale, 0.25f),
        std::max(yscale, 0.25f)
    };
}

double glfw_time_seconds()
{
    return glfwGetTime();
}

bool glfw_window_should_close(GLFWwindow* window)
{
    return !window || glfwWindowShouldClose(window);
}

void poll_glfw_events()
{
    glfwPollEvents();
}

void set_glfw_window_title(GLFWwindow* window, const char* title)
{
    if (window && title)
    {
        glfwSetWindowTitle(window, title);
    }
}

void set_glfw_window_decorated(GLFWwindow* window, bool decorated)
{
    if (!window)
    {
        return;
    }
    glfwSetWindowAttrib(
        window,
        GLFW_DECORATED,
        decorated ? GLFW_TRUE : GLFW_FALSE);
#if defined(_WIN32)
    HWND nativeWindow = glfwGetWin32Window(window);
    if (nativeWindow)
    {
        LONG_PTR style = GetWindowLongPtrW(nativeWindow, GWL_STYLE);
        if (decorated)
        {
            style |= WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                WS_MAXIMIZEBOX | WS_SYSMENU;
        }
        else
        {
            style &= ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZEBOX |
                WS_MAXIMIZEBOX | WS_SYSMENU);
        }
        SetWindowLongPtrW(nativeWindow, GWL_STYLE, style);
        SetWindowPos(
            nativeWindow,
            nullptr,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
                SWP_NOACTIVATE | SWP_FRAMECHANGED);
    }
#endif
}

void set_glfw_window_always_on_top(GLFWwindow* window, bool alwaysOnTop)
{
    if (!window)
    {
        return;
    }
    glfwSetWindowAttrib(
        window,
        GLFW_FLOATING,
        alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
#if defined(_WIN32)
    HWND nativeWindow = glfwGetWin32Window(window);
    if (nativeWindow)
    {
        SetWindowPos(
            nativeWindow,
            alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW);
    }
#endif
}

#if !defined(__APPLE__)
bool apply_glfw_window_placement(
    GLFWwindow* window,
    const GlfwWindowPlacement& placement)
{
    if (!window || placement.size.x <= 0 || placement.size.y <= 0)
    {
        return false;
    }
    set_glfw_window_decorated(window, placement.decorated);
#if defined(_WIN32)
    // Win32 applies bounds, z-order, and the frame change atomically. This is
    // important for transparent composition windows: separate move/resize
    // mutations can expose an old retained surface between DWM transactions.
    glfwSetWindowAttrib(
        window,
        GLFW_FLOATING,
        placement.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
    HWND nativeWindow = glfwGetWin32Window(window);
    if (!nativeWindow)
    {
        return false;
    }
    return SetWindowPos(
        nativeWindow,
        placement.alwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST,
        placement.position.x,
        placement.position.y,
        placement.size.x,
        placement.size.y,
        SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_FRAMECHANGED) != FALSE;
#else
    // GLFW maps these operations to the active Unix window system (X11 or
    // Wayland), including the compositor's supported always-above hint.
    glfwSetWindowPos(window, placement.position.x, placement.position.y);
    glfwSetWindowSize(window, placement.size.x, placement.size.y);
    glfwSetWindowAttrib(
        window,
        GLFW_FLOATING,
        placement.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
    return true;
#endif
}
#endif

void set_glfw_window_position(GLFWwindow* window, int x, int y)
{
    if (window)
    {
        glfwSetWindowPos(window, x, y);
    }
}

void set_glfw_window_size(GLFWwindow* window, int width, int height)
{
    if (window)
    {
        glfwSetWindowSize(window, std::max(width, 1), std::max(height, 1));
    }
}

void set_glfw_window_should_close(GLFWwindow* window, bool shouldClose)
{
    if (window)
    {
        glfwSetWindowShouldClose(window, shouldClose ? GLFW_TRUE : GLFW_FALSE);
    }
}

void* glfw_native_window_handle(GLFWwindow* window)
{
#if defined(_WIN32)
    return window ? static_cast<void*>(glfwGetWin32Window(window)) : nullptr;
#else
    (void)window;
    return nullptr;
#endif
}

#if !defined(__APPLE__)
bool begin_glfw_native_window_drag(GLFWwindow* window)
{
    (void)window;
    return false;
}
#endif

void iconify_glfw_window(GLFWwindow* window)
{
    if (window)
    {
        glfwIconifyWindow(window);
    }
}

void maximize_glfw_window(GLFWwindow* window)
{
    if (window)
    {
        glfwMaximizeWindow(window);
    }
}

void restore_glfw_window(GLFWwindow* window)
{
    if (window)
    {
        glfwRestoreWindow(window);
    }
}

bool glfw_window_maximized(GLFWwindow* window)
{
    return window && glfwGetWindowAttrib(window, GLFW_MAXIMIZED) == GLFW_TRUE;
}

bool glfw_window_monitor_work_area(
    GLFWwindow* window,
    glm::ivec2& position,
    glm::ivec2& size)
{
    position = { 0, 0 };
    size = { 0, 0 };
    GLFWmonitor* monitor = monitor_for_window(window);
    if (!monitor)
    {
        return false;
    }
    glfwGetMonitorWorkarea(
        monitor,
        &position.x,
        &position.y,
        &size.x,
        &size.y);
    return size.x > 0 && size.y > 0;
}

bool set_glfw_window_fullscreen(
    GLFWwindow* window,
    bool fullscreen,
    glm::ivec2 windowedPosition,
    glm::ivec2 windowedSize)
{
    if (!window)
    {
        return false;
    }

    if (!fullscreen)
    {
        glfwSetWindowMonitor(
            window,
            nullptr,
            windowedPosition.x,
            windowedPosition.y,
            std::max(windowedSize.x, 1),
            std::max(windowedSize.y, 1),
            GLFW_DONT_CARE);
        return true;
    }

    GLFWmonitor* monitor = monitor_for_window(window);
    const GLFWvidmode* videoMode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!monitor || !videoMode)
    {
        return false;
    }
    glfwSetWindowMonitor(
        window,
        monitor,
        0,
        0,
        videoMode->width,
        videoMode->height,
        videoMode->refreshRate);
    return true;
}

void set_glfw_window_user_pointer(GLFWwindow* window, void* userPointer)
{
    if (window)
    {
        glfwSetWindowUserPointer(window, userPointer);
    }
}

void* glfw_window_user_pointer(GLFWwindow* window)
{
    return window ? glfwGetWindowUserPointer(window) : nullptr;
}

void set_glfw_mouse_passthrough(GLFWwindow* window, bool enabled)
{
#ifdef GLFW_MOUSE_PASSTHROUGH
    if (window)
    {
        glfwSetWindowAttrib(window, GLFW_MOUSE_PASSTHROUGH, enabled ? GLFW_TRUE : GLFW_FALSE);
    }
#else
    (void)window;
    (void)enabled;
#endif
}

void set_glfw_callbacks(GLFWwindow* window, const GlfwCallbackSet& callbacks)
{
    if (!window)
    {
        return;
    }

    glfwSetFramebufferSizeCallback(window, callbacks.framebufferSize);
    glfwSetWindowContentScaleCallback(window, callbacks.contentScale);
    glfwSetMouseButtonCallback(window, callbacks.mouseButton);
    glfwSetScrollCallback(window, callbacks.scroll);
    glfwSetKeyCallback(window, callbacks.key);
    glfwSetCharCallback(window, callbacks.character);
    glfwSetDropCallback(window, callbacks.drop);
}

GlfwCursorSet create_glfw_standard_cursors()
{
    GlfwCursorSet cursors = {};
#ifdef GLFW_IBEAM_CURSOR
    cursors.text = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
#endif
#ifdef GLFW_POINTING_HAND_CURSOR
    cursors.pointer = glfwCreateStandardCursor(GLFW_POINTING_HAND_CURSOR);
#elif defined(GLFW_HAND_CURSOR)
    cursors.pointer = glfwCreateStandardCursor(GLFW_HAND_CURSOR);
#endif
#ifdef GLFW_NOT_ALLOWED_CURSOR
    cursors.unavailable = glfwCreateStandardCursor(GLFW_NOT_ALLOWED_CURSOR);
#else
    cursors.unavailable = glfwCreateStandardCursor(GLFW_CURSOR_NORMAL);
#endif
#ifdef GLFW_RESIZE_NWSE_CURSOR
    cursors.resizeNwse = glfwCreateStandardCursor(GLFW_RESIZE_NWSE_CURSOR);
#elif defined(GLFW_HRESIZE_CURSOR)
    cursors.resizeNwse = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
#endif
#ifdef GLFW_RESIZE_NESW_CURSOR
    cursors.resizeNesw = glfwCreateStandardCursor(GLFW_RESIZE_NESW_CURSOR);
#elif defined(GLFW_HRESIZE_CURSOR)
    cursors.resizeNesw = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
#endif
#ifdef GLFW_RESIZE_EW_CURSOR
    cursors.resizeEw = glfwCreateStandardCursor(GLFW_RESIZE_EW_CURSOR);
#elif defined(GLFW_HRESIZE_CURSOR)
    cursors.resizeEw = glfwCreateStandardCursor(GLFW_HRESIZE_CURSOR);
#endif
#ifdef GLFW_RESIZE_NS_CURSOR
    cursors.resizeNs = glfwCreateStandardCursor(GLFW_RESIZE_NS_CURSOR);
#elif defined(GLFW_VRESIZE_CURSOR)
    cursors.resizeNs = glfwCreateStandardCursor(GLFW_VRESIZE_CURSOR);
#endif
    return cursors;
}

void destroy_glfw_standard_cursors(GlfwCursorSet& cursors)
{
    auto destroy_cursor = [](GLFWcursor*& cursor) {
        if (cursor)
        {
            glfwDestroyCursor(cursor);
            cursor = nullptr;
        }
    };

    destroy_cursor(cursors.text);
    destroy_cursor(cursors.pointer);
    destroy_cursor(cursors.unavailable);
    destroy_cursor(cursors.resizeNwse);
    destroy_cursor(cursors.resizeNesw);
    destroy_cursor(cursors.resizeEw);
    destroy_cursor(cursors.resizeNs);
}

GLFWcursor* glfw_cursor_for_kind(const GlfwCursorSet& cursors, UiCursorKind cursorKind)
{
    switch (cursorKind)
    {
    case UiCursorKind::eText:
        return cursors.text;
    case UiCursorKind::ePointer:
        return cursors.pointer;
    case UiCursorKind::eUnavailable:
        return cursors.unavailable;
    case UiCursorKind::eResizeNwse:
        return cursors.resizeNwse ? cursors.resizeNwse : cursors.resizeEw;
    case UiCursorKind::eResizeNesw:
        return cursors.resizeNesw ? cursors.resizeNesw : cursors.resizeEw;
    case UiCursorKind::eResizeEw:
        return cursors.resizeEw;
    case UiCursorKind::eResizeNs:
        return cursors.resizeNs;
    case UiCursorKind::eDefault:
    default:
        return nullptr;
    }
}

void clear_glfw_cursor(GLFWwindow* window)
{
    if (window)
    {
        glfwSetCursor(window, nullptr);
    }
}

void set_glfw_cursor_for_kind(GLFWwindow* window, const GlfwCursorSet& cursors, UiCursorKind cursorKind)
{
    if (window)
    {
        glfwSetCursor(window, glfw_cursor_for_kind(cursors, cursorKind));
    }
}

InputModifiers input_modifiers_from_glfw(int mods)
{
    return {
        (mods & GLFW_MOD_SHIFT) != 0,
        (mods & GLFW_MOD_CONTROL) != 0,
        (mods & GLFW_MOD_ALT) != 0,
        (mods & GLFW_MOD_SUPER) != 0
    };
}

PointerButton pointer_button_from_glfw(int button)
{
    switch (button)
    {
    case GLFW_MOUSE_BUTTON_LEFT:
        return PointerButton::eLeft;
    case GLFW_MOUSE_BUTTON_RIGHT:
        return PointerButton::eRight;
    case GLFW_MOUSE_BUTTON_MIDDLE:
        return PointerButton::eMiddle;
    default:
        return PointerButton::eOther;
    }
}

UiInputAction input_action_from_glfw(int action)
{
    return action == GLFW_RELEASE ?
        UiInputAction::eRelease :
        action == GLFW_REPEAT ? UiInputAction::eRepeat : UiInputAction::ePress;
}

UiKeyMap ui_key_map_from_glfw()
{
    return {
        GLFW_KEY_ESCAPE,
        GLFW_KEY_BACKSPACE,
        GLFW_KEY_DELETE,
        GLFW_KEY_ENTER,
        GLFW_KEY_V,
        GLFW_KEY_U
    };
}

bool glfw_cursor_framebuffer_point(GLFWwindow* window, int renderWidth, int renderHeight, glm::vec2& point)
{
    // Convert window co-ordinates to framebuffer co-ordinates for renderer hit testing
    point = glm::vec2(0.0f);
    if (!window || renderWidth <= 0 || renderHeight <= 0)
    {
        return false;
    }

    int windowWidth = 0;
    int windowHeight = 0;
    if (!glfw_window_size(window, windowWidth, windowHeight))
    {
        return false;
    }

    double cursorX = 0.0;
    double cursorY = 0.0;
    glfwGetCursorPos(window, &cursorX, &cursorY);
    if (cursorX < 0.0 ||
        cursorY < 0.0 ||
        cursorX >= static_cast<double>(windowWidth) ||
        cursorY >= static_cast<double>(windowHeight))
    {
        return false;
    }

    point = {
        static_cast<float>(cursorX * static_cast<double>(renderWidth) / static_cast<double>(windowWidth)),
        static_cast<float>(cursorY * static_cast<double>(renderHeight) / static_cast<double>(windowHeight))
    };
    return true;
}

bool glfw_left_mouse_pressed(GLFWwindow* window)
{
    return window && glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
}

bool glfw_engine_render_size(const Engine* engine, int& width, int& height)
{
    // Engine render size is the common source for UI hit testing
    if (!engine)
    {
        width = 0;
        height = 0;
        return false;
    }

    width = static_cast<int>(engine->render_width());
    height = static_cast<int>(engine->render_height());
    return width > 0 && height > 0;
}

bool glfw_ui_input_has_pointer_capture(const UiInputState& inputState)
{
    // Any active pointer owner should keep receiving updates outside normal hover
    return inputState.draggedPanel != entt::null ||
        inputState.resizingPanel != entt::null ||
        inputState.activeSlider != entt::null ||
        inputState.activeScrollBar != entt::null ||
        inputState.pressedInputEntity != entt::null ||
        inputState.pointerInputCapture != entt::null;
}

GlfwUiPointerSample glfw_ui_pointer_sample(
    GLFWwindow* window,
    const Engine* engine,
    bool pointerEnabled)
{
    GlfwUiPointerSample sample = {};
    glfw_engine_render_size(engine, sample.renderWidth, sample.renderHeight);
    sample.hasPoint = pointerEnabled &&
        glfw_cursor_framebuffer_point(window, sample.renderWidth, sample.renderHeight, sample.point);
    return sample;
}

void glfw_update_ui_drag_inputs(
    GLFWwindow* window,
    Engine& engine,
    UiInputState& inputState,
    const GlfwUiPointerSample& pointer)
{
    // Shared drag path for panels, sliders, and scrollbars
    const bool leftPressed = glfw_left_mouse_pressed(window);
    Renderer2DScene& scene = engine.renderer2d_scene();
    ui_update_panel_resize(
        scene,
        inputState,
        leftPressed,
        pointer.hasPoint,
        pointer.point,
        { static_cast<float>(pointer.renderWidth), static_cast<float>(pointer.renderHeight) });
    ui_update_slider_drag(
        scene,
        engine.renderer2d_font_atlas(),
        inputState,
        leftPressed,
        pointer.hasPoint,
        pointer.point);
    ui_update_scrollbar_drag(
        scene,
        inputState,
        leftPressed,
        pointer.hasPoint,
        pointer.point);
    ui_update_panel_drag(
        scene,
        inputState,
        leftPressed,
        pointer.hasPoint,
        pointer.point,
        { static_cast<float>(pointer.renderWidth), static_cast<float>(pointer.renderHeight) });
}

UiHoverResult glfw_update_ui_hover(
    GLFWwindow* window,
    Engine& engine,
    const GlfwCursorSet& cursors,
    UiInputState& inputState,
    const GlfwUiPointerSample& pointer,
    bool updateCursor)
{
    // Hover uses the renderer hit-test order and returns the cursor requested by the front item
    const UiHoverResult hover = ui_update_input_hover(
        engine.renderer2d_scene(),
        engine.renderer2d_font_atlas(),
        inputState,
        pointer.hasPoint,
        pointer.point);
    if (updateCursor)
    {
        set_glfw_cursor_for_kind(window, cursors, hover.cursor);
    }
    return hover;
}

void glfw_update_ui_timed_controls(
    Engine& engine,
    UiInputState& inputState,
    double currentTimeSeconds)
{
    // Per-frame UI effects live together so windows can share the same maintenance path
    ui_update_text_input_carets(engine.renderer2d_scene(), inputState, currentTimeSeconds);
    ui_update_scrollbar_fade(engine.renderer2d_scene(), currentTimeSeconds);
    ui_update_switch_animations(engine.renderer2d_scene(), currentTimeSeconds);
    ui_update_slider_smoothing(engine.renderer2d_scene(), engine.renderer2d_font_atlas(), currentTimeSeconds);
}

UiPointerButtonInput glfw_pointer_button_input(
    GLFWwindow* window,
    int button,
    int action,
    int mods,
    int renderWidth,
    int renderHeight)
{
    UiPointerButtonInput input = {};
    input.hasPoint = glfw_cursor_framebuffer_point(window, renderWidth, renderHeight, input.point);
    input.button = pointer_button_from_glfw(button);
    input.platformButton = button;
    input.action = input_action_from_glfw(action);
    input.modifiers = input_modifiers_from_glfw(mods);
    return input;
}

UiScrollInput glfw_scroll_input(
    GLFWwindow* window,
    double xoffset,
    double yoffset,
    int renderWidth,
    int renderHeight)
{
    UiScrollInput input = {};
    input.hasPoint = glfw_cursor_framebuffer_point(window, renderWidth, renderHeight, input.point);
    input.offsetX = xoffset;
    input.offsetY = yoffset;
    return input;
}

UiKeyInput glfw_key_input(GLFWwindow* window, int key, int action, int mods)
{
    UiKeyInput input = {};
    input.key = key;
    input.action = input_action_from_glfw(action);
    input.modifiers = input_modifiers_from_glfw(mods);
    input.keys = ui_key_map_from_glfw();

    if (window && action == GLFW_PRESS && input.modifiers.control && key == GLFW_KEY_V)
    {
        const char* clipboard = glfwGetClipboardString(window);
        if (clipboard)
        {
            input.clipboardText = clipboard;
        }
    }

    return input;
}

UiDropInput glfw_drop_input(
    GLFWwindow* window,
    int pathCount,
    const char** paths,
    int renderWidth,
    int renderHeight)
{
    UiDropInput input = {};
    input.hasPoint = glfw_cursor_framebuffer_point(window, renderWidth, renderHeight, input.point);
    if (pathCount > 0 && paths)
    {
        input.paths.reserve(static_cast<std::size_t>(pathCount));
        for (int i = 0; i < pathCount; ++i)
        {
            if (paths[i])
            {
                input.paths.emplace_back(paths[i]);
            }
        }
    }
    return input;
}

void glfw_handle_ui_pointer_button(
    GLFWwindow* window,
    Engine& engine,
    UiInputState& inputState,
    int button,
    int action,
    int mods)
{
    // Converts a GLFW mouse event and dispatches it through reusable UI interactions
    if (action != GLFW_PRESS && action != GLFW_RELEASE)
    {
        return;
    }

    int renderWidth = 0;
    int renderHeight = 0;
    glfw_engine_render_size(&engine, renderWidth, renderHeight);
    const UiPointerButtonInput input = glfw_pointer_button_input(
        window,
        button,
        action,
        mods,
        renderWidth,
        renderHeight);
    ui_handle_pointer_button(
        engine.renderer2d_scene(),
        engine.renderer2d_font_atlas(),
        inputState,
        input);
}

void glfw_handle_ui_scroll(
    GLFWwindow* window,
    Engine& engine,
    double xoffset,
    double yoffset)
{
    int renderWidth = 0;
    int renderHeight = 0;
    glfw_engine_render_size(&engine, renderWidth, renderHeight);
    const UiScrollInput input = glfw_scroll_input(
        window,
        xoffset,
        yoffset,
        renderWidth,
        renderHeight);
    ui_handle_scroll(engine.renderer2d_scene(), input);
}

void glfw_handle_ui_key(
    GLFWwindow* window,
    Engine& engine,
    UiInputState& inputState,
    int key,
    int action,
    int mods)
{
    if (action != GLFW_PRESS && action != GLFW_REPEAT)
    {
        return;
    }

    const UiKeyInput input = glfw_key_input(window, key, action, mods);
    ui_handle_key(
        engine.renderer2d_scene(),
        engine.renderer2d_font_atlas(),
        inputState,
        input);
}

void glfw_handle_ui_char(
    Engine& engine,
    UiInputState& inputState,
    unsigned int codepoint)
{
    ui_append_text_input(
        engine.renderer2d_scene(),
        engine.renderer2d_font_atlas(),
        inputState,
        codepoint);
}

void glfw_handle_ui_drop(
    GLFWwindow* window,
    Engine& engine,
    int pathCount,
    const char** paths)
{
    int renderWidth = 0;
    int renderHeight = 0;
    glfw_engine_render_size(&engine, renderWidth, renderHeight);
    const UiDropInput input = glfw_drop_input(
        window,
        pathCount,
        paths,
        renderWidth,
        renderHeight);
    ui_handle_drop(engine.renderer2d_scene(), input);
}

bool update_glfw_mouse_passthrough(
    GLFWwindow* window,
    bool requested,
    bool& enabled,
    Renderer2DScene& scene,
    int renderWidth,
    int renderHeight,
    const UiInputState& inputState)
{
#ifdef GLFW_MOUSE_PASSTHROUGH
    if (!requested || !window)
    {
        if (window && enabled)
        {
            set_glfw_mouse_passthrough(window, false);
        }
        enabled = false;
        return false;
    }

    if (inputState.draggedPanel != entt::null || inputState.resizingPanel != entt::null)
    {
        if (enabled)
        {
            set_glfw_mouse_passthrough(window, false);
            enabled = false;
        }
        return false;
    }

    glm::vec2 point(0.0f);
    const bool hasPoint = glfw_cursor_framebuffer_point(window, renderWidth, renderHeight, point);
    const bool shouldPassThrough = hasPoint && !scene.hit_test(point);
    if (shouldPassThrough != enabled)
    {
        set_glfw_mouse_passthrough(window, shouldPassThrough);
        enabled = shouldPassThrough;
    }
    return enabled;
#else
    (void)window;
    (void)requested;
    (void)scene;
    (void)renderWidth;
    (void)renderHeight;
    (void)inputState;
    enabled = false;
    return false;
#endif
}

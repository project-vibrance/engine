#include <vibranceUI/glfw/window.h>
#include <vibranceUI/core/logger.h>
#include <vibranceUI/ui/controls.h>
#include <algorithm>
#include <sstream>

namespace
{
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

CameraInput poll_glfw_camera_input(
    GLFWwindow* window,
    double now,
    double& lastFrameTime,
    double& lastMouseX,
    double& lastMouseY,
    bool& hasMousePosition)
{
    CameraInput input = {};
    input.deltaSeconds = static_cast<float>(std::max(0.0, now - lastFrameTime));
    lastFrameTime = now;

    if (!window)
    {
        return input;
    }

    input.forward += glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS ? -1.0f : 0.0f;
    input.forward += glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS ? 1.0f : 0.0f;
    input.right += glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS ? 1.0f : 0.0f;
    input.right += glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS ? -1.0f : 0.0f;
    input.up += glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS ? 1.0f : 0.0f;
    input.up += glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS ? -1.0f : 0.0f;

    double mouseX = 0.0;
    double mouseY = 0.0;
    glfwGetCursorPos(window, &mouseX, &mouseY);
    if (!hasMousePosition)
    {
        lastMouseX = mouseX;
        lastMouseY = mouseY;
        hasMousePosition = true;
    }

    const bool looking = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (looking)
    {
        constexpr float sensitivity = 0.0025f;
        input.yawDelta = static_cast<float>(mouseX - lastMouseX) * sensitivity;
        input.pitchDelta = static_cast<float>(mouseY - lastMouseY) * sensitivity;
    }

    lastMouseX = mouseX;
    lastMouseY = mouseY;
    return input;
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

#include <vibranceUI/glfw/ui_surface.h>
#include <vibranceUI/renderer/renderer.h>

#include <algorithm>
#include <utility>

GlfwUiSurface::GlfwUiSurface(
    GLFWwindow* window,
    Engine* engine,
    GlfwUiSurfaceOptions options)
{
    attach(window, engine, std::move(options));
}

GlfwUiSurface::~GlfwUiSurface()
{
    detach();
}

bool GlfwUiSurface::attach(
    GLFWwindow* nextWindow,
    Engine* nextEngine,
    GlfwUiSurfaceOptions options)
{
    detach();
    if (!nextWindow || !nextEngine)
    {
        return false;
    }
    window = nextWindow;
    surfaceEngine = nextEngine;
    surfaceOptions = std::move(options);
    cursors = create_glfw_standard_cursors();
    set_glfw_window_user_pointer(window, this);

    GlfwCallbackSet callbacks = {};
    callbacks.framebufferSize = framebuffer_size_callback;
    callbacks.contentScale = content_scale_callback;
    callbacks.mouseButton = mouse_button_callback;
    callbacks.scroll = scroll_callback;
    callbacks.key = key_callback;
    callbacks.character = char_callback;
    callbacks.drop = drop_callback;
    set_glfw_callbacks(window, callbacks);
    request_rebuild();
    rebuild_if_needed();
    return true;
}

void GlfwUiSurface::detach()
{
    if (window && mousePassthroughEnabled)
    {
        set_glfw_mouse_passthrough(window, false);
    }
    mousePassthroughEnabled = false;
    if (window)
    {
        clear_glfw_cursor(window);
        set_glfw_callbacks(window, {});
        set_glfw_window_user_pointer(window, nullptr);
    }
    destroy_glfw_standard_cursors(cursors);
    inputState.clear_capture();
    inputState.clear_hover();
    inputState.clear_focus();
    window = nullptr;
    surfaceEngine = nullptr;
    lastRenderWidth = -1;
    lastRenderHeight = -1;
    rebuildRequested = true;
    lastTickSeconds = -1.0;
}

void GlfwUiSurface::set_options(GlfwUiSurfaceOptions options)
{
    surfaceOptions = std::move(options);
}

void GlfwUiSurface::request_rebuild()
{
    rebuildRequested = true;
    lastRenderWidth = -1;
    lastRenderHeight = -1;
}

void GlfwUiSurface::tick()
{
    if (!window || !surfaceEngine || glfw_window_should_close(window))
    {
        return;
    }

    const double now = glfw_time_seconds();
    const double delta = lastTickSeconds < 0.0 ?
        0.0 : std::max(now - lastTickSeconds, 0.0);
    lastTickSeconds = now;
    surfaceEngine->update_timing(now);

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    if (glfw_framebuffer_size(window, framebufferWidth, framebufferHeight))
    {
        surfaceEngine->resize(
            static_cast<uint32_t>(std::max(framebufferWidth, 0)),
            static_cast<uint32_t>(std::max(framebufferHeight, 0)));
    }
    rebuild_if_needed();

    const bool pointerEnabled = glfw_ui_input_has_pointer_capture(inputState) ||
        (surfaceOptions.pointerInputEnabled ?
            surfaceOptions.pointerInputEnabled() :
            (glfw_window_focused(window) || glfw_window_hovered(window)));
    const GlfwUiPointerSample pointer = glfw_ui_pointer_sample(
        window,
        surfaceEngine,
        pointerEnabled);
    glfw_update_ui_timed_controls(*surfaceEngine, inputState, now);
    glfw_update_ui_hover(
        window,
        *surfaceEngine,
        cursors,
        inputState,
        pointer,
        surfaceOptions.hoverWhenUnfocused);
    if (pointerEnabled)
    {
        glfw_update_ui_drag_inputs(window, *surfaceEngine, inputState, pointer);
    }

    if (surfaceOptions.update)
    {
        surfaceOptions.update(now, delta);
    }
    update_mouse_passthrough();
    surfaceEngine->draw();
}

bool GlfwUiSurface::attached() const
{
    return window && surfaceEngine;
}

bool GlfwUiSurface::should_close() const
{
    return !window || glfw_window_should_close(window);
}

UiInputState& GlfwUiSurface::input_state()
{
    return inputState;
}

const UiInputState& GlfwUiSurface::input_state() const
{
    return inputState;
}

GLFWwindow* GlfwUiSurface::native_window() const
{
    return window;
}

Engine* GlfwUiSurface::engine() const
{
    return surfaceEngine;
}

void GlfwUiSurface::framebuffer_size_callback(
    GLFWwindow* window,
    int width,
    int height)
{
    auto* self = static_cast<GlfwUiSurface*>(glfw_window_user_pointer(window));
    if (!self || !self->surfaceEngine)
    {
        return;
    }
    self->surfaceEngine->resize(
        static_cast<uint32_t>(std::max(width, 0)),
        static_cast<uint32_t>(std::max(height, 0)));
    self->request_rebuild();
}

void GlfwUiSurface::content_scale_callback(GLFWwindow* window, float, float)
{
    auto* self = static_cast<GlfwUiSurface*>(glfw_window_user_pointer(window));
    if (self)
    {
        self->request_rebuild();
    }
}

void GlfwUiSurface::mouse_button_callback(
    GLFWwindow* window,
    int button,
    int action,
    int mods)
{
    auto* self = static_cast<GlfwUiSurface*>(glfw_window_user_pointer(window));
    if (self && self->surfaceEngine)
    {
        glfw_handle_ui_pointer_button(
            window,
            *self->surfaceEngine,
            self->inputState,
            button,
            action,
            mods);
    }
}

void GlfwUiSurface::scroll_callback(
    GLFWwindow* window,
    double xoffset,
    double yoffset)
{
    auto* self = static_cast<GlfwUiSurface*>(glfw_window_user_pointer(window));
    if (self && self->surfaceEngine)
    {
        glfw_handle_ui_scroll(
            window,
            *self->surfaceEngine,
            xoffset,
            yoffset);
    }
}

void GlfwUiSurface::key_callback(
    GLFWwindow* window,
    int key,
    int scancode,
    int action,
    int mods)
{
    auto* self = static_cast<GlfwUiSurface*>(glfw_window_user_pointer(window));
    if (!self || !self->surfaceEngine)
    {
        return;
    }
    if (self->surfaceOptions.onKey)
    {
        self->surfaceOptions.onKey(key, scancode, action, mods);
    }
    glfw_handle_ui_key(
        window,
        *self->surfaceEngine,
        self->inputState,
        key,
        action,
        mods);
}

void GlfwUiSurface::char_callback(GLFWwindow* window, unsigned int codepoint)
{
    auto* self = static_cast<GlfwUiSurface*>(glfw_window_user_pointer(window));
    if (self && self->surfaceEngine)
    {
        glfw_handle_ui_char(*self->surfaceEngine, self->inputState, codepoint);
    }
}

void GlfwUiSurface::drop_callback(
    GLFWwindow* window,
    int count,
    const char** paths)
{
    auto* self = static_cast<GlfwUiSurface*>(glfw_window_user_pointer(window));
    if (self && self->surfaceOptions.onDrop)
    {
        self->surfaceOptions.onDrop(count, paths);
    }
}

void GlfwUiSurface::rebuild_if_needed()
{
    if (!window || !surfaceEngine)
    {
        return;
    }
    int renderWidth = 0;
    int renderHeight = 0;
    if (!glfw_engine_render_size(surfaceEngine, renderWidth, renderHeight))
    {
        return;
    }
    if (!rebuildRequested && renderWidth == lastRenderWidth &&
        renderHeight == lastRenderHeight)
    {
        return;
    }
    rebuildRequested = false;
    lastRenderWidth = renderWidth;
    lastRenderHeight = renderHeight;
    inputState.clear_capture();
    inputState.clear_hover();
    inputState.clear_focus();
    if (surfaceOptions.build)
    {
        surfaceOptions.build();
    }
}

void GlfwUiSurface::update_mouse_passthrough()
{
    if (!window || !surfaceEngine)
    {
        return;
    }
    int renderWidth = 0;
    int renderHeight = 0;
    if (!glfw_engine_render_size(
            surfaceEngine,
            renderWidth,
            renderHeight))
    {
        return;
    }
    update_glfw_mouse_passthrough(
        window,
        surfaceOptions.mousePassthrough,
        mousePassthroughEnabled,
        surfaceEngine->renderer2d_scene(),
        renderWidth,
        renderHeight,
        inputState);
}

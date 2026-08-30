#pragma once

#include <vibranceUI/glfw/window.h>
#include <vibranceUI/ui/interactions.h>

#include <functional>

struct GlfwUiSurfaceOptions
{
    bool mousePassthrough = false;
    bool hoverWhenUnfocused = false;
    std::function<bool()> pointerInputEnabled {};
    std::function<void()> build {};
    std::function<void(double currentTimeSeconds, double deltaSeconds)> update {};
    std::function<void(int key, int scancode, int action, int mods)> onKey {};
    std::function<void(int count, const char** paths)> onDrop {};
};

// Lifecycle controller for a retained UI scene hosted by an existing GLFW
// window/Engine pair. It centralises callbacks, input state, resize/rebuild,
// cursor feedback, passthrough hit testing, timing, and drawing. Product views
// only provide build/update hooks.
class VIBRANCE_GLFW_API GlfwUiSurface
{
public:
    GlfwUiSurface() = default;
    GlfwUiSurface(
        GLFWwindow* window,
        Engine* engine,
        GlfwUiSurfaceOptions options = {});
    ~GlfwUiSurface();

    GlfwUiSurface(const GlfwUiSurface&) = delete;
    GlfwUiSurface& operator=(const GlfwUiSurface&) = delete;
    GlfwUiSurface(GlfwUiSurface&&) = delete;
    GlfwUiSurface& operator=(GlfwUiSurface&&) = delete;

    bool attach(
        GLFWwindow* window,
        Engine* engine,
        GlfwUiSurfaceOptions options = {});
    void detach();
    void set_options(GlfwUiSurfaceOptions options);
    void request_rebuild();
    void tick();

    bool attached() const;
    bool should_close() const;
    UiInputState& input_state();
    const UiInputState& input_state() const;
    GLFWwindow* native_window() const;
    Engine* engine() const;

private:
    static void framebuffer_size_callback(GLFWwindow*, int width, int height);
    static void content_scale_callback(GLFWwindow*, float xscale, float yscale);
    static void mouse_button_callback(GLFWwindow*, int button, int action, int mods);
    static void scroll_callback(GLFWwindow*, double xoffset, double yoffset);
    static void key_callback(GLFWwindow*, int key, int scancode, int action, int mods);
    static void char_callback(GLFWwindow*, unsigned int codepoint);
    static void drop_callback(GLFWwindow*, int count, const char** paths);

    void rebuild_if_needed();
    void update_mouse_passthrough();

    GLFWwindow* window = nullptr;
    Engine* surfaceEngine = nullptr;
    GlfwUiSurfaceOptions surfaceOptions {};
    UiInputState inputState {};
    GlfwCursorSet cursors {};
    int lastRenderWidth = -1;
    int lastRenderHeight = -1;
    bool rebuildRequested = true;
    bool mousePassthroughEnabled = false;
    double lastTickSeconds = -1.0;
};

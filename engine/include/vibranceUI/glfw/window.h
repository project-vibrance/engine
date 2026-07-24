#pragma once

#include "vibranceUI/export.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdint>
#include <filesystem>
#include <vector>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/renderer.h>
#include <vibranceUI/ui/interactions.h>

struct GlfwWindowCreateInfo
{
    // Narrow GLFW-facing creation data kept separate from EngineCreateInfo
    int width = 1280;
    int height = 720;
    const char* name = "vibranceUI";
    bool transparentFramebuffer = false;
    bool decorated = true;
};

struct GlfwCursorSet
{
    // Standard cursors are cached per window owner and destroyed together
    GLFWcursor* text = nullptr;
    GLFWcursor* pointer = nullptr;
    GLFWcursor* unavailable = nullptr;
    GLFWcursor* resizeNwse = nullptr;
    GLFWcursor* resizeNesw = nullptr;
    GLFWcursor* resizeEw = nullptr;
    GLFWcursor* resizeNs = nullptr;
};

struct GlfwCallbackSet
{
    // Passing a set at once keeps callback ownership easy to reset on shutdown
    GLFWframebuffersizefun framebufferSize = nullptr;
    GLFWwindowcontentscalefun contentScale = nullptr;
    GLFWmousebuttonfun mouseButton = nullptr;
    GLFWscrollfun scroll = nullptr;
    GLFWkeyfun key = nullptr;
    GLFWcharfun character = nullptr;
    GLFWdropfun drop = nullptr;
};

struct GlfwUiPointerSample
{
    // Per-frame pointer sample in renderer framebuffer co-ordinates
    int renderWidth = 0;
    int renderHeight = 0;
    bool hasPoint = false;
    glm::vec2 point { 0.0f };
};

VIBRANCE_GLFW_API GLFWwindow* build_glfw_window(const GlfwWindowCreateInfo& createInfo);
VIBRANCE_GLFW_API GLFWwindow* build_glfw_window(int width, int height, const char* name, bool transparent);
VIBRANCE_GLFW_API void destroy_glfw_window(GLFWwindow* window);
VIBRANCE_GLFW_API void terminate_glfw();

VIBRANCE_GLFW_API int vibrance_glfw_create_surface(void* instance, void* userData, void* surfaceOut);
// Wrappers below keep app and engine code from depending on raw GLFW calls
VIBRANCE_GLFW_API const char** glfw_required_instance_extensions(uint32_t& count);
VIBRANCE_GLFW_API bool glfw_framebuffer_size(GLFWwindow* window, int& width, int& height);
VIBRANCE_GLFW_API bool glfw_window_size(GLFWwindow* window, int& width, int& height);
VIBRANCE_GLFW_API bool glfw_window_position(GLFWwindow* window, int& x, int& y);
VIBRANCE_GLFW_API bool glfw_cursor_window_point(GLFWwindow* window, glm::vec2& point);
VIBRANCE_GLFW_API bool glfw_window_focused(GLFWwindow* window);
VIBRANCE_GLFW_API bool glfw_window_hovered(GLFWwindow* window);
VIBRANCE_GLFW_API glm::vec2 glfw_content_scale(GLFWwindow* window);
VIBRANCE_GLFW_API double glfw_time_seconds();
VIBRANCE_GLFW_API bool glfw_window_should_close(GLFWwindow* window);
VIBRANCE_GLFW_API void poll_glfw_events();
VIBRANCE_GLFW_API void set_glfw_window_title(GLFWwindow* window, const char* title);
VIBRANCE_GLFW_API void set_glfw_window_position(GLFWwindow* window, int x, int y);
VIBRANCE_GLFW_API void set_glfw_window_size(GLFWwindow* window, int width, int height);
VIBRANCE_GLFW_API void set_glfw_window_should_close(GLFWwindow* window, bool shouldClose);
VIBRANCE_GLFW_API void* glfw_native_window_handle(GLFWwindow* window);
VIBRANCE_GLFW_API bool begin_glfw_native_window_drag(GLFWwindow* window);
VIBRANCE_GLFW_API void iconify_glfw_window(GLFWwindow* window);
VIBRANCE_GLFW_API void maximize_glfw_window(GLFWwindow* window);
VIBRANCE_GLFW_API void restore_glfw_window(GLFWwindow* window);
VIBRANCE_GLFW_API bool glfw_window_maximized(GLFWwindow* window);
VIBRANCE_GLFW_API void set_glfw_window_user_pointer(GLFWwindow* window, void* userPointer);
VIBRANCE_GLFW_API void* glfw_window_user_pointer(GLFWwindow* window);
VIBRANCE_GLFW_API void set_glfw_mouse_passthrough(GLFWwindow* window, bool enabled);
VIBRANCE_GLFW_API void set_glfw_callbacks(GLFWwindow* window, const GlfwCallbackSet& callbacks);

VIBRANCE_GLFW_API GlfwCursorSet create_glfw_standard_cursors();
VIBRANCE_GLFW_API void destroy_glfw_standard_cursors(GlfwCursorSet& cursors);
VIBRANCE_GLFW_API GLFWcursor* glfw_cursor_for_kind(const GlfwCursorSet& cursors, UiCursorKind cursorKind);
VIBRANCE_GLFW_API void clear_glfw_cursor(GLFWwindow* window);
VIBRANCE_GLFW_API void set_glfw_cursor_for_kind(GLFWwindow* window, const GlfwCursorSet& cursors, UiCursorKind cursorKind);

VIBRANCE_GLFW_API InputModifiers input_modifiers_from_glfw(int mods);
// These adapters translate GLFW events into the engine's reusable UI input types
VIBRANCE_GLFW_API PointerButton pointer_button_from_glfw(int button);
VIBRANCE_GLFW_API UiInputAction input_action_from_glfw(int action);
VIBRANCE_GLFW_API UiKeyMap ui_key_map_from_glfw();
VIBRANCE_GLFW_API bool glfw_cursor_framebuffer_point(
    GLFWwindow* window,
    int renderWidth,
    int renderHeight,
    glm::vec2& point);
VIBRANCE_GLFW_API bool glfw_left_mouse_pressed(GLFWwindow* window);
VIBRANCE_GLFW_API bool glfw_engine_render_size(const Engine* engine, int& width, int& height);
VIBRANCE_GLFW_API bool glfw_ui_input_has_pointer_capture(const UiInputState& inputState);
VIBRANCE_GLFW_API GlfwUiPointerSample glfw_ui_pointer_sample(
    GLFWwindow* window,
    const Engine* engine,
    bool pointerEnabled = true);
VIBRANCE_GLFW_API void glfw_update_ui_drag_inputs(
    GLFWwindow* window,
    Engine& engine,
    UiInputState& inputState,
    const GlfwUiPointerSample& pointer);
VIBRANCE_GLFW_API UiHoverResult glfw_update_ui_hover(
    GLFWwindow* window,
    Engine& engine,
    const GlfwCursorSet& cursors,
    UiInputState& inputState,
    const GlfwUiPointerSample& pointer,
    bool updateCursor = true);
VIBRANCE_GLFW_API void glfw_update_ui_timed_controls(
    Engine& engine,
    UiInputState& inputState,
    double currentTimeSeconds);

VIBRANCE_GLFW_API UiPointerButtonInput glfw_pointer_button_input(
    GLFWwindow* window,
    int button,
    int action,
    int mods,
    int renderWidth,
    int renderHeight);
VIBRANCE_GLFW_API UiScrollInput glfw_scroll_input(
    GLFWwindow* window,
    double xoffset,
    double yoffset,
    int renderWidth,
    int renderHeight);
VIBRANCE_GLFW_API UiKeyInput glfw_key_input(GLFWwindow* window, int key, int action, int mods);
VIBRANCE_GLFW_API UiDropInput glfw_drop_input(
    GLFWwindow* window,
    int pathCount,
    const char** paths,
    int renderWidth,
    int renderHeight);
VIBRANCE_GLFW_API void glfw_handle_ui_pointer_button(
    GLFWwindow* window,
    Engine& engine,
    UiInputState& inputState,
    int button,
    int action,
    int mods);
VIBRANCE_GLFW_API void glfw_handle_ui_scroll(
    GLFWwindow* window,
    Engine& engine,
    double xoffset,
    double yoffset);
VIBRANCE_GLFW_API void glfw_handle_ui_key(
    GLFWwindow* window,
    Engine& engine,
    UiInputState& inputState,
    int key,
    int action,
    int mods);
VIBRANCE_GLFW_API void glfw_handle_ui_char(
    Engine& engine,
    UiInputState& inputState,
    unsigned int codepoint);
VIBRANCE_GLFW_API void glfw_handle_ui_drop(
    GLFWwindow* window,
    Engine& engine,
    int pathCount,
    const char** paths);

VIBRANCE_GLFW_API CameraInput poll_glfw_camera_input(
    GLFWwindow* window,
    double now,
    double& lastFrameTime,
    double& lastMouseX,
    double& lastMouseY,
    bool& hasMousePosition);

VIBRANCE_GLFW_API bool update_glfw_mouse_passthrough(
    GLFWwindow* window,
    bool requested,
    bool& enabled,
    Renderer2DScene& scene,
    int renderWidth,
    int renderHeight,
    const UiInputState& inputState);

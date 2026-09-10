#pragma once

#include "vibranceUI/export.h"
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
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
    // DirectComposition top-level windows must opt out of the DWM
    // redirection bitmap while the HWND is being created. This is kept
    // separate from transparentFramebuffer so native Vulkan windows retain
    // GLFW's normal per-pixel transparency path.
    bool windowsCompositionSurface = false;
    bool decorated = true;
    bool alwaysOnTop = false;
    // Initial desktop-shell behavior is applied before the window is first
    // shown so overlays do not flash, steal focus, or briefly enter switchers.
    bool focusOnShow = true;
    bool showInTaskbar = true;
    bool showInAltTab = true;
    // When supplied, the window is created hidden, positioned, and only then
    // shown. This prevents the default-position flash common to secondary UI.
    std::optional<glm::ivec2> position {};
};

struct GlfwMonitorInfo
{
    GLFWmonitor* handle = nullptr;
    // Stable platform identity used for persisted monitor selections.
    std::string id;
    std::string name;
    glm::ivec2 position { 0 };
    glm::ivec2 size { 0 };
    glm::ivec2 workPosition { 0 };
    glm::ivec2 workSize { 0 };
    glm::vec2 contentScale { 1.0f };
    bool primary = false;
};

enum class GlfwWindowAlignment : std::uint8_t
{
    eTopLeft,
    eTopCenter,
    eTopRight,
    eMiddleLeft,
    eCenter,
    eMiddleRight,
    eBottomLeft,
    eBottomCenter,
    eBottomRight
};

struct GlfwWindowPositionOptions
{
    // Empty monitorId and referencePoint select the primary monitor. A stable
    // id wins over the reference point when both are supplied.
    GlfwWindowAlignment alignment = GlfwWindowAlignment::eCenter;
    std::string monitorId {};
    std::optional<glm::ivec2> referencePoint {};
    // left, top, right, bottom inset inside the selected work area.
    glm::ivec4 margins { 0 };
    glm::ivec2 offset { 0 };
};

struct GlfwWindowPlacement
{
    glm::ivec2 position { 0 };
    glm::ivec2 size { 1 };
    bool decorated = true;
    bool alwaysOnTop = false;
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

using GlfwWindowEngineConfigurator = std::function<void(EngineCreateInfo&)>;

struct GlfwWindowHostOptions
{
    // The common window + renderer settings live together so callers do not
    // have to manually copy native surface details into EngineCreateInfo.
    std::string title = "vibranceUI";
    glm::ivec2 size { 1280, 720 };
    std::optional<glm::ivec2> position {};
    std::optional<GlfwWindowPositionOptions> positioning {};
    std::optional<GlfwWindowPlacement> placement {};
    bool transparentFramebuffer = false;
    bool decorated = true;
    bool alwaysOnTop = false;
    bool focusOnShow = true;
    bool showInTaskbar = true;
    bool showInAltTab = true;
    bool enableAudio = true;
    uint32_t maxRenderPixels = 0;
    uint32_t msaaSamples = 4;
    RendererPresentMode presentMode = RendererPresentMode::eAuto;
    RenderBackend renderBackend = RenderBackend::eVulkan;
    // Empty selects native presentation, except transparent Win32 windows,
    // which request Composition and safely fall back inside Engine.
    std::optional<PresentationBackend> presentationBackend {};
    uint32_t targetFrameRate = 0;
    GlfwWindowEngineConfigurator configureEngine {};
};

// Owns the native window and its surface-bound Engine in the required order.
// Interface/controller objects should be destroyed before close() is called.
class VIBRANCE_GLFW_API GlfwWindowHost
{
public:
    explicit GlfwWindowHost(GlfwWindowHostOptions options = {});
    ~GlfwWindowHost();

    GlfwWindowHost(const GlfwWindowHost&) = delete;
    GlfwWindowHost& operator=(const GlfwWindowHost&) = delete;
    GlfwWindowHost(GlfwWindowHost&& other) noexcept;
    GlfwWindowHost& operator=(GlfwWindowHost&& other) noexcept;

    void set_options(GlfwWindowHostOptions options);
    const GlfwWindowHostOptions& options() const;

    bool open();
    void close();
    bool is_open() const;

    GLFWwindow* window() const;
    Engine* engine() const;

private:
    GlfwWindowHostOptions hostOptions {};
    GLFWwindow* hostedWindow = nullptr;
    std::unique_ptr<Engine> hostedEngine {};
};

VIBRANCE_GLFW_API GLFWwindow* build_glfw_window(const GlfwWindowCreateInfo& createInfo);
VIBRANCE_GLFW_API GLFWwindow* build_glfw_window(int width, int height, const char* name, bool transparent);
VIBRANCE_GLFW_API void destroy_glfw_window(GLFWwindow* window);
VIBRANCE_GLFW_API void terminate_glfw();
VIBRANCE_GLFW_API std::vector<GlfwMonitorInfo> glfw_connected_monitors();
VIBRANCE_GLFW_API std::string glfw_monitor_identifier(GLFWmonitor* monitor);
// Pure geometry helper for tests and callers that already own an area.
VIBRANCE_GLFW_API glm::ivec2 glfw_aligned_window_position(
    glm::ivec2 areaPosition,
    glm::ivec2 areaSize,
    glm::ivec2 windowSize,
    GlfwWindowAlignment alignment = GlfwWindowAlignment::eCenter,
    glm::ivec4 margins = glm::ivec4(0),
    glm::ivec2 offset = glm::ivec2(0));
// Fits a requested logical window size into an inset monitor work area.
VIBRANCE_GLFW_API glm::ivec2 glfw_fitted_window_size(
    glm::ivec2 areaSize,
    glm::ivec2 requestedSize,
    glm::ivec4 margins = glm::ivec4(0));
// Resolves the target monitor using the same policy as window positioning.
VIBRANCE_GLFW_API std::optional<glm::ivec2>
resolve_glfw_window_size(
    glm::ivec2 requestedSize,
    const GlfwWindowPositionOptions& options = {});
// Resolves a reusable policy against connected monitor work areas.
VIBRANCE_GLFW_API std::optional<glm::ivec2>
resolve_glfw_window_position(
    glm::ivec2 windowSize,
    const GlfwWindowPositionOptions& options = {});
VIBRANCE_GLFW_API bool glfw_screen_cursor_position(
    GLFWwindow* referenceWindow,
    glm::ivec2& position);

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
VIBRANCE_GLFW_API void focus_glfw_window(GLFWwindow* window);
VIBRANCE_GLFW_API void set_glfw_window_visible(
    GLFWwindow* window,
    bool visible);
VIBRANCE_GLFW_API void set_glfw_window_title(GLFWwindow* window, const char* title);
VIBRANCE_GLFW_API void set_glfw_window_decorated(
    GLFWwindow* window,
    bool decorated);
VIBRANCE_GLFW_API void set_glfw_window_always_on_top(
    GLFWwindow* window,
    bool alwaysOnTop);
VIBRANCE_GLFW_API void set_glfw_window_application_presence(
    GLFWwindow* window,
    bool showInTaskbar,
    bool showInAltTab);
VIBRANCE_GLFW_API bool apply_glfw_window_placement(
    GLFWwindow* window,
    const GlfwWindowPlacement& placement);
VIBRANCE_GLFW_API void set_glfw_window_position(GLFWwindow* window, int x, int y);
VIBRANCE_GLFW_API void set_glfw_window_size(GLFWwindow* window, int width, int height);
VIBRANCE_GLFW_API void set_glfw_window_should_close(GLFWwindow* window, bool shouldClose);
VIBRANCE_GLFW_API void* glfw_native_window_handle(GLFWwindow* window);
VIBRANCE_GLFW_API bool begin_glfw_native_window_drag(GLFWwindow* window);
VIBRANCE_GLFW_API void iconify_glfw_window(GLFWwindow* window);
VIBRANCE_GLFW_API void maximize_glfw_window(GLFWwindow* window);
VIBRANCE_GLFW_API void restore_glfw_window(GLFWwindow* window);
VIBRANCE_GLFW_API bool glfw_window_maximized(GLFWwindow* window);
VIBRANCE_GLFW_API bool glfw_window_monitor_work_area(
    GLFWwindow* window,
    glm::ivec2& position,
    glm::ivec2& size);
VIBRANCE_GLFW_API bool set_glfw_window_fullscreen(
    GLFWwindow* window,
    bool fullscreen,
    glm::ivec2 windowedPosition = glm::ivec2(0),
    glm::ivec2 windowedSize = glm::ivec2(1));
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

VIBRANCE_GLFW_API bool update_glfw_mouse_passthrough(
    GLFWwindow* window,
    bool requested,
    bool& enabled,
    Renderer2DScene& scene,
    int renderWidth,
    int renderHeight,
    const UiInputState& inputState);

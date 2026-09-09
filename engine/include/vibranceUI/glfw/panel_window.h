#pragma once

#include <vibranceUI/core/logger.h>
#include <vibranceUI/glfw/window.h>
#include <vibranceUI/renderer/renderer.h>
#include <vibranceUI/ui/interactions.h>
#include <vibranceUI/ui/layout.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/surfaces.h>
#include <array>
#include <cstddef>
#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class UiBuilder;
struct LayoutScale;

struct GlfwPanelWindowTemplateOptions;
struct GlfwPanelWindowTemplateContext;
using GlfwPanelWindowContentBuilder = std::function<void(GlfwPanelWindowTemplateContext&)>;
using GlfwPanelWindowTemplateFactory = std::function<GlfwPanelWindowTemplateOptions(Engine&)>;
using GlfwPanelWindowEngineConfigurator = std::function<void(EngineCreateInfo&)>;

enum class GlfwPanelWindowTrafficLightPlacement
{
    eTopLeft,
    eTopRight
};

enum class GlfwPanelWindowMaximiseMode
{
    // Fullscreen attaches the GLFW window to the monitor. The remaining modes
    // preserve the windowed surface and use the monitor work area.
    eFullscreen,
    eWidth,
    eHeight,
    eWidthAndHeight
};

struct GlfwPanelWindowTrafficLightOptions
{
    // Visibility controls chrome layout; accessibility controls all pointer
    // interaction while retaining a disabled visual when the lights are shown.
    bool visible = true;
    bool accessible = true;
    bool minimiseEnabled = true;
    bool maximiseEnabled = true;
    GlfwPanelWindowMaximiseMode maximiseMode =
        GlfwPanelWindowMaximiseMode::eWidthAndHeight;
    GlfwPanelWindowTrafficLightPlacement placement = GlfwPanelWindowTrafficLightPlacement::eTopLeft;
    glm::vec2 firstCenter { 28.0f, 32.0f };
    float spacing = 22.0f;
    float radius = 7.0f;
    std::string closeColor = "rgba(255, 90, 98, 1)";
    std::string minimiseColor = "rgba(255, 189, 46, 1)";
    std::string maximiseColor = "rgba(41, 200, 64, 1)";
    std::string disabledColor = "rgba(89, 99, 109, 0.58)";
    std::string outlineColor = "rgba(0, 0, 0, 0)";
    float outlineWidth = 0.0f;
    float edgeSoftness = 0.5f;
    bool showIconsOnHover = true;
    // Each list is ordered from user override to packaged fallback.
    std::vector<std::filesystem::path> closeIconCandidates {};
    std::vector<std::filesystem::path> minimiseIconCandidates {};
    std::vector<std::filesystem::path> maximiseIconCandidates {};
    glm::vec2 iconSize { 8.0f, 8.0f };
    std::string iconTint = "rgba(17, 17, 17, 0.85)";
};

struct GlfwPanelWindowTemplateOptions
{
    // Template options define the reusable window chrome and the content slot
    std::string title = "Template Window";
    std::string subtitle = "";
    std::filesystem::path localisationDirectory {};
    std::filesystem::path fontPath {};
    std::filesystem::path fontDirectory {};
    std::filesystem::path fontProfilePath {};
    std::vector<std::filesystem::path> fallbackFontPaths {};
    int fontWeight = 0;
    bool requireLocalisedGlyphs = true;
    bool useLocalePrimaryFont = true;
    UiTheme theme {};
    Renderer2DPrimitive panelPrimitive = Renderer2DPrimitive::eSquircle;
    std::optional<ShapeStyleComponent> panelStyle {};
    std::vector<UiPanelBackgroundLayer> panelBackgroundLayers {};
    bool panelBackgroundLayersReplacePanelFill = false;
    std::optional<ShadowComponent> panelShadow {};
    std::optional<TextStyleComponent> titleStyle;
    std::optional<TextStyleComponent> subtitleStyle;
    std::optional<std::size_t> titleBackgroundLayerIndex {};
    UiAlignment titleAlignment = UiAlignment::eMiddleLeft;
    glm::vec2 titleOffset { 0.0f };
    float titleFontSize = 17.0f;
    glm::ivec2 initialSize { 0, 0 };
    glm::ivec2 minSize { 340, 220 };
    glm::ivec2 maxSize { 1600, 1200 };
    float titleHeight = 58.0f;
    float resizeEdgeThickness = 12.0f;
    float cornerRadius = 28.0f;
    float panelSquircleAmount = 1.0f;
    float panelSquirclePower = 3.65f;
    glm::vec4 panelInset { 0.0f };
    glm::vec4 contentMargin { 24.0f, 72.0f, 24.0f, 24.0f };
    bool resizable = true;
    bool draggable = true;
    // Overrides the interactive top strip without changing title/content
    // layout. Null keeps the legacy derived title region; zero disables it.
    std::optional<float> topDragHeight {};
    bool closeOnEscape = true;
    bool showTitleText = true;
    GlfwPanelWindowTrafficLightOptions trafficLights {};
    GlfwPanelWindowContentBuilder buildContent;
};

struct GlfwPanelWindowTemplateContext
{
    // Passed to app builders so they can draw inside the template without owning it
    UiBuilder& ui;
    Renderer2DScene& scene;
    const Renderer2DFontAtlas& fontAtlas;
    const Localisation& localisation;
    entt::entity panel = entt::null;
    entt::entity titleBar = entt::null;
    entt::entity titleText = entt::null;
    entt::entity contentRoot = entt::null;
    std::vector<entt::entity> panelBackgroundLayers {};
    glm::vec2 windowSize { 0.0f };
    glm::vec2 contentSize { 0.0f };
    const GlfwPanelWindowTemplateOptions& options;
};

class VIBRANCE_GLFW_API GlfwPanelWindow
{
public:
    GlfwPanelWindow(GLFWwindow* window, Engine* engine, GlfwPanelWindowTemplateOptions options = {});
    ~GlfwPanelWindow();

    bool should_close() const;
    bool is_hovered() const;
    void tick();

    const GlfwPanelWindowTemplateOptions& template_options() const;
    // Replaces chrome, theme, localisation, and content options in one rebuild
    void set_template_options(GlfwPanelWindowTemplateOptions nextOptions);
    // Keeps the current chrome while swapping only the content builder
    void set_content_builder(GlfwPanelWindowContentBuilder builder);
    void set_size(int width, int height);
    // Forces a full scene rebuild after external state has changed
    void rebuild();

private:
    enum ResizeEdge : uint32_t
    {
        eResizeNone = 0u,
        eResizeLeft = 1u << 0u,
        eResizeRight = 1u << 1u,
        eResizeTop = 1u << 2u,
        eResizeBottom = 1u << 3u
    };

    enum class TrafficLightKind
    {
        eNone,
        eClose,
        eMinimise,
        eMaximise
    };

    enum class WindowPointerAction
    {
        eNone,
        eMove,
        eResize
    };

    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height);
    static void content_scale_callback(GLFWwindow* window, float xscale, float yscale);
    static void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
    static void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
    static void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void char_callback(GLFWwindow* window, unsigned int codepoint);

    void apply_initial_size();
    void load_template_font();
    // Recreates the root panel, chrome, background columns, and app content
    void build_scene();
    void build_default_content(GlfwPanelWindowTemplateContext& context);
    void load_traffic_light_icons();
    bool handle_window_action_press(const UiPointerButtonInput& input);
    bool begin_window_action(glm::vec2 framebufferPoint);
    void clear_window_action();
    void handle_mouse_button(int button, int action, int mods);
    void handle_scroll(double xoffset, double yoffset);
    void handle_key(int key, int scancode, int action, int mods);
    void handle_char(unsigned int codepoint);
    void update_window_action();
    void update_hover_cursor();

    bool cursor_framebuffer_point(glm::vec2& point) const;
    bool cursor_screen_point(glm::vec2& point) const;
    uint32_t resize_edges_at(glm::vec2 windowPoint) const;
    bool title_hit_test(glm::vec2 windowPoint) const;
    bool scene_blocks_window_action(glm::vec2 framebufferPoint) const;
    TrafficLightKind traffic_light_at(glm::vec2 windowPoint) const;
    glm::vec2 traffic_light_center(TrafficLightKind kind) const;
    bool traffic_light_enabled(TrafficLightKind kind) const;
    Media2DHandle traffic_light_icon(TrafficLightKind kind) const;
    void set_hovered_traffic_light(TrafficLightKind kind);
    void perform_traffic_light_action(TrafficLightKind kind);
    void restore_traffic_light_maximise();
    UiCursorKind cursor_for_resize_edges(uint32_t edges) const;

    GLFWwindow* window = nullptr;
    Engine* engine = nullptr;
    Logger* logger = nullptr;
    GlfwCursorSet cursors {};
    GlfwPanelWindowTemplateOptions options {};
    UiInputState inputState {};
    std::array<Media2DHandle, 3> trafficLightIcons {};
    std::array<entt::entity, 3> trafficLightIconEntities {
        entt::null,
        entt::null,
        entt::null
    };
    TrafficLightKind hoveredTrafficLight = TrafficLightKind::eNone;
    bool trafficLightMaximiseActive = false;
    GlfwPanelWindowMaximiseMode activeTrafficLightMaximiseMode =
        GlfwPanelWindowMaximiseMode::eWidthAndHeight;
    glm::ivec2 trafficLightRestorePosition { 0 };
    glm::ivec2 trafficLightRestoreSize { 0 };

    WindowPointerAction windowPointerAction = WindowPointerAction::eNone;
    uint32_t activeResizeEdges = eResizeNone;
    glm::vec2 actionStartCursorScreen { 0.0f };
    glm::ivec2 actionStartWindowPosition { 0 };
    glm::ivec2 actionStartWindowSize { 0 };
    int lastRenderWidth = 0;
    int lastRenderHeight = 0;
    double lastUiUpdateSeconds = 0.0;
    bool hasUiUpdateSample = false;
};

struct GlfwPanelWindowHostOptions
{
    // Owns a complete secondary panel window stack without making the app repeat lifecycle code
    int width = 1000;
    int height = 850;
    int x = 80;
    int y = 80;
    bool hasInitialPosition = true;
    std::optional<GlfwWindowPositionOptions> positioning {};
    // Clamp the initial logical size to the selected monitor work area. This
    // protects fixed-size panels from small displays and mixed-DPI layouts.
    bool fitToWorkArea = false;
    glm::ivec4 workAreaMargins { 0 };
    bool transparentFramebuffer = true;
    bool decorated = false;
    bool alwaysOnTop = false;
    bool focusOnShow = true;
    bool showInTaskbar = true;
    bool showInAltTab = true;
    bool enableAudio = false;
    uint32_t maxRenderPixels = 0;
    uint32_t msaaSamples = 4;
    RendererPresentMode presentMode = RendererPresentMode::eAuto;
    RenderBackend renderBackend = RenderBackend::eVulkan;
    PresentationBackend presentationBackend = PresentationBackend::eNative;
    uint32_t targetFrameRate = 0;
    // Host-level override applied after makeTemplateOptions. This lets window
    // owners opt into a top drag strip without modifying reusable content.
    std::optional<float> topDragHeight {};
    std::string title = "vibranceUI panel window";
    GlfwPanelWindowEngineConfigurator configureEngine;
    GlfwPanelWindowTemplateFactory makeTemplateOptions;
};

class VIBRANCE_GLFW_API GlfwPanelWindowHost
{
public:
    explicit GlfwPanelWindowHost(GlfwPanelWindowHostOptions options = {});
    ~GlfwPanelWindowHost();

    void set_options(GlfwPanelWindowHostOptions options);
    const GlfwPanelWindowHostOptions& options() const;

    void request_open();
    bool open();
    void close();
    void tick();

    bool is_open() const;
    bool should_close() const;
    bool is_hovered() const;
    // Re-runs the configured template factory and reapplies host overrides.
    // Prefer this when host policies such as topDragHeight must remain active.
    bool refresh_template();

    GLFWwindow* window() const;
    Engine* engine() const;
    GlfwPanelWindow* panel() const;

private:
    Engine* build_engine_for_window();
    GlfwPanelWindowTemplateOptions make_template_options();

    GlfwPanelWindowHostOptions hostOptions {};
    bool openRequested = false;
    GLFWwindow* hostedWindow = nullptr;
    Engine* hostedEngine = nullptr;
    std::unique_ptr<GlfwPanelWindow> hostedPanel;
};

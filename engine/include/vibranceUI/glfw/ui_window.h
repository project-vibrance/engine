#pragma once

#include <vibranceUI/glfw/panel_window.h>
#include <vibranceUI/ui/builder.h>

#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <vector>

struct UiWindowContext
{
    UiBuilder& ui;
    Engine& engine;
    const Renderer2DFontAtlas& fontAtlas;
    const Localisation& localisation;
    entt::entity root = entt::null;
    glm::vec2 windowSize { 0.0f };
    glm::vec2 contentSize { 0.0f };
};

using UiWindowContentBuilder = std::function<void(UiWindowContext&)>;
using UiWindowPanelConfigurator =
    std::function<void(GlfwPanelWindowTemplateOptions&)>;

struct UiWindowOptions
{
    // A compact path for normal application windows. The lower-level panel
    // and GLFW options remain available through the two configurators.
    std::string title = "vibranceUI";
    glm::ivec2 size { 900, 640 };
    std::optional<glm::ivec2> position {};
    std::optional<GlfwWindowPositionOptions> positioning {};
    glm::ivec2 minSize { 320, 220 };
    glm::ivec2 maxSize { 0, 0 };
    glm::vec4 contentMargin { 24.0f, 72.0f, 24.0f, 24.0f };
    bool transparentFramebuffer = true;
    bool alwaysOnTop = false;
    bool focusOnShow = true;
    bool showInTaskbar = true;
    bool showInAltTab = true;
    bool resizable = true;
    bool draggable = true;
    bool closeOnEscape = true;
    bool showTitle = true;
    bool showWindowControls = true;
    bool enableAudio = false;
    uint32_t targetFrameRate = 0;
    uint32_t maxRenderPixels = 0;
    uint32_t msaaSamples = 1;
    std::optional<PresentationBackend> presentationBackend {};

    // Set this once and use view.fontAtlas in the build callback. UiWindow
    // loads the font and refreshes renderer bindings automatically.
    std::filesystem::path fontPath {};
    std::filesystem::path fontDirectory {};
    std::filesystem::path fontProfilePath {};
    std::vector<std::filesystem::path> fallbackFontPaths {};
    // Selects a static face from collections such as Inter.ttc. Zero leaves
    // collection face selection at its legacy default.
    int fontWeight = 0;
    // False keeps fontPath/profile default as the Roman-text primary and uses
    // locale/script faces only as per-glyph fallbacks.
    bool useLocalePrimaryFont = true;
    std::filesystem::path localisationDirectory {};
    UiTheme theme {};

    GlfwWindowEngineConfigurator configureEngine {};
    UiWindowPanelConfigurator configurePanel {};
    UiWindowContentBuilder build {};
};

// Complete single-window application facade. It owns window, renderer,
// custom chrome, input callbacks, resize/rebuild behavior, and event polling.
class VIBRANCE_GLFW_API UiWindow
{
public:
    explicit UiWindow(UiWindowOptions options = {});
    ~UiWindow();

    UiWindow(const UiWindow&) = delete;
    UiWindow& operator=(const UiWindow&) = delete;
    UiWindow(UiWindow&&) = delete;
    UiWindow& operator=(UiWindow&&) = delete;

    void set_options(UiWindowOptions options);
    const UiWindowOptions& options() const;

    bool open();
    void close();
    bool is_open() const;

    // tick() includes event polling. Use the lower-level hosts when one
    // process needs a custom cadence across several windows.
    void tick();
    int run();
    void rebuild();

    GLFWwindow* native_window() const;
    Engine* engine() const;
    GlfwPanelWindow* panel() const;

private:
    GlfwPanelWindowHostOptions make_host_options();
    GlfwPanelWindowTemplateOptions make_template_options(Engine& engine);

    UiWindowOptions windowOptions {};
    GlfwPanelWindowHost host;
};

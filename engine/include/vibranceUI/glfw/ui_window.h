// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/glfw/panel_window.h>
#include <vibranceUI/ui/builder.h>

#include <filesystem>
#include <functional>
#include <glm/glm.hpp>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
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



    UiPlacement block(std::string_view colour, float cornerRadius = 0.0f)
    {
        return ui.place(ui.block(colour, cornerRadius)).inside(root);
    }

    UiPlacement text(std::string value, float size = 16.0f,
        std::string_view colour = "#FFFFFFFF")
    {
        return ui.place(ui.text(std::move(value), fontAtlas, size, colour)).inside(root);
    }

    UiPlacement media(Media2DHandle handle, glm::vec2 size,
        Media2DFit fit = Media2DFit::eContain)
    {
        return ui.place(ui.media(handle, size, fit)).inside(root).size(size);
    }
};

using UiWindowContentBuilder = std::function<void(UiWindowContext&)>;
using UiWindowPanelConfigurator =
    std::function<void(GlfwPanelWindowTemplateOptions&)>;

struct UiWindowOptions
{


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



    std::filesystem::path fontPath {};
    std::filesystem::path fontDirectory {};
    std::filesystem::path fontProfilePath {};
    std::vector<std::filesystem::path> fallbackFontPaths {};
    std::vector<std::filesystem::path> emojiFontPaths {};


    int fontWeight = 0;


    bool useLocalePrimaryFont = true;
    std::filesystem::path localisationDirectory {};
    UiTheme theme {};

    GlfwWindowEngineConfigurator configureEngine {};
    UiWindowPanelConfigurator configurePanel {};
    UiWindowContentBuilder build {};
};



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

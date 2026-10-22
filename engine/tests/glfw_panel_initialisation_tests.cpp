#include <vibranceUI/glfw/panel_window.h>

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void require_closed(const GlfwPanelWindowHost& host, HWND window)
{
    require(!host.is_open() && !host.window() && !host.engine() && !host.panel(),
        "A failed panel must release every host handle");
    require(window && !IsWindow(window),
        "A failed panel must destroy its native window");
}

GlfwPanelWindowHostOptions passive_options()
{
    GlfwPanelWindowHostOptions options;
    options.title = "Vibrance panel initialisation test";
    options.width = 80;
    options.height = 80;
    options.focusOnShow = false;
    options.showInTaskbar = options.showInAltTab = false;
    options.transparentFramebuffer = false;
    options.enableAudio = false;
    options.msaaSamples = 1u;
    return options;
}

HWND hide_window(EngineCreateInfo& info)
{
    set_glfw_window_visible(static_cast<GLFWwindow*>(info.surfaceUserData), false);
    return static_cast<HWND>(info.nativeWindowHandle);
}

int run(bool rendererChecks)
{
    LoggerOutputOptions logs;
    logs.directory = std::filesystem::temp_directory_path() / "vibrance-panel-initialisation-tests";
    logs.filePrefix = "panel_initialisation_test";
    Logger::configure_output(std::move(logs));
    struct GlfwExit { ~GlfwExit() { terminate_glfw(); } } glfwExit;

    if (!rendererChecks)
    {
        HWND window = nullptr;
        bool templateCalled = false;
        auto options = passive_options();
        options.configureEngine = [&](EngineCreateInfo& info) {
            window = hide_window(info);
            // The existing unsupported backend returns an unready Engine
            // without creating a Vulkan device or modifying production state.
            info.renderBackend = RenderBackend::eDirectX11;
        };
        options.makeTemplateOptions = [&](Engine&) {
            templateCalled = true;
            return GlfwPanelWindowTemplateOptions {};
        };
        GlfwPanelWindowHost host(options);
        require(!host.open(), "An unready renderer must fail panel initialisation");
        if (!window) return 77;
        require(!templateCalled, "Unready renderers must not build panel content");
        require_closed(host, window);
        host.request_open();
        host.tick();
        require_closed(host, window);
        require(!templateCalled, "A queued retry must also reject an unready renderer");

        for (const bool standardException : { true, false })
        {
            options.configureEngine = [&](EngineCreateInfo& info) {
                window = hide_window(info);
                if (standardException) throw std::runtime_error("Fixture configuration failure");
                throw 7;
            };
            host.set_options(options);
            require(!host.open(), "Configuration exceptions must fail panel initialisation");
            require_closed(host, window);
        }
        return 0;
    }

    HWND window = nullptr;
    auto options = passive_options();
    options.configureEngine = [&](EngineCreateInfo& info) { window = hide_window(info); };
    options.makeTemplateOptions = [](Engine&) {
        GlfwPanelWindowTemplateOptions panel;
        panel.title.clear();
        panel.trafficLights.visible = false;
        panel.buildContent = [](GlfwPanelWindowTemplateContext&) {};
        return panel;
    };
    GlfwPanelWindowHost host(options);
    if (!host.open()) return 77;
    require(host.engine()->ready(), "The renderer fixture must initialise successfully");
    host.close();
    require_closed(host, window);

    const auto normalTemplate = options.makeTemplateOptions;
    options.makeTemplateOptions = [](Engine&) -> GlfwPanelWindowTemplateOptions {
        throw std::runtime_error("Fixture template factory failure");
    };
    host.set_options(options);
    require(!host.open(), "Template exceptions must fail panel initialisation");
    require_closed(host, window);

    options.makeTemplateOptions = [normalTemplate](Engine& engine) {
        auto panel = normalTemplate(engine);
        panel.buildContent = [](GlfwPanelWindowTemplateContext&) {
            throw std::runtime_error("Fixture content builder failure");
        };
        return panel;
    };
    host.set_options(options);
    require(!host.open(), "Content builder exceptions must fail panel initialisation");
    require_closed(host, window);

    options.makeTemplateOptions = normalTemplate;
    host.set_options(options);
    require(host.open(), "A host must recover after a failed panel construction");
    require(host.engine()->ready() && host.panel(), "Recovery must publish a complete panel");
    host.close();
    require_closed(host, window);
    return 0;
}
}

int main(int argc, char** argv)
{
    try
    {
        return run(argc > 1 && std::string_view(argv[1]) == "--renderer");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

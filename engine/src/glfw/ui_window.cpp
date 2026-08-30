#include <vibranceUI/glfw/ui_window.h>

#include <algorithm>
#include <cstdlib>
#include <utility>

UiWindow::UiWindow(UiWindowOptions options) :
    windowOptions(std::move(options)),
    host(make_host_options())
{
}

UiWindow::~UiWindow()
{
    close();
}

void UiWindow::set_options(UiWindowOptions options)
{
    windowOptions = std::move(options);
    host.set_options(make_host_options());
}

const UiWindowOptions& UiWindow::options() const
{
    return windowOptions;
}

bool UiWindow::open()
{
    return host.open();
}

void UiWindow::close()
{
    host.close();
}

bool UiWindow::is_open() const
{
    return host.is_open();
}

void UiWindow::tick()
{
    if (!host.is_open())
    {
        return;
    }
    poll_glfw_events();
    host.tick();
}

int UiWindow::run()
{
    if (!is_open() && !open())
    {
        return EXIT_FAILURE;
    }

    while (is_open())
    {
        tick();
    }
    close();
    terminate_glfw();
    return EXIT_SUCCESS;
}

void UiWindow::rebuild()
{
    if (GlfwPanelWindow* hostedPanel = host.panel())
    {
        hostedPanel->rebuild();
    }
}

GLFWwindow* UiWindow::native_window() const
{
    return host.window();
}

Engine* UiWindow::engine() const
{
    return host.engine();
}

GlfwPanelWindow* UiWindow::panel() const
{
    return host.panel();
}

GlfwPanelWindowHostOptions UiWindow::make_host_options()
{
    GlfwPanelWindowHostOptions options = {};
    options.width = std::max(windowOptions.size.x, 1);
    options.height = std::max(windowOptions.size.y, 1);
    options.hasInitialPosition = windowOptions.position.has_value();
    if (windowOptions.position)
    {
        options.x = windowOptions.position->x;
        options.y = windowOptions.position->y;
    }
    options.positioning = windowOptions.position ?
        std::nullopt : windowOptions.positioning;
    options.transparentFramebuffer =
        windowOptions.transparentFramebuffer;
    options.decorated = false;
    options.alwaysOnTop = windowOptions.alwaysOnTop;
    options.focusOnShow = windowOptions.focusOnShow;
    options.showInTaskbar = windowOptions.showInTaskbar;
    options.showInAltTab = windowOptions.showInAltTab;
    options.enableAudio = windowOptions.enableAudio;
    options.maxRenderPixels = windowOptions.maxRenderPixels;
    options.msaaSamples = windowOptions.msaaSamples;
    options.targetFrameRate = windowOptions.targetFrameRate;
    options.title = windowOptions.title;
    options.presentationBackend =
        windowOptions.presentationBackend.value_or(
#if defined(_WIN32)
            windowOptions.transparentFramebuffer ?
                PresentationBackend::eWindowsCompositionD3D11 :
                PresentationBackend::eNative
#else
            PresentationBackend::eNative
#endif
        );
    options.configureEngine = windowOptions.configureEngine;
    options.makeTemplateOptions = [this](Engine& engine) {
        return make_template_options(engine);
    };
    return options;
}

GlfwPanelWindowTemplateOptions UiWindow::make_template_options(
    Engine& engine)
{
    GlfwPanelWindowTemplateOptions options = {};
    options.title = windowOptions.title;
    options.fontPath = windowOptions.fontPath;
    options.fontDirectory = windowOptions.fontDirectory;
    options.fontProfilePath = windowOptions.fontProfilePath;
    options.fallbackFontPaths = windowOptions.fallbackFontPaths;
    options.fontWeight = windowOptions.fontWeight;
    options.useLocalePrimaryFont = windowOptions.useLocalePrimaryFont;
    options.localisationDirectory = windowOptions.localisationDirectory;
    options.theme = windowOptions.theme;
    options.initialSize = windowOptions.size;
    options.minSize = windowOptions.minSize;
    options.maxSize = windowOptions.maxSize;
    options.contentMargin = windowOptions.contentMargin;
    options.resizable = windowOptions.resizable;
    options.draggable = windowOptions.draggable;
    options.closeOnEscape = windowOptions.closeOnEscape;
    options.showTitleText = windowOptions.showTitle;
    options.trafficLights.visible = windowOptions.showWindowControls;
    options.trafficLights.accessible = windowOptions.showWindowControls;

    Engine* enginePointer = &engine;
    options.buildContent = [this, enginePointer](
        GlfwPanelWindowTemplateContext& context) {
        if (!windowOptions.build || !enginePointer)
        {
            return;
        }

        UiWindowContext simpleContext {
            context.ui,
            *enginePointer,
            context.fontAtlas,
            context.localisation,
            context.contentRoot,
            context.windowSize,
            context.contentSize
        };
        windowOptions.build(simpleContext);
    };
    if (windowOptions.configurePanel)
    {
        windowOptions.configurePanel(options);
    }
    return options;
}

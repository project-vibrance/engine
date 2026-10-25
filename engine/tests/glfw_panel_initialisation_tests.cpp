#include <vibranceUI/glfw/panel_window.h>

#include <windows.h>

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{
// Exercise the production hover path at a controlled point without moving the
// user's pointer or activating the hidden fixture window.
struct PanelHoverAccess
{
    using type = void (GlfwPanelWindow::*)(glm::vec2);
    friend type private_member(PanelHoverAccess);
};
struct PanelLightAccess
{
    using type = std::array<entt::entity, 3> GlfwPanelWindow::*;
    friend type private_member(PanelLightAccess);
};
struct PanelIconAccess
{
    using type = std::array<entt::entity, 3> GlfwPanelWindow::*;
    friend type private_member(PanelIconAccess);
};
template<class Tag, typename Tag::type Member>
struct PrivateMemberAccess
{
    friend typename Tag::type private_member(Tag) { return Member; }
};
template struct PrivateMemberAccess<PanelHoverAccess, &GlfwPanelWindow::update_hover_cursor_at>;
template struct PrivateMemberAccess<PanelLightAccess, &GlfwPanelWindow::trafficLightEntities>;
template struct PrivateMemberAccess<PanelIconAccess, &GlfwPanelWindow::trafficLightIconEntities>;

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

void stationary_traffic_light_hover_retains_the_scene(GlfwPanelWindowHost& host)
{
    auto& panel = *host.panel();
    auto options = panel.template_options();
    options.trafficLights.visible = true;
    options.trafficLights.showIconsOnHover = true;
    panel.set_template_options(std::move(options));
    auto& scene = host.engine()->renderer2d_scene();
    auto& registry = scene.registry();
    const auto lights = panel.*private_member(PanelLightAccess {});
    auto& icons = panel.*private_member(PanelIconAccess {});
    for (std::size_t index = 0; index < lights.size(); ++index)
    {
        require(registry.valid(lights[index]), "The hover fixture must contain all traffic lights");
        // Glyph loading is unrelated to this regression. Synthetic icon paint
        // keeps the actual group visibility path exercised with no asset files.
        if (!registry.valid(icons[index]))
            icons[index] = scene.create_shape({}, {1, 1}, {});
        registry.get<RenderLayer2DComponent>(icons[index]).visible = false;
    }
    scene.build_render_plan(0.0, 0u);
    const auto point_for = [&](entt::entity light) {
        const auto visual = scene.resolved_shape_visual_state(light, 0.0);
        require(visual.has_value(), "Traffic-light bounds must resolve");
        return glm::vec2(visual->rect) + glm::vec2(visual->rect.z, visual->rect.w) * 0.5f;
    };
    const auto hover_at = [&](glm::vec2 point) {
        (panel.*private_member(PanelHoverAccess {}))(point);
    };
    const auto require_icons = [&](bool visible) {
        for (const auto icon : icons)
            require(registry.get<RenderLayer2DComponent>(icon).visible == visible,
                "Traffic-light group visibility must follow its final input owner");
    };
    const auto require_retained = [&](glm::vec2 point, entt::entity owner) {
        hover_at(point);
        require(panel.input_state().hoveredButton == owner, "The expected button must own hover");
        const auto generation = scene.frame_generation();
        for (unsigned tick = 0; tick < 120u; ++tick) hover_at(point);
        require(scene.frame_generation() == generation,
            "A stationary hover must not repeatedly dirty traffic-light icons");
    };

    const auto closePoint = point_for(lights[0]);
    require_retained(closePoint, lights[0]);
    require_icons(true);
    require_retained(point_for(lights[1]), lights[1]);
    require_icons(true);

    ShapeStyleComponent overlayStyle;
    overlayStyle.set_color("#112233FF");
    const auto overlay = scene.create_shape(closePoint - glm::vec2(10), {20, 20}, overlayStyle);
    auto& layer = registry.get<RenderLayer2DComponent>(overlay);
    layer.layer = 200;
    layer.alwaysOnTop = true;
    registry.emplace<ButtonInputComponent>(overlay).enabled = true;
    scene.mark_dirty(overlay);
    require_retained(closePoint, overlay);
    require_icons(false);
    scene.destroy_entity(overlay);
    require_retained(closePoint, lights[0]);
    require_icons(true);
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
    stationary_traffic_light_hover_retains_the_scene(host);
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

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/glfw/panel_window.h>
#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/colour_palette_panel.h>
#include <vibranceUI/ui/liquid_controls.h>

#include <windows.h>

#include <filesystem>
#include <chrono>
#include <thread>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace
{


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

void generic_control_layout(GlfwPanelWindowHost& host)
{
    wchar_t windowsDirectory[MAX_PATH] {};
    require(GetWindowsDirectoryW(windowsDirectory, MAX_PATH) > 0, "Cannot find the system font directory");
    require(host.engine()->load_renderer2d_font(std::filesystem::path(windowsDirectory) / "Fonts/segoeui.ttf"),
        "The generic control fixture must load a real font");
    const auto& font = host.engine()->renderer2d_font_atlas();
    for (const float dpi : { 1.0f, 1.5f, 2.0f })
    {
        Renderer2DScene scene;
        LayoutScale scale;
        scale.factor = glm::vec2(dpi);
        scale.logicalSize = { 600.0f, 400.0f };
        UiBuilder ui(scene, scale);
        auto& registry = scene.registry();
        UiButtonOptions options;
        options.truncateLabel = true;
        options.primitive = Renderer2DPrimitive::eSquircle;
        options.squircleAmount = .7f;
        options.squirclePower = 5.0f;
        options.fontWeight = 600.0f;
        const std::string label = "Enregistrer les modifications du profil";
        const auto button = ui_create_aligned_text_button(ui, font, ui.root(), UiAlignment::eTopLeft,
            {}, label, { 120.0f, 32.0f }, 2, 0u, options);
        const auto& text = registry.get<TextComponent>(button.label);
        const auto& shape = registry.get<ShapeComponent>(button.root);
        require(text.text != label && !text.text.empty() && text.bounds.x <= 96.0f * dpi + 1.0f,
            "Generic fixed-width buttons must retain readable labels inside their padded bounds at every DPI");
        require(shape.primitive == options.primitive && shape.squircleAmount == options.squircleAmount &&
            shape.squirclePower == options.squirclePower &&
            registry.get<TextStyleComponent>(button.label).fontWeight == 600.0f,
            "Generic button geometry and typography must honour their options");
        options.fitContentWidth = true;
        const auto fitted = ui_create_aligned_text_button(ui, font, ui.root(), UiAlignment::eTopLeft,
            { 0.0f, 50.0f }, label, { 120.0f, 32.0f }, 2, 0u, options);
        scene.build_render_plan(0.0, 0u);
        require(registry.get<TextComponent>(fitted.label).text == label &&
            registry.get<ShapeComponent>(fitted.root).size.x > 120.0f * dpi,
            "Content-sized buttons must retain the complete label rather than truncate it");
        for (const auto direction : { UiLayoutDirection::eLeftToRight, UiLayoutDirection::eRightToLeft })
        {
            registry.emplace_or_replace<UiLayoutDirectionComponent>(ui.root(), direction);
            UiSearchFieldOptions fieldOptions;
            fieldOptions.textAlignment = TextHorizontalAlignment2D::eCenter;
            const auto field = ui_create_search_field(ui, font, ui.root(), UiAlignment::eTopLeft,
                { 20.0f, 100.0f }, { 120.0f, 34.0f }, 2, 0u, fieldOptions);
            auto& input = registry.get<TextInputComponent>(field.root);
            for (const auto* value : { "255", "336699", "0123456789ABCDEF0123456789ABCDEF" })
            {
                input.value = value;
                ui_update_text_input_visual(scene, font, field.root);
                scene.build_render_plan(0.0, 0u);
                const auto frame = ui_entity_framebuffer_rect(registry, field.root);
                const auto labelFrame = ui_entity_framebuffer_rect(registry, field.label);
                require(std::abs(frame.x + frame.z * .5f - labelFrame.x - labelFrame.z * .5f) < 1.0f,
                    "Centred input values must remain centred after edits in both interface directions");
                require(labelFrame.z <= frame.z - 20.0f * dpi + 1.0f && input.value == value,
                    "Text clipping must preserve the complete editable value and both padding edges");
                const auto caret = ui_entity_framebuffer_rect(registry, field.caret);
                require(caret.x >= frame.x && caret.x + caret.z <= frame.x + frame.z,
                    "A centred input's caret must follow its displayed text inside the field");
            }
            registry.get<ShapeComponent>(field.root).size.x = 200.0f * dpi;
            registry.get<Layout2DComponent>(field.root).size.x = 200.0f * dpi;
            input.value = "255";
            ui_update_text_input_visual(scene, font, field.root);
            scene.build_render_plan(0.0, 0u);
            const auto frame = ui_entity_framebuffer_rect(registry, field.root);
            const auto labelFrame = ui_entity_framebuffer_rect(registry, field.label);
            require(std::abs(frame.x + frame.z * .5f - labelFrame.x - labelFrame.z * .5f) < 1.0f,
                "Centred inputs must follow changes to the field width without per-frame application callbacks");
        }
    }
}

void generic_panel_visibility(GlfwPanelWindowHost& host, GlfwPanelWindowHostOptions options)
{
    for (const bool deferred : { true, false })
    {
        options.deferCloseToOwner = deferred;
        options.makeTemplateOptions = [](Engine&) {
            GlfwPanelWindowTemplateOptions panel;
            panel.trafficLights.visible = false;
            panel.buildContent = [](GlfwPanelWindowTemplateContext&) {};
            DisplayTransition2DComponent transition;
            transition.durationSeconds = .12f;
            transition.curve = DisplayTransitionCurve2D::eLinear;
            transition.fromScale = glm::vec2(.74f);
            transition.fromOpacity = .2f;
            panel.openingTransition = transition;
            transition.fromScale = glm::vec2(1.0f);
            transition.toScale = glm::vec2(.92f);
            transition.fromOpacity = 1.0f;
            transition.toOpacity = 0.0f;
            panel.closingTransition = transition;
            return panel;
        };
        host.set_options(options);
        require(host.open(), "A panel with caller-defined visibility transitions must open");
        set_glfw_window_visible(host.window(), true);
        const auto frame = [&] {
            poll_glfw_events();
            Engine::begin_externally_paced_frame(60u);
            host.tick();
            Engine::end_externally_paced_frame();
        };
        frame();
        auto* engine = host.engine();
        auto& registry = engine->renderer2d_scene().registry();
        double start = -1.0;
        for (const auto entity : registry.view<DisplayTransition2DComponent>())
        {
            const auto& transition = registry.get<DisplayTransition2DComponent>(entity);
            require(transition.fromScale == glm::vec2(.74f) && transition.hasStarted,
                "Panel opening must honour caller-defined transitions on the renderer clock");
            start = transition.startSeconds;
        }
        require(start >= 0.0, "The panel must schedule its configured entrance");
        require(host.refresh_template(), "A panel must allow ordinary template refreshes while opening");
        for (const auto entity : registry.view<DisplayTransition2DComponent>())
            require(registry.get<DisplayTransition2DComponent>(entity).startSeconds == start,
                "Template refreshes must retain an active entrance rather than restart it");
        if (deferred) host.request_close();
        else { set_glfw_window_should_close(host.window(), true); frame(); }
        require(host.is_open() && host.is_closing() && !host.close_ready(),
            "Animated close requests must retain the native window and renderer through the exit");
        require(!host.refresh_template(), "A closing panel must retain its final scene");
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        bool ready = false;
        while (host.is_open() && std::chrono::steady_clock::now() < deadline)
        {
            require(host.engine() == engine, "A panel exit must retain its existing renderer");
            frame();
            if (deferred && host.close_ready()) { ready = true; break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(4));
        }
        require(deferred ? ready && host.is_open() : !host.is_open(),
            "Owner-managed exits must remain reviewable; automatic hosts must close after presentation");
        host.close();
    }
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

void generic_colour_picker()
{
    wchar_t windowsDirectory[MAX_PATH] {};
    require(GetWindowsDirectoryW(windowsDirectory, MAX_PATH) > 0, "Cannot locate a standalone picker font");
    const auto font = std::filesystem::path(windowsDirectory) / "Fonts/segoeui.ttf";
    unsigned configurations = 0u;
    UiColourPalettePanelCreateInfo options;
    options.focusOnShow = false;
    options.configureEngine = [font](EngineCreateInfo& info, const UiResourceDirectories&, std::string_view) {
        info.defaultRenderer2DFontPath = font;
    };
    options.configurePanel = [font, &configurations](GlfwPanelWindowTemplateOptions& panel,
        const UiResourceDirectories&, std::string_view) {
        panel.fontPath = font;
        ++configurations;
    };
    UiColourPalettePanel picker(options);
    picker.request_open(0x007AFFu, "SDK accent", {});
    for (std::size_t tab = 0u; tab < 5u; ++tab)
    {
        picker.select_tab(tab);
        picker.tick();
        require(picker.host()->engine() && picker.host()->engine()->ready(),
            "Every colour-picker tab must render using only SDK types and a caller-supplied font");
        set_glfw_window_visible(picker.host()->window(), false);
        auto& registry = picker.host()->engine()->renderer2d_scene().registry();
        require(!registry.view<TextComponent>().empty(), "A standalone picker must retain its text contents");
        unsigned lights = 0u;
        for (const auto entity : registry.view<UiLiquidControlSurface, LiquidGlassComponent, RenderLayer2DComponent>())
        {
            if (registry.get<RenderLayer2DComponent>(entity).layer != 100) continue;
            const auto& glass = registry.get<LiquidGlassComponent>(entity);
            require(registry.get<UiLiquidControlSurface>(entity).preserveColour &&
                glass.sampleOwnPaint && glass.blur == 0.0f,
                "Colour-panel traffic lights must keep their own sharp coloured face at the default panel blur");
            ++lights;
        }
        require(lights == 3u, "Every colour-panel tab must retain all three glass traffic lights");
        for (const auto entity : registry.view<TextComponent>())
            require(registry.get<TextComponent>(entity).text.find("unavailable") == std::string::npos,
                "Omitting optional artwork must use the geometric fallback without an import error");
    }
    const auto before = configurations;
    picker.set_appearance(ui_dark_theme(), "en_gb");
    picker.tick();
    require(configurations > before && picker.colour() == 0x007AFFu,
        "A runtime appearance refresh must reapply caller configuration and preserve the selected colour");
    picker.close(true);
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
    bool transparentStroke = false;
    auto& registry = host.engine()->renderer2d_scene().registry();
    for (const auto entity : registry.view<ShapeStyleComponent, RenderLayer2DComponent>())
        if (registry.get<RenderLayer2DComponent>(entity).layer == 99)
        {
            const auto& paint = registry.get<ShapeStyleComponent>(entity);
            transparentStroke = paint.fill == Renderer2DFill::eNone &&
                paint.color0.a == 0.0f && paint.color1.a == 0.0f && paint.outlineWidth > 0.0f;
        }
    require(transparentStroke, "The outer stroke must have a transparent fill so panel contents remain visible");
    stationary_traffic_light_hover_retains_the_scene(host);
    generic_control_layout(host);
    options.deferCloseToOwner = true;
    host.set_options(options);
    require(host.open(), "The deferred-close fixture must open");
    set_glfw_window_should_close(host.window(), true);
    host.tick();
    require(host.is_open() && host.should_close() && host.engine()->ready(),
        "An owner-managed close must retain the window and renderer until its animation completes");
    host.close();
    require_closed(host, window);
    generic_panel_visibility(host, options);
    generic_colour_picker();
    return 0;
}
}

int main(int argc, char** argv)
{
    try
    {
        const GlfwPanelWindowTemplateOptions defaults;
        require(defaults.panelInset == glm::vec4(20.0f) && defaults.panelShadow &&
            defaults.panelShadow->blurRadius == 18.0f && defaults.fontWeight == 450 &&
            defaults.trafficLights.iconSize == glm::vec2(10.0f) && defaults.trafficLights.matchIconTintToButton,
            "ordinary engine panels must inherit the shared appearance without an application preset");
        for (auto theme : { ui_light_theme(), ui_dark_theme() })
        {
            const auto lights = UiPanelAppearance::traffic_lights(theme);
            require(lights.closeColor == "#FC6F5FFF" && lights.minimiseColor == "#FFC73FFF" &&
                lights.maximiseColor == "#88D454FF",
                "Both appearances must share the coral, gold and green window-control palette");
            theme.danger = "#010203FF";
            theme.warning = "#040506FF";
            theme.success = "#070809FF";
            theme.trafficLightClose = "#123456FF";
            const auto customisedLights = UiPanelAppearance::traffic_lights(theme);
            require(customisedLights.closeColor == "#123456FF" && customisedLights.minimiseColor == lights.minimiseColor &&
                customisedLights.maximiseColor == lights.maximiseColor,
                "Window-control colours must be customisable independently of warning and error colours");
            const auto style = UiPanelAppearance::surface_style(theme);
            require(style.color0 == renderer2d_hex_color(theme.panel_palette().contentSurface) &&
                style.outlineColor == renderer2d_hex_color(theme.outlineStrong) && style.outlineWidth == .75f,
                "default panel surfaces and subtle strokes must follow both theme palettes");
            theme.panel_palette().contentSurface = "#123456FF";
            require(theme.settingsPanel.contentSurface == "#123456FF" &&
                theme.settings_content_style().color0 == theme.panel_content_style().color0,
                "generic palette names must preserve existing theme documents without duplicating colours");
        }
        return run(argc > 1 && std::string_view(argv[1]) == "--renderer");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

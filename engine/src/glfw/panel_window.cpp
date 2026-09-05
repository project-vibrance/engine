#include <vibranceUI/glfw/panel_window.h>

#include <vibranceUI/ui/builder.h>
#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/surfaces.h>
#include <vibranceUI/ui/text.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <utility>
#include <vector>

namespace
{
    constexpr int kFallbackMaxWindowSize = 1000000;

    int option_min(int value)
    {
        return std::max(value, 1);
    }

    int option_max(int value, int minValue)
    {
        return value > 0 ? std::max(value, minValue) : kFallbackMaxWindowSize;
    }

    int clamp_window_dimension(int value, int minValue, int maxValue)
    {
        return std::clamp(value, option_min(minValue), option_max(maxValue, minValue));
    }

    // Keeps Retina-scaled panel maths in one place for rendering and hit tests
    struct PanelGeometry
    {
        LayoutScale scale {};
        glm::vec2 framebufferSize { 0.0f };
        glm::vec2 logicalSize { 0.0f };
        glm::vec4 logicalInset { 0.0f };
        glm::vec4 inset { 0.0f };
        glm::vec2 position { 0.0f };
        glm::vec2 size { 1.0f };
        glm::vec2 logicalPanelSize { 1.0f };
    };

    LayoutScale panel_window_layout_scale(GLFWwindow* window, int framebufferWidth, int framebufferHeight)
    {
        LayoutScale scale = {};

        int windowWidth = 0;
        int windowHeight = 0;
        if (!glfw_window_size(window, windowWidth, windowHeight))
        {
            windowWidth = std::max(framebufferWidth, 1);
            windowHeight = std::max(framebufferHeight, 1);
        }
        if (framebufferWidth <= 0)
        {
            framebufferWidth = windowWidth;
        }
        if (framebufferHeight <= 0)
        {
            framebufferHeight = windowHeight;
        }

        scale.factor = {
            static_cast<float>(std::max(framebufferWidth, 1)) / static_cast<float>(std::max(windowWidth, 1)),
            static_cast<float>(std::max(framebufferHeight, 1)) / static_cast<float>(std::max(windowHeight, 1))
        };
        scale.contentScale = glfw_content_scale(window);
        scale.logicalSize = {
            static_cast<float>(std::max(windowWidth, 1)),
            static_cast<float>(std::max(windowHeight, 1))
        };
        return scale;
    }

    PanelGeometry make_panel_geometry(GLFWwindow* window, int framebufferWidth, int framebufferHeight, glm::vec4 panelInset)
    {
        // Convert logical panel insets into framebuffer-space geometry
        PanelGeometry geometry = {};
        geometry.scale = panel_window_layout_scale(window, framebufferWidth, framebufferHeight);
        geometry.framebufferSize = {
            static_cast<float>(std::max(framebufferWidth, 1)),
            static_cast<float>(std::max(framebufferHeight, 1))
        };
        geometry.logicalSize = geometry.scale.logicalSize;
        geometry.logicalInset = glm::max(panelInset, glm::vec4(0.0f));
        geometry.inset = scaled_edges(
            geometry.logicalInset.x,
            geometry.logicalInset.y,
            geometry.logicalInset.z,
            geometry.logicalInset.w,
            geometry.scale);
        geometry.position = { geometry.inset.x, geometry.inset.y };
        geometry.size = {
            std::max(1.0f, geometry.framebufferSize.x - geometry.inset.x - geometry.inset.z),
            std::max(1.0f, geometry.framebufferSize.y - geometry.inset.y - geometry.inset.w)
        };
        geometry.logicalPanelSize = {
            std::max(1.0f, geometry.logicalSize.x - geometry.logicalInset.x - geometry.logicalInset.z),
            std::max(1.0f, geometry.logicalSize.y - geometry.logicalInset.y - geometry.logicalInset.w)
        };
        return geometry;
    }

    bool panel_geometry_for_window(GLFWwindow* window, glm::vec4 panelInset, PanelGeometry& geometry)
    {
        int width = 0;
        int height = 0;
        if (!glfw_framebuffer_size(window, width, height))
        {
            return false;
        }

        geometry = make_panel_geometry(window, width, height, panelInset);
        return true;
    }

    float logical_title_height(const GlfwPanelWindowTemplateOptions& options)
    {
        // Reserve enough height for draggable chrome and traffic lights
        const float trafficLightHeight = options.trafficLights.visible ?
            options.trafficLights.firstCenter.y * 2.0f :
            0.0f;
        return std::max(
            std::max(options.titleHeight, options.contentMargin.y),
            trafficLightHeight);
    }

    glm::vec2 traffic_light_center_at_index(
        const GlfwPanelWindowTrafficLightOptions& options,
        int index,
        const PanelGeometry& panelGeometry)
    {
        glm::vec2 center = scaled_offset(
            options.firstCenter.x,
            options.firstCenter.y,
            panelGeometry.scale);
        const float spacing = scaled_scalar(options.spacing, panelGeometry.scale);
        if (options.placement == GlfwPanelWindowTrafficLightPlacement::eTopLeft)
        {
            center.x += static_cast<float>(index) * spacing;
        }
        else
        {
            center.x = panelGeometry.size.x - center.x - static_cast<float>(index) * spacing;
        }
        return panelGeometry.position + center;
    }

    TextStyleComponent panel_text_style(std::string_view color)
    {
        TextStyleComponent style = {};
        style.set_color(color);
        style.shadowColor = glm::vec4(0.0f);
        style.effectColor = glm::vec4(0.0f);
        style.outlineWidth = 0.0f;
        style.shadowBlur = 0.0f;
        style.glowRadius = 0.0f;
        style.blurRadius = 0.0f;
        return style;
    }

}

GlfwPanelWindow::GlfwPanelWindow(GLFWwindow* window, Engine* engine, GlfwPanelWindowTemplateOptions options)
    : window(window),
      engine(engine),
      logger(Logger::fetch_logger()),
      options(std::move(options))
{
    set_glfw_window_user_pointer(window, this);
    GlfwCallbackSet callbacks = {};
    callbacks.framebufferSize = framebuffer_resize_callback;
    callbacks.contentScale = content_scale_callback;
    callbacks.mouseButton = mouse_button_callback;
    callbacks.scroll = scroll_callback;
    callbacks.key = key_callback;
    callbacks.character = char_callback;
    set_glfw_callbacks(window, callbacks);
    cursors = create_glfw_standard_cursors();

    // Template-owned localisation lets every panel share the same language files
    if (engine && !this->options.localisationDirectory.empty())
    {
        engine->load_localisation_directory(this->options.localisationDirectory);
        engine->set_locale(Engine::shared_locale());
    }

    load_template_font();

    load_traffic_light_icons();
    apply_initial_size();
    build_scene();
}

GlfwPanelWindow::~GlfwPanelWindow()
{
    if (window)
    {
        clear_glfw_cursor(window);
        set_glfw_callbacks(window, {});
        set_glfw_window_user_pointer(window, nullptr);
    }
    destroy_glfw_standard_cursors(cursors);
}

bool GlfwPanelWindow::should_close() const
{
    return glfw_window_should_close(window);
}

bool GlfwPanelWindow::is_hovered() const
{
    return glfw_window_hovered(window);
}

void GlfwPanelWindow::tick()
{
    if (!window || !engine || glfw_window_should_close(window))
    {
        return;
    }

    const double now = glfw_time_seconds();
    engine->update_timing(now);

    const uint32_t uiUpdateRate = engine->recommended_ui_update_rate();
    const double uiUpdateInterval = 1.0 / static_cast<double>(uiUpdateRate);
    if (hasUiUpdateSample &&
        now - lastUiUpdateSeconds < uiUpdateInterval)
    {
        engine->draw();
        return;
    }
    lastUiUpdateSeconds = now;
    hasUiUpdateSample = true;
    update_window_action();

    const GlfwUiPointerSample pointer = glfw_ui_pointer_sample(window, engine, true);
    glfw_update_ui_timed_controls(*engine, inputState, now);
    glfw_update_ui_drag_inputs(window, *engine, inputState, pointer);

    if (windowPointerAction != WindowPointerAction::eNone ||
        inputState.draggedPanel != entt::null ||
        inputState.resizingPanel != entt::null ||
        inputState.pointerInputCapture != entt::null ||
        inputState.activeSlider != entt::null ||
        inputState.activeScrollBar != entt::null ||
        glfw_window_focused(window) ||
        glfw_window_hovered(window))
    {
        update_hover_cursor();
    }
    else
    {
        ui_update_input_hover(
            engine->renderer2d_scene(),
            engine->renderer2d_font_atlas(),
            inputState,
            false,
            glm::vec2(0.0f));
        set_hovered_traffic_light(TrafficLightKind::eNone);
        clear_glfw_cursor(window);
    }

    int width = pointer.renderWidth;
    int height = pointer.renderHeight;
    engine->resize(static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0)));
    if (width > 0 && height > 0 && (width != lastRenderWidth || height != lastRenderHeight))
    {
        build_scene();
    }

    engine->draw();
}

const GlfwPanelWindowTemplateOptions& GlfwPanelWindow::template_options() const
{
    return options;
}

void GlfwPanelWindow::set_template_options(GlfwPanelWindowTemplateOptions nextOptions)
{
    // A template swap may change media paths, sizing, title text, and content
    restore_traffic_light_maximise();
    options = std::move(nextOptions);
    load_template_font();
    load_traffic_light_icons();
    apply_initial_size();
    build_scene();
}

void GlfwPanelWindow::set_content_builder(GlfwPanelWindowContentBuilder builder)
{
    options.buildContent = std::move(builder);
    build_scene();
}

void GlfwPanelWindow::set_size(int width, int height)
{
    if (!window)
    {
        return;
    }

    const int clampedWidth = clamp_window_dimension(width, options.minSize.x, options.maxSize.x);
    const int clampedHeight = clamp_window_dimension(height, options.minSize.y, options.maxSize.y);
    set_glfw_window_size(window, clampedWidth, clampedHeight);
}

void GlfwPanelWindow::rebuild()
{
    build_scene();
}

void GlfwPanelWindow::framebuffer_resize_callback(GLFWwindow* window, int width, int height)
{
    GlfwPanelWindow* app = static_cast<GlfwPanelWindow*>(glfw_window_user_pointer(window));
    if (!app || !app->engine)
    {
        return;
    }

    app->engine->resize(static_cast<uint32_t>(std::max(width, 0)), static_cast<uint32_t>(std::max(height, 0)));
    app->build_scene();
}

void GlfwPanelWindow::content_scale_callback(GLFWwindow* window, float xscale, float yscale)
{
    (void)xscale;
    (void)yscale;

    GlfwPanelWindow* app = static_cast<GlfwPanelWindow*>(glfw_window_user_pointer(window));
    if (app)
    {
        app->build_scene();
    }
}

void GlfwPanelWindow::mouse_button_callback(GLFWwindow* window, int button, int action, int mods)
{
    GlfwPanelWindow* app = static_cast<GlfwPanelWindow*>(glfw_window_user_pointer(window));
    if (!app)
    {
        return;
    }

    app->handle_mouse_button(button, action, mods);
}

void GlfwPanelWindow::scroll_callback(GLFWwindow* window, double xoffset, double yoffset)
{
    GlfwPanelWindow* app = static_cast<GlfwPanelWindow*>(glfw_window_user_pointer(window));
    if (app)
    {
        app->handle_scroll(xoffset, yoffset);
    }
}

void GlfwPanelWindow::key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    GlfwPanelWindow* app = static_cast<GlfwPanelWindow*>(glfw_window_user_pointer(window));
    if (app)
    {
        app->handle_key(key, scancode, action, mods);
    }
}

void GlfwPanelWindow::char_callback(GLFWwindow* window, unsigned int codepoint)
{
    GlfwPanelWindow* app = static_cast<GlfwPanelWindow*>(glfw_window_user_pointer(window));
    if (app)
    {
        app->handle_char(codepoint);
    }
}

void GlfwPanelWindow::handle_mouse_button(int button, int action, int mods)
{
    if (!engine || !window || (action != GLFW_PRESS && action != GLFW_RELEASE))
    {
        return;
    }

    if (button != GLFW_MOUSE_BUTTON_LEFT)
    {
        int renderWidth = 0;
        int renderHeight = 0;
        glfw_framebuffer_size(window, renderWidth, renderHeight);
        const UiPointerButtonInput input = glfw_pointer_button_input(window, button, action, mods, renderWidth, renderHeight);
        ui_handle_pointer_button(
            engine->renderer2d_scene(),
            engine->renderer2d_font_atlas(),
            inputState,
            input);
        return;
    }

    if (action == GLFW_PRESS)
    {
        int renderWidth = 0;
        int renderHeight = 0;
        glfw_framebuffer_size(window, renderWidth, renderHeight);
        const UiPointerButtonInput input = glfw_pointer_button_input(window, button, action, mods, renderWidth, renderHeight);
        if (input.hasPoint)
        {
            const bool sceneBlocksWindowAction =
                scene_blocks_window_action(input.point);
            const TrafficLightKind light = !sceneBlocksWindowAction ?
                traffic_light_at(input.point) : TrafficLightKind::eNone;
            if (light != TrafficLightKind::eNone)
            {
                perform_traffic_light_action(light);
                return;
            }

            if (!sceneBlocksWindowAction &&
                handle_window_action_press(input))
            {
                return;
            }
        }

        clear_window_action();
        glfw_handle_ui_pointer_button(window, *engine, inputState, button, action, mods);
    }
    else if (action == GLFW_RELEASE)
    {
        glfw_handle_ui_pointer_button(window, *engine, inputState, button, action, mods);
        clear_window_action();
    }
}

void GlfwPanelWindow::handle_scroll(double xoffset, double yoffset)
{
    if (!engine || !window)
    {
        return;
    }

    glfw_handle_ui_scroll(window, *engine, xoffset, yoffset);
}

void GlfwPanelWindow::handle_key(int key, int scancode, int action, int mods)
{
    (void)scancode;
    if (!engine || !window)
    {
        return;
    }

    glfw_handle_ui_key(window, *engine, inputState, key, action, mods);

    if (options.closeOnEscape && action == GLFW_PRESS && key == GLFW_KEY_ESCAPE)
    {
        set_glfw_window_should_close(window, true);
    }
}

void GlfwPanelWindow::handle_char(unsigned int codepoint)
{
    if (!engine)
    {
        return;
    }

    glfw_handle_ui_char(*engine, inputState, codepoint);
}

void GlfwPanelWindow::apply_initial_size()
{
    if (!window || options.initialSize.x <= 0 || options.initialSize.y <= 0)
    {
        return;
    }

    set_size(options.initialSize.x, options.initialSize.y);
}

void GlfwPanelWindow::load_template_font()
{
    if (!engine ||
        (
        options.fontPath.empty() &&
        options.fallbackFontPaths.empty() &&
        options.fontProfilePath.empty()))
    {
        return;
    }

    Renderer2DFontAtlasLoadOptions fontOptions = {};
    fontOptions.fontWeight = options.fontWeight;
    fontOptions.preloadText = engine->localisation().locale_text();
    fontOptions.requireRequestedGlyphs =
        options.requireLocalisedGlyphs &&
        !fontOptions.preloadText.empty();

    std::filesystem::path primaryFontPath = options.fontPath;
    fontOptions.fallbackFontPaths = options.fallbackFontPaths;
    std::filesystem::path fontDirectory = options.fontDirectory;
    if (fontDirectory.empty() && !options.fontPath.empty())
    {
        fontDirectory = options.fontPath.parent_path();
    }
    if (fontDirectory.empty() && !fontOptions.fallbackFontPaths.empty())
    {
        fontDirectory = fontOptions.fallbackFontPaths.front().parent_path();
    }
    Renderer2DFontProfile fontProfile = {};
    const bool hasFontProfile = renderer2d_load_font_profile(
        options.fontProfilePath,
        fontDirectory,
        fontProfile);

    auto addFallback = [&fontOptions](const std::filesystem::path& path) {
        if (!path.empty() &&
            std::find(fontOptions.fallbackFontPaths.begin(), fontOptions.fallbackFontPaths.end(), path) ==
                fontOptions.fallbackFontPaths.end())
        {
            fontOptions.fallbackFontPaths.push_back(path);
        }
    };
    if (hasFontProfile)
    {
        if (!fontProfile.defaultFontPath.empty())
        {
            if (primaryFontPath.empty())
            {
                primaryFontPath = fontProfile.defaultFontPath;
            }
            else if (primaryFontPath != fontProfile.defaultFontPath)
            {
                addFallback(fontProfile.defaultFontPath);
            }
        }
        for (const std::filesystem::path& path : fontProfile.fallbackFontPaths)
        {
            addFallback(path);
        }
    }
    else if (!options.fontProfilePath.empty() && logger)
    {
        logger->warning("Panel window could not load font profile: " + options.fontProfilePath.string());
    }
    // Profiles may intentionally stay small, but literal UI text can contain a
    // script that is not part of the active locale. Add packaged and available
    // platform fallbacks so controls do not need localisation-only font tricks.
    for (const std::filesystem::path& path :
        renderer2d_common_font_fallbacks(fontDirectory))
    {
        addFallback(path);
    }
    if (primaryFontPath.empty() && !fontOptions.fallbackFontPaths.empty())
    {
        primaryFontPath = fontOptions.fallbackFontPaths.front();
        fontOptions.fallbackFontPaths.erase(fontOptions.fallbackFontPaths.begin());
    }
    if (options.useLocalePrimaryFont)
    {
        const std::filesystem::path localeFontPath = hasFontProfile ?
            renderer2d_primary_font_for_locale(
                fontProfile,
                engine->localisation().locale(),
                primaryFontPath) :
            renderer2d_primary_font_for_locale(
                fontDirectory,
                engine->localisation().locale(),
                primaryFontPath);
        if (!localeFontPath.empty() && localeFontPath != primaryFontPath)
        {
            addFallback(primaryFontPath);
            primaryFontPath = localeFontPath;
        }
    }
    if (primaryFontPath.empty())
    {
        return;
    }

    if (engine->load_renderer2d_font(primaryFontPath, fontOptions))
    {
        return;
    }

    if (fontOptions.requireRequestedGlyphs)
    {
        fontOptions.requireRequestedGlyphs = false;
        if (engine->load_renderer2d_font(primaryFontPath, fontOptions))
        {
            return;
        }
    }

    if (logger)
    {
        logger->warning("Panel window could not load a font covering the active localisation text");
    }
}

void GlfwPanelWindow::build_scene()
{
    if (!window || !engine)
    {
        return;
    }

    int renderWidth = 0;
    int renderHeight = 0;
    if (!glfw_framebuffer_size(window, renderWidth, renderHeight))
    {
        return;
    }
    lastRenderWidth = renderWidth;
    lastRenderHeight = renderHeight;

    Renderer2DScene& scene = engine->renderer2d_scene();
    const Renderer2DFontAtlas& fontAtlas = engine->renderer2d_font_atlas();
    const Localisation& localisation = engine->localisation();
    scene.clear();
    inputState = {};
    trafficLightIconEntities.fill(entt::null);
    const PanelGeometry panelGeometry = make_panel_geometry(window, renderWidth, renderHeight, options.panelInset);
    UiBuilder ui(scene, panelGeometry.scale);

    ShapeStyleComponent panelStyle = options.panelStyle.value_or(options.theme.panel_style(1.25f, 1.0f));
    // Background layers can replace the base fill when the window is split into regions
    if (options.panelBackgroundLayersReplacePanelFill && !options.panelBackgroundLayers.empty())
    {
        panelStyle = make_solid_style("rgba(0, 0, 0, 0)", "rgba(0, 0, 0, 0)", 0.0f, 0.0f);
    }

    entt::entity panel = scene.create_shape(
        panelGeometry.position,
        panelGeometry.size,
        panelStyle,
        options.panelPrimitive);
    ui.set_layer(panel, -4, 0u);
    ui.set_shape(panel, options.cornerRadius);
    ui.set_shape_squircle(panel, options.panelSquircleAmount, options.panelSquirclePower);
    if (options.panelShadow)
    {
        ShadowComponent shadow = *options.panelShadow;
        shadow.offset = scaled_offset(shadow.offset.x, shadow.offset.y, ui.scale());
        shadow.blurRadius = scaled_scalar(shadow.blurRadius, ui.scale());
        shadow.spread = scaled_scalar(shadow.spread, ui.scale());
        scene.registry().emplace_or_replace<ShadowComponent>(panel, shadow);
    }
    scene.enable_mask(panel, false);

    std::vector<entt::entity> panelBackgroundLayerEntities;
    if (!options.panelBackgroundLayers.empty())
    {
        panelBackgroundLayerEntities = ui_create_panel_background_layers(ui, panel, options.panelBackgroundLayers, -4, 0u);
    }

    entt::entity titleBar = scene.create_shape(
        panelGeometry.position,
        { panelGeometry.size.x, std::min(scaled_scalar(options.titleHeight, ui.scale()), panelGeometry.size.y) },
        make_solid_style("#00000000"),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(titleBar, -3, 0u);

    if (options.trafficLights.visible)
    {
        const std::array<TrafficLightKind, 3> lights {
            TrafficLightKind::eClose,
            TrafficLightKind::eMinimise,
            TrafficLightKind::eMaximise
        };
        for (std::size_t i = 0; i < lights.size(); ++i)
        {
            const TrafficLightKind kind = lights[i];
            const bool enabled = traffic_light_enabled(kind);
            const std::string color = kind == TrafficLightKind::eClose ?
                options.trafficLights.closeColor :
                kind == TrafficLightKind::eMinimise ?
                    options.trafficLights.minimiseColor :
                    options.trafficLights.maximiseColor;

            const glm::vec4 outlineColor = renderer2d_hex_color(
                options.trafficLights.outlineColor,
                glm::vec4(0.0f));
            const float outlineWidth = outlineColor.a > 0.001f ?
                std::max(scaled_scalar(options.trafficLights.outlineWidth, ui.scale()), 0.0f) :
                0.0f;
            ShapeStyleComponent dotStyle = make_solid_style(
                renderer2d_hex_color(enabled ? color : options.trafficLights.disabledColor),
                outlineColor,
                outlineWidth,
                enabled ? 1.0f : 0.58f);
            dotStyle.edgeSoftness = std::max(scaled_scalar(options.trafficLights.edgeSoftness, ui.scale()), 0.0f);
            const float lightRadius = std::max(scaled_scalar(options.trafficLights.radius, ui.scale()), 1.0f);

            entt::entity dot = scene.create_shape(
                traffic_light_center(kind) - glm::vec2(lightRadius),
                glm::vec2(lightRadius * 2.0f),
                dotStyle,
                Renderer2DPrimitive::eEllipse);
            ui.set_layer(dot, 100, static_cast<uint32_t>(i), true);

            if (enabled && options.trafficLights.showIconsOnHover)
            {
                const Media2DHandle iconHandle = traffic_light_icon(kind);
                if (iconHandle.valid())
                {
                    const glm::vec2 iconSize = glm::max(
                        scaled_size(options.trafficLights.iconSize.x, options.trafficLights.iconSize.y, ui.scale()),
                        glm::vec2(1.0f));
                    entt::entity icon = scene.create_media(
                        traffic_light_center(kind) - iconSize * 0.5f,
                        iconSize,
                        iconHandle,
                        Media2DFit::eContain);
                    if (Media2DComponent* media = scene.registry().try_get<Media2DComponent>(icon))
                    {
                        media->set_mask_tint(options.trafficLights.iconTint);
                    }
                    RenderLayer2DComponent& iconLayer = ui.set_layer(icon, 101, static_cast<uint32_t>(i), true);
                    iconLayer.visible = hoveredTrafficLight == kind;
                    trafficLightIconEntities[i] = icon;
                }
            }
        }
    }

    entt::entity titleText = entt::null;
    if (options.showTitleText)
    {
        entt::entity titleParent = panel;
        UiAlignment titleAlignment = UiAlignment::eMiddleLeft;
        glm::vec2 titleOffset = options.titleOffset;
        if (options.titleBackgroundLayerIndex &&
            *options.titleBackgroundLayerIndex < options.panelBackgroundLayers.size())
        {
            const UiPanelBackgroundLayer& titleLayer = options.panelBackgroundLayers[*options.titleBackgroundLayerIndex];
            const glm::vec4 rect = titleLayer.rect;
            const glm::vec2 anchorMin {
                std::clamp(rect.x, 0.0f, 1.0f),
                std::clamp(rect.y, 0.0f, 1.0f)
            };
            const glm::vec2 fixedSize {
                titleLayer.fixedSize.x > 0.0f ? titleLayer.fixedSize.x * ui.scale().factor.x : 0.0f,
                titleLayer.fixedSize.y > 0.0f ? titleLayer.fixedSize.y * ui.scale().factor.y : 0.0f
            };
            const glm::vec2 anchorMax {
                fixedSize.x > 0.0f ? anchorMin.x : std::clamp(rect.x + rect.z, 0.0f, 1.0f),
                fixedSize.y > 0.0f ? anchorMin.y : std::clamp(rect.y + rect.w, 0.0f, 1.0f)
            };
            const bool validX = fixedSize.x > 0.0f || anchorMax.x > anchorMin.x;
            const bool validY = fixedSize.y > 0.0f || anchorMax.y > anchorMin.y;
            if (validX && validY)
            {
                const float titleRowHeight = std::clamp(
                    scaled_scalar(
                        options.trafficLights.visible ?
                            options.trafficLights.firstCenter.y * 2.0f :
                            options.titleHeight,
                        ui.scale()),
                    1.0f,
                    panelGeometry.size.y);
                const float titleRowFraction = titleRowHeight / std::max(panelGeometry.size.y, 1.0f);
                const glm::vec2 titleAnchorMax {
                    anchorMax.x,
                    fixedSize.y > 0.0f ?
                        anchorMax.y :
                        std::max(anchorMin.y, std::min(anchorMax.y, anchorMin.y + titleRowFraction))
                };
                const glm::vec4 titleMargin = scaled_edges(
                    titleLayer.margin.x,
                    titleLayer.margin.y,
                    titleLayer.margin.z,
                    titleLayer.margin.w,
                    ui.scale());
                titleParent = scene.create_shape(
                    { 0.0f, 0.0f },
                    glm::vec2(1.0f),
                    make_solid_style("#00000000", "#00000000", 0.0f, 0.0f),
                    Renderer2DPrimitive::eRectangle);
                ui.set_layer(titleParent, -3, 1u);
                ui.attach_stretch(
                    titleParent,
                    panel,
                    anchorMin,
                    titleAnchorMax,
                    titleLayer.pivot,
                    titleMargin,
                    scaled_offset(titleLayer.offset.x, titleLayer.offset.y, ui.scale()),
                    fixedSize);
                titleAlignment = options.titleAlignment;
            }
        }
        else
        {
            const bool trafficOnLeft = options.trafficLights.visible &&
                options.trafficLights.placement == GlfwPanelWindowTrafficLightPlacement::eTopLeft;
            const float titleX = trafficOnLeft ?
                options.trafficLights.firstCenter.x + options.trafficLights.spacing * 3.0f + 8.0f :
                24.0f;
            titleOffset += glm::vec2(
                titleX,
                (options.trafficLights.visible ?
                    options.trafficLights.firstCenter.y :
                    options.titleHeight * 0.5f) - panelGeometry.logicalPanelSize.y * 0.5f);
        }

        const float titleFontSize = std::max(options.titleFontSize, 1.0f);
        const TextStyleComponent titleStyle = options.titleStyle.value_or(options.theme.text_style(500.0f));
        titleText = ui.create_aligned_text(
            options.title,
            fontAtlas,
            titleParent,
            titleAlignment,
            titleFontSize,
            titleStyle,
            2,
            0u,
            titleOffset);
    }

    entt::entity contentRoot = scene.create_shape(
        { 0.0f, 0.0f },
        glm::vec2(1.0f),
        make_solid_style("#00000000", "#00000000", 0.0f, 0.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(contentRoot, -2, 0u);
    // The content root is the safe app-owned area inside the reusable chrome
    ui.attach_fill(contentRoot, panel, options.contentMargin);

    const glm::vec2 contentSize {
        std::max(0.0f, panelGeometry.logicalPanelSize.x - options.contentMargin.x - options.contentMargin.z),
        std::max(0.0f, panelGeometry.logicalPanelSize.y - options.contentMargin.y - options.contentMargin.w)
    };
    GlfwPanelWindowTemplateContext context {
        ui,
        scene,
        fontAtlas,
        localisation,
        panel,
        titleBar,
        titleText,
        contentRoot,
        panelBackgroundLayerEntities,
        panelGeometry.logicalSize,
        contentSize,
        options
    };

    if (options.buildContent)
    {
        options.buildContent(context);
    }
    else
    {
        build_default_content(context);
    }

    scene.mark_dirty();
}

void GlfwPanelWindow::build_default_content(GlfwPanelWindowTemplateContext& context)
{
    UiBuilder& ui = context.ui;
    Renderer2DScene& scene = context.scene;

    entt::entity card = scene.create_shape(
        { 0.0f, 0.0f },
        glm::vec2(1.0f),
        make_solid_style("#111A24D8", "#FFFFFF24", 0.8f, 0.96f),
        Renderer2DPrimitive::eSquircle);
    ui.set_layer(card, 0, 0u);
    ui.set_shape(card, 18.0f);
    ui.attach_fill(card, context.contentRoot);
    ui.set_padding(card, 20.0f, 18.0f, 20.0f, 18.0f, true);

    ui.create_aligned_text(
        "Content Slot",
        context.fontAtlas,
        card,
        UiAlignment::eTopLeft,
        18.0f,
        panel_text_style("#F5FFFCF0"),
        2,
        0u,
        { 20.0f, 18.0f });
    ui.create_aligned_text(
        "Attach your own builder to draw controls, panels, media, or tools here.",
        context.fontAtlas,
        card,
        UiAlignment::eTopLeft,
        13.0f,
        panel_text_style("#AFC2C9D8"),
        2,
        1u,
        { 20.0f, 48.0f });
}

void GlfwPanelWindow::load_traffic_light_icons()
{
    trafficLightIcons = {};
    if (!engine || !options.trafficLights.visible)
    {
        return;
    }

    Media2DLoadOptions mediaOptions = {};
    // Rasterise tiny SVG glyphs above display size so hover icons stay crisp
    mediaOptions.rasterWidth = std::max(
        16u,
        static_cast<uint32_t>(std::round(std::max(options.trafficLights.iconSize.x, 1.0f) * 4.0f)));
    mediaOptions.rasterHeight = std::max(
        16u,
        static_cast<uint32_t>(std::round(std::max(options.trafficLights.iconSize.y, 1.0f) * 4.0f)));
    mediaOptions.premultiplyAlpha = true;

    const std::array<std::vector<std::filesystem::path>, 3> candidates {
        options.trafficLights.closeIconCandidates,
        options.trafficLights.minimiseIconCandidates,
        options.trafficLights.maximiseIconCandidates
    };

    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        for (const std::filesystem::path& path : candidates[i])
        {
            trafficLightIcons[i] = engine->load_media_2d(path, mediaOptions);
            if (trafficLightIcons[i].drawable)
            {
                break;
            }
        }
    }
}

bool GlfwPanelWindow::handle_window_action_press(const UiPointerButtonInput& input)
{
    if (!input.hasPoint || scene_blocks_window_action(input.point))
    {
        return false;
    }

    return begin_window_action(input.point);
}

bool GlfwPanelWindow::begin_window_action(glm::vec2 framebufferPoint)
{
    clear_window_action();

    activeResizeEdges = resize_edges_at(framebufferPoint);
    const bool resize = activeResizeEdges != eResizeNone;
    const bool move = !resize && title_hit_test(framebufferPoint);
    if (!resize && !move)
    {
        return false;
    }

    if (move)
    {
        begin_glfw_native_window_drag(window);
    }

    if (!cursor_screen_point(actionStartCursorScreen) ||
        !glfw_window_position(window, actionStartWindowPosition.x, actionStartWindowPosition.y) ||
        !glfw_window_size(window, actionStartWindowSize.x, actionStartWindowSize.y))
    {
        clear_window_action();
        return false;
    }

    windowPointerAction = resize ? WindowPointerAction::eResize : WindowPointerAction::eMove;
    return true;
}

void GlfwPanelWindow::clear_window_action()
{
    windowPointerAction = WindowPointerAction::eNone;
    activeResizeEdges = eResizeNone;
}

void GlfwPanelWindow::update_window_action()
{
    if (windowPointerAction == WindowPointerAction::eNone)
    {
        return;
    }

    if (!glfw_left_mouse_pressed(window))
    {
        clear_window_action();
        return;
    }

    glm::vec2 currentScreen(0.0f);
    if (!cursor_screen_point(currentScreen))
    {
        return;
    }

    const glm::vec2 delta = currentScreen - actionStartCursorScreen;
    if (windowPointerAction == WindowPointerAction::eMove)
    {
        set_glfw_window_position(
            window,
            actionStartWindowPosition.x + static_cast<int>(std::round(delta.x)),
            actionStartWindowPosition.y + static_cast<int>(std::round(delta.y)));
        return;
    }

    if (windowPointerAction != WindowPointerAction::eResize)
    {
        return;
    }

    int newX = actionStartWindowPosition.x;
    int newY = actionStartWindowPosition.y;
    int newWidth = actionStartWindowSize.x;
    int newHeight = actionStartWindowSize.y;
    const int dx = static_cast<int>(std::round(delta.x));
    const int dy = static_cast<int>(std::round(delta.y));
    const int minWidth = option_min(options.minSize.x);
    const int minHeight = option_min(options.minSize.y);
    const int maxWidth = option_max(options.maxSize.x, minWidth);
    const int maxHeight = option_max(options.maxSize.y, minHeight);

    if ((activeResizeEdges & eResizeLeft) != 0u)
    {
        newWidth = std::clamp(actionStartWindowSize.x - dx, minWidth, maxWidth);
        newX = actionStartWindowPosition.x + actionStartWindowSize.x - newWidth;
    }
    if ((activeResizeEdges & eResizeRight) != 0u)
    {
        newWidth = std::clamp(actionStartWindowSize.x + dx, minWidth, maxWidth);
    }
    if ((activeResizeEdges & eResizeTop) != 0u)
    {
        newHeight = std::clamp(actionStartWindowSize.y - dy, minHeight, maxHeight);
        newY = actionStartWindowPosition.y + actionStartWindowSize.y - newHeight;
    }
    if ((activeResizeEdges & eResizeBottom) != 0u)
    {
        newHeight = std::clamp(actionStartWindowSize.y + dy, minHeight, maxHeight);
    }

    set_glfw_window_position(window, newX, newY);
    set_glfw_window_size(window, newWidth, newHeight);
}

void GlfwPanelWindow::update_hover_cursor()
{
    if (!window)
    {
        return;
    }

    if (windowPointerAction == WindowPointerAction::eResize)
    {
        set_hovered_traffic_light(TrafficLightKind::eNone);
        set_glfw_cursor_for_kind(window, cursors, cursor_for_resize_edges(activeResizeEdges));
        return;
    }
    if (windowPointerAction == WindowPointerAction::eMove)
    {
        set_hovered_traffic_light(TrafficLightKind::eNone);
        set_glfw_cursor_for_kind(window, cursors, UiCursorKind::ePointer);
        return;
    }

    glm::vec2 point(0.0f);
    if (!cursor_framebuffer_point(point))
    {
        ui_update_input_hover(
            engine->renderer2d_scene(),
            engine->renderer2d_font_atlas(),
            inputState,
            false,
            glm::vec2(0.0f));
        set_hovered_traffic_light(TrafficLightKind::eNone);
        clear_glfw_cursor(window);
        return;
    }

    const bool blocksWindowAction = scene_blocks_window_action(point);
    const TrafficLightKind light = !blocksWindowAction ?
        traffic_light_at(point) : TrafficLightKind::eNone;
    if (light != TrafficLightKind::eNone)
    {
        ui_update_input_hover(
            engine->renderer2d_scene(),
            engine->renderer2d_font_atlas(),
            inputState,
            false,
            glm::vec2(0.0f));
        set_hovered_traffic_light(light);
        set_glfw_cursor_for_kind(
            window,
            cursors,
            traffic_light_enabled(light) ? UiCursorKind::ePointer : UiCursorKind::eUnavailable);
        return;
    }
    set_hovered_traffic_light(TrafficLightKind::eNone);

    const uint32_t edges = resize_edges_at(point);
    if (!blocksWindowAction && edges != eResizeNone)
    {
        ui_update_input_hover(
            engine->renderer2d_scene(),
            engine->renderer2d_font_atlas(),
            inputState,
            false,
            glm::vec2(0.0f));
        set_glfw_cursor_for_kind(window, cursors, cursor_for_resize_edges(edges));
        return;
    }
    if (!blocksWindowAction && title_hit_test(point))
    {
        ui_update_input_hover(
            engine->renderer2d_scene(),
            engine->renderer2d_font_atlas(),
            inputState,
            false,
            glm::vec2(0.0f));
        set_glfw_cursor_for_kind(window, cursors, UiCursorKind::ePointer);
        return;
    }

    const UiHoverResult hover = ui_update_input_hover(
        engine->renderer2d_scene(),
        engine->renderer2d_font_atlas(),
        inputState,
        true,
        point);
    if (hover.cursor != UiCursorKind::eDefault)
    {
        set_glfw_cursor_for_kind(window, cursors, hover.cursor);
        return;
    }

    if (!blocksWindowAction && edges != eResizeNone)
    {
        set_glfw_cursor_for_kind(window, cursors, cursor_for_resize_edges(edges));
    }
    else if (!blocksWindowAction && title_hit_test(point))
    {
        set_glfw_cursor_for_kind(window, cursors, UiCursorKind::ePointer);
    }
    else
    {
        clear_glfw_cursor(window);
    }
}

bool GlfwPanelWindow::cursor_framebuffer_point(glm::vec2& point) const
{
    int renderWidth = 0;
    int renderHeight = 0;
    if (!glfw_framebuffer_size(window, renderWidth, renderHeight))
    {
        return false;
    }
    return glfw_cursor_framebuffer_point(window, renderWidth, renderHeight, point);
}

bool GlfwPanelWindow::cursor_screen_point(glm::vec2& point) const
{
    glm::vec2 cursorPoint(0.0f);
    int windowX = 0;
    int windowY = 0;
    if (!glfw_cursor_window_point(window, cursorPoint) || !glfw_window_position(window, windowX, windowY))
    {
        return false;
    }

    point = {
        static_cast<float>(windowX) + cursorPoint.x,
        static_cast<float>(windowY) + cursorPoint.y
    };
    return true;
}

uint32_t GlfwPanelWindow::resize_edges_at(glm::vec2 windowPoint) const
{
    if (!options.resizable || glfw_window_maximized(window) || trafficLightMaximiseActive)
    {
        return eResizeNone;
    }

    PanelGeometry panelGeometry = {};
    if (!panel_geometry_for_window(window, options.panelInset, panelGeometry))
    {
        return eResizeNone;
    }

    const float panelX = panelGeometry.position.x;
    const float panelY = panelGeometry.position.y;
    const float panelWidth = panelGeometry.size.x;
    const float panelHeight = panelGeometry.size.y;
    const float panelRight = panelX + panelWidth;
    const float panelBottom = panelY + panelHeight;
    const float thickness = std::max(scaled_scalar(options.resizeEdgeThickness, panelGeometry.scale), 1.0f);
    const float halfThickness = thickness * 0.5f;

    if (windowPoint.x < panelX - halfThickness ||
        windowPoint.x > panelRight + halfThickness ||
        windowPoint.y < panelY - halfThickness ||
        windowPoint.y > panelBottom + halfThickness)
    {
        return eResizeNone;
    }

    uint32_t edges = eResizeNone;
    if (windowPoint.x >= panelX - halfThickness && windowPoint.x <= panelX + halfThickness)
    {
        edges |= eResizeLeft;
    }
    else if (windowPoint.x >= panelRight - halfThickness && windowPoint.x <= panelRight + halfThickness)
    {
        edges |= eResizeRight;
    }

    if (windowPoint.y >= panelY - halfThickness && windowPoint.y <= panelY + halfThickness)
    {
        edges |= eResizeTop;
    }
    else if (windowPoint.y >= panelBottom - halfThickness && windowPoint.y <= panelBottom + halfThickness)
    {
        edges |= eResizeBottom;
    }
    return edges;
}

bool GlfwPanelWindow::title_hit_test(glm::vec2 windowPoint) const
{
    if (!options.draggable)
    {
        return false;
    }

    PanelGeometry panelGeometry = {};
    if (!panel_geometry_for_window(window, options.panelInset, panelGeometry))
    {
        return false;
    }

    const float panelX = panelGeometry.position.x;
    const float panelY = panelGeometry.position.y;
    const float panelWidth = panelGeometry.size.x;
    const float panelHeight = panelGeometry.size.y;
    const float dragHeight = options.topDragHeight.value_or(
        logical_title_height(options));
    if (dragHeight <= 0.0f)
    {
        return false;
    }
    const float titleBottom = panelY +
        std::min(
            scaled_scalar(dragHeight, panelGeometry.scale),
            panelHeight);
    return windowPoint.x >= panelX &&
        windowPoint.x <= panelX + panelWidth &&
        windowPoint.y >= panelY &&
        windowPoint.y <= titleBottom;
}

bool GlfwPanelWindow::scene_blocks_window_action(glm::vec2 framebufferPoint) const
{
    if (!engine)
    {
        return true;
    }

    Renderer2DScene& scene = engine->renderer2d_scene();
    const entt::registry& registry = scene.registry();
    if (inputState.pressedInputEntity != entt::null ||
        inputState.pointerInputCapture != entt::null ||
        inputState.activeSlider != entt::null ||
        inputState.activeScrollBar != entt::null)
    {
        return true;
    }

    if (scene.draggable_parent_at(framebufferPoint) != entt::null)
    {
        return true;
    }

    uint32_t resizeEdges = ePanelResizeNone;
    if (ui_resizable_panel_at(scene, framebufferPoint, resizeEdges) != entt::null)
    {
        return true;
    }

    const entt::entity hit = scene.entity_at(framebufferPoint);
    const entt::entity buttonOwner = ui_component_owner<ButtonInputComponent>(registry, hit);
    if (buttonOwner != entt::null)
    {
        const ButtonInputComponent* button = registry.try_get<ButtonInputComponent>(buttonOwner);
        return button && button->enabled;
    }

    const entt::entity textOwner = ui_component_owner<TextInputComponent>(registry, hit);
    if (textOwner != entt::null)
    {
        const TextInputComponent* textInput = registry.try_get<TextInputComponent>(textOwner);
        return textInput && textInput->enabled;
    }

    const entt::entity dropOwner = ui_component_owner<DropTargetComponent>(registry, hit);
    if (dropOwner != entt::null)
    {
        const DropTargetComponent* dropTarget = registry.try_get<DropTargetComponent>(dropOwner);
        return dropTarget && dropTarget->enabled;
    }

    const entt::entity scrollBarOwner = ui_component_owner<ScrollBarInputComponent>(registry, hit);
    if (scrollBarOwner != entt::null)
    {
        const ScrollBarInputComponent* scrollBar = registry.try_get<ScrollBarInputComponent>(scrollBarOwner);
        return scrollBar && scrollBar->enabled;
    }

    const entt::entity sliderOwner = ui_component_owner<SliderInputComponent>(registry, hit);
    if (sliderOwner != entt::null)
    {
        const SliderInputComponent* slider = registry.try_get<SliderInputComponent>(sliderOwner);
        return slider && slider->enabled;
    }

    return false;
}

GlfwPanelWindow::TrafficLightKind GlfwPanelWindow::traffic_light_at(glm::vec2 windowPoint) const
{
    if (!options.trafficLights.visible || !options.trafficLights.accessible)
    {
        return TrafficLightKind::eNone;
    }

    PanelGeometry panelGeometry = {};
    if (!panel_geometry_for_window(window, options.panelInset, panelGeometry))
    {
        return TrafficLightKind::eNone;
    }

    const std::array<TrafficLightKind, 3> lights {
        TrafficLightKind::eClose,
        TrafficLightKind::eMinimise,
        TrafficLightKind::eMaximise
    };
    const float hitRadius = std::max(
        scaled_scalar(options.trafficLights.radius + 4.0f, panelGeometry.scale),
        scaled_scalar(10.0f, panelGeometry.scale));
    for (std::size_t i = 0; i < lights.size(); ++i)
    {
        const glm::vec2 center = traffic_light_center_at_index(
            options.trafficLights,
            static_cast<int>(i),
            panelGeometry);
        const glm::vec2 delta = windowPoint - center;
        if (glm::dot(delta, delta) <= hitRadius * hitRadius)
        {
            return lights[i];
        }
    }
    return TrafficLightKind::eNone;
}

glm::vec2 GlfwPanelWindow::traffic_light_center(TrafficLightKind kind) const
{
    PanelGeometry panelGeometry = {};
    if (!panel_geometry_for_window(window, options.panelInset, panelGeometry))
    {
        return glm::vec2(0.0f);
    }

    const int index = kind == TrafficLightKind::eClose ?
        0 :
        kind == TrafficLightKind::eMinimise ?
            1 :
            2;

    return traffic_light_center_at_index(options.trafficLights, index, panelGeometry);
}

bool GlfwPanelWindow::traffic_light_enabled(TrafficLightKind kind) const
{
    if (!options.trafficLights.visible || !options.trafficLights.accessible)
    {
        return false;
    }

    switch (kind)
    {
    case TrafficLightKind::eClose:
        return true;
    case TrafficLightKind::eMinimise:
        return options.trafficLights.minimiseEnabled;
    case TrafficLightKind::eMaximise:
        return options.trafficLights.maximiseEnabled;
    default:
        return false;
    }
}

Media2DHandle GlfwPanelWindow::traffic_light_icon(TrafficLightKind kind) const
{
    switch (kind)
    {
    case TrafficLightKind::eClose:
        return trafficLightIcons[0];
    case TrafficLightKind::eMinimise:
        return trafficLightIcons[1];
    case TrafficLightKind::eMaximise:
        return trafficLightIcons[2];
    default:
        return {};
    }
}

void GlfwPanelWindow::set_hovered_traffic_light(TrafficLightKind kind)
{
    if (hoveredTrafficLight == kind)
    {
        return;
    }

    auto traffic_light_index = [](TrafficLightKind light) -> std::optional<std::size_t> {
        switch (light)
        {
        case TrafficLightKind::eClose:
            return 0u;
        case TrafficLightKind::eMinimise:
            return 1u;
        case TrafficLightKind::eMaximise:
            return 2u;
        default:
            return std::nullopt;
        }
    };

    auto set_icon_visible = [&](TrafficLightKind light, bool visible) {
        if (!engine)
        {
            return;
        }

        const std::optional<std::size_t> index = traffic_light_index(light);
        if (!index)
        {
            return;
        }

        Renderer2DScene& scene = engine->renderer2d_scene();
        entt::registry& registry = scene.registry();
        const entt::entity icon = trafficLightIconEntities[*index];
        if (icon == entt::null || !registry.valid(icon))
        {
            return;
        }

        RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(icon);
        if (!layer || layer->visible == visible)
        {
            return;
        }

        layer->visible = visible;
        scene.mark_dirty(icon);
    };

    const TrafficLightKind previous = hoveredTrafficLight;
    hoveredTrafficLight = kind;
    set_icon_visible(previous, false);
    set_icon_visible(hoveredTrafficLight, true);
}

void GlfwPanelWindow::perform_traffic_light_action(TrafficLightKind kind)
{
    if (!traffic_light_enabled(kind))
    {
        return;
    }

    switch (kind)
    {
    case TrafficLightKind::eClose:
        set_glfw_window_should_close(window, true);
        break;
    case TrafficLightKind::eMinimise:
        iconify_glfw_window(window);
        break;
    case TrafficLightKind::eMaximise:
        if (trafficLightMaximiseActive)
        {
            restore_traffic_light_maximise();
        }
        else
        {
            if (glfw_window_maximized(window))
            {
                restore_glfw_window(window);
                break;
            }

            int windowX = 0;
            int windowY = 0;
            int windowWidth = 0;
            int windowHeight = 0;
            if (!glfw_window_position(window, windowX, windowY) ||
                !glfw_window_size(window, windowWidth, windowHeight))
            {
                break;
            }
            trafficLightRestorePosition = { windowX, windowY };
            trafficLightRestoreSize = { windowWidth, windowHeight };
            activeTrafficLightMaximiseMode = options.trafficLights.maximiseMode;

            if (activeTrafficLightMaximiseMode ==
                GlfwPanelWindowMaximiseMode::eFullscreen)
            {
                if (!set_glfw_window_fullscreen(window, true))
                {
                    break;
                }
                trafficLightMaximiseActive = true;
                break;
            }

            glm::ivec2 workPosition(0);
            glm::ivec2 workSize(0);
            if (!glfw_window_monitor_work_area(window, workPosition, workSize))
            {
                break;
            }
            if (activeTrafficLightMaximiseMode ==
                GlfwPanelWindowMaximiseMode::eWidthAndHeight)
            {
                maximize_glfw_window(window);
            }
            else if (activeTrafficLightMaximiseMode ==
                GlfwPanelWindowMaximiseMode::eWidth)
            {
                set_glfw_window_position(window, workPosition.x, windowY);
                set_glfw_window_size(window, workSize.x, windowHeight);
            }
            else
            {
                set_glfw_window_position(window, windowX, workPosition.y);
                set_glfw_window_size(window, windowWidth, workSize.y);
            }
            trafficLightMaximiseActive = true;
        }
        break;
    default:
        break;
    }
}

void GlfwPanelWindow::restore_traffic_light_maximise()
{
    if (!window || !trafficLightMaximiseActive)
    {
        return;
    }

    if (activeTrafficLightMaximiseMode ==
        GlfwPanelWindowMaximiseMode::eFullscreen)
    {
        set_glfw_window_fullscreen(
            window,
            false,
            trafficLightRestorePosition,
            trafficLightRestoreSize);
    }
    else
    {
        if (glfw_window_maximized(window))
        {
            restore_glfw_window(window);
        }
        set_glfw_window_position(
            window,
            trafficLightRestorePosition.x,
            trafficLightRestorePosition.y);
        set_glfw_window_size(
            window,
            trafficLightRestoreSize.x,
            trafficLightRestoreSize.y);
    }
    trafficLightMaximiseActive = false;
}

UiCursorKind GlfwPanelWindow::cursor_for_resize_edges(uint32_t edges) const
{
    const bool left = (edges & eResizeLeft) != 0u;
    const bool right = (edges & eResizeRight) != 0u;
    const bool top = (edges & eResizeTop) != 0u;
    const bool bottom = (edges & eResizeBottom) != 0u;

    if ((left && top) || (right && bottom))
    {
        return UiCursorKind::eResizeNwse;
    }
    if ((right && top) || (left && bottom))
    {
        return UiCursorKind::eResizeNesw;
    }
    if (left || right)
    {
        return UiCursorKind::eResizeEw;
    }
    if (top || bottom)
    {
        return UiCursorKind::eResizeNs;
    }
    return UiCursorKind::eDefault;
}

GlfwPanelWindowHost::GlfwPanelWindowHost(GlfwPanelWindowHostOptions options) :
    hostOptions(std::move(options))
{
}

GlfwPanelWindowHost::~GlfwPanelWindowHost()
{
    close();
}

void GlfwPanelWindowHost::set_options(GlfwPanelWindowHostOptions options)
{
    const bool wasOpen = is_open();
    if (wasOpen)
    {
        close();
    }
    hostOptions = std::move(options);
    if (wasOpen)
    {
        request_open();
    }
}

const GlfwPanelWindowHostOptions& GlfwPanelWindowHost::options() const
{
    return hostOptions;
}

void GlfwPanelWindowHost::request_open()
{
    openRequested = true;
}

bool GlfwPanelWindowHost::open()
{
    openRequested = false;
    if (hostedPanel && !hostedPanel->should_close())
    {
        return true;
    }

    close();

    GlfwWindowCreateInfo windowCreateInfo = {};
    windowCreateInfo.width = hostOptions.width;
    windowCreateInfo.height = hostOptions.height;
    windowCreateInfo.name = hostOptions.title.c_str();
    windowCreateInfo.transparentFramebuffer = hostOptions.transparentFramebuffer;
    windowCreateInfo.decorated = hostOptions.decorated;
    windowCreateInfo.alwaysOnTop = hostOptions.alwaysOnTop;
    windowCreateInfo.focusOnShow = hostOptions.focusOnShow;
    windowCreateInfo.showInTaskbar = hostOptions.showInTaskbar;
    windowCreateInfo.showInAltTab = hostOptions.showInAltTab;
    if (hostOptions.positioning)
    {
        windowCreateInfo.position = resolve_glfw_window_position(
            { hostOptions.width, hostOptions.height },
            *hostOptions.positioning);
    }
    if (!windowCreateInfo.position && hostOptions.hasInitialPosition)
    {
        windowCreateInfo.position = { hostOptions.x, hostOptions.y };
    }

    hostedWindow = build_glfw_window(windowCreateInfo);
    if (!hostedWindow)
    {
        if (Logger* logger = Logger::fetch_logger())
        {
            logger->error("Panel window host could not create a GLFW window.");
        }
        return false;
    }

    hostedEngine = build_engine_for_window();
    if (!hostedEngine)
    {
        close();
        return false;
    }

    hostedPanel = std::make_unique<GlfwPanelWindow>(
        hostedWindow,
        hostedEngine,
        make_template_options());
    return true;
}

void GlfwPanelWindowHost::close()
{
    hostedPanel.reset();
    delete hostedEngine;
    hostedEngine = nullptr;
    if (hostedWindow)
    {
        destroy_glfw_window(hostedWindow);
        hostedWindow = nullptr;
    }
    openRequested = false;
}

void GlfwPanelWindowHost::tick()
{
    if (openRequested)
    {
        open();
    }
    if (!hostedPanel)
    {
        return;
    }
    if (hostedPanel->should_close())
    {
        close();
        return;
    }
    hostedPanel->tick();
    if (hostedPanel && hostedPanel->should_close())
    {
        close();
    }
}

bool GlfwPanelWindowHost::is_open() const
{
    return hostedPanel != nullptr;
}

bool GlfwPanelWindowHost::should_close() const
{
    return hostedPanel && hostedPanel->should_close();
}

bool GlfwPanelWindowHost::refresh_template()
{
    if (!hostedPanel || !hostedEngine)
    {
        return false;
    }
    hostedPanel->set_template_options(make_template_options());
    return true;
}

bool GlfwPanelWindowHost::is_hovered() const
{
    return hostedPanel && hostedPanel->is_hovered();
}

GLFWwindow* GlfwPanelWindowHost::window() const
{
    return hostedWindow;
}

Engine* GlfwPanelWindowHost::engine() const
{
    return hostedEngine;
}

GlfwPanelWindow* GlfwPanelWindowHost::panel() const
{
    return hostedPanel.get();
}

Engine* GlfwPanelWindowHost::build_engine_for_window()
{
    if (!hostedWindow)
    {
        return nullptr;
    }

    int framebufferWidth = 0;
    int framebufferHeight = 0;
    if (!glfw_framebuffer_size(hostedWindow, framebufferWidth, framebufferHeight))
    {
        return nullptr;
    }

    uint32_t glfwExtensionCount = 0;
    const char** glfwExtensions = glfw_required_instance_extensions(glfwExtensionCount);

    EngineCreateInfo engineCreateInfo = {};
    engineCreateInfo.applicationName = hostOptions.title.c_str();
    engineCreateInfo.framebufferWidth = static_cast<uint32_t>(framebufferWidth);
    engineCreateInfo.framebufferHeight = static_cast<uint32_t>(framebufferHeight);
    engineCreateInfo.maxRenderPixels = hostOptions.maxRenderPixels;
    engineCreateInfo.msaaSamples = hostOptions.msaaSamples;
    engineCreateInfo.presentMode = hostOptions.presentMode;
    engineCreateInfo.renderBackend = hostOptions.renderBackend;
    engineCreateInfo.presentationBackend = hostOptions.presentationBackend;
    engineCreateInfo.targetFrameRate = hostOptions.targetFrameRate;
    engineCreateInfo.instanceExtensionCount = glfwExtensionCount;
    engineCreateInfo.instanceExtensions = glfwExtensions;
    engineCreateInfo.surfaceUserData = hostedWindow;
    engineCreateInfo.nativeWindowHandle = glfw_native_window_handle(hostedWindow);
    engineCreateInfo.createSurface = vibrance_glfw_create_surface;
    engineCreateInfo.transparentFramebuffer = hostOptions.transparentFramebuffer;
    engineCreateInfo.enableAudio = hostOptions.enableAudio;
    if (hostOptions.configureEngine)
    {
        hostOptions.configureEngine(engineCreateInfo);
    }
    return new Engine(engineCreateInfo);
}

GlfwPanelWindowTemplateOptions GlfwPanelWindowHost::make_template_options()
{
    GlfwPanelWindowTemplateOptions options = {};
    if (hostedEngine && hostOptions.makeTemplateOptions)
    {
        options = hostOptions.makeTemplateOptions(*hostedEngine);
    }
    if (hostOptions.topDragHeight)
    {
        options.topDragHeight = std::max(
            *hostOptions.topDragHeight,
            0.0f);
        options.draggable = *options.topDragHeight > 0.0f;
    }
    return options;
}

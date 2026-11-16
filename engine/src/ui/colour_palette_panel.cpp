// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/ui/colour_palette_panel.h>
#include <vibranceUI/graphics/colour_palette.h>
#include <vibranceUI/ui/control_appearance.h>
#include <vibranceUI/graphics/svg_colour_template.h>
#include <vibranceUI/platform/image_services.h>
#include <vibranceUI/core/file.h>
#include <vibranceUI/ui/builder.h>
#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/liquid_controls.h>
#include <vibranceUI/ui/resource_loading.h>
#include <vibranceUI/ui/text.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <future>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stop_token>
#include <utility>

namespace
{
constexpr float panelWidth = 416.0f;
constexpr float panelHeight = 736.0f;
constexpr float pi = 3.14159265358979323846f;

glm::vec4 paint(std::uint32_t colour)
{
    return { float((colour >> 16u) & 255u) / 255.0f,
        float((colour >> 8u) & 255u) / 255.0f,
        float(colour & 255u) / 255.0f, 1.0f };
}

std::uint32_t components(float red, float green, float blue)
{
    const auto channel = [](float value) { return std::uint32_t(std::lround(std::clamp(value, 0.0f, 255.0f))); };
    return channel(red) << 16u | channel(green) << 8u | channel(blue);
}

std::string number(float value, int decimals = 0)
{
    std::ostringstream result;
    result << std::fixed << std::setprecision(decimals) << value;
    return result.str();
}
}

struct UiColourPalettePanel::Impl
{
    enum class Action { eNone, eFile, eClipboard, eRename, eRemove, eCopy, eEyedropper };
    enum class Surface { eNone, eWheel, eImage };
    struct Imported
    {
        colour_palette::Image image;
        std::string error;
        std::uint64_t generation = 0u;
    };
    struct Channel
    {
        entt::entity slider = entt::null;
        entt::entity field = entt::null;
        entt::entity stroke = entt::null;
        unsigned index = 0u;
        float maximum = 255.0f;
    };

    UiColourPalettePanelCreateInfo info;
    UiTheme appearanceTheme = ui_light_theme();
    GlfwPanelWindowHost windowHost;
    Change preview;
    Change commit;
    std::string targetLabel;
    std::string status;
    std::string search;
    std::string renameValue;
    std::uint32_t rgb = 0xEE4545u;
    colour_palette::Hsv hsv { 0.0f, 0.7f, 0.94f };
    std::array<std::optional<std::uint32_t>, 40> swatches {};
    std::size_t tab = 0u;
    std::size_t sliderMode = 0u;
    std::size_t paletteMode = 0u;
    std::size_t selectedImage = 0u;
    colour_palette::ImageLibrary images;
    colour_palette::Image wheel;
    colour_palette::Image spectrum;
    std::filesystem::path directory;
    std::vector<std::filesystem::path> temporaryFiles;
    std::array<Media2DHandle, 5> icons {};
    Media2DHandle chevron, check, sampler;
    vibrance::SvgColourTemplate crayonArtwork;
    std::array<Media2DHandle, 48> crayonHandles {};
    std::array<std::filesystem::path, 48> crayonPaths {};
    bool crayonArtworkAttempted = false;
    std::array<entt::entity, 48> crayonEntities {};
    std::array<float, 48> crayonLift {}, crayonVelocity {};
    std::optional<std::size_t> selectedCrayon, hoveredCrayon;
    bool crayonAnimating = false;
    double lastCrayonFrame = 0.0;
    Media2DHandle wheelHandle;
    Media2DHandle spectrumHandle;
    std::vector<std::pair<std::filesystem::path, Media2DHandle>> imageHandles;
    std::uint64_t imageSerial = 0u;
    unsigned uploadedImages = 0u;
    std::vector<Channel> channels;
    entt::entity hexField = entt::null;
    entt::entity brightness = entt::null;
    entt::entity brightnessStroke = entt::null;
    entt::entity swatch = entt::null;
    entt::entity marker = entt::null;
    entt::entity samplingSurface = entt::null;
    entt::entity statusText = entt::null;
    entt::entity pencilName = entt::null;
    entt::entity actionLabel = entt::null;
    entt::entity renameField = entt::null;
    entt::entity previousFocused = entt::null;
    std::array<entt::entity, 40> swatchEntities {};
    Surface dragging = Surface::eNone;
    Action pendingAction = Action::eNone;
    bool openRequested = false;
    bool rebuildPending = false;
    bool visualDirty = true;
    bool previewPending = false;
    bool commitPending = false;
    bool editedSinceCommit = false;
    bool livePreview = true;
    bool renaming = false;
    bool pickingScreen = false;
    bool screenReleased = false;
    bool nativeScreenPicker = false;
    bool resetEngine = false;
    std::optional<std::uint32_t> lastPreview;
    std::optional<std::uint32_t> lastCommit;
    std::future<Imported> import;
    std::stop_source importStop;
    std::uint64_t importGeneration = 0u;
    std::chrono::steady_clock::time_point previewAt {};
    std::chrono::steady_clock::time_point editCommitAt {};
    std::shared_ptr<std::uint64_t> lifetimeGeneration = std::make_shared<std::uint64_t>(0u);

    explicit Impl(UiColourPalettePanelCreateInfo createInfo) : info(std::move(createInfo))
    {
        livePreview = info.livePreview;
        appearanceTheme = info.theme;
        crayonArtwork = vibrance::SvgColourTemplate(info.pencilColourMarker);
        info.uiScale = std::clamp(info.uiScale, 0.5f, 2.0f);
        info.previewIntervalSeconds = std::clamp(info.previewIntervalSeconds, 1.0 / 120.0, 1.0);
        static std::atomic<std::uint64_t> serial { 0u };
        directory = std::filesystem::temp_directory_path() / ("vibrance-colour-palette-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
            std::to_string(serial.fetch_add(1u)));
        std::error_code error;
        std::filesystem::create_directory(directory, error);
        GlfwPanelWindowHostOptions options;
        options.width = static_cast<int>(std::lround(panelWidth * info.uiScale));
        options.height = static_cast<int>(std::lround(panelHeight * info.uiScale));
        options.title = "Colours";
        options.positioning = info.positioning;
        options.fitToWorkArea = true;
        options.workAreaMargins = { 12, 12, 12, 12 };
        options.transparentFramebuffer = true;
        options.focusOnShow = info.focusOnShow;
        options.showInTaskbar = true;
        options.showInAltTab = true;
        options.deferCloseToOwner = true;
        options.msaaSamples = 1u;
        options.enableAudio = false;
        options.targetFrameRate = 60u;
        options.topDragHeight = 64.0f;
#if defined(_WIN32)
        options.presentationBackend = PresentationBackend::eWindowsCompositionD3D11;
#endif
        options.configureEngine = [this](EngineCreateInfo& engineOptions) {
            if (info.configureEngine) info.configureEngine(engineOptions, info.resources, info.locale);
        };
        options.makeTemplateOptions = [this](Engine& engine) { return template_options(engine); };
        windowHost.set_options(std::move(options));
    }

    ~Impl()
    {
        close(true);
        lifetimeGeneration.reset();
        importStop.request_stop();
        if (import.valid()) import.wait();
        std::error_code error;
        for (const auto& path : temporaryFiles)
            if (path.parent_path() == directory) std::filesystem::remove(path, error);
        std::filesystem::remove(directory, error);
    }

    bool dark() const { const auto c = renderer2d_hex_color(appearanceTheme.panel_palette().contentSurface);
        return c.r * .2126f + c.g * .7152f + c.b * .0722f < .5f; }
    std::string foreground() const { return appearanceTheme.text; }
    std::string muted() const { return appearanceTheme.textMuted; }
    std::string surface() const { return appearanceTheme.panel_palette().cardSurface; }
    std::string outline() const { return appearanceTheme.panel_palette().cardOutline; }
    std::string tr(std::string_view key, std::string_view fallback) const
    {
        return windowHost.engine() ? windowHost.engine()->resolve_text(ui_tr(key, fallback)) : std::string(fallback);
    }

    std::string colour_name(std::string_view name) const
    {
        std::string key = "colour_palette.";
        for (const unsigned char character : name)
            key += character == ' ' ? '_' : char(std::tolower(character));
        return tr(key, name);
    }

    GlfwPanelWindowTemplateOptions template_options(Engine& engine)
    {
        engine.load_localisation_directories(info.localisationDirectories);
        if (!info.locale.empty()) engine.set_locale(info.locale);
        GlfwPanelWindowTemplateOptions options;
        options.title = tr("colour_palette.title", "Colours");
        if (windowHost.window()) set_glfw_window_title(windowHost.window(), options.title.c_str());
        options.uiScale = info.uiScale;
        options.resizable = false;
        options.minSize = { 240, 400 };
        options.showTitleText = false;
        options.theme = appearanceTheme;
        ui_panel_transitions(options);
        options.contentMargin = {};
        options.trafficLights = UiPanelAppearance::traffic_lights(appearanceTheme);
        if (info.configurePanel) info.configurePanel(options, info.resources, info.locale);
        options.beforeDraw = [this](Engine& renderer) {
            if (visualDirty) refresh_visuals();
            refresh_colour_thumbs();
            const auto material = info.controlMaterial ? info.controlMaterial(appearanceTheme) : ui_liquid_glass_options();
            ui_update_liquid_controls(renderer.renderer2d_scene(), material.material == GlassMaterial::eLiquid,
                material, false, entt::null, true, !info.controlMaterial);
        };
        options.buildContent = [this](GlfwPanelWindowTemplateContext& context) { build(context); };
        return options;
    }

    entt::entity rectangle(UiBuilder& ui, entt::entity parent, glm::vec2 position,
        glm::vec2 size, const ShapeStyleComponent& style, float radius = 0.0f, int layer = 2)
    {
        auto entity = ui.scene().create_shape({}, scaled_size(size.x, size.y, ui.scale()), style,
            radius > 0.0f ? Renderer2DPrimitive::eRoundedRectangle : Renderer2DPrimitive::eRectangle);
        ui.set_shape(entity, radius);
        ui.set_layer(entity, layer, 0u);
        ui.attach_aligned(entity, parent, UiAlignment::eTopLeft,
            scaled_offset(position.x, position.y, ui.scale()), scaled_size(size.x, size.y, ui.scale()));
        return entity;
    }

    entt::entity label(UiBuilder& ui, const Renderer2DFontAtlas& font,
        entt::entity parent, std::string text, glm::vec2 position,
        float size = 15.0f, std::string colour = {}, int layer = 4)
    {
        return ui.create_aligned_text(std::move(text), font, parent, UiAlignment::eTopLeft,
            size, make_flat_text_style(colour.empty() ? foreground() : colour), layer, 0u, position);
    }

    UiControlHandle button(UiBuilder& ui, const Renderer2DFontAtlas& font,
        entt::entity parent, std::string text, glm::vec2 position, glm::vec2 size,
        std::function<void(const PointerInputEvent&)> clicked, bool clear = false)
    {
        auto options = ui_panel_button_options(appearanceTheme);
        if (clear)
        {
            options.backgroundColor = options.hoveredBackgroundColor = "#00000000";
            options.outlineColor = options.hoveredOutlineColor = options.pressedOutlineColor = "#00000000";
        }
        options.onClick = std::move(clicked);
        return ui_create_panel_text_button(ui, font, parent, UiAlignment::eTopLeft,
            position, std::move(text), size, 4, 0u, std::move(options));
    }

    UiControlHandle field(UiBuilder& ui, const Renderer2DFontAtlas& font,
        entt::entity parent, glm::vec2 position, glm::vec2 size,
        std::string value, std::function<void(const std::string&)> changed,
        std::string placeholder = {}, bool searchShape = false,
        TextHorizontalAlignment2D alignment = TextHorizontalAlignment2D::eStart)
    {
        UiSearchFieldOptions options;
        options.placeholder = std::move(placeholder);
        options.textColor = foreground();
        options.placeholderColor = muted();
        options.backgroundColor = dark() ? "#242426FF" : "#FFFFFFFF";
        options.focusedBackgroundColor = options.backgroundColor;
        options.backgroundOutlineColor = outline();
        options.focusedOutlineColor = appearanceTheme.accent;
        options.backgroundOutlineWidth = 0.8f;
        options.backgroundOpacity = 1.0f;
        options.maxBytes = 128u;
        if (alignment != TextHorizontalAlignment2D::eStart) options.textAlignment = alignment;
        options.onChanged = std::move(changed);
        auto result = ui_create_search_field(ui, font, parent, UiAlignment::eTopLeft,
            position, size, 4, 0u, std::move(options));
        ui.set_shape(result.root, searchShape ? size.y * .5f : 7.0f);
        ui.registry().get<TextComponent>(result.label).fontSize = scaled_scalar(searchShape ? 16.0f : 18.0f, ui.scale());
        auto& input = ui.registry().get<TextInputComponent>(result.root);
        input.caretHeight = scaled_scalar(18.0f, ui.scale());
        input.value = std::move(value);
        ui_update_text_input_visual(ui.scene(), font, result.root);
        return result;
    }

    UiDropdownOptions dropdown() const
    {
        auto options = ui_panel_dropdown_options(appearanceTheme, chevron, check);
        options.menuWidth = 230.0f;
        return options;
    }

    UiControlHandle create_dropdown(UiBuilder& ui, const Renderer2DFontAtlas& font,
        entt::entity parent, UiAlignment alignment, glm::vec2 offset, glm::vec2 size,
        std::vector<std::string> items, int layer, std::uint32_t order, UiDropdownOptions options)
    {
        auto result = ui_create_dropdown(ui, font, parent, alignment, offset, size,
            std::move(items), layer, order, std::move(options));
        auto& control = ui.registry().get<UiDropdownControlComponent>(result.root);
        const auto preserve = [&](entt::entity entity) {
            if (auto* visual = ui.registry().try_get<ButtonVisualComponent>(entity))
                visual->hovered = visual->idle;
        };
        preserve(result.root);
        for (const auto row : control.itemRows) preserve(row);
        return result;
    }

    Media2DHandle texture(const colour_palette::Image& image,
        const std::filesystem::path& path, Engine& engine)
    {
        if (!image.valid()) return {};
        if (!ui_path_exists(path))
        {
            std::string error;
            if (!colour_palette::write_image_preview(image, path, error)) { status = error; return {}; }
            temporaryFiles.push_back(path);
        }
        Media2DLoadOptions options;
        options.generateMipmaps = false;
        options.premultiplyAlpha = true;
        if (std::max(image.width, image.height) > 512u)
        {
            options.resizeRasterImages = true;
            if (image.width >= image.height) options.rasterWidth = 512u;
            else options.rasterHeight = 512u;
        }
        return engine.load_media_2d(path, options);
    }

    const colour_palette::Image& current_image() const
    {
        const auto entries = images.images();
        return selectedImage > 0u && selectedImage <= entries.size() ? entries[selectedImage - 1u] : spectrum;
    }

    Media2DHandle current_image_handle(Engine& engine)
    {
        if (selectedImage == 0u) return spectrumHandle;
        const auto& image = current_image();
        auto existing = std::find_if(imageHandles.begin(), imageHandles.end(),
            [&](const auto& item) { return item.first == image.sourcePath; });
        if (existing != imageHandles.end()) return existing->second;
        auto handle = texture(image, image.sourcePath, engine);
        imageHandles.emplace_back(image.sourcePath, handle);
        ++uploadedImages;
        return handle;
    }

    void set_colour(std::uint32_t value, bool finished = false, bool preserveHsv = false)
    {
        value &= 0xFFFFFFu;
        if (!preserveHsv)
        {
            const auto converted = colour_palette::rgb_to_hsv(value);
            if (converted.saturation > 0.00001f) hsv.hue = converted.hue;
            hsv.saturation = converted.saturation;
            hsv.value = converted.value;
        }
        if (rgb != value)
        {
            rgb = value;
            visualDirty = true;
            previewPending = true;
            editedSinceCommit = true;
        }
        if (finished) commitPending = true;
    }

    void edit_colour(std::uint32_t value)
    {
        set_colour(value);
        editCommitAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(450);
    }

    void set_hsv(colour_palette::Hsv value, bool finished = false)
    {
        hsv = value;
        visualDirty = true;
        set_colour(colour_palette::hsv_to_rgb(hsv), finished, true);
    }

    std::array<float, 4> values() const
    {
        if (sliderMode == 1u) return { hsv.hue, hsv.saturation * 100.0f, hsv.value * 100.0f, 0.0f };
        if (sliderMode == 2u)
        {
            const auto cmyk = colour_palette::rgb_to_cmyk(rgb);
            return { cmyk.cyan * 100.0f, cmyk.magenta * 100.0f,
                cmyk.yellow * 100.0f, cmyk.black * 100.0f };
        }
        if (sliderMode == 3u) return { hsv.value * 100.0f, 0.0f, 0.0f, 0.0f };
        return { float((rgb >> 16u) & 255u), float((rgb >> 8u) & 255u), float(rgb & 255u), 0.0f };
    }

    void channel_changed(unsigned index, float value, bool textEdit = false)
    {
        auto channels = values();
        channels[index] = value;
        std::uint32_t next;
        if (sliderMode == 1u)
        {
            auto nextHsv = colour_palette::Hsv { channels[0], channels[1] / 100.0f, channels[2] / 100.0f };
            set_hsv(nextHsv);
            if (textEdit)
                editCommitAt = std::chrono::steady_clock::now() + std::chrono::milliseconds(450);
            return;
        }
        else if (sliderMode == 2u) next = colour_palette::cmyk_to_rgb({ channels[0] / 100.0f,
            channels[1] / 100.0f, channels[2] / 100.0f, channels[3] / 100.0f });
        else if (sliderMode == 3u) next = components(value * 2.55f, value * 2.55f, value * 2.55f);
        else next = components(channels[0], channels[1], channels[2]);
        if (textEdit) edit_colour(next);
        else set_colour(next, false, sliderMode == 1u);
    }

    void sample(glm::vec2 point)
    {
        auto* engine = windowHost.engine();
        if (!engine || samplingSurface == entt::null) return;
        const auto rect = ui_entity_framebuffer_rect(engine->renderer2d_scene().registry(), samplingSurface);
        const float x = (point.x - rect.x) / std::max(rect.z, 1.0f);
        const float y = (point.y - rect.y) / std::max(rect.w, 1.0f);
        if (dragging == Surface::eWheel)
        {
            const float dx = (x - 0.5f) * 2.0f, dy = (y - 0.5f) * 2.0f;
            hsv.hue = std::fmod(std::atan2(-dy, dx) * 180.0f / pi + 360.0f, 360.0f);
            hsv.saturation = std::clamp(std::sqrt(dx * dx + dy * dy), 0.0f, 1.0f);
            set_hsv(hsv);
        }
        else if (dragging == Surface::eImage)
        {
            if (const auto value = current_image().sample(x * 342.0f, y * 322.0f, 342.0f, 322.0f))
            {
                set_colour(*value);
                imagePoint = { std::clamp(x, 0.0f, 1.0f), std::clamp(y, 0.0f, 1.0f) };
                visualDirty = true;
            }
        }
    }

    glm::vec2 imagePoint { 0.5f, 0.5f };

    void bind_surface(UiBuilder& ui, entt::entity entity, Surface surfaceType)
    {
        ui.registry().emplace_or_replace<HitRegion2DComponent>(entity);
        ButtonInputComponent input;
        input.onPress = [this, surfaceType](const PointerInputEvent& event) {
            dragging = surfaceType;
            sample(event.framebufferPosition);
        };
        input.onRelease = [this](const PointerInputEvent& event) {
            sample(event.framebufferPosition);
            dragging = Surface::eNone;
            commitPending = true;
        };
        ui.registry().emplace_or_replace<ButtonInputComponent>(entity, std::move(input));
    }

    void build(GlfwPanelWindowTemplateContext& context)
    {
        auto& ui = context.ui;
        auto& engine = *windowHost.engine();
        auto& scene = context.scene;
        const auto& font = context.fontAtlas;
        const auto root = context.panel;
        auto titleStyle = make_flat_text_style(foreground());
        titleStyle.set_font_weight(650.0f);
        ui.create_aligned_text(tr("colour_palette.title", "Colours"), font, root,
            UiAlignment::eTopCentre, 19.0f, titleStyle, 4, 0u, { 0.0f, 20.0f });
        for (const auto entity : ui.registry().view<UiLiquidControlSurface, RenderLayer2DComponent>())
            if (ui.registry().get<RenderLayer2DComponent>(entity).layer == 100)
                ui.registry().get<UiLiquidControlSurface>(entity).alwaysEnabled = true;
        channels.clear();
        crayonEntities.fill(entt::null);
        hoveredCrayon.reset();
        crayonAnimating = false;
        hexField = brightness = swatch = marker = samplingSurface = statusText = pencilName = actionLabel = renameField = entt::null;
        brightnessStroke = entt::null;
        swatchEntities.fill(entt::null);
        if (!wheel.valid()) wheel = colour_palette::colour_wheel(384u);
        if (!spectrum.valid()) spectrum = colour_palette::colour_spectrum(384u, 384u);
        if (!wheelHandle.valid()) wheelHandle = texture(wheel, directory / "wheel.ppm", engine);
        if (!spectrumHandle.valid()) spectrumHandle = texture(spectrum, directory / "spectrum.ppm", engine);
        const auto iconOptions = ui_svg_icon_load_options(96u);
        const auto controlIcon = [&](Media2DHandle& handle, const std::filesystem::path& path) {
            if (!handle.valid() && !path.empty()) handle = ui_load_layered_media(engine, info.resources, path, iconOptions);
        };
        controlIcon(chevron, info.chevronIconPath);
        controlIcon(check, info.checkIconPath);
        controlIcon(sampler, info.samplerIconPath);
        for (std::size_t index = 0u; index < info.tabIconPaths.size(); ++index)
        {
            if (!icons[index].valid() && !info.tabIconPaths[index].empty())
                icons[index] = ui_load_layered_media(engine, info.resources, info.tabIconPaths[index], iconOptions);
            const auto background = rectangle(ui, root, { 18.0f + float(index) * 68.0f, 52.0f }, { 62.0f, 46.0f },
                make_solid_style(index == tab ? appearanceTheme.surfaceMuted : surface(),
                    outline(), 0.7f), 12.0f, 3);
            ButtonInputComponent input;
            input.onClick = [this, index](const PointerInputEvent&) { tab = index; renaming = false; rebuildPending = true; };
            ui.registry().emplace<ButtonInputComponent>(background, std::move(input));
            ui.registry().emplace<HitRegion2DComponent>(background);
            ButtonVisualComponent visual;
            visual.idle = visual.hovered = visual.pressed = ui.registry().get<ShapeStyleComponent>(background);
            ui.registry().emplace<ButtonVisualComponent>(background, visual);
            ui_apply_liquid_button(ui, background);
            ui.create_aligned_media(icons[index], background, UiAlignment::eCenter,
                { 30.0f, 30.0f }, 4, 0u);
        }
        label(ui, font, root, targetLabel, { 20.0f, 106.0f }, 12.0f, muted());
        if (tab == 0u) build_wheel(ui, font, root);
        else if (tab == 1u) build_sliders(ui, font, root);
        else if (tab == 2u) build_named(ui, font, root);
        else if (tab == 3u) build_image(ui, font, root);
        else build_pencils(ui, font, root);
        UiCheckboxOptions live;
        live.checked = livePreview;
        live.checkIcon = check;
        live.labelColor = muted();
        live.labelFontSize = 12.0f;
        live.onChanged = [this](bool value, const PointerInputEvent&) {
            livePreview = value;
            if (value) previewPending = true;
        };
        ui_create_checkbox(ui, font, root, UiAlignment::eTopLeft, { 20.0f, 522.0f },
            tr("colour_palette.live_preview", "Live preview"), { 140.0f, 22.0f }, 4, 0u, std::move(live));
        rectangle(ui, root, { 20.0f, 554.0f }, { 336.0f, 1.0f }, make_solid_style(outline()));
        swatch = rectangle(ui, root, { 20.0f, 574.0f }, { 46.0f, 42.0f }, make_solid_style(paint(rgb),
            renderer2d_hex_color(outline()), 1.0f), 8.0f, 4);
        ButtonInputComponent save;
        save.onClick = [this](const PointerInputEvent&) {
            const auto slot = std::find(swatches.begin(), swatches.end(), std::nullopt);
            if (slot != swatches.end()) *slot = rgb;
            else swatches[0] = rgb;
            visualDirty = true;
        };
        ui.registry().emplace<ButtonInputComponent>(swatch, std::move(save));
        ui.registry().emplace<HitRegion2DComponent>(swatch);
        const auto samplerButton = button(ui, font, root, "", { 22.0f, 628.0f }, { 42.0f, 32.0f },
            [this](const PointerInputEvent&) { pendingAction = Action::eEyedropper; }, true);
        const auto samplerIcon = ui.create_aligned_media(sampler, samplerButton.root, UiAlignment::eCentre,
            { 28.0f, 28.0f }, 5, 0u);
        ui_set_media_mask_tint(scene, samplerIcon, appearanceTheme.icon);
        for (std::size_t index = 0u; index < swatches.size(); ++index)
        {
            auto cell = rectangle(ui, root,
                { 77.0f + float(index % 10u) * 28.0f, 574.0f + float(index / 10u) * 28.0f },
                { 24.0f, 24.0f }, make_solid_style(swatches[index] ? paint(*swatches[index]) :
                    renderer2d_hex_color(dark() ? "#242426FF" : "#FFFFFFFF"),
                    renderer2d_hex_color(outline()), 0.85f), 5.0f, 4);
            swatchEntities[index] = cell;
            ButtonInputComponent input;
            input.onClick = [this, index](const PointerInputEvent& event) {
                if (event.modifiers.shift) swatches[index] = rgb;
                else if (swatches[index]) set_colour(*swatches[index], true);
                else swatches[index] = rgb;
                visualDirty = true;
            };
            input.onRightClick = [this, index](const PointerInputEvent&) { swatches[index].reset(); visualDirty = true; };
            ui.registry().emplace<ButtonInputComponent>(cell, std::move(input));
            ui.registry().emplace<HitRegion2DComponent>(cell);
        }
        statusText = label(ui, font, root, status, { 172.0f, 527.0f }, 10.5f, muted());
        TextLayout2DComponent statusLayout;
        statusLayout.options.maximumWidth = 184.0f * ui.scale().factor.x;
        statusLayout.options.wrapMode = TextWrapMode2D::eWord;
        ui.registry().emplace<TextLayout2DComponent>(statusText, statusLayout);
        if (renaming)
        {
            const auto card = rectangle(ui, root, { 18.0f, 126.0f }, { 340.0f, 76.0f },
                make_solid_style(surface(), outline(), 1.0f), 10.0f, 60);
            const auto rename = field(ui, font, card, { 8.0f, 9.0f }, { 222.0f, 30.0f }, renameValue,
                [this](const std::string& value) { renameValue = value; });
            renameField = rename.root;
            auto& registry = ui.registry();
            for (const auto entity : { rename.root, rename.label, rename.caret })
                if (auto* layer = registry.try_get<RenderLayer2DComponent>(entity)) layer->layer += 60;
            const auto apply = button(ui, font, card, tr("colour_palette.save", "Save"),
                { 238.0f, 9.0f }, { 90.0f, 30.0f }, [this](const PointerInputEvent&) {
                    if (selectedImage > 0u) images.rename(selectedImage - 1u, renameValue);
                    renaming = false;
                    rebuildPending = true;
                });
            for (const auto entity : { apply.root, apply.label })
                if (auto* layer = registry.try_get<RenderLayer2DComponent>(entity)) layer->layer += 60;
        }
        for (const auto entity : ui.registry().view<UiLiquidControlSurface>())
            if (!ui.registry().get<UiLiquidControlSurface>(entity).alwaysEnabled)
                ui.registry().remove<UiLiquidControlSurface>(entity);
        for (const auto entity : ui.registry().view<ButtonInputComponent, ShapeStyleComponent>())
            if (!ui.registry().all_of<ButtonVisualComponent>(entity))
                ui.registry().emplace<ButtonVisualComponent>(entity,
                    ui_fixed_colour_button_visual(ui.registry().get<ShapeStyleComponent>(entity)));
        visualDirty = true;
    }

    void build_wheel(UiBuilder& ui, const Renderer2DFontAtlas& font, entt::entity root)
    {
        samplingSurface = ui.create_aligned_media(wheelHandle, root, UiAlignment::eTopLeft,
            { 344.0f, 344.0f }, 3, 0u, { 16.0f, 132.0f }, Media2DFit::eStretch);
        if (auto* media = ui.registry().try_get<Media2DComponent>(samplingSurface))
        {
            media->primitive = Renderer2DPrimitive::eEllipse;
            media->cornerRadius = 0.0f;
        }
        bind_surface(ui, samplingSurface, Surface::eWheel);
        marker = rectangle(ui, root, {}, { 16.0f, 16.0f },
            make_solid_style("#00000000", "#222222DD", 1.25f), 8.0f, 7);
        ui.registry().emplace<InputTransparent2DComponent>(marker);
        UiSliderOptions options;
        options.value = 1.0f - hsv.value;
        options.fillColor = "#00000000";
        options.trackColor = "#F2F2F4FF";
        options.outlineColor = "#00000000";
        options.outlineWidth = 0.0f;
        options.thumbColor = "#FFFFFFFF";
        options.thumbOutlineColor = "#FFFFFFFF";
        options.thumbOutlineWidth = 3.0f;
        options.thumbDiameter = 30.0f;
        options.insetThumb = true;
        options.renderThumbShadow = false;
        options.onChanged = [this](const SliderInputEvent& event) { hsv.value = 1.0f - event.value; set_hsv(hsv); };
        options.onCommitted = [this](const SliderInputEvent&) { commitPending = true; };
        brightnessStroke = rectangle(ui, root, { 16.0f, 482.0f }, { 344.0f, 34.0f },
            make_solid_style("#00000000"), 17.0f, 3);
        brightness = ui_create_slider(ui, root, UiAlignment::eTopLeft, { 16.7f, 482.7f },
            { 342.6f, 32.6f }, 4, 0u, std::move(options)).root;
    }

    void build_sliders(UiBuilder& ui, const Renderer2DFontAtlas& font, entt::entity root)
    {
        auto options = dropdown();
        options.selectedIndex = static_cast<int>(sliderMode);
        options.onChanged = [this](int index, std::string_view, const PointerInputEvent&) {
            sliderMode = std::clamp(index, 0, 3); rebuildPending = true;
        };
        create_dropdown(ui, font, root, UiAlignment::eTopLeft, { 16.0f, 132.0f }, { 344.0f, 36.0f },
            { tr("colour_palette.rgb_sliders", "RGB Sliders"), tr("colour_palette.hsv_sliders", "HSB Sliders"),
              tr("colour_palette.cmyk_sliders", "CMYK Sliders"), tr("colour_palette.grey_sliders", "Greyscale Slider") },
            5, 0u, std::move(options));
        const std::array<std::vector<std::string>, 4> names {{
            { tr("colour_palette.red", "Red"), tr("colour_palette.green", "Green"), tr("colour_palette.blue", "Blue") },
            { tr("colour_palette.hue", "Hue"), tr("colour_palette.saturation", "Saturation"), tr("colour_palette.brightness", "Brightness") },
            { tr("colour_palette.cyan", "Cyan"), tr("colour_palette.magenta", "Magenta"), tr("colour_palette.yellow", "Yellow"), tr("colour_palette.black", "Black") },
            { tr("colour_palette.brightness", "Brightness") }
        }};
        const auto current = values();
        for (unsigned index = 0u; index < names[sliderMode].size(); ++index)
        {
            const float y = 184.0f + float(index) * (sliderMode == 2u ? 65.0f : 78.0f);
            label(ui, font, root, names[sliderMode][index], { 16.0f, y }, 14.0f, muted());
            const float maximum = sliderMode == 0u ? 255.0f : (sliderMode == 1u && index == 0u ? 360.0f : 100.0f);
            UiSliderOptions slider;
            slider.value = current[index];
            slider.maxValue = maximum;
            slider.step = 1.0f;
            slider.trackColor = "#F2F2F4FF";
            slider.fillColor = "#00000000";
            slider.thumbColor = "#FFFFFFFF";
            slider.thumbOutlineColor = "#FFFFFFFF";
            slider.thumbOutlineWidth = 2.0f;
            slider.thumbDiameter = 30.0f;
            slider.insetThumb = true;
            slider.outlineColor = "#00000000";
            slider.outlineWidth = 0.0f;
            slider.renderThumbShadow = false;
            slider.onChanged = [this, index](const SliderInputEvent& event) { channel_changed(index, event.value); };
            slider.onCommitted = [this](const SliderInputEvent&) { commitPending = true; };
            const auto stroke = rectangle(ui, root, { 16.0f, y + 24.0f }, { 255.0f, 34.0f },
                make_solid_style("#00000000"), 17.0f, 3);
            const auto control = ui_create_slider(ui, root, UiAlignment::eTopLeft,
                { 16.7f, y + 24.7f }, { 253.6f, 32.6f }, 4, 0u, std::move(slider));
            const auto numeric = field(ui, font, root, { 284.0f, y + 23.0f }, { 76.0f, 36.0f }, number(current[index]),
                [this, index, maximum](const std::string& text) {
                    if (text.empty() || text.size() > 5u || text.front() == '+') return;
                    float value = 0.0f;

                    std::istringstream parsed(text);
                    parsed.imbue(std::locale::classic());
                    parsed >> std::noskipws >> value;
                    if (parsed && parsed.eof() &&
                        std::isfinite(value) && value >= 0.0f && value <= maximum) channel_changed(index, value, true);
                }, {}, false, TextHorizontalAlignment2D::eCenter);
            channels.push_back({ control.root, numeric.root, stroke, index, maximum });
        }
        label(ui, font, root, tr("colour_palette.hex", "Hex Colour #"), { 157.0f, 478.0f }, 14.0f, muted());
        hexField = field(ui, font, root, { 258.0f, 466.0f }, { 102.0f, 36.0f }, colour_palette::hex_colour(rgb),
            [this](const std::string& text) { if (const auto value = colour_palette::parse_hex_colour(text)) edit_colour(*value); },
            {}, false, TextHorizontalAlignment2D::eCenter).root;
    }

    std::vector<colour_palette::NamedColour> named() const
    {
        const auto source = paletteMode == 1u ? colour_palette::pencil_colours() : colour_palette::named_colours();
        std::vector<colour_palette::NamedColour> result;
        std::string query = search;
        std::transform(query.begin(), query.end(), query.begin(), [](unsigned char value) { return char(std::tolower(value)); });
        for (const auto& colour : source)
        {
            std::string name = colour_name(colour.name);
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char value) { return char(std::tolower(value)); });
            if (query.empty() || name.find(query) != std::string::npos) result.push_back(colour);
        }
        return result;
    }

    void build_named(UiBuilder& ui, const Renderer2DFontAtlas& font, entt::entity root)
    {
        auto options = dropdown();
        options.selectedIndex = static_cast<int>(paletteMode);
        options.onChanged = [this](int index, std::string_view, const PointerInputEvent&) {
            paletteMode = std::clamp(index, 0, 1); rebuildPending = true;
        };
        create_dropdown(ui, font, root, UiAlignment::eTopLeft, { 16.0f, 132.0f }, { 344.0f, 36.0f },
            { tr("colour_palette.standard", "Standard"), tr("colour_palette.crayons", "Crayons") }, 5, 0u, std::move(options));
        field(ui, font, root, { 16.0f, 182.0f }, { 344.0f, 34.0f }, search,
            [this](const std::string& value) { search = value; searchChanged = true; },
            tr("colour_palette.search", "Search"), true);
        namedRowsRoot = rectangle(ui, root, { 16.0f, 229.0f }, { 344.0f, 279.0f },
            make_solid_style(surface(), outline(), 0.8f), 9.0f);
        namedRows.clear();
        for (unsigned index = 0u; index < 10u; ++index)
        {
            const auto row = rectangle(ui, namedRowsRoot, { 5.0f, 5.0f + float(index) * 26.5f },
                { 334.0f, 26.5f }, make_solid_style("#00000000"), 4.0f, 3);
            const auto chip = rectangle(ui, row, { 9.0f, 2.0f }, { 22.0f, 22.0f }, make_solid_style("#FFFFFFFF", outline(), 0.7f), 4.0f, 4);
            const auto text = label(ui, font, row, "", { 43.0f, 4.0f }, 16.0f);
            ButtonInputComponent input;
            input.onClick = [this, index](const PointerInputEvent&) {
                const auto colours = named();
                if (namedOffset + index < colours.size()) set_colour(colours[namedOffset + index].rgb, true);
            };
            ui.registry().emplace<ButtonInputComponent>(row, std::move(input));
            ui.registry().emplace<HitRegion2DComponent>(row);
            namedRows.push_back({ row, chip, text });
        }
        ScrollInputComponent scroll;
        scroll.step = 1.0f;
        scroll.maxOffset = float(std::max<int>(0, int(named().size()) - 10));
        scroll.onScroll = [this](const ScrollInputEvent& event) {
            if (auto* engine = windowHost.engine())
                if (const auto* value = engine->renderer2d_scene().registry().try_get<ScrollInputComponent>(event.target))
                { namedOffset = std::size_t(std::max(0.0f, std::round(value->offset))); searchChanged = true; }
        };
        ui.registry().emplace<ScrollInputComponent>(namedRowsRoot, std::move(scroll));
        searchChanged = true;
    }

    std::size_t namedOffset = 0u;
    entt::entity namedRowsRoot = entt::null;
    std::vector<std::array<entt::entity, 3>> namedRows;
    bool searchChanged = false;

    void build_image(UiBuilder& ui, const Renderer2DFontAtlas& font, entt::entity root)
    {
        auto options = dropdown();
        options.selectedIndex = static_cast<int>(selectedImage);
        options.onChanged = [this](int index, std::string_view, const PointerInputEvent&) {
            selectedImage = std::size_t(std::max(index, 0)); imagePoint = { 0.5f, 0.5f }; rebuildPending = true;
        };
        std::vector<std::string> choices { tr("colour_palette.spectrum", "Spectrum") };
        for (const auto& image : images.images()) choices.push_back(image.name);
        create_dropdown(ui, font, root, UiAlignment::eTopLeft, { 16.0f, 132.0f }, { 294.0f, 36.0f }, choices,
            5, 0u, std::move(options));
        auto actions = dropdown();
        actions.chevronIcon = {};
        actions.menuWidth = 225.0f;
        actions.maxVisibleItems = 5u;
        actions.onChanged = [this](int index, std::string_view, const PointerInputEvent&) {
            static constexpr std::array values { Action::eFile, Action::eRename, Action::eRemove, Action::eCopy, Action::eClipboard };
            if (index >= 0 && std::size_t(index) < values.size()) pendingAction = values[std::size_t(index)];
            if (windowHost.engine() && actionLabel != entt::null)
                set_text_entity(windowHost.engine()->renderer2d_scene(), windowHost.engine()->renderer2d_font_atlas(), actionLabel, "...");
        };
        auto menu = create_dropdown(ui, font, root, UiAlignment::eTopLeft, { 322.0f, 132.0f }, { 38.0f, 36.0f },
            { tr("colour_palette.new_file", "New from File..."), tr("colour_palette.rename", "Rename..."),
              tr("colour_palette.remove", "Remove"), tr("colour_palette.copy", "Copy"),
              tr("colour_palette.new_clipboard", "New from Clipboard") }, 6, 0u, std::move(actions));
        actionLabel = menu.label;
        set_text_entity(ui.scene(), font, menu.label, "...");
        auto& actionMenuLayout = ui.registry().get<Layout2DComponent>(menu.menu);
        actionMenuLayout.offset.x = (38.0f - 225.0f) * ui.scale().factor.x;
        if (selectedImage == 0u)
        {
            auto& control = ui.registry().get<UiDropdownControlComponent>(menu.root);
            for (const unsigned index : { 1u, 2u })
                if (index < control.itemRows.size())
                {
                    if (auto* input = ui.registry().try_get<ButtonInputComponent>(control.itemRows[index])) input->enabled = false;
                    if (auto* text = ui.registry().try_get<TextStyleComponent>(control.itemLabels[index])) text->set_color(muted());
                }
        }
        const auto border = rectangle(ui, root, { 16.0f, 184.0f }, { 344.0f, 324.0f },
            make_solid_style(surface(), outline(), 0.9f), 8.0f);
        const auto media = ui.create_aligned_media(current_image_handle(*windowHost.engine()), border,
            UiAlignment::eCenter, { 342.0f, 322.0f }, 3, 0u, {}, Media2DFit::eContain);
        if (auto* image = ui.registry().try_get<Media2DComponent>(media)) image->cornerRadius = 6.0f;
        samplingSurface = media;
        bind_surface(ui, media, Surface::eImage);
        marker = rectangle(ui, root, {}, { 15.0f, 15.0f }, make_solid_style("#00000000", "#4C4C50DD", 1.2f), 7.5f, 7);
        ui.registry().emplace<InputTransparent2DComponent>(marker);
    }

    void build_pencils(UiBuilder& ui, const Renderer2DFontAtlas& font, entt::entity root)
    {
        auto caption = make_flat_text_style(foreground()); caption.set_font_weight(650.0f);
        pencilName = ui.create_aligned_text(tr("colour_palette.crayons", "Crayons"), font, root,
            UiAlignment::eTopCentre, 16.0f, caption, 4, 0u, { 0.0f, 132.0f });
        const auto grid = rectangle(ui, root, { 16.0f, 165.0f }, { 344.0f, 355.0f },
            make_solid_style("#00000000"), 8.0f, 3);
        ui.scene().enable_mask(grid, false);
        selectedCrayon.reset();
        const auto colours = colour_palette::pencil_colours();
        const std::size_t count = std::min<std::size_t>(colours.size(), 48u);
        if (!crayonArtworkAttempted && !info.pencilArtworkPath.empty())
        {
            crayonArtworkAttempted = true;
            for (const auto& path : info.resources.asset_candidates(info.pencilArtworkPath))
                if (ui_path_exists(path))
                {
                    crayonArtwork.load(path, status);
                    break;
                }
        }
        for (std::size_t index = 0u; index < count; ++index)
        {
            const auto colour = colours[index];
            if (colour.rgb == rgb) selectedCrayon = index;
            crayonLift[index] = colour.rgb == rgb ? 10.0f : 0.0f;
            crayonVelocity[index] = 0.0f;
            const float x = float(index % 12u) * (344.0f / 12.0f);
            const float y = 10.0f + float(index / 12u) * 75.0f - crayonLift[index];
            if (!crayonHandles[index].valid() && crayonArtwork.valid())
            {
                const auto path = directory / crayonArtwork.filename(colour.rgb);
                if (crayonArtwork.write(colour.rgb, path, status))
                {
                    crayonPaths[index] = path;
                    temporaryFiles.push_back(path);
                    Media2DLoadOptions options;
                    options.rasterWidth = 96u;
                    options.trimTransparentBorders = true;
                    options.generateMipmaps = false;
                    options.premultiplyAlpha = true;
                    crayonHandles[index] = windowHost.engine()->load_media_2d(path, options);
                }
            }
            const int layer = 4 + int(index / 12u);
            const auto body = crayonHandles[index].valid() ?
                ui.create_aligned_media(crayonHandles[index], grid, UiAlignment::eTopLeft,
                    { 344.0f / 12.0f + .35f, 120.0f + crayonLift[index] }, layer, std::uint32_t(index), { x, y }, Media2DFit::eStretch) :
                rectangle(ui, grid, { x, y + 42.0f }, { 344.0f / 12.0f + .35f, 78.0f + crayonLift[index] },
                    make_solid_style(paint(colour.rgb)), 3.0f, layer);
            ButtonInputComponent input;
            input.onClick = [this, colour, index](const PointerInputEvent&) {
                set_colour(colour.rgb, true);
                selectedCrayon = index;
                crayonAnimating = true;
                lastCrayonFrame = glfw_time_seconds();
                refresh_crayon_name();
            };
            input.onHoverChanged = [this, index](bool hovered) {
                if (hovered) hoveredCrayon = index;
                else if (hoveredCrayon == index) hoveredCrayon.reset();
                refresh_crayon_name();
            };
            crayonEntities[index] = body;
            ui.registry().emplace<ButtonInputComponent>(body, std::move(input));
            ui.registry().emplace<HitRegion2DComponent>(body);
        }
    }

    void refresh_crayon_name()
    {
        auto* engine = windowHost.engine();
        if (!engine || !engine->renderer2d_scene().registry().valid(pencilName)) return;
        const auto index = hoveredCrayon ? hoveredCrayon : selectedCrayon;
        const auto colours = colour_palette::pencil_colours();
        const auto name = index && *index < colours.size() ? colour_name(colours[*index].name) :
            tr("colour_palette.crayons", "Crayons");
        if (engine->renderer2d_scene().registry().get<TextComponent>(pencilName).text != name)
            set_text_entity(engine->renderer2d_scene(), engine->renderer2d_font_atlas(), pencilName, name);
    }

    void animate_crayons()
    {
        if (tab != 4u || !crayonAnimating || !windowHost.engine()) return;
        const double now = glfw_time_seconds();
        const double delta = std::clamp(now - lastCrayonFrame, 0.0, .05);
        lastCrayonFrame = now;
        auto& scene = windowHost.engine()->renderer2d_scene();
        auto& registry = scene.registry();
        crayonAnimating = false;
        for (std::size_t index = 0u; index < crayonEntities.size(); ++index)
        {
            const auto entity = crayonEntities[index];
            if (!registry.valid(entity)) continue;
            const float target = selectedCrayon == index ? 10.0f : 0.0f;
            const float previous = crayonLift[index];
            ui_damped_spring_step(crayonLift[index], crayonVelocity[index], target, delta, 23.0f, .78f);
            if (std::abs(crayonLift[index] - target) < .01f && std::abs(crayonVelocity[index]) < .05f)
            { crayonLift[index] = target; crayonVelocity[index] = 0.0f; }
            else crayonAnimating = true;
            if (crayonLift[index] == previous) continue;
            auto& layout = registry.get<Layout2DComponent>(entity);
            const float scale = layout.size.x / (344.0f / 12.0f + .35f);
            const bool artwork = registry.all_of<Media2DComponent>(entity);
            layout.offset.y = (10.0f + float(index / 12u) * 75.0f + (artwork ? 0.0f : 42.0f) - crayonLift[index]) * scale;
            layout.size.y = ((artwork ? 120.0f : 78.0f) + crayonLift[index]) * scale;
            ui_mark_direct_manipulation_dirty(scene, entity, .25f);
        }
    }

    void refresh_colour_thumbs()
    {
        auto* engine = windowHost.engine();
        if (!engine) return;
        auto& scene = engine->renderer2d_scene();
        auto& registry = scene.registry();
        const auto apply = [&](entt::entity entity) {
            const auto* slider = registry.try_get<SliderInputComponent>(entity);
            if (!slider) return;
            auto* style = registry.try_get<ShapeStyleComponent>(slider->thumbEntity);
            const auto* shape = registry.try_get<ShapeComponent>(slider->thumbEntity);
            if (!style || !shape) return;
            const auto* original = registry.try_get<UiLiquidThumbPaint>(slider->thumbEntity);
            auto resting = original ? original->shape : *style;
            resting.set_color(paint(rgb));
            resting.outlineColor = glm::vec4(1.0f);
            resting.outlineWidth = slider->thumbSize.y * .09f;
            ui_set_liquid_thumb_resting_style(scene, slider->thumbEntity, resting);
        };
        apply(brightness);
        for (const auto& channel : channels) apply(channel.slider);
    }

    void set_input(entt::entity entity, const std::string& value)
    {
        auto* engine = windowHost.engine();
        if (!engine || entity == entt::null) return;
        auto& scene = engine->renderer2d_scene();
        if (auto* field = scene.registry().try_get<TextInputComponent>(entity); field && !field->focused && field->value != value)
        {
            field->value = value;
            ui_update_text_input_visual(scene, engine->renderer2d_font_atlas(), entity);
        }
    }

    void refresh_named()
    {
        if (tab != 2u || !windowHost.engine()) return;
        auto& scene = windowHost.engine()->renderer2d_scene();
        const auto colours = named();
        const auto maximum = colours.size() > 10u ? colours.size() - 10u : 0u;
        namedOffset = std::min(namedOffset, maximum);
        if (auto* scroll = scene.registry().try_get<ScrollInputComponent>(namedRowsRoot))
        { scroll->maxOffset = float(maximum); scroll->offset = float(namedOffset); }
        for (std::size_t index = 0u; index < namedRows.size(); ++index)
        {
            const auto& row = namedRows[index];
            const bool visible = namedOffset + index < colours.size();
            for (const auto entity : row)
                if (auto* layer = scene.registry().try_get<RenderLayer2DComponent>(entity)) layer->visible = visible;
            if (auto* input = scene.registry().try_get<ButtonInputComponent>(row[0])) input->enabled = visible;
            if (visible)
            {
                const auto& colour = colours[namedOffset + index];
                auto& style = scene.registry().get<ShapeStyleComponent>(row[1]);
                style.color0 = style.color1 = paint(colour.rgb);
                set_text_entity(scene, windowHost.engine()->renderer2d_font_atlas(), row[2], colour_name(colour.name));
            }
            for (const auto entity : row) scene.mark_dirty(entity);
        }
        searchChanged = false;
    }

    void refresh_visuals()
    {
        auto* engine = windowHost.engine();
        if (!engine) return;
        auto& scene = engine->renderer2d_scene();
        auto& registry = scene.registry();
        const auto& font = engine->renderer2d_font_atlas();
        if (registry.valid(swatch))
        {
            auto& style = registry.get<ShapeStyleComponent>(swatch);
            style.color0 = style.color1 = paint(rgb);
            registry.emplace_or_replace<ButtonVisualComponent>(swatch, ui_fixed_colour_button_visual(style));
            scene.mark_dirty(swatch);
        }
        for (std::size_t index = 0u; index < swatchEntities.size(); ++index)
            if (registry.valid(swatchEntities[index]))
            {
                auto& style = registry.get<ShapeStyleComponent>(swatchEntities[index]);
                style.color0 = style.color1 = swatches[index] ? paint(*swatches[index]) : renderer2d_hex_color(dark() ? "#242426FF" : "#FFFFFFFF");
                registry.emplace_or_replace<ButtonVisualComponent>(swatchEntities[index], ui_fixed_colour_button_visual(style));
                scene.mark_dirty(swatchEntities[index]);
            }
        if (registry.valid(marker))
        {
            auto& layout = registry.get<Layout2DComponent>(marker);
            const auto uiScale = windowHost.panel()->template_options().uiScale;
            glm::vec2 factor { uiScale };
            int width = 0, height = 0;
            if (glfw_framebuffer_size(windowHost.window(), width, height))
            {
                int logicalWidth = 0, logicalHeight = 0;
                if (glfw_window_size(windowHost.window(), logicalWidth, logicalHeight) && logicalWidth > 0 && logicalHeight > 0)
                    factor *= glm::vec2(float(width) / logicalWidth, float(height) / logicalHeight);
            }
            if (tab == 0u)
            {
                const float angle = hsv.hue * pi / 180.0f;
                layout.offset = glm::vec2(16.0f + 172.0f + std::cos(angle) * hsv.saturation * 172.0f - 8.0f,
                    132.0f + 172.0f - std::sin(angle) * hsv.saturation * 172.0f - 8.0f) * factor;
            }
            else layout.offset = glm::vec2(17.0f + imagePoint.x * 342.0f - 7.5f,
                185.0f + imagePoint.y * 322.0f - 7.5f) * factor;
            scene.mark_dirty(marker);
        }
        if (registry.valid(brightness))
        {
            auto& slider = registry.get<SliderInputComponent>(brightness);
            slider.value = slider.visualValue = 1.0f - hsv.value;
            ui_update_slider_visual(scene, font, brightness);
            auto& style = registry.get<ShapeStyleComponent>(brightness);
            style.fill = Renderer2DFill::eLinearGradient;
            style.color0 = paint(colour_palette::hsv_to_rgb({ hsv.hue, hsv.saturation, 1.0f }));
            style.color1 = { 0.0f, 0.0f, 0.0f, 1.0f };
            style.gradientStart = { 0.0f, 0.0f }; style.gradientEnd = { 1.0f, 0.0f };
            if (auto* stroke = registry.try_get<ShapeStyleComponent>(brightnessStroke))
            {
                stroke->fill = Renderer2DFill::eLinearGradient;
                stroke->gradientStart = style.gradientStart; stroke->gradientEnd = style.gradientEnd;
                stroke->color0 = glm::vec4(glm::vec3(style.color0) * .82f, style.color0.a);
                stroke->color1 = style.color1;
                scene.mark_dirty(brightnessStroke);
            }
            scene.mark_dirty(brightness);
        }
        const auto current = values();
        for (const auto& channel : channels)
        {
            if (!registry.valid(channel.slider)) continue;
            auto& slider = registry.get<SliderInputComponent>(channel.slider);
            slider.value = slider.visualValue = current[channel.index];
            ui_update_slider_visual(scene, font, channel.slider);
            auto& style = registry.get<ShapeStyleComponent>(channel.slider);
            style.fill = Renderer2DFill::eLinearGradient;
            style.gradientStart = { 0.0f, 0.0f }; style.gradientEnd = { 1.0f, 0.0f };
            auto low = paint(rgb), high = paint(rgb);
            if (sliderMode == 0u) { low[channel.index] = 0.0f; high[channel.index] = 1.0f; }
            else { low = { 0.0f, 0.0f, 0.0f, 1.0f }; high = paint(rgb); }
            style.color0 = low; style.color1 = high;
            if (auto* stroke = registry.try_get<ShapeStyleComponent>(channel.stroke))
            {
                stroke->fill = Renderer2DFill::eLinearGradient;
                stroke->gradientStart = { 0.0f, 0.0f }; stroke->gradientEnd = { 1.0f, 0.0f };
                stroke->color0 = glm::vec4(glm::vec3(low) * .82f, low.a);
                stroke->color1 = glm::vec4(glm::vec3(high) * .82f, high.a);
                scene.mark_dirty(channel.stroke);
            }
            scene.mark_dirty(channel.slider);
            set_input(channel.field, number(current[channel.index]));
        }
        if (tab == 4u)
        {
            const auto colours = colour_palette::pencil_colours();
            std::optional<std::size_t> selected;
            for (std::size_t index = 0u; index < colours.size(); ++index)
                if (colours[index].rgb == rgb) { selected = index; break; }
            if (selected != selectedCrayon)
            {
                selectedCrayon = selected;
                crayonAnimating = true;
                lastCrayonFrame = glfw_time_seconds();
            }
            refresh_crayon_name();
        }
        set_input(hexField, colour_palette::hex_colour(rgb));
        if (registry.valid(statusText)) set_text_entity(scene, font, statusText, status);
        if (searchChanged) refresh_named();
        visualDirty = false;
    }

    void start_import(colour_palette::ImageSource source)
    {
        if (import.valid()) { status = tr("colour_palette.loading", "An image is already loading."); visualDirty = true; return; }
        if (images.images().size() >= colour_palette::ImageLibrary::maximumImages)
        { status = tr("colour_palette.image_limit", "Remove an image palette before adding another."); visualDirty = true; return; }
        status = tr("colour_palette.loading_image", "Loading image...");
        visualDirty = true;
        importStop = std::stop_source {};
        const auto cancellation = importStop.get_token();
        const auto generation = importGeneration;
        import = std::async(std::launch::async, [source = std::move(source), cancellation, generation]() {
            Imported result;
            result.generation = generation;
            result.image = colour_palette::decode_image(source, result.error, cancellation);
            return result;
        });
    }

    void execute_action()
    {
        const auto action = std::exchange(pendingAction, Action::eNone);
        if (action == Action::eNone) return;
        auto* owner = glfw_native_window_handle(windowHost.window());
        status.clear();
        if (action == Action::eFile || action == Action::eClipboard)
        {
            auto source = action == Action::eFile ? colour_palette::choose_image_file(owner, status) :
                colour_palette::clipboard_image(owner, status);
            if (source) start_import(std::move(*source));
        }
        else if (action == Action::eRename && selectedImage > 0u)
        { renaming = true; renameValue = current_image().name; rebuildPending = true; }
        else if (action == Action::eRemove && selectedImage > 0u)
        {
            const auto path = current_image().sourcePath;
            images.remove(selectedImage - 1u);
            if (path.parent_path() == directory)
            {
                std::error_code error;
                std::filesystem::remove(path, error);
                if (!error) std::erase(temporaryFiles, path);
            }
            selectedImage = 0u;
            rebuildPending = true;
        }
        else if (action == Action::eCopy)
            colour_palette::copy_image_to_clipboard(owner, current_image(), status);
        else if (action == Action::eEyedropper)
        {
            pickingScreen = true;
            screenReleased = false;
            set_glfw_window_visible(windowHost.window(), false);
            const auto generation = *lifetimeGeneration;
            const std::weak_ptr<std::uint64_t> lifetime = lifetimeGeneration;
            nativeScreenPicker = colour_palette::begin_native_screen_pick(
                [this, lifetime, generation](std::optional<std::uint32_t> value) {
                    const auto current = lifetime.lock();
                    if (!current || *current != generation) return;
                    if (value) set_colour(*value, true);
                    pickingScreen = nativeScreenPicker = false;
                    if (windowHost.is_open()) set_glfw_window_visible(windowHost.window(), true);
                    visualDirty = true;
                });
        }
        visualDirty = true;
    }

    void deliver(bool force)
    {
        const auto now = std::chrono::steady_clock::now();
        if (previewPending && livePreview && preview && (force ||
            std::chrono::duration<double>(now - previewAt).count() >= info.previewIntervalSeconds))
        {
            if (!lastPreview || *lastPreview != rgb) preview(rgb);
            lastPreview = rgb;
            previewAt = now;
            previewPending = false;
        }
        if (force && commit && editedSinceCommit)
        {
            commit(rgb);
            lastCommit = rgb;
            editedSinceCommit = false;
        }
    }

    void clear_gpu_handles()
    {
        wheelHandle = spectrumHandle = {};
        icons.fill({});
        chevron = check = sampler = {};
        crayonHandles.fill({});
        crayonArtwork = vibrance::SvgColourTemplate(info.pencilColourMarker);
        crayonArtworkAttempted = false;
        for (auto& path : crayonPaths)
            if (!path.empty() && path.parent_path() == directory)
            {
                std::error_code error;
                std::filesystem::remove(path, error);
                if (!error) std::erase(temporaryFiles, path);
                path.clear();
            }
        imageHandles.clear();
        uploadedImages = 0u;
    }

    void close(bool immediately = false)
    {
        deliver(true);
        importStop.request_stop();
        ++importGeneration;
        if (lifetimeGeneration) ++*lifetimeGeneration;
        pickingScreen = false;
        dragging = Surface::eNone;
        openRequested = false;
        if (!immediately && windowHost.is_open())
        {
            windowHost.request_close();
            return;
        }
        windowHost.close();
        clear_gpu_handles();
    }

    void tick()
    {
        if (openRequested)
        {
            openRequested = false;
            windowHost.request_open();
            windowHost.open();
        }
        if (!windowHost.is_open()) return;
        if (windowHost.should_close() && !windowHost.is_closing()) { close(); return; }
        if (windowHost.is_closing())
        {
            windowHost.tick();
            if (windowHost.close_ready()) close(true);
            return;
        }
        if (pickingScreen)
        {
            if (nativeScreenPicker) return;
            const auto state = colour_palette::screen_pick_state();
            if (!state.primaryDown) screenReleased = true;
            if (state.cancelled || (screenReleased && state.primaryDown))
            {
                if (!state.cancelled)
                    if (const auto value = colour_palette::screen_colour_at_cursor(status)) set_colour(*value, true);
                pickingScreen = false;
                set_glfw_window_visible(windowHost.window(), true);
                visualDirty = true;
            }
            else return;
        }
        if (dragging != Surface::eNone)
        {
            const auto pointer = glfw_ui_pointer_sample(windowHost.window(), windowHost.engine());
            if (pointer.hasPoint) sample(pointer.point);
            if (!glfw_left_mouse_pressed(windowHost.window()))
            { dragging = Surface::eNone; commitPending = true; }
        }
        if (import.valid() && import.wait_for(std::chrono::seconds(0)) == std::future_status::ready)
        {
            auto result = import.get();
            if (result.generation == importGeneration)
            {
                status = result.error;
                if (result.image.valid())
                {
                    result.image.sourcePath = directory / ("image-" + std::to_string(++imageSerial) + ".ppm");
                    if (images.add(std::move(result.image)))
                    {
                        selectedImage = images.images().size();
                        tab = 3u;
                        resetEngine = uploadedImages >= 8u;
                        rebuildPending = true;
                    }
                }
                visualDirty = true;
            }
        }
        if (resetEngine)
        {
            resetEngine = false;
            int x = 0, y = 0;
            const bool positioned = glfw_window_position(windowHost.window(), x, y);
            windowHost.close();
            clear_gpu_handles();
            windowHost.open();
            if (positioned && windowHost.is_open()) set_glfw_window_position(windowHost.window(), x, y);
            rebuildPending = false;
        }
        else if (rebuildPending)
        {
            rebuildPending = false;
            windowHost.refresh_template();
        }
        if (searchChanged) { refresh_named(); }
        animate_crayons();
        windowHost.tick();
        if (!windowHost.is_open()) { close(); return; }
        execute_action();
        const auto focused = windowHost.panel()->input_state().focusedTextInput;
        if (previousFocused != entt::null && focused != previousFocused && previewPending) commitPending = true;
        previousFocused = focused;
        if (editCommitAt != std::chrono::steady_clock::time_point {} &&
            std::chrono::steady_clock::now() >= editCommitAt)
        { editCommitAt = {}; commitPending = true; }
        deliver(std::exchange(commitPending, false));
    }
};

UiColourPalettePanel::UiColourPalettePanel(UiColourPalettePanelCreateInfo createInfo) :
    impl(std::make_unique<Impl>(std::move(createInfo))) {}
UiColourPalettePanel::~UiColourPalettePanel() = default;

void UiColourPalettePanel::request_open(std::uint32_t colour, std::string targetLabel,
    Change onPreview, Change onCommit)
{
    if (impl->windowHost.is_closing()) impl->close(true);
    if (impl->windowHost.is_open()) impl->deliver(true);
    if (impl->pickingScreen)
    {
        ++*impl->lifetimeGeneration;
        impl->pickingScreen = impl->nativeScreenPicker = false;
        if (impl->windowHost.is_open()) set_glfw_window_visible(impl->windowHost.window(), true);
    }
    impl->rgb = colour & 0xFFFFFFu;
    impl->hsv = colour_palette::rgb_to_hsv(impl->rgb);
    impl->preview = std::move(onPreview);
    impl->commit = std::move(onCommit);
    impl->targetLabel = std::move(targetLabel);
    impl->lastPreview = impl->rgb;
    impl->lastCommit = impl->rgb;
    impl->previewPending = impl->commitPending = impl->editedSinceCommit = false;
    impl->editCommitAt = {};
    impl->dragging = Impl::Surface::eNone;
    impl->pendingAction = Impl::Action::eNone;
    impl->previousFocused = entt::null;
    impl->renaming = false;
    impl->status.clear();
    impl->visualDirty = true;
    if (impl->windowHost.is_open())
    {
        impl->rebuildPending = true;
        if (impl->info.focusOnShow) focus_glfw_window(impl->windowHost.window());
    }
    else impl->openRequested = true;
}

void UiColourPalettePanel::tick() { impl->tick(); }
void UiColourPalettePanel::close(bool immediately) { impl->close(immediately); }
bool UiColourPalettePanel::is_open() const { return impl->openRequested || impl->windowHost.is_open(); }
bool UiColourPalettePanel::has_pointer_capture() const
{
    return impl->pickingScreen || impl->dragging != Impl::Surface::eNone ||
        (impl->windowHost.panel() && impl->windowHost.panel()->has_pointer_capture());
}
bool UiColourPalettePanel::is_focused() const { return impl->windowHost.window() && glfw_window_focused(impl->windowHost.window()); }
bool UiColourPalettePanel::is_hovered() const { return impl->windowHost.is_hovered(); }
bool UiColourPalettePanel::busy() const { return impl->import.valid() || impl->pickingScreen; }
std::uint32_t UiColourPalettePanel::colour() const { return impl->rgb; }
std::size_t UiColourPalettePanel::active_tab() const { return impl->tab; }
void UiColourPalettePanel::select_tab(std::size_t tab) { impl->tab = std::min<std::size_t>(tab, 4u); impl->rebuildPending = true; }
void UiColourPalettePanel::set_appearance(UiTheme theme, std::string locale)
{
    impl->info.theme = std::move(theme);
    impl->info.locale = std::move(locale);
    impl->appearanceTheme = impl->info.theme;
    impl->rebuildPending = true;
}
void UiColourPalettePanel::import_image(colour_palette::ImageSource source) { impl->start_import(std::move(source)); }
GlfwPanelWindowHost* UiColourPalettePanel::host() { return &impl->windowHost; }

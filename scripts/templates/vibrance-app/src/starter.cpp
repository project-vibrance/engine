// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "starter.h"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
std::filesystem::path find_font(const std::filesystem::path& assets)
{
    std::vector<std::filesystem::path> candidates;
    if (const char* overridePath = std::getenv("VIBRANCE_STARTER_FONT")) candidates.emplace_back(overridePath);
    candidates.push_back(assets / "fonts/Starter.ttf");
    candidates.push_back(assets / "fonts/Inter-Regular.ttf");
#if defined(_WIN32)
    const char* windows = std::getenv("WINDIR");
    const auto fonts = std::filesystem::path(windows ? windows : "C:/Windows") / "Fonts";
    candidates.push_back(fonts / "segoeui.ttf");
    candidates.push_back(fonts / "arial.ttf");
#elif defined(__APPLE__)
    candidates.emplace_back("/System/Library/Fonts/SFNS.ttf");
    candidates.emplace_back("/System/Library/Fonts/Supplemental/Arial.ttf");
    candidates.emplace_back("/Library/Fonts/Arial.ttf");
#else
    candidates.emplace_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    candidates.emplace_back("/usr/share/fonts/truetype/liberation2/LiberationSans-Regular.ttf");
    candidates.emplace_back("/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf");
    candidates.emplace_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
#endif
    for (const auto& path : candidates)
        if (ui_path_exists(path)) return path;
    throw std::runtime_error("No starter font was found. Add assets/fonts/Starter.ttf or set VIBRANCE_STARTER_FONT to a font file.");
}

glm::vec4 tint_colour(std::uint32_t rgb, float alpha = 1.0f)
{
    return { float((rgb >> 16u) & 255u) / 255.0f, float((rgb >> 8u) & 255u) / 255.0f,
        float(rgb & 255u) / 255.0f, alpha };
}
}

UiTheme StarterApplication::theme() const { return dark ? ui_dark_theme() : ui_light_theme(); }

std::string StarterApplication::tr(std::string_view key, std::string_view fallback) const
{
    return engine ? engine->localisation().resolve(ui_tr(std::string(key), std::string(fallback))) : std::string(fallback);
}

entt::entity StarterApplication::label(UiWindowContext& view, entt::entity parent,
    std::string value, glm::vec2 position, float width, float size, bool muted)
{
    UiTextBlockOptions options;
    options.width = width;
    options.fontSize = size;
    options.offset = position;
    options.layer = 5;
    options.style = muted ? theme().muted_text_style() : theme().text_style();
    return ui_create_text_block(view.ui, view.fontAtlas, parent, std::move(value), options).entity;
}

entt::entity StarterApplication::card(UiWindowContext& view, entt::entity parent,
    glm::vec2 position, glm::vec2 size)
{
    return view.block(theme().panel_palette().cardSurface, 16.0f).inside(parent)
        .at(UiAlignment::eTopLeft).offset(position).size(size).layer(2).entity();
}

UiControlHandle StarterApplication::button(UiWindowContext& view, entt::entity parent,
    std::string caption, glm::vec2 position, float width, std::function<void()> clicked, bool primary)
{
    auto options = ui_button_options_from_theme(theme(), primary);
    options.truncateLabel = true;
    options.onClick = [clicked = std::move(clicked)](const PointerInputEvent&) { clicked(); };
    const auto result = ui_create_text_button(view.ui, view.fontAtlas, std::move(caption),
        { width, 34.0f }, 6, 0, std::move(options));
    view.ui.place(result.root).inside(parent).at(UiAlignment::eTopLeft).offset(position).size(width, 34.0f);
    return result;
}

void StarterApplication::set_label(entt::entity entity, std::string value)
{
    if (!engine || entity == entt::null) return;
    auto& scene = engine->renderer2d_scene();
    const auto* text = scene.registry().try_get<TextComponent>(entity);
    if (text && value.find('\n') == std::string::npos)
        if (const auto* layout = scene.registry().try_get<TextLayout2DComponent>(entity))
            value = ui_truncate_text_to_width_with_ellipsis(engine->renderer2d_font_atlas(), value,
                text->fontSize, layout->options.maximumWidth, 0u);
    if (text && text->text != value) set_text_entity(scene, engine->renderer2d_font_atlas(), entity, value);
}

void StarterApplication::set_status(std::string value)
{
    status = ui_truncate_text_with_ellipsis(value, 120);
    set_label(statusLabel, status);
}

void StarterApplication::build(UiWindowContext& view)
{
    engine = &view.engine;
    const float width = view.contentSize.x;
    label(view, view.root, tr("starter.heading", "Build with vibranceUI"), { 0, 0 }, width - 300, 27);
    const auto& manifest = vibrance_engine_manifest();
    label(view, view.root, std::string("C++20  /  ") +
        (engine->render_backend() == RenderBackend::eMetal ? "Metal" : "Vulkan") + "  /  " + manifest.full_version,
        { 0, 38 }, width - 300, 12, true);
    const std::array<std::string, 3> captions { tr("tab.controls", "Controls"),
        tr("tab.graphics", "Graphics"), tr("tab.services", "Services") };
    for (std::size_t index = 0; index < captions.size(); ++index)
        button(view, view.root, captions[index], { float(index) * 128.0f, 72 }, 116,
            [this, index] { pageIndex = index; ui_page_stack_show(engine->renderer2d_scene(), pages, index); });

    auto appearance = ui_dropdown_options_from_theme(theme());
    appearance.selectedIndex = dark ? 1 : 0;
    appearance.onChanged = [this](int index, std::string_view, const PointerInputEvent&) {
        dark = index == 1; rebuildPending = true;
    };
    ui_create_dropdown(view.ui, view.fontAtlas, view.root, UiAlignment::eTopLeft,
        { width - 280, 4 }, { 132, 32 }, { tr("theme.light", "Light"), tr("theme.dark", "Dark") }, 10, 0, appearance);
    auto language = ui_dropdown_options_from_theme(theme());
    language.selectedIndex = locale == "de_de" ? 1 : 0;
    language.onChanged = [this](int index, std::string_view, const PointerInputEvent&) {
        locale = index == 1 ? "de_de" : "en_gb";
        Engine::set_shared_locale(locale);
        rebuildPending = true;
    };
    ui_create_dropdown(view.ui, view.fontAtlas, view.root, UiAlignment::eTopLeft,
        { width - 136, 4 }, { 136, 32 }, { "English (UK)", "Deutsch" }, 10, 0, language);

    const auto content = view.ui.layout_group(1, 0);
    view.ui.place(content).inside(view.root).at(UiAlignment::eTopLeft).offset(0, 126)
        .size(width, std::max(view.contentSize.y - 178.0f, 400.0f));
    for (auto& root : pageRoots) root = ui_create_page_root(view.ui, content, 1);
    build_controls(view, pageRoots[0], width);
    build_graphics(view, pageRoots[1], width);
    build_services(view, pageRoots[2], width);
    UiPageStackOptions pageOptions;
    pageOptions.initialPage = pageIndex;
    pages = ui_create_page_stack(view.ui.scene(), { { 0, pageRoots[0] }, { 1, pageRoots[1] }, { 2, pageRoots[2] } }, pageOptions);
    if (status.empty()) status = tr("status.ready", "Ready. Explore the controls, graphics and services.");
    statusLabel = label(view, view.root, status, { 0, view.contentSize.y - 35 }, width, 12, true);
    update_level();
    apply_material();
    update_timer();
}

void StarterApplication::build_controls(UiWindowContext& view, entt::entity page, float width)
{
    const float column = (width - 20.0f) * 0.5f;
    const auto left = card(view, page, {}, { column, 400 });
    const auto right = card(view, page, { column + 20, 0 }, { column, 400 });
    label(view, left, tr("controls.title", "Interactive controls"), { 20, 18 }, column - 40, 20);
    label(view, left, tr("controls.copy", "Real SDK controls with callbacks and retained state."),
        { 20, 52 }, column - 40, 13, true);
    actionButton = button(view, left, tr("controls.button", "Count a click"), { 20, 110 }, column - 40,
        [this] { set_status(tr("status.clicks", "Clicks") + ": " + std::to_string(++clicks)); }, true);
    view.ui.registry().get<ButtonInputComponent>(actionButton.root).enabled = actionEnabled;
    label(view, left, tr("controls.switch", "Enable action button"), { 20, 169 }, column - 120);
    auto toggle = ui_switch_options_from_theme(theme(), actionEnabled);
    toggle.onChanged = [this](bool value, const PointerInputEvent&) {
        actionEnabled = value; rebuildPending = true;
    };
    ui_create_switch(view.ui, left, UiAlignment::eTopLeft, { column - 94, 166 }, { 64, 24 }, 6, 0, toggle);
    UiCheckboxOptions checkbox;
    checkbox.checked = checked;
    checkbox.labelColor = theme().text;
    checkbox.checkedColor = theme().accent;
    checkbox.onChanged = [this](bool value, const PointerInputEvent&) {
        checked = value;
        set_status(tr("controls.checkbox", "Sample checkbox") + ": " +
            (value ? tr("status.on", "on") : tr("status.off", "off")));
    };
    optionCheckbox = ui_create_checkbox(view.ui, view.fontAtlas, left, UiAlignment::eTopLeft,
        { 20, 221 }, tr("controls.checkbox", "Sample checkbox"), { column - 40, 28 }, 6, 0, checkbox);
    label(view, left, tr("controls.dropdown", "Dropdown selection"), { 20, 277 }, column - 40, 13, true);
    auto format = ui_dropdown_options_from_theme(theme());
    format.selectedIndex = selectedFormat;
    format.onChanged = [this](int index, std::string_view item, const PointerInputEvent&) {
        selectedFormat = index; set_status(std::string(item));
    };
    ui_create_dropdown(view.ui, view.fontAtlas, left, UiAlignment::eTopLeft, { 20, 307 },
        { column - 40, 34 }, { "PNG", "SVG", "Lottie", "Video" }, 7, 0, format);

    label(view, right, tr("controls.input", "Text and progress"), { 20, 18 }, column - 40, 20);
    auto search = ui_search_field_options_from_theme(theme(), {}, tr("controls.placeholder", "Type something…"));
    search.backdropBlurRadius = 0;
    search.maxBytes = 1024;
    search.onChanged = [this](const std::string& value) {
        inputValue = value;
        set_label(echoLabel, tr("controls.echo", "You typed") + ": " + ui_truncate_text_with_ellipsis(value, 120));
    };
    search.onSubmitted = [this](const std::string& value) { set_status(value); };
    inputField = ui_create_search_field(view.ui, view.fontAtlas, right, UiAlignment::eTopLeft,
        { 20, 64 }, { column - 40, 36 }, 6, 0, search);
    view.ui.registry().get<TextInputComponent>(inputField.root).value = inputValue;
    ui_update_text_input_visual(view.ui.scene(), view.fontAtlas, inputField.root);
    echoLabel = label(view, right, tr("controls.echo", "You typed") + ": " +
        ui_truncate_text_with_ellipsis(inputValue, 120), { 20, 117 }, column - 40);
    set_label(echoLabel, tr("controls.echo", "You typed") + ": " + inputValue);
    label(view, right, tr("controls.input_hint", "Try word selection, clipboard shortcuts and undo/redo."),
        { 20, 161 }, column - 40, 12, true);
    label(view, right, tr("controls.slider", "Progress and audio volume"), { 20, 219 }, column - 40, 13);
    auto slider = ui_slider_options_from_theme(theme(), level);
    slider.maxValue = 100;
    slider.step = 1;
    slider.onChanged = [this](const SliderInputEvent& event) { level = event.value; update_level(); };
    levelSlider = ui_create_slider(view.ui, right, UiAlignment::eTopLeft, { 20, 258 }, { column - 40, 6 }, 6, 0, slider);
    UiCircularProgressOptions progress;
    progress.value = level;
    progress.maxValue = 100;
    progress.trackColor = theme().surfaceMuted;
    progress.progressColor = theme().accent;
    levelRing = ui_create_circular_progress(view.ui, right, UiAlignment::eTopLeft, { 20, 300 }, 68, 6, 0, progress).root;
    levelLabel = label(view, right, std::to_string(int(level)) + "%", { 110, 319 }, column - 130, 24);
}

void StarterApplication::build_graphics(UiWindowContext& view, entt::entity page, float width)
{
    const float column = (width - 20.0f) * 0.62f;
    const float side = width - column - 20;
    const auto left = card(view, page, {}, { column, 400 });
    const auto right = card(view, page, { column + 20, 0 }, { side, 400 });
    label(view, left, tr("graphics.title", "Media and materials"), { 20, 18 }, column - 40, 20);
    if (!sampleImage.valid())
    {
        Media2DLoadOptions load;
        load.rasterWidth = 640;
        load.rasterHeight = 240;
        sampleImage = engine->load_media_2d(assets / "images/sample.svg", load);
        if (!displayedImage.valid()) displayedImage = sampleImage;
    }
    mediaPreview = view.media(displayedImage, { column - 40, 180 }, Media2DFit::eContain)
        .inside(left).at(UiAlignment::eTopLeft).offset(20, 58).layer(3).entity();
    glassPreview = view.block("#FFFFFF12", 26).inside(left).at(UiAlignment::eTopLeft)
        .offset(52, 96).size(std::min(260.0f, column - 104), 106).layer(4).entity();
    view.text("vibranceUI", 23, "#FFFFFFFF").inside(glassPreview).at(UiAlignment::eCenter).layer(6);
    label(view, left, tr("graphics.blur", "Blur"), { 20, 266 }, column - 40, 13);
    auto diffusion = ui_slider_options_from_theme(theme(), blur);
    diffusion.maxValue = 100;
    diffusion.step = 1;
    diffusion.onChanged = [this](const SliderInputEvent& event) { blur = event.value; apply_material(); };
    ui_create_slider(view.ui, left, UiAlignment::eTopLeft, { 20, 300 }, { column - 40, 6 }, 6, 0, diffusion);
    label(view, left, tr("graphics.refraction", "Refraction"), { 20, 332 }, column - 40, 13);
    auto bend = diffusion;
    bend.value = refraction;
    bend.onChanged = [this](const SliderInputEvent& event) { refraction = event.value; apply_material(); };
    ui_create_slider(view.ui, left, UiAlignment::eTopLeft, { 20, 366 }, { column - 40, 6 }, 6, 0, bend);
    label(view, right, tr("graphics.material", "Surface material"), { 20, 18 }, side - 40, 18);
    auto material = ui_dropdown_options_from_theme(theme());
    material.selectedIndex = liquid ? 1 : 0;
    material.onChanged = [this](int index, std::string_view, const PointerInputEvent&) { liquid = index == 1; apply_material(); };
    ui_create_dropdown(view.ui, view.fontAtlas, right, UiAlignment::eTopLeft, { 20, 66 },
        { side - 40, 34 }, { tr("material.solid", "Solid"), "Liquid Glass" }, 7, 0, material);
    button(view, right, tr("graphics.colour", "Choose glass tint…"), { 20, 122 }, side - 40, [this] { choose_colour(); });
    colourSwatch = view.block("#6699FFFF", 10).inside(right).at(UiAlignment::eTopLeft)
        .offset(20, 175).size(side - 40, 40).layer(5).entity();
    button(view, right, tr("graphics.load", "Load image / animation…"), { 20, 239 }, side - 40, [this] { choose_media(); });
    label(view, right, tr("graphics.caption", "SVG artwork is bundled. Import raster images, SVG, Lottie or a short video."),
        { 20, 295 }, side - 40, 12, true);
}

void StarterApplication::build_services(UiWindowContext& view, entt::entity page, float width)
{
    const float column = (width - 20.0f) * 0.5f;
    const auto left = card(view, page, {}, { column, 400 });
    const auto right = card(view, page, { column + 20, 0 }, { column, 400 });
    label(view, left, tr("services.title", "Countdown and audio"), { 20, 18 }, column - 40, 20);
    UiCircularProgressOptions progress;
    progress.trackColor = theme().surfaceMuted;
    progress.progressColor = theme().accent;
    timerRing = ui_create_circular_progress(view.ui, left, UiAlignment::eTopLeft, { 20, 64 }, 76, 6, 0, progress).root;
    timerLabel = label(view, left, "0:30", { 120, 84 }, column - 140, 28);
    const float actionWidth = (column - 52) * 0.5f;
    button(view, left, tr("services.start", "Start / pause"), { 20, 170 }, actionWidth, [this] {
        const double now = glfw_time_seconds();
        if (timer.status() == CountdownTimerStatus::eIdle || timer.status() == CountdownTimerStatus::eFinished) timer.start(now);
        else timer.toggle_pause(now);
        update_timer();
    }, true);
    button(view, left, tr("services.reset", "Reset"), { 32 + actionWidth, 170 }, actionWidth,
        [this] { timer.cancel(); update_timer(); });
    label(view, left, engine->audio().available() ? engine->audio().backend_name() :
        tr("services.audio_unavailable", "Audio playback unavailable"), { 20, 239 }, column - 40, 13, true);
    auto sound = button(view, left, tr("services.audio", "Play sample sound"), { 20, 278 }, column - 40, [this] {
        if (!sampleAudio.valid()) sampleAudio = engine->load_audio_clip(assets / "audio/sample.wav");
        if (voice.valid()) engine->audio().stop(voice);
        voice = engine->audio().play(sampleAudio);
        if (!voice.valid()) set_status(tr("services.audio_unavailable", "Audio playback unavailable"));
    });
    view.ui.registry().get<ButtonInputComponent>(sound.root).enabled = engine->audio().available();
    button(view, left, tr("services.stop", "Stop sound"), { 20, 326 }, column - 40,
        [this] { if (voice.valid()) engine->audio().stop(voice); });
    label(view, right, tr("services.native", "Platform services"), { 20, 18 }, column - 40, 20);
    const auto support = [this](bool value) {
        return value ? tr("services.supported", "supported") : tr("services.unavailable", "unavailable");
    };
    const std::string capabilities = tr("services.displays", "Displays") + ": " + std::to_string(glfw_connected_monitors().size()) +
        "\n" + tr("services.system_glass", "System backdrop") + ": " + support(engine->system_backdrop_available()) +
        "\n" + tr("services.sessions", "Media sessions") + ": " + support(GlobalMediaSession::platform_supported()) +
        "\n" + tr("services.capture", "Audio capture") + ": " + support(AudioLoopbackCapture::platform_supported()) +
        "\n" + tr("services.notifications", "Notification provider") + ": " + support(SystemNotificationProvider::platform_supported());
    label(view, right, capabilities, { 20, 65 }, column - 40, 13, true);
    button(view, right, tr("services.folder", "Choose a folder…"), { 20, 226 }, column - 40, [this] {
        NativeFolderDialogOptions options;
        options.title = tr("services.folder", "Choose a folder");
        options.parentWindow = glfw_native_window_handle(window->native_window());
        if (const auto path = show_native_folder_dialog(options)) set_status(vibrance::path_utf8(*path));
    });
    button(view, right, tr("services.assets", "Open assets folder"), { 20, 274 }, column - 40, [this] {
        std::string error;
        if (!native_open_directory(assets, error)) set_status(error);
    });
    auto session = button(view, right, tr("services.media", "Read media session"), { 20, 322 }, column - 40, [this] {
        if (!mediaSession) mediaSession = std::make_unique<GlobalMediaSession>();
        mediaSession->refresh();
        const auto& snapshot = mediaSession->snapshot();
        set_status(snapshot.sessionAvailable ? snapshot.title + " — " + snapshot.artist :
            tr("services.no_media", "No active media session"));
    });
    view.ui.registry().get<ButtonInputComponent>(session.root).enabled = GlobalMediaSession::platform_supported();
}

void StarterApplication::apply_material()
{
    if (!engine || glassPreview == entt::null) return;
    auto& scene = engine->renderer2d_scene();
    auto& registry = scene.registry();
    UiBuilder ui(scene);
    if (liquid)
    {
        auto material = ui_liquid_glass_options();
        material.sceneBackdrop = true;
        material.liquidBackend = LiquidGlassBackend::eEngineRenderer;
        material.liquidBlur = blur / 100.0f;
        material.liquidRefraction = refraction / 100.0f;
        material.liquidRimAnimated = false;
        const auto colour = tint_colour(tint, 0.18f);
        registry.get<ShapeStyleComponent>(glassPreview).set_color(colour);
        ui_apply_glass_material(ui, glassPreview, material);
        ui_set_liquid_stroke_surface(scene, glassPreview, colour, colour);
    }
    else
    {
        ui_clear_glass_material(ui, glassPreview);
        registry.get<ShapeStyleComponent>(glassPreview).color0 = tint_colour(tint);
        registry.get<ShapeStyleComponent>(glassPreview).color1 = tint_colour(tint);
    }
    if (registry.valid(colourSwatch))
    {
        auto& swatch = registry.get<ShapeStyleComponent>(colourSwatch);
        swatch.color0 = swatch.color1 = tint_colour(tint);
        scene.mark_dirty(colourSwatch);
    }
    scene.mark_dirty(glassPreview);
}

void StarterApplication::update_level()
{
    if (!engine) return;
    engine->audio().set_global_volume(level / 100.0f);
    ui_set_circular_progress(engine->renderer2d_scene(), levelRing, level);
    set_label(levelLabel, std::to_string(int(level)) + "%");
}

void StarterApplication::update_timer()
{
    if (!engine) return;
    const double now = glfw_time_seconds();
    timer.update(now);
    const auto snapshot = timer.snapshot(now);
    const bool idle = snapshot.status == CountdownTimerStatus::eIdle;
    set_label(timerLabel, format_countdown_time(idle ? timer.selected_seconds() : snapshot.remainingSeconds));
    ui_set_circular_progress(engine->renderer2d_scene(), timerRing, idle ? 1.0f : snapshot.remainingFraction);
}

void StarterApplication::choose_colour()
{
    if (!palette)
    {
        UiColourPalettePanelCreateInfo options;
        options.resources.packagedAssetsDirectory = assets;
        options.theme = theme();
        options.locale = locale;
        options.focusOnShow = window->options().focusOnShow;
        options.controlMaterial = [](const UiTheme&) { return UiGlassOptions {}; };
        options.localisationDirectories = { assets / "lang" };
        options.configurePanel = [this](GlfwPanelWindowTemplateOptions& panel,
            const UiResourceDirectories&, std::string_view) { panel.fontPath = font; };
        palette = std::make_unique<UiColourPalettePanel>(std::move(options));
    }
    const auto changed = [this](std::uint32_t value) { tint = value; apply_material(); };
    palette->request_open(tint, tr("graphics.colour", "Glass tint"), changed, changed);
}

void StarterApplication::choose_media()
{
    NativeFileDialogOptions options;
    options.title = tr("graphics.load", "Load image / animation");
    options.parentWindow = glfw_native_window_handle(window->native_window());
    options.filters = { { "Images and animations", { "png", "jpg", "jpeg", "webp", "gif", "svg", "json", "mp4" } } };
    std::string error;
    const auto path = show_native_file_dialog(options, error);
    if (!path) { if (!error.empty()) set_status(error); return; }
    try
    {
        if (std::filesystem::file_size(*path) > 64u * 1024u * 1024u)
            throw std::runtime_error(tr("graphics.too_large", "Choose a media file up to 64 MiB for this example."));
        Media2DLoadOptions load;
        load.rasterWidth = 640;
        load.rasterHeight = 360;
        load.resizeRasterImages = true;
        load.maxAnimationFrames = load.svgAnimationFrames = load.lottieAnimationFrames = load.maxVideoFrames = 48;
        load.maxVideoPixels = 640u * 360u;
        load.maxDecodedBytes = 48u * 1024u * 1024u;
        const auto media = engine->load_media_2d(*path, load);
        if (!media.valid() || !media.drawable) throw std::runtime_error("The selected media could not be decoded.");
        displayedImage = media;
        auto& scene = engine->renderer2d_scene();
        scene.registry().get<Media2DComponent>(mediaPreview).set_media(media);
        scene.mark_dirty(mediaPreview);
        set_status(vibrance::path_utf8(path->filename()));
    }
    catch (const std::exception& failure) { set_status(failure.what()); }
}

void StarterApplication::refresh_theme()
{
    rebuildPending = false;
    auto options = window->panel()->template_options();
    options.theme = theme();
    window->panel()->set_template_options(std::move(options));
    if (palette) palette->set_appearance(theme(), locale);
}

void StarterApplication::smoke_test()
{
    const auto require = [](bool condition, const char* message) {
        if (!condition) throw std::runtime_error(message);
    };
    auto& scene = engine->renderer2d_scene();
    require(sampleImage.valid() && sampleImage.drawable, "The packaged SVG was not loaded.");
    require(scene.registry().get<TextComponent>(echoLabel).bounds.y > 0, "The starter font was not loaded.");
    scene.registry().get<SliderInputComponent>(levelSlider.root).onChanged({ levelSlider.root, {}, 75, 0.75f });
    auto& input = scene.registry().get<TextInputComponent>(inputField.root);
    input.value = "first second";
    input.onChanged(input.value);
    scene.registry().get<UiCheckboxControlComponent>(optionCheckbox.root).onChanged(true, {});
    require(scene.registry().get<UiCircularProgressComponent>(levelRing).value == 75, "The slider did not update retained progress.");
    require(scene.registry().get<TextComponent>(echoLabel).text.find("first second") != std::string::npos,
        "The text field did not update its retained output.");
    input.value.assign(200, 'W');
    input.onChanged(input.value);
    require(scene.registry().get<TextComponent>(echoLabel).bounds.x <=
        scene.registry().get<TextLayout2DComponent>(echoLabel).options.maximumWidth + 1.0f,
        "Long text escaped the output label bounds.");
    input.value = "first second";
    input.onChanged(input.value);
    ui_page_stack_show(scene, pages, 1);
    require(ui_page_stack_active_page(scene, pages) == 1u, "Page navigation failed.");
    window->tick();
    liquid = false;
    apply_material();
    require(!scene.registry().any_of<LiquidGlassComponent>(glassPreview), "The solid material retained a glass pass.");
    window->tick();
    liquid = true;
    apply_material();
    require(scene.registry().get<LiquidGlassComponent>(glassPreview).sceneBackdrop,
        "The starter glass must sample the scene.");
    window->tick();
    ui_page_stack_show(scene, pages, 2);
    window->tick();
    dark = !dark;
    refresh_theme();
    require(level == 75 && checked && engine->renderer2d_scene().registry().get<TextInputComponent>(inputField.root).value == "first second",
        "A theme rebuild discarded the edited controls.");
    Engine::set_shared_locale("de_de");
    locale = "de_de";
    refresh_theme();
    require(tr("tab.controls", "Controls") == "Bedienelemente", "The packaged German dictionary was not loaded.");
    window->panel()->set_size(1080, 800);
    window->tick();
    require(level == 75 && checked && engine->renderer2d_scene().registry().get<TextInputComponent>(inputField.root).value == "first second",
        "Resizing discarded the edited controls.");
    choose_colour();
    for (unsigned frame = 0; frame < 4; ++frame) palette->tick();
    require(palette->is_open() && palette->host() && palette->host()->engine(), "The colour-picker window did not open.");
    auto& pickerRegistry = palette->host()->engine()->renderer2d_scene().registry();
    for (const auto entity : pickerRegistry.view<SystemBackdropComponent>())
        require(pickerRegistry.get<SystemBackdropComponent>(entity).region.material != GlassMaterial::eLiquid,
            "The starter colour picker must not request native glass deployment.");
    palette->close(true);
    std::cout << "Starter smoke test passed: runtime, assets, fonts, controls, pages, materials, themes, German, resize and colour picker.\n";
}

int StarterApplication::run(bool smokeTest)
{
    assets = current_executable_path().parent_path() / "assets";
    font = find_font(assets);
    Engine::set_shared_locale(locale);
    UiWindowOptions options;
    options.title = "vibranceUI SDK starter";
    options.size = { 1040, 760 };
    options.minSize = { 940, 740 };
    options.contentMargin = { 32, 72, 32, 26 };
    options.transparentFramebuffer = true;
    options.focusOnShow = !smokeTest;
    options.fontPath = font;
    options.useLocalePrimaryFont = false;
    options.localisationDirectory = assets / "lang";
    options.theme = theme();
    options.enableAudio = !smokeTest;
    options.targetFrameRate = 60;
    options.build = [this](UiWindowContext& view) { build(view); };
    window = std::make_unique<UiWindow>(std::move(options));
    if (!window->open()) return 1;
    if (smokeTest)
    {
        for (unsigned frame = 0; frame < 3; ++frame) window->tick();
        smoke_test();
        window->close();
        terminate_glfw();
        return 0;
    }
    while (window->is_open())
    {
        if (rebuildPending && !window->panel()->has_pointer_capture()) refresh_theme();
        update_timer();
        window->tick();
        if (!window->is_open()) break;
        if (palette) palette->tick();
        double wait = Engine::idle_event_wait_seconds();
        if (timer.status() == CountdownTimerStatus::eRunning) wait = std::min(wait, 0.05);
        if (wait > 0) wait_glfw_events(wait);
    }
    engine = nullptr;
    if (palette) palette->close(true);
    window->close();
    terminate_glfw();
    return 0;
}

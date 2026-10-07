#include <vibranceUI/engine.h>
#include <vibranceUI/ui/liquid_controls.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "control interaction test failed: " << message << '\n';
        }
        return condition;
    }
}

int main()
{
    bool passed = true;
    for (const auto& palette : { ui_light_theme(), ui_dark_theme() })
    {
        const auto slider = ui_slider_options_from_theme(palette);
        passed &= expect(slider.thumbOutlineWidth == 0.0f &&
            renderer2d_hex_color(slider.thumbOutlineColor).a == 0.0f && slider.renderThumbShadow,
            "Settings slider thumbs must retain the same borderless paint in both themes and every material");
        Renderer2DScene thumbScene;
        UiBuilder thumbUi(thumbScene);
        const auto handle = ui_create_slider(thumbUi, thumbUi.root(), UiAlignment::eTopLeft,
            { 20.0f, 50.0f }, { 300.0f, 7.0f }, 2, 0u, slider);
        const auto* shadow = thumbScene.registry().try_get<ShadowComponent>(handle.knob);
        passed &= expect(shadow && shadow->outsideOnly && shadow->spread == 0.0f &&
            shadow->blurRadius == 8.0f && shadow->opacity <= 0.12f,
            "Settings thumbs must have a subtle outside shadow without a dark ring");
    }

    {
        Renderer2DScene lightsScene;
        UiBuilder lightsUi(lightsScene);
        const glm::vec4 colour = renderer2d_hex_color("#FF5F57FF");
        const auto light = lightsScene.create_shape({ 20.0f, 20.0f }, { 14.0f, 14.0f },
            make_solid_style(colour), Renderer2DPrimitive::eEllipse);
        auto& registry = lightsScene.registry();
        registry.emplace<UiLiquidControlSurface>(light, UiLiquidControlSurface { false, false, true });
        int actions = 0;
        ButtonInputComponent button {};
        button.onClick = [&](const PointerInputEvent&) { ++actions; };
        registry.emplace<ButtonInputComponent>(light, std::move(button));
        StretchDynamicsOptions stretch {};
        stretch.enabled = false;
        stretch.grabScale = 1.15f;
        stretch.consumeClickAfterDrag = true;
        stretch.returnDampingFraction = 0.54f;
        ui_enable_stretch_dynamics(lightsScene, light, stretch);
        auto optics = ui_liquid_glass_options();
        optics.liquidRefraction = 0.85f;
        optics.liquidTint = { 0.0f, 0.0f, 0.0f, 0.25f };
        ui_update_liquid_controls(lightsScene, true, optics);
        const auto glass = registry.get<ShapeStyleComponent>(light).color0;
        passed &= expect(registry.get<LiquidGlassComponent>(light).refraction == 0.85f &&
            glass.r > glass.g * 2.0f && glass.r > glass.b * 2.0f && glass.a < 1.0f,
            "traffic light lenses must follow the optical settings and retain their colour");
        const auto& lightPaint = registry.get<ShapeStyleComponent>(light);
        passed &= expect(lightPaint.fill == Renderer2DFill::eLinearGradient &&
            lightPaint.color0.r > lightPaint.color1.r && lightPaint.outlineWidth > 0.0f &&
            lightPaint.outlineColor.a < 0.3f,
            "traffic light glass must retain a curved colour highlight and a subtle coloured rim");
        const auto& shadow = registry.get<ShadowComponent>(light);
        passed &= expect(shadow.outsideOnly && shadow.spread == 0.0f && shadow.opacity <= 0.06f,
            "shared glass surfaces must have only a faint outside shadow");
        lightsScene.build_render_plan(0.0, 0u);
        UiInputState state {};
        const Renderer2DFontAtlas atlas;
        const glm::vec2 point { 27.0f, 27.0f };
        ui_handle_pointer_button(lightsScene, atlas, state,
            UiPointerButtonInput { true, point, PointerButton::eLeft, 0, UiInputAction::ePress });
        passed &= expect(actions == 0 && registry.get<StretchDynamicsComponent>(light).scale.x > 1.1f,
            "pressing a traffic light must expand it without immediately triggering its action");
        ui_update_stretch_dynamics_drag(lightsScene, state, true, true, point + glm::vec2(4.0f, 0.0f));
        ui_handle_pointer_button(lightsScene, atlas, state,
            UiPointerButtonInput { true, point + glm::vec2(4.0f, 0.0f), PointerButton::eLeft, 0, UiInputAction::eRelease });
        passed &= expect(actions == 0 && registry.get<StretchDynamicsComponent>(light).settling,
            "a traffic light drag must consume the release action and spring back");
        for (int frame = 0; frame < 150; ++frame)
            ui_update_stretch_dynamics_settling(lightsScene, state, 1.0 + frame / 120.0);
        lightsScene.build_render_plan(3.0, 0u);
        ui_handle_pointer_button(lightsScene, atlas, state,
            UiPointerButtonInput { true, point, PointerButton::eLeft, 0, UiInputAction::ePress });
        ui_handle_pointer_button(lightsScene, atlas, state,
            UiPointerButtonInput { true, point, PointerButton::eLeft, 0, UiInputAction::eRelease });
        passed &= expect(actions == 1, "an ordinary traffic light click must commit exactly once on release");
        ui_update_liquid_controls(lightsScene, false, optics);
        passed &= expect(!registry.any_of<LiquidGlassComponent>(light) &&
            registry.get<ShapeStyleComponent>(light).color0 == colour &&
            !registry.any_of<ShadowComponent>(light) && !registry.get<StretchDynamicsComponent>(light).options.enabled,
            "traffic lights must restore their solid colour when Liquid Glass is disabled");
    }

    const std::filesystem::path themeDirectory =
        std::filesystem::temp_directory_path() /
        ("vibrance-theme-choices-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code fileError;
    std::filesystem::create_directories(themeDirectory, fileError);
    {
        std::ofstream(themeDirectory / "light.json") << "{}";
        std::ofstream(themeDirectory / "dark.json") << "{}";
    }
    int selectedTheme = -1;
    const Localisation localisation;
    const std::vector<UiOptionChoice> themeChoices =
        ui_theme_choices_from_directory(
            localisation,
            themeDirectory,
            "theme",
            &selectedTheme);
    passed &= expect(
        themeChoices.size() == 2u &&
            themeChoices[0].code == "light" &&
            themeChoices[1].code == "dark",
        "the legacy light alias and light.json must produce one Light choice");
    passed &= expect(
        selectedTheme == 0,
        "the legacy theme preference should select canonical light");
    std::filesystem::remove_all(themeDirectory, fileError);

    Renderer2DScene scene;
    entt::registry& registry = scene.registry();
    ShapeStyleComponent visibleStyle = {};
    visibleStyle.set_color("#FFFFFFFF");
    ShapeStyleComponent transparentStyle = {};
    transparentStyle.set_color("#00000000");

    const entt::entity menu = scene.create_shape(
        { 0.0f, 0.0f },
        { 160.0f, 80.0f },
        visibleStyle,
        Renderer2DPrimitive::eRectangle);
    registry.get<Transform2DComponent>(menu).origin = { 0.0f, 0.0f };
    registry.emplace<ButtonInputComponent>(menu);

    bool rowClicked = false;
    const entt::entity row = scene.create_shape(
        { 8.0f, 8.0f },
        { 144.0f, 28.0f },
        transparentStyle,
        Renderer2DPrimitive::eRectangle);
    registry.get<Transform2DComponent>(row).origin = { 0.0f, 0.0f };
    registry.emplace<Parent2DComponent>(row, Parent2DComponent { menu });
    ButtonInputComponent rowInput = {};
    rowInput.onClick = [&rowClicked](const PointerInputEvent&) {
        rowClicked = true;
    };
    registry.emplace<ButtonInputComponent>(row, std::move(rowInput));
    registry.emplace<HitRegion2DComponent>(row);

    const Renderer2DFontAtlas fontAtlas;
    UiInputState inputState = {};
    ui_update_input_hover(scene, fontAtlas, inputState, true, { 4.0f, 4.0f });
    passed &= expect(
        inputState.hoveredButton == menu,
        "the menu surface should initially own its padding hover");
    ui_update_input_hover(scene, fontAtlas, inputState, true, { 120.0f, 20.0f });
    passed &= expect(
        inputState.hoveredButton == row &&
            registry.get<ButtonInputComponent>(row).hovered,
        "a nested row should take hover ownership from its menu parent");

    const UiPointerButtonInput press {
        true,
        { 120.0f, 20.0f },
        PointerButton::eLeft,
        0,
        UiInputAction::ePress,
        {}
    };
    UiPointerButtonInput release = press;
    release.action = UiInputAction::eRelease;
    ui_handle_pointer_button(scene, fontAtlas, inputState, press);
    ui_handle_pointer_button(scene, fontAtlas, inputState, release);
    passed &= expect(
        rowClicked,
        "transparent whitespace inside a row should trigger its click");

    LayoutScale scale = {};
    scale.factor = { 1.0f, 1.0f };
    scale.contentScale = scale.factor;
    scale.logicalSize = { 400.0f, 300.0f };
    UiBuilder ui(scene, scale);
    const entt::entity root = ui.root();
    UiButtonOptions transparentButton = {};
    transparentButton.backgroundColor = "#00000000";
    const UiControlHandle textButton = ui_create_text_button(
        ui,
        fontAtlas,
        "Submenu",
        { 260.0f, 52.0f },
        5,
        0u,
        std::move(transparentButton));
    ui.attach_aligned(
        textButton.root,
        root,
        UiAlignment::eTopLeft,
        { 0.0f, 100.0f },
        { 260.0f, 52.0f });
    passed &= expect(
        registry.all_of<HitRegion2DComponent>(textButton.root),
        "standard transparent buttons should own their full rectangle");

    UiDropdownOptions dropdownOptions = {};
    const UiControlHandle dropdown = ui_create_dropdown(
        ui,
        fontAtlas,
        root,
        UiAlignment::eTopLeft,
        { 0.0f, 170.0f },
        { 180.0f, 36.0f },
        { "First", "Second" },
        6,
        0u,
        std::move(dropdownOptions));
    const UiDropdownControlComponent& dropdownControl =
        registry.get<UiDropdownControlComponent>(dropdown.root);
    passed &= expect(
        dropdownControl.itemRows.size() == 2u &&
            registry.all_of<HitRegion2DComponent>(dropdownControl.itemRows[0]) &&
            registry.all_of<HitRegion2DComponent>(dropdownControl.itemRows[1]),
        "every transparent dropdown option should own its complete row");

    for (float dpi : { 1.0f, 1.5f, 1.75f, 3.0f, 5.0f })
    {
        Renderer2DScene sliderScene;
        LayoutScale sliderScale {};
        sliderScale.factor = glm::vec2(dpi);
        sliderScale.logicalSize = { 600.0f, 200.0f };
        UiBuilder sliderUi(sliderScene, sliderScale);
        UiSliderOptions sliderOptions {};
        sliderOptions.minValue = 0.0f;
        sliderOptions.maxValue = 100.0f;
        sliderOptions.thumbSize = { 24.0f, 18.0f };
        sliderOptions.hoveredThumbScale = 1.3f;
        sliderOptions.smoothScrubbing = false;
        const auto sliderHandle = ui_create_slider(sliderUi, sliderUi.root(),
            UiAlignment::eTopLeft, { 20.0f, 50.0f }, { 500.0f, 7.0f }, 2, 0u, sliderOptions);
        auto& sliderRegistry = sliderScene.registry();
        auto& slider = sliderRegistry.get<SliderInputComponent>(sliderHandle.root);
        for (float expansion : { 0.0f, 0.5f, 1.0f })
        {
            slider.hoverExpansion = expansion;
            for (float value : { 0.0f, 1.0f, 5.0f, 50.0f, 100.0f })
            {
                slider.value = value;
                sliderScene.build_render_plan(0.0, 0u);
                ui_update_slider_visual(sliderScene, fontAtlas, sliderHandle.root);
                sliderScene.build_render_plan(0.0, 0u);
                const auto track = ui_entity_framebuffer_rect(sliderRegistry, sliderHandle.root);
                const auto thumb = ui_entity_framebuffer_rect(sliderRegistry, sliderHandle.knob);
                passed &= expect(thumb.x >= track.x - 0.01f &&
                    thumb.x + thumb.z <= track.x + track.z + 0.01f,
                    "slider thumb must remain inside both track ends at every hover size and DPI");
                const auto fill = ui_entity_framebuffer_rect(sliderRegistry, sliderHandle.indicator);
                passed &= expect(std::abs(fill.x + fill.z - thumb.x - thumb.z * 0.5f) < 0.02f,
                    "the slider fill must reach its thumb centre even at the left endpoint and low values");
            }
        }
        const auto track = ui_entity_framebuffer_rect(sliderRegistry, sliderHandle.root);
        const float inset = ui_slider_thumb_inset(track.z, ui_slider_thumb_reserved_width(slider));
        ui_set_slider_value_from_point(sliderScene, sliderHandle.root,
            { track.x + inset, track.y });
        passed &= expect(slider.value == 0.0f, "left thumb endpoint must select the minimum");
        ui_set_slider_value_from_point(sliderScene, sliderHandle.root,
            { track.x + track.z - inset, track.y });
        passed &= expect(slider.value == 100.0f, "right thumb endpoint must select the maximum");
        ui_set_slider_value_from_point(sliderScene, sliderHandle.root,
            { track.x + inset + (track.z - 2.0f * inset) * 0.25f, track.y });
        passed &= expect(std::abs(slider.value - 25.0f) < 0.01f,
            "slider hit mapping must use the thumb travel range");
    }

    {
        Renderer2DScene glassScene;
        LayoutScale glassScale {};
        glassScale.logicalSize = { 400.0f, 200.0f };
        UiBuilder glassUi(glassScene, glassScale);
        const auto button = ui_create_text_button(glassUi, fontAtlas,
            "Normal", { 160.0f, 40.0f }, 2, 0u, UiButtonOptions {});
        UiSliderOptions sliderOptions {};
        sliderOptions.maxValue = 100.0f;
        sliderOptions.thumbSize = { 20.0f, 20.0f };
        sliderOptions.renderThumbShadow = true;
        const auto sliderHandle = ui_create_slider(glassUi, glassUi.root(),
            UiAlignment::eTopLeft, { 20.0f, 70.0f }, { 300.0f, 7.0f }, 2, 0u, sliderOptions);
        auto& glassRegistry = glassScene.registry();
        const auto original = glassRegistry.get<ShapeStyleComponent>(button.root);
        auto& slider = glassRegistry.get<SliderInputComponent>(sliderHandle.root);
        UiGlassOptions optics = ui_liquid_glass_options();
        ui_update_liquid_controls(glassScene, true, optics);
        passed &= expect(!glassRegistry.any_of<LiquidGlassComponent>(button.root) &&
            !glassRegistry.any_of<LiquidGlassComponent>(sliderHandle.root) &&
            !glassRegistry.any_of<LiquidGlassComponent>(slider.fillEntity) &&
            !glassRegistry.any_of<LiquidGlassComponent>(slider.thumbEntity) &&
            glassRegistry.get<ShapeStyleComponent>(button.root).color0 == original.color0,
            "ordinary buttons, viewports, tracks, fills and idle thumbs must retain normal paint");
        passed &= expect(glassRegistry.get<ShapeStyleComponent>(slider.thumbEntity).color0 == glm::vec4(1.0f),
            "an idle slider thumb must be white");
        slider.dragging = true;
        slider.value = 74.5f;
        ui_update_liquid_controls(glassScene, true, optics);
        auto plan = glassScene.build_render_plan(0.0, 0u);
        const auto containsOptics = [](const auto& batches) {
            return std::any_of(batches.begin(), batches.end(), [](const auto& batch) {
                return (batch.flags & eRenderer2DStyleLiquidGlass) != 0u;
            });
        };
        passed &= expect(glassRegistry.any_of<LiquidGlassComponent>(slider.thumbEntity) &&
            (containsOptics(plan.blurs) || containsOptics(plan.cachedBlurs)) &&
            !glassRegistry.any_of<SystemBackdropComponent>(slider.thumbEntity),
            "only a dragged thumb must emit portable optical drawing");
        const auto thumbHasLiquidRim = [&](const auto& batches) {
            return std::any_of(batches.begin(), batches.end(), [&](const auto& batch) {
                return batch.entity == slider.thumbEntity &&
                    (batch.flags & eRenderer2DStyleLiquidRim) != 0u;
            });
        };
        const auto& sliderThumbStyle =
            glassRegistry.get<ShapeStyleComponent>(slider.thumbEntity);
        passed &= expect(sliderThumbStyle.outlineWidth == 0.0f &&
            sliderThumbStyle.outlineColor.a == 0.0f &&
            sliderThumbStyle.edgeSoftness == 0.5f &&
            glassRegistry.any_of<ShadowComponent>(slider.thumbEntity) &&
            glassRegistry.get<ShadowComponent>(slider.thumbEntity).blurRadius >= 8.0f &&
            glassRegistry.get<ShadowComponent>(slider.thumbEntity).opacity <= 0.12f &&
            glassRegistry.get<ShadowComponent>(slider.thumbEntity).spread <= 0.75f &&
            !thumbHasLiquidRim(plan.shapes) && !thumbHasLiquidRim(plan.cachedShapes),
            "configured slider thumbs must have a soft shadow without gaining an outline in normal or liquid rendering");
        float peakScale = 1.0f;
        for (int frame = 0; frame <= 120; ++frame)
        {
            ui_update_slider_smoothing(glassScene, fontAtlas, 1.0 + frame / 120.0);
            peakScale = std::max(peakScale, slider.liquidThumbScale);
        }
        passed &= expect(peakScale > 1.25f && peakScale < 1.32f && slider.value == 74.5f,
            "the grab spring must overshoot slightly without changing the input value");
        for (float value : { 0.0f, 100.0f })
        {
            slider.value = value;
            slider.visualValue = value;
            slider.liquidThumbScale = peakScale;
            ui_update_slider_visual(glassScene, fontAtlas, sliderHandle.root);
            glassScene.build_render_plan(2.0, 0u);
            const auto track = ui_entity_framebuffer_rect(glassRegistry, sliderHandle.root);
            const auto thumb = ui_entity_framebuffer_rect(glassRegistry, slider.thumbEntity);
            passed &= expect(thumb.x >= track.x - 0.01f && thumb.x + thumb.z <= track.x + track.z + 0.01f,
                "a bouncing glass thumb must stay inside both slider endpoints");
        }
        slider.dragging = false;
        slider.hovered = true;
        ui_update_liquid_controls(glassScene, true, optics);
        passed &= expect(!glassRegistry.any_of<LiquidGlassComponent>(slider.thumbEntity) &&
            glassRegistry.get<ShapeStyleComponent>(slider.thumbEntity).color0 == glm::vec4(1.0f),
            "releasing a thumb must immediately restore white paint even while hovered");
        for (int frame = 1; frame <= 120; ++frame)
            ui_update_slider_smoothing(glassScene, fontAtlas, 2.0 + frame / 120.0);
        passed &= expect(std::abs(slider.liquidThumbScale - 1.0f) < 0.0001f,
            "the release spring must settle to normal size");
        const auto cluster = glassScene.create_shape({ 20.0f, 120.0f }, { 62.0f, 32.0f }, original);
        glassRegistry.emplace<UiLiquidControlSurface>(cluster);
        ui_update_liquid_controls(glassScene, true, optics);
        passed &= expect(glassRegistry.any_of<LiquidGlassComponent>(cluster) &&
            !glassRegistry.any_of<LiquidGlassComponent>(button.root),
            "an explicit navigation cluster must own one lens without giving its buttons lenses");
        bool clicked = false;
        glassRegistry.get<ButtonInputComponent>(button.root).onClick =
            [&clicked](const PointerInputEvent&) { clicked = true; };
        glassRegistry.get<ButtonInputComponent>(button.root).onClick(PointerInputEvent {});
        ui_update_liquid_controls(glassScene, false, optics);
        passed &= expect(clicked && !glassRegistry.any_of<LiquidGlassComponent>(cluster) &&
            !slider.liquidThumbEnabled && slider.value == 100.0f &&
            glassRegistry.get<ShapeStyleComponent>(cluster).color0 == original.color0,
            "switching material must restore paints without resetting callbacks or slider values");
    }

    {
        Renderer2DScene controlsScene;
        LayoutScale scale {};
        scale.logicalSize = { 500.0f, 240.0f };
        UiBuilder ui(controlsScene, scale);
        auto& registry = controlsScene.registry();
        const auto search = ui_create_search_field(ui, fontAtlas, ui.root(), UiAlignment::eTopLeft,
            { 20.0f, 20.0f }, { 260.0f, 30.0f }, 2, 0u);
        const auto nav = ui_create_nav_cluster(ui, fontAtlas, ui.root(), UiAlignment::eTopLeft,
            { 20.0f, 70.0f }, { 90.0f, 32.0f }, 2, 0u);
        const auto originalSearch = registry.get<TextInputVisualComponent>(search.root);
        for (auto entity : { search.root, nav.root })
        {
            registry.emplace<UiLiquidControlSurface>(entity);
            StretchDynamicsOptions stretch {};
            stretch.allowTextInputDrag = entity == search.root;
            stretch.consumeClickAfterDrag = true;
            stretch.returnDampingFraction = 0.54f;
            ui_enable_stretch_dynamics(controlsScene, entity, stretch);
        }
        auto optics = ui_liquid_glass_options();
        ui_update_liquid_controls(controlsScene, true, optics);
        controlsScene.build_render_plan(0.0, 0u);
        auto& input = registry.get<TextInputComponent>(search.root);
        input.focused = true;
        input.value = "Search query";
        ui_update_text_input_visual(controlsScene, fontAtlas, search.root);
        const auto focused = registry.get<ShapeStyleComponent>(search.root);
        passed &= expect(focused.fill == Renderer2DFill::eSolid &&
            focused.color0 == glm::vec4(1.0f, 1.0f, 1.0f, 0.04f) && focused.color1 == focused.color0 && focused.outlineWidth == 0.0f &&
            registry.any_of<LiquidGlassComponent>(search.root) &&
            registry.any_of<LiquidGlassComponent>(nav.root) &&
            !registry.any_of<LiquidGlassComponent>(nav.label) &&
            !registry.any_of<LiquidGlassComponent>(nav.caret),
            "search focus and navigation must retain uniform scene glass without gradients or button lenses");
        optics.liquidRefraction = 0.95f;
        optics.liquidBlur = 0.8f;
        optics.liquidTint = { 0.0f, 0.0f, 0.0f, 0.8f };
        ui_update_liquid_controls(controlsScene, true, optics);
        passed &= expect(registry.get<LiquidGlassComponent>(search.root).blur == 0.8f &&
            registry.get<LiquidGlassComponent>(nav.root).refraction == 0.95f &&
            registry.get<ShapeStyleComponent>(search.root).color1.a >= 0.8f &&
            registry.get<ShapeStyleComponent>(nav.root).color0.r < 0.1f,
            "all shared controls must respond to the live optical and tint preferences");
        UiInputState state {};
        const glm::vec2 point { 80.0f, 35.0f };
        ui_handle_pointer_button(controlsScene, fontAtlas, state,
            UiPointerButtonInput { true, point, PointerButton::eLeft, 0, UiInputAction::ePress });
        passed &= expect(input.focused && registry.get<StretchDynamicsComponent>(search.root).dragging,
            "search surface dragging must preserve text focus");
        ui_update_stretch_dynamics_drag(controlsScene, state, true, true, point + glm::vec2(30.0f, 0.0f));
        ui_handle_pointer_button(controlsScene, fontAtlas, state,
            UiPointerButtonInput { true, point + glm::vec2(30.0f, 0.0f), PointerButton::eLeft, 0, UiInputAction::eRelease });
        passed &= expect(registry.get<StretchDynamicsComponent>(search.root).settling && input.value == "Search query",
            "search drag release must spring back without changing the query");
        ui_update_liquid_controls(controlsScene, false, optics);
        passed &= expect(registry.get<TextInputVisualComponent>(search.root).focused.color0 == originalSearch.focused.color0 &&
            registry.get<ShapeStyleComponent>(search.root).color0 == originalSearch.focused.color0 && input.focused,
            "leaving glass must restore the focused search style and preserve focus");
    }

    {
        for (float factor : { 1.0f, 1.5f, 2.0f })
        {
            Renderer2DScene hoverScene;
            LayoutScale scale {};
            scale.factor = glm::vec2(factor);
            scale.logicalSize = { 400.0f, 200.0f };
            UiBuilder ui(hoverScene, scale);
            UiSliderOptions options {};
            options.hoveredTrackHeight = 12.0f;
            options.thumbSize = { 24.0f, 18.0f };
            options.maxValue = 100.0f;
            options.value = 25.0f;
            const auto handle = ui_create_slider(ui, ui.root(), UiAlignment::eTopLeft,
                { 20.0f, 50.0f }, { 300.0f, 6.0f }, 2, 0u, options);
            auto& registry = hoverScene.registry();
            hoverScene.build_render_plan(0.0, 0u);
            const auto authored = ui_entity_framebuffer_rect(registry, handle.knob);
            ui_update_liquid_controls(hoverScene, true, ui_liquid_glass_options());
            ui_update_slider_visual(hoverScene, fontAtlas, handle.root);
            hoverScene.build_render_plan(0.0, 0u);
            const auto before = ui_entity_framebuffer_rect(registry, handle.knob);
            passed &= expect(glm::length(authored - before) < 0.01f,
                "enabling glass must preserve the initial white thumb's geometry at every DPI");
            auto& slider = registry.get<SliderInputComponent>(handle.root);
            slider.hovered = true;
            slider.hoverExpansion = 1.0f;
            ui_update_slider_visual(hoverScene, fontAtlas, handle.root);
            hoverScene.build_render_plan(0.1, 0u);
            const auto after = ui_entity_framebuffer_rect(registry, handle.knob);
            passed &= expect(glm::length(before - after) < 0.01f,
                "hovering a top-aligned slider must not move or resize its white thumb at any DPI");
        }
    }

#if defined(_WIN32)
    {
        Renderer2DScene nativeScene;
        LayoutScale scale {};
        scale.logicalSize = { 400.0f, 200.0f };
        UiBuilder ui(nativeScene, scale);
        const auto parent = ui.root();
        const auto search = ui_create_search_field(ui, fontAtlas, parent, UiAlignment::eTopLeft,
            { 20.0f, 20.0f }, { 260.0f, 30.0f }, 2, 0u);
        auto& registry = nativeScene.registry();
        registry.emplace<UiLiquidControlSurface>(search.root, UiLiquidControlSurface { true, true });
        registry.get<ShapeStyleComponent>(parent) = make_solid_style("#202020FF", "#00000000", 0.0f, 1.0f);
        const auto originalSection = registry.get<ShapeComponent>(parent);
        auto section = originalSection;
        section.primitive = Renderer2DPrimitive::eSquircle;
        section.set_corner_radii(24.25f, 0.0f, 0.0f, 24.25f);
        section.squirclePower = 3.647f;
        registry.emplace<UiLiquidControlParentContour>(parent, UiLiquidControlParentContour { originalSection, section });
        const auto optics = ui_liquid_glass_options();
        ui_update_liquid_controls(nativeScene, true, optics);
        const auto* hole = registry.try_get<ShapeCutout2DComponent>(parent);
        passed &= expect(registry.any_of<SystemBackdropComponent>(search.root) &&
            !registry.get<LiquidGlassComponent>(search.root).sceneBackdrop &&
            registry.get<SystemBackdropComponent>(search.root).region.clipRawBackdropToShape &&
            hole && hole->source == search.root && !hole->affectBackdrop,
            "native Settings lenses must expose the compositor through a UI-only background cutout");
        const auto plan = nativeScene.build_render_plan(0.0, 0u);
        const auto contourRetained = [](const auto& batches) {
            return std::any_of(batches.begin(), batches.end(), [](const auto& batch) {
                return (batch.flags & eRenderer2DStyleMaskedLiquidShapeParameters) != 0u &&
                    (batch.flags & eRenderer2DStyleCornerRadii) != 0u &&
                    ((batch.packedData >> 20u) & 15u) == 9u && batch.color2.y == 3.647f;
            });
        };
        passed &= expect(contourRetained(plan.shapes) || contourRetained(plan.cachedShapes),
            "native control openings must preserve the section's outer squircle corners and square joining edge");
        registry.get<UiLiquidControlSurface>(search.root) = {};
        ui_update_liquid_controls(nativeScene, true, optics);
        passed &= expect(!registry.any_of<SystemBackdropComponent>(search.root) &&
            registry.get<LiquidGlassComponent>(search.root).sceneBackdrop &&
            registry.get<LiquidGlassComponent>(search.root).backend == LiquidGlassBackend::eEngineRenderer &&
            !registry.any_of<ShapeCutout2DComponent>(parent) &&
            registry.get<ShapeComponent>(parent).primitive == originalSection.primitive,
            "changing the source to interface content must remove the desktop lens and restore its background paint");
        ui_update_liquid_controls(nativeScene, false, optics);
        passed &= expect(!registry.any_of<ShapeCutout2DComponent>(parent) &&
            !registry.any_of<SystemBackdropComponent>(search.root) &&
            registry.get<ShapeComponent>(parent).primitive == originalSection.primitive,
            "leaving Liquid Glass must restore the Settings background and original contour without leaving an opening");
    }
#endif

    {
        Renderer2DScene capScene;
        LayoutScale scale {};
        scale.logicalSize = { 500.0f, 200.0f };
        UiBuilder ui(capScene, scale);
        auto& registry = capScene.registry();
        const auto parent = ui.root();
        // Native unions suppress their UI cap paint; a portable child lens
        // must still include that cap when it samples the interface.
        registry.get<ShapeStyleComponent>(parent).opacity = 1.0f;
        auto& nativeUnion = registry.emplace<SystemBackdropUnionComponent>(parent);
        nativeUnion.participants[0] = parent;
        nativeUnion.paintCapGradient = true;
        nativeUnion.capGradient = { 1.0f, 0.0f, 0.5f, 1.0f };
        nativeUnion.capColour = { 0.2f, 0.6f, 1.0f };
        const auto nav = ui_create_nav_cluster(ui, fontAtlas, parent, UiAlignment::eTopLeft,
            { 20.0f, 20.0f }, { 90.0f, 32.0f }, 2, 0u);
        registry.emplace<UiLiquidControlSurface>(nav.root);
        ui_update_liquid_controls(capScene, true, ui_liquid_glass_options());
        const auto plan = capScene.build_render_plan(0.0, 0u);
        bool hasNativeCap = false;
        const auto checkCap = [&](const auto& batches) {
            for (const auto& batch : batches)
                if (batch.entity == nav.root)
                    hasNativeCap = (batch.flags & eRenderer2DStyleLiquidGlass) != 0u &&
                        (batch.packedData & (1u << 29u)) != 0u &&
                        batch.color0 == ui_entity_visual_framebuffer_rect(registry, parent) &&
                        batch.color1 == glm::vec4(1.0f, 0.0f, 0.5f, 1.0f) &&
                        batch.backdropCapColour == (51u | (153u << 8u) | (255u << 16u));
        };
        checkCap(plan.blurs);
        checkCap(plan.cachedBlurs);
        passed &= expect(hasNativeCap && !registry.any_of<SystemBackdropComponent>(nav.root),
            "a shared scene lens must sample the island cap without opening a desktop backdrop hole");
    }

    {
        Renderer2DScene thumbScene;
        LayoutScale thumbScale {};
        thumbScale.logicalSize = { 400.0f, 200.0f };
        UiBuilder thumbUi(thumbScene, thumbScale);
        UiSliderOptions hidden {};
        hidden.thumbColor = "#00000000";
        hidden.thumbOutlineColor = "#00000000";
        hidden.thumbOutlineWidth = 0.0f;
        hidden.renderThumbShadow = false;
        auto media = ui_create_slider(thumbUi, thumbUi.root(), UiAlignment::eTopLeft,
            { 20.0f, 30.0f }, { 300.0f, 7.0f }, 2, 0u, hidden);
        auto& slider = thumbScene.registry().get<SliderInputComponent>(media.root);
        slider.dragging = true;
        ui_update_liquid_controls(thumbScene, true, ui_liquid_glass_options());
        passed &= expect(!slider.liquidThumbEnabled &&
            !thumbScene.registry().any_of<LiquidGlassComponent>(slider.thumbEntity) &&
            thumbScene.registry().get<ShapeStyleComponent>(slider.thumbEntity).color0.a == 0.0f,
            "media seek sliders must retain their intentionally hidden thumbs during dragging");
    }
    {
        Renderer2DScene switchScene;
        LayoutScale switchScale {};
        switchScale.logicalSize = { 400.0f, 200.0f };
        UiBuilder switchUi(switchScene, switchScale);
        int changes = 0;
        UiSwitchOptions options {};
        options.onChanged = [&changes](bool, const PointerInputEvent&) { ++changes; };
        const auto switchRoot = switchUi.root();
        const auto toggle = ui_create_switch(switchUi, switchRoot, UiAlignment::eTopLeft,
            { 20.0f, 30.0f }, { 64.0f, 28.0f }, 2, 0u, options);
        auto& switchRegistry = switchScene.registry();
        // Settings switches live inside a scrolling page. The viewport must
        // not prevent its child control from taking a captured knob drag.
        switchRegistry.emplace<ScrollInputComponent>(switchRoot);
        auto& control = switchRegistry.get<UiSwitchControlComponent>(toggle.root);
        const auto& switchShadow = switchRegistry.get<ShadowComponent>(toggle.knob);
        passed &= expect(switchShadow.outsideOnly && switchShadow.spread == 0.0f &&
            switchShadow.blurRadius == 8.0f && switchShadow.opacity <= 0.12f,
            "switch thumbs must share the slider thumb outer shadow");
        UiInputState gestureState {};
        const auto optics = ui_liquid_glass_options();
        ui_update_liquid_controls(switchScene, true, optics);
        switchScene.build_render_plan(0.0, 0u);
        for (bool checked : { false, true })
        {
            control.checked = checked;
            ui_update_switch_visual(switchScene, toggle.root);
            switchScene.build_render_plan(0.0, 0u);
            const auto before = ui_entity_visual_framebuffer_rect(switchRegistry, toggle.knob);
            const glm::vec2 point { before.x + before.z * 0.5f, before.y + before.w * 0.5f };
            ui_update_input_hover(switchScene, fontAtlas, gestureState, true, point);
            ui_update_switch_animations(switchScene, 0.1);
            switchScene.build_render_plan(0.1, 0u);
            const auto hovered = ui_entity_visual_framebuffer_rect(switchRegistry, toggle.knob);
            ui_update_input_hover(switchScene, fontAtlas, gestureState, true, { 390.0f, 190.0f });
            switchScene.build_render_plan(0.2, 0u);
            const auto after = ui_entity_visual_framebuffer_rect(switchRegistry, toggle.knob);
            passed &= expect(glm::length(before - hovered) < 0.01f && glm::length(before - after) < 0.01f,
                "entering and leaving either switch state must keep the idle knob in its authored position");
        }
        control.checked = false;
        ui_update_switch_visual(switchScene, toggle.root);
        ui_update_switch_animations(switchScene, 1.0);
        auto drag = [&](float displacement, double time) {
            switchScene.build_render_plan(time, 0u);
            const auto rect = ui_entity_framebuffer_rect(switchRegistry, toggle.knob);
            const glm::vec2 point { rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f };
            UiPointerButtonInput press { true, point, PointerButton::eLeft, 0, UiInputAction::ePress };
            ui_handle_pointer_button(switchScene, fontAtlas, gestureState, press);
            // Reproduce the Settings tick order and a gesture prevented by
            // its enclosing hierarchy. Button capture must still own the drag.
            gestureState.activeStretchDynamics = entt::null;
            switchRegistry.get<StretchDynamicsComponent>(toggle.root).dragging = false;
            ui_update_liquid_controls(switchScene, true, optics);
            ui_update_switch_animations(switchScene, time + 0.1);
            ui_update_stretch_dynamics_settling(switchScene, gestureState, time + 0.1);
            ui_update_switch_drag(switchScene, gestureState, true, true, point);
            ui_update_switch_animations(switchScene, time + 0.2);
            const glm::vec2 expandedKnobSize =
                switchRegistry.get<ShapeComponent>(toggle.knob).size;
            switchScene.build_render_plan(time + 0.2, 0u);
            const glm::vec4 expandedKnobRect =
                ui_entity_visual_framebuffer_rect(switchRegistry, toggle.knob);
            passed &= expect(control.dragging && control.liquidThumbScale > 1.0f &&
                switchRegistry.any_of<LiquidGlassComponent>(toggle.knob) &&
                !switchRegistry.any_of<LiquidGlassComponent>(toggle.root),
                "a grabbed switch knob inside a scroll view must expand with glass while its track stays normal");
            passed &= expect(expandedKnobSize.y >= control.knobBaseSize.y * 1.4f &&
                expandedKnobRect.w >= control.knobBaseSize.y * 1.4f,
                "switch expansion must resize the actual liquid thumb geometry at least 40% taller");
            passed &= expect(std::abs(switchRegistry.get<ShapeComponent>(toggle.knob).cornerRadius * 2.0f -
                expandedKnobRect.w) < 0.01f,
                "an expanded switch thumb must keep a full pill radius rather than rounded rectangle corners");
            passed &= expect(switchRegistry.get<ScrollInputComponent>(switchRoot).offset == 0.0f,
                "dragging a switch must not start a competing viewport scroll");
            const glm::vec2 end = point + glm::vec2(displacement, 0.0f);
            ui_update_switch_drag(switchScene, gestureState, true, true, end);
            ui_update_stretch_dynamics_drag(switchScene, gestureState, true, true, end);
            UiPointerButtonInput release { true, end, PointerButton::eLeft, 0, UiInputAction::eRelease };
            ui_handle_pointer_button(switchScene, fontAtlas, gestureState, release);
            ui_update_liquid_controls(switchScene, true, optics);
            for (int frame = 1; frame <= 120; ++frame)
                ui_update_switch_animations(switchScene, time + 0.1 + frame / 120.0);
            passed &= expect(!control.dragging && control.liquidThumbScale == 1.0f &&
                !switchRegistry.any_of<LiquidGlassComponent>(toggle.knob) &&
                switchRegistry.get<ShapeStyleComponent>(toggle.knob).color0 == glm::vec4(1.0f),
                "a released switch knob must return to its normal white paint and size");
        };
        drag(80.0f, 1.0);
        passed &= expect(control.checked && changes == 1,
            "dragging right beyond the track must commit On exactly once");
        drag(-80.0f, 3.0);
        passed &= expect(!control.checked && changes == 2,
            "dragging left beyond the track must commit Off exactly once");
        switchScene.build_render_plan(5.0, 0u);
        const auto track = ui_entity_framebuffer_rect(switchRegistry, toggle.root);
        const glm::vec2 point { track.x + track.z * 0.5f, track.y + track.w * 0.5f };
        UiPointerButtonInput press { true, point, PointerButton::eLeft, 0, UiInputAction::ePress };
        UiPointerButtonInput release = press;
        release.action = UiInputAction::eRelease;
        ui_handle_pointer_button(switchScene, fontAtlas, gestureState, press);
        ui_handle_pointer_button(switchScene, fontAtlas, gestureState, release);
        passed &= expect(control.checked && changes == 3,
            "switch clicks must retain their existing single-toggle behaviour");
    }

    {
        Renderer2DScene maskScene;
        ShapeStyleComponent cap {};
        cap.set_gradient({ 0.0f, 0.0f, 0.0f, 0.98f }, { 0.0f, 0.0f, 0.0f, 0.015f },
            { 0.0f, 0.0f }, { 0.0f, 1.0f });
        cap.outlineWidth = 0.8f;
        cap.outlineColor = { 1.0f, 1.0f, 1.0f, 0.12f };
        const auto panel = maskScene.create_shape({ 20.0f, 20.0f }, { 500.0f, 260.0f },
            cap, Renderer2DPrimitive::eNotchedSquircle);
        const auto cluster = maskScene.create_shape({ 40.0f, 40.0f }, { 62.0f, 32.0f });
        auto& maskRegistry = maskScene.registry();
        maskRegistry.get<ShapeComponent>(cluster).set_corner_radius(16.0f);
        auto& geometry = maskRegistry.get<ShapeComponent>(panel);
        geometry.squircleAmount = 0.731f;
        geometry.squirclePower = 3.647f;
        geometry.notchAmount = 0.953f;
        geometry.notchDepth = 78.125f;
        maskRegistry.emplace<LiquidGlassComponent>(panel, LiquidGlassComponent { GlassMaterial::eLiquid });
        maskRegistry.emplace<ShapeCutout2DComponent>(panel, ShapeCutout2DComponent { cluster, false });
        const auto plan = maskScene.build_render_plan(0.0, 0u);
        bool preserved = false;
        const auto check = [&](const auto& batches) {
            for (const auto& batch : batches)
                if (batch.entity == panel)
                    preserved = (batch.flags & eRenderer2DStyleMaskedLiquidShapeParameters) != 0u &&
                        batch.color2 == glm::vec4(0.731f, 3.647f, 0.953f, 78.125f) &&
                        batch.uvRect == glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
        };
        check(plan.shapes);
        check(plan.cachedShapes);
        passed &= expect(preserved,
            "exposing the nav lens must preserve full-precision notch contours and cap gradient coordinates");
    }

    return passed ? 0 : 1;
}

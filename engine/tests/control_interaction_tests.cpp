// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/engine.h>
#include <vibranceUI/ui/liquid_controls.h>
#include <vibranceUI/ui/surfaces.h>
#include <vibranceUI/ui/surface_paint.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <string_view>

namespace
{


    template <typename Tag, typename Tag::type Member>
    struct SceneMemberAccess
    {
        friend typename Tag::type scene_member(Tag) { return Member; }
    };
    struct SceneDamagePending
    {
        using type = bool (Renderer2DScene::*)(entt::entity, double) const;
        friend type scene_member(SceneDamagePending);
    };
    struct SceneCommitBounds
    {
        using type = void (Renderer2DScene::*)(
            std::vector<std::pair<entt::entity, glm::uvec4>>);
        friend type scene_member(SceneCommitBounds);
    };
    template struct SceneMemberAccess<SceneDamagePending, &Renderer2DScene::damage_pending>;
    template struct SceneMemberAccess<SceneCommitBounds, &Renderer2DScene::commit_presented_bounds>;

    bool scene_damage_pending(const Renderer2DScene& scene, entt::entity entity, double time)
    {
        return (scene.*scene_member(SceneDamagePending {}))(entity, time);
    }
    void commit_scene_bounds(Renderer2DScene& scene,
        std::vector<std::pair<entt::entity, glm::uvec4>> bounds)
    {
        (scene.*scene_member(SceneCommitBounds {}))(std::move(bounds));
    }

    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "control interaction test failed: " << message << '\n';
        }
        return condition;
    }

    bool lost_button_releases_recover_capture()
    {
        bool passed = true;
        Renderer2DScene scene;
        auto& registry = scene.registry();
        const Renderer2DFontAtlas atlas;
        UiInputState state;
        const auto entity = scene.create_shape({ 10.0f, 10.0f }, { 100.0f, 40.0f },
            make_solid_style({ 0.1f, 0.2f, 0.3f, 1.0f }));
        int leftReleases = 0;
        int rightReleases = 0;
        int clicks = 0;
        ButtonInputComponent button;
        button.onRelease = [&](const PointerInputEvent& event) {
            (event.button == PointerButton::eLeft ? leftReleases : rightReleases)++;
        };
        button.onClick = [&](const PointerInputEvent&) { ++clicks; };
        registry.emplace<ButtonInputComponent>(entity, std::move(button));
        ButtonVisualComponent visual;
        visual.idle = registry.get<ShapeStyleComponent>(entity);
        visual.hovered = visual.idle;
        visual.pressed = make_solid_style({ 0.8f, 0.2f, 0.3f, 1.0f });
        registry.emplace<ButtonVisualComponent>(entity, visual);
        const glm::vec2 point { 30.0f, 25.0f };
        const auto input = [&](PointerButton selected, UiInputAction action) {
            ui_handle_pointer_button(scene, atlas, state,
                UiPointerButtonInput { true, point, selected, 0, action });
        };

        input(PointerButton::eLeft, UiInputAction::ePress);
        passed &= expect(state.pressedInputEntity == entity &&
            state.pointerInputCapture == entity &&
            !ui_recover_pointer_button_release(scene, state, true, false, point),
            "a physically held ordinary button must keep capture");
        passed &= expect(ui_recover_pointer_button_release(scene, state, false, false, point) &&
            state.pressedInputEntity == entt::null && state.pointerInputCapture == entt::null &&
            !registry.get<ButtonInputComponent>(entity).leftPressed &&
            registry.get<ShapeStyleComponent>(entity).color0 == visual.idle.color0 &&
            leftReleases == 1 && clicks == 0,
            "a lost ordinary button release must cancel capture and pressed paint without clicking");
        passed &= expect(!ui_recover_pointer_button_release(scene, state, false, false, point),
            "a recovered button release must finish exactly once");
        input(PointerButton::eLeft, UiInputAction::eRelease);
        passed &= expect(leftReleases == 1 && clicks == 0,
            "a delayed native release after recovery must not release or click twice");
        input(PointerButton::eLeft, UiInputAction::ePress);
        input(PointerButton::eLeft, UiInputAction::eRelease);
        passed &= expect(leftReleases == 2 && clicks == 1,
            "an ordinary button must accept a real click immediately after release recovery");

        input(PointerButton::eLeft, UiInputAction::ePress);
        input(PointerButton::eRight, UiInputAction::ePress);
        passed &= expect(ui_recover_pointer_button_release(scene, state, true, false, point) &&
            rightReleases == 1 && state.pressedInputEntity == entity &&
            state.pointerInputCapture == entity &&
            registry.get<ButtonInputComponent>(entity).leftPressed,
            "recovering a right release must preserve a physically held left button");
        input(PointerButton::eRight, UiInputAction::eRelease);
        passed &= expect(rightReleases == 1 && state.pressedInputEntity == entity &&
            state.pointerInputCapture == entity,
            "a delayed right release must preserve the remaining left capture");
        ui_recover_pointer_button_release(scene, state, false, false, point);
        passed &= expect(leftReleases == 3 && clicks == 1 &&
            state.pressedInputEntity == entt::null && state.pointerInputCapture == entt::null,
            "the remaining left release must finish independently without a click");

        input(PointerButton::eLeft, UiInputAction::ePress);
        registry.get<ButtonInputComponent>(entity).onRelease =
            [&](const PointerInputEvent&) { scene.destroy_entity_tree(entity); };
        passed &= expect(ui_recover_pointer_button_release(scene, state, false, false, point) &&
            !registry.valid(entity) && state.pressedInputEntity == entt::null &&
            state.pointerInputCapture == entt::null,
            "release recovery must tolerate a callback removing its captured button");

        const auto removed = scene.create_shape({}, { 40.0f, 40.0f }, visual.idle);
        registry.emplace<ButtonInputComponent>(removed);
        state.pressedInputEntity = removed;
        state.pointerInputCapture = removed;
        scene.destroy_entity_tree(removed);
        passed &= expect(ui_recover_pointer_button_release(scene, state, false, false, point) &&
            state.pressedInputEntity == entt::null && state.pointerInputCapture == entt::null,
            "a removed captured button must not leave stale pointer ownership");

        const auto textInput = scene.create_shape({ 10.0f, 10.0f }, { 100.0f, 40.0f }, visual.idle);
        registry.emplace<TextInputComponent>(textInput);
        input(PointerButton::eLeft, UiInputAction::ePress);
        passed &= expect(state.pressedInputEntity == entt::null &&
            state.pointerInputCapture == textInput && state.focusedTextInput == textInput &&
            !ui_recover_pointer_button_release(scene, state, true, false, point),
            "a held text input must retain left capture without a button press owner");
        passed &= expect(ui_recover_pointer_button_release(scene, state, false, false, point) &&
            state.pointerInputCapture == entt::null && state.focusedTextInput == textInput,
            "a lost text input release must clear capture while preserving keyboard focus");
        const auto next = scene.create_shape({ 10.0f, 10.0f }, { 100.0f, 40.0f }, visual.idle);
        registry.get<RenderLayer2DComponent>(next).order = 100;
        ButtonInputComponent nextButton;
        nextButton.onClick = [&](const PointerInputEvent&) { ++clicks; };
        registry.emplace<ButtonInputComponent>(next, std::move(nextButton));
        input(PointerButton::eLeft, UiInputAction::ePress);
        input(PointerButton::eLeft, UiInputAction::eRelease);
        passed &= expect(clicks == 2 && state.pointerInputCapture == entt::null,
            "the next ordinary click must work immediately after a text input loses its release");
        return passed;
    }
    bool dropdowns_dismiss_globally()
    {
        bool passed = true;
        Renderer2DScene scene;
        auto& registry = scene.registry();
        Renderer2DFontAtlas atlas;
        UiInputState state;
        LayoutScale scale;
        scale.logicalSize = { 600.0f, 400.0f };
        UiBuilder ui(scene, scale);
        const auto parent = ui.root();
        unsigned selections = 0u, clicks = 0u;
        UiDropdownOptions options;
        options.menuWidth = 180.0f;
        options.onChanged = [&](int, std::string_view, const PointerInputEvent&) { ++selections; };
        const auto first = ui_create_dropdown(ui, atlas, parent, UiAlignment::eTopLeft,
            { 20.0f, 20.0f }, { 160.0f, 36.0f }, { "First", "Second" }, 4, 0u, options);
        const auto second = ui_create_dropdown(ui, atlas, parent, UiAlignment::eTopLeft,
            { 260.0f, 20.0f }, { 160.0f, 36.0f }, { "Third", "Fourth" }, 4, 1u, options);
        UiButtonOptions buttonOptions;
        buttonOptions.onClick = [&](const PointerInputEvent&) { ++clicks; };
        const auto other = ui_create_aligned_text_button(ui, atlas, parent, UiAlignment::eTopLeft,
            { 260.0f, 200.0f }, "Other control", { 160.0f, 36.0f }, 4, 2u, buttonOptions);
        const auto centre = [&](entt::entity entity) {
            scene.build_render_plan(0.0, 0u);
            const auto rect = ui_entity_framebuffer_rect(registry, entity);
            return glm::vec2(rect.x + rect.z * 0.5f, rect.y + rect.w * 0.5f);
        };
        const auto click = [&](glm::vec2 point, PointerButton button = PointerButton::eLeft) {
            scene.build_render_plan(0.0, 0u);
            UiPointerButtonInput input { true, point, button, 0, UiInputAction::ePress };
            ui_handle_pointer_button(scene, atlas, state, input);
            input.action = UiInputAction::eRelease;
            ui_handle_pointer_button(scene, atlas, state, input);
        };
        const auto open = [&](entt::entity entity) {
            return registry.get<UiDropdownControlComponent>(entity).open;
        };
        click(centre(first.root));
        passed &= expect(open(first.root) && registry.get<OutsidePointerInputComponent>(first.root).enabled,
            "Opening any engine dropdown must subscribe it to outside presses immediately");
        click({ 580.0f, 380.0f });
        passed &= expect(!open(first.root) && !registry.get<OutsidePointerInputComponent>(first.root).enabled,
            "A rapid outside press and release must close a dropdown without a rendered frame");
        click(centre(first.root));
        click(centre(other.root));
        passed &= expect(!open(first.root) && clicks == 1u,
            "An outside press must dismiss the dropdown and activate another control in the same click");
        click(centre(first.root));
        click(centre(second.root));
        passed &= expect(!open(first.root) && open(second.root),
            "Clicking another dropdown must switch menus in one click");
        click(centre(second.root));
        passed &= expect(!open(second.root), "Clicking an open dropdown button must close its own menu");
        click(centre(first.root));
        const auto disabled = registry.get<UiDropdownControlComponent>(first.root).itemRows[1];
        registry.get<ButtonInputComponent>(disabled).enabled = false;
        click(centre(disabled));
        passed &= expect(open(first.root) && selections == 0u,
            "A disabled dropdown row is inside its menu and must not select or dismiss it");
        const auto selected = registry.get<UiDropdownControlComponent>(first.root).itemRows[0];
        click(centre(selected));
        passed &= expect(!open(first.root) && selections == 1u,
            "A menu row must still select normally and end its outside subscription");
        click(centre(first.root));
        click({ 580.0f, 380.0f }, PointerButton::eRight);
        passed &= expect(!open(first.root), "An outside right click must dismiss a dropdown");
        ui_set_dropdown_open(scene, first.root, true);
        ui_handle_window_focus(scene, state, false);
        passed &= expect(open(first.root), "An initially unfocused fixture must retain its dropdown");
        ui_handle_window_focus(scene, state, true);
        ui_handle_window_focus(scene, state, false);
        passed &= expect(!open(first.root), "Losing window focus must dismiss every open dropdown");
        const auto generation = scene.frame_generation();
        for (unsigned sample = 0u; sample < 100u; ++sample) ui_handle_window_focus(scene, state, false);
        passed &= expect(scene.frame_generation() == generation,
            "Unchanged focus must not dirty retained UI or repeat menu dismissal");

        Renderer2DScene mutationScene;
        auto& mutations = mutationScene.registry();
        const auto one = mutationScene.create_shape({}, { 10.0f, 10.0f }, make_solid_style("#FFFFFFFF"));
        const auto two = mutationScene.create_shape({}, { 10.0f, 10.0f }, make_solid_style("#FFFFFFFF"));
        unsigned callbacks = 0u;
        mutations.emplace<OutsidePointerInputComponent>(one, OutsidePointerInputComponent {
            true, [&](const PointerInputEvent&) { ++callbacks; mutationScene.destroy_entity(two); }, {} });
        mutations.emplace<OutsidePointerInputComponent>(two, OutsidePointerInputComponent {
            true, [&](const PointerInputEvent&) { ++callbacks; mutationScene.destroy_entity(one); }, {} });
        ui_dispatch_outside_pointer_press(mutationScene, entt::null, {});
        passed &= expect(callbacks == 1u, "Outside callbacks may remove another subscribed control safely");
        return passed;
    }
}

int main()
{
    bool passed = true;
    {
        for (const bool desktop : { false, true })
        {
            Renderer2DScene scene;
            UiBuilder ui(scene);
            UiSliderOptions options;
            options.thumbColor = "#EE4466FF";
            options.thumbOutlineColor = "#FFFFFFFF";
            options.thumbOutlineWidth = 2.0f;
            const auto handle = ui_create_slider(ui, ui.root(), UiAlignment::eTopLeft, {}, { 300.0f, 6.0f }, 1, 0u, options);
            auto& registry = scene.registry();
            auto& slider = registry.get<SliderInputComponent>(handle.root);
            slider.dragging = true;
            slider.liquidInteraction.formation = 1.0f;
            auto optics = ui_liquid_glass_options();
            optics.liquidBlur = 1.0f;
            for (const float alpha : { 0.0f, 0.7f, 1.0f })
            {
                optics.liquidTint = { 0.1f, 0.2f, 0.9f, alpha };
                ui_update_liquid_controls(scene, true, optics, desktop);
                const auto& glass = registry.get<LiquidGlassComponent>(slider.thumbEntity);
                const auto& paint = registry.get<ShapeStyleComponent>(slider.thumbEntity);
                passed &= expect(glass.blur == 0.0f && glass.preserveBackdropColour &&
                    paint.color0.a == 0.0f && paint.color1.a == 0.0f &&
                    paint.outlineWidth == 0.0f,
                    "Slider thumbs must use clear glass independently of panel tint, diffusion and resting colour");
                if (const auto* backdrop = registry.try_get<SystemBackdropComponent>(slider.thumbEntity))
                    passed &= expect(backdrop->region.liquidTint.alpha == 0.0f && backdrop->region.liquidBlur == 0.0f,
                        "Native slider thumbs must also ignore the panel's tint and diffusion");
            }
            slider.liquidThumbScale = 1.25f;
            slider.liquidDrag.stretch = 1.0f;
            for (const float value : { 0.0f, 1.0f })
            {
                slider.value = slider.visualValue = value;
                ui_update_slider_geometry(scene, handle.root);
                const auto plan = scene.build_render_plan(1.0, 0u);
#if !defined(__APPLE__)
                const auto neutralLens = [&](const auto& batches) {
                    return std::any_of(batches.begin(), batches.end(), [&](const auto& batch) {
                        return batch.entity == slider.thumbEntity &&
                            (batch.flags & eRenderer2DStyleLiquidPreserveBackdropColour) != 0u;
                    });
                };
                passed &= expect((neutralLens(plan.blurs) || neutralLens(plan.cachedBlurs)) &&
                    !registry.any_of<SystemBackdropComponent>(slider.thumbEntity),
                    "A clear slider lens must preserve sampled interface colours, including when its panel uses a desktop backdrop");
#endif
                const auto track = ui_entity_framebuffer_rect(registry, handle.root);
                const auto thumb = ui_entity_framebuffer_rect(registry, slider.thumbEntity);
                const auto fill = ui_entity_framebuffer_rect(registry, slider.fillEntity);
                passed &= expect(thumb.x >= track.x - 0.01f && thumb.x + thumb.z <= track.x + track.z + 0.01f &&
                    std::abs(fill.x + fill.z - thumb.x - thumb.z * 0.5f) < 0.01f,
                    "A clear, stretched thumb must stay inside both endpoints and retain the inset fill beneath its centre");
            }
            auto resting = make_solid_style("#22CC88FF", "#FFFFFFFF", 2.0f);
            ui_set_liquid_thumb_resting_style(scene, slider.thumbEntity, resting);
            ui_update_liquid_controls(scene, true, optics, desktop);
            passed &= expect(registry.get<ShapeStyleComponent>(slider.thumbEntity).color0.a == 0.0f,
                "Updating a colour-slider swatch during a drag must not paint over its clear lens");
            slider.dragging = false;
            slider.liquidInteraction = {};
            ui_update_liquid_controls(scene, true, optics, desktop);
            const auto& paint = registry.get<ShapeStyleComponent>(slider.thumbEntity);
            passed &= expect(paint.color0 == resting.color0 && paint.outlineWidth == resting.outlineWidth,
                "A colour-slider thumb must restore the latest selected colour and outline after release");
        }
        float reference = 0.0f;
        for (const unsigned rate : { 60u, 120u, 240u })
        {
            UiLiquidDragState drag;
            ui_update_liquid_drag(drag, true, 0.0f, 1.0 / rate);
            for (unsigned frame = 1u; frame <= rate / 5u; ++frame)
                ui_update_liquid_drag(drag, true, 36.0f * frame / rate, 1.0 / rate);
            if (rate == 60u) reference = drag.stretch;
            passed &= expect(drag.stretch > 0.8f && drag.stretch < 1.0f && std::abs(drag.stretch - reference) < 0.0001f,
                "Velocity-sensitive thumb stretch must remain bounded and consistent across display rates");
            ui_update_liquid_drag(drag, false, drag.position, 1.0 / rate);
            passed &= expect(drag.stretch > 0.70f,
                "Releasing a moving thumb must retain its deformation through the initial return frame");
            for (unsigned frame = 0u; frame < rate; ++frame)
                ui_update_liquid_drag(drag, false, drag.position, 1.0 / rate);
            passed &= expect(drag.stretch == 0.0f && drag.velocity == 0.0f,
                "A settled thumb must stop its directional stretch completely");
        }
    }
    {
        const auto dark = make_solid_style("rgba(255, 255, 255, 0.13)");
        const auto light = make_solid_style("rgba(255, 255, 255, 0.36)");
        passed &= expect(!ui_liquid_control_light_surface(dark) && ui_liquid_control_light_surface(light),
            "Faint white overlays on dark panels must not select the light control finish");
        passed &= expect(!ui_liquid_control_light_surface(light, UiLiquidControlAppearance::eDark) &&
            ui_liquid_control_light_surface(dark, UiLiquidControlAppearance::eLight),
            "An explicit control appearance must take priority over the overlay's RGB and opacity");
        Renderer2DScene scene;
        UiBuilder ui(scene);
        const auto button = scene.create_shape({}, { 14.0f, 14.0f }, make_solid_style("#FC6F5FFF"),
            Renderer2DPrimitive::eEllipse);
        scene.registry().emplace<ButtonInputComponent>(button);
        ui_apply_liquid_button(ui, button);
        const auto authored = scene.registry().get<ShapeStyleComponent>(button);
        for (const float blur : { 0.0f, 0.15f, 1.0f })
        {
            auto options = ui_liquid_glass_options();
            options.liquidBlur = blur;
            ui_update_liquid_controls(scene, true, options);
            passed &= expect(scene.registry().get<LiquidGlassComponent>(button).blur == 0.0f &&
                scene.registry().get<ShapeStyleComponent>(button).color0 == authored.color0 &&
                scene.registry().get<ShapeStyleComponent>(button).color1 == authored.color1,
                "Coloured glass controls must keep their face sharp at every panel blur setting");
            const auto generation = scene.frame_generation();
            ui_update_liquid_controls(scene, true, options);
            passed &= expect(scene.frame_generation() == generation,
                "A coloured glass control must not invalidate its frame repeatedly to override panel blur");
        }
    }
    {
        UiLiquidInteractionState quick;
        for (unsigned frame = 0u; frame < 6u; ++frame)
            ui_update_liquid_interaction(quick, true, false, 0.01);
        for (unsigned frame = 0u; frame < 30u; ++frame)
            ui_update_liquid_interaction(quick, false, false, 0.01);
        passed &= expect(quick.formation == 0.0f && quick.velocity == 0.0f,
            "A quick mouse click must not form a held glass lens");
        float reference = 0.0f;
        for (const unsigned rate : { 60u, 120u, 240u })
        {
            UiLiquidInteractionState held;
            for (unsigned frame = 0u; frame < rate / 2u; ++frame)
                ui_update_liquid_interaction(held, true, true, 1.0 / rate);
            if (rate == 60u) reference = held.formation;
            passed &= expect(held.formation > 0.99f && std::abs(held.formation - reference) < 0.0001f,
                "A held or dragged lens must form consistently at different display rates");
            ui_update_liquid_interaction(held, false, false, 1.0 / rate);
            passed &= expect(held.formation > 0.8f,
                "Releasing a lens must retain its material through the spring return");
            for (unsigned frame = 0u; frame < rate; ++frame)
                ui_update_liquid_interaction(held, false, false, 1.0 / rate);
            passed &= expect(held.formation == 0.0f && held.velocity == 0.0f,
                "A settled lens must stop its animation completely");
        }
        Renderer2DScene scene;
        UiBuilder ui(scene);
        const auto button = scene.create_shape({ 20.0f, 30.0f }, { 80.0f, 32.0f }, make_solid_style("#3399FFFF"));
        scene.registry().emplace<ButtonInputComponent>(button);
        scene.registry().emplace<ButtonVisualComponent>(button,
            ui_fixed_colour_button_visual(scene.registry().get<ShapeStyleComponent>(button)));
        ui_apply_liquid_button(ui, button);
        const auto glassPaint = scene.registry().get<ShapeStyleComponent>(button);
        scene.registry().get<ButtonInputComponent>(button).hovered = true;
        ui_update_button_visual(scene, button);
        const auto hoveredGlassPaint = scene.registry().get<ShapeStyleComponent>(button);
        passed &= expect(hoveredGlassPaint.fill == glassPaint.fill &&
            hoveredGlassPaint.color0 == glassPaint.color0 && hoveredGlassPaint.color1 == glassPaint.color1,
            "A glass button state update must retain its optical face paint instead of restoring a flat fill");
        scene.build_render_plan(1.0, 0u);
        UiInputState input;
        const Renderer2DFontAtlas atlas;
        const auto rect = ui_entity_base_framebuffer_rect(scene.registry(), button);
        const glm::vec2 point = glm::vec2(rect) + glm::vec2(rect.z, rect.w) * 0.5f;
        const auto send = [&](UiInputAction action, glm::vec2 position) {
            ui_handle_pointer_button(scene, atlas, input,
                UiPointerButtonInput { true, position, PointerButton::eLeft, 0, action, {} });
        };
        passed &= expect(scene.registry().get<InteractiveVisual2DComponent>(button).scale == glm::vec2(1.0f),
            "An idle or hovered glass button must retain its authored size");
        send(UiInputAction::ePress, point);
        ui_update_stretch_dynamics_settling(scene, input, 1.0);
        for (unsigned frame = 1u; frame <= 60u; ++frame)
            ui_update_stretch_dynamics_settling(scene, input, 1.0 + frame / 120.0);
        passed &= expect(scene.registry().get<InteractiveVisual2DComponent>(button).scale.x > 1.005f &&
            scene.registry().all_of<LiquidGlassContact2DComponent>(button),
            "A pressed glass button must lift and illuminate its contact point");
        send(UiInputAction::eRelease, point);
        for (unsigned frame = 1u; frame <= 120u; ++frame)
            ui_update_stretch_dynamics_settling(scene, input, 1.5 + frame / 120.0);
        passed &= expect(scene.registry().get<InteractiveVisual2DComponent>(button).scale == glm::vec2(1.0f) &&
            !scene.registry().any_of<LiquidGlassContact2DComponent>(button),
            "A released glass button must return to rest and remove its contact light");
        const auto generation = scene.frame_generation();
        ui_update_stretch_dynamics_settling(scene, input, 3.0);
        passed &= expect(scene.frame_generation() == generation,
            "A settled glass button must retain its frame without repeated invalidation");
    }
    {
        Renderer2DScene scene;
        UiBuilder ui(scene);
        auto& registry = scene.registry();
        const auto gear = scene.create_shape({ 20.0f, 20.0f }, { 24.0f, 24.0f },
            make_solid_style("#00000000"), Renderer2DPrimitive::eEllipse);
        registry.get<Transform2DComponent>(gear).origin = {};
        registry.emplace<UiLiquidControlSurface>(gear);
        unsigned clicks = 0u;
        registry.emplace<ButtonInputComponent>(gear).onClick = [&](const PointerInputEvent&) { ++clicks; };
        ButtonVisualComponent authored;
        authored.idle = authored.hovered = make_solid_style("#00000000");
        authored.pressed = make_solid_style("#303238FF");
        authored.idle.opacity = authored.hovered.opacity = authored.pressed.opacity = 0.0f;
        registry.emplace<ButtonVisualComponent>(gear, authored);
        ui_update_liquid_controls(scene, true, ui_liquid_glass_options());


        for (float opacity : { 0.25f, 0.65f, 1.0f })
        {
            authored.idle.opacity = authored.hovered.opacity = authored.pressed.opacity = opacity;
            ui_set_liquid_button_visual(scene, gear, authored);
            ui_update_liquid_controls(scene, true, ui_liquid_glass_options());
        }
        UiInputState state;
        const Renderer2DFontAtlas atlas;
        const glm::vec2 point { 32.0f, 32.0f };
        ui_handle_pointer_button(scene, atlas, state,
            UiPointerButtonInput { true, point, PointerButton::eLeft, 0, UiInputAction::ePress });
        ui_update_stretch_dynamics_drag(scene, state, true, true, point + glm::vec2(12.0f, 0.0f));
        ui_update_stretch_dynamics_settling(scene, state, 1.0);
        ui_update_stretch_dynamics_settling(scene, state, 1.1);
        ui_update_liquid_controls(scene, true, ui_liquid_glass_options());
        const auto& paint = registry.get<ShapeStyleComponent>(gear);
        passed &= expect(paint.opacity == 1.0f && paint.color0.a < 0.1f &&
            registry.get<LiquidGlassComponent>(gear).material == GlassMaterial::eLiquid &&
            registry.get<StretchDynamicsComponent>(gear).dragging,
            "Revealed island buttons must retain visible glass through press, drag and material refreshes");
        authored.idle.opacity = authored.hovered.opacity = authored.pressed.opacity = 0.7f;
        ui_set_liquid_button_visual(scene, gear, authored);
        ui_update_liquid_controls(scene, true, ui_liquid_glass_options());
        passed &= expect(registry.get<ShapeStyleComponent>(gear).opacity == 0.7f,
            "Changing a captured button's reveal opacity must preserve its current glass press state");
        ui_handle_pointer_button(scene, atlas, state,
            UiPointerButtonInput { true, point + glm::vec2(12.0f, 0.0f), PointerButton::eLeft, 0, UiInputAction::eRelease });
        passed &= expect(clicks == 0u, "Dragging the island Settings icon must not activate Settings");
        ui_update_liquid_controls(scene, false, ui_liquid_glass_options());
        passed &= expect(registry.get<ButtonVisualComponent>(gear).idle.opacity == 0.7f,
            "Removing glass must restore the latest navigation visual rather than its initially hidden authoring");
    }
    {
        Renderer2DScene scene;
        auto& registry = scene.registry();
        const auto island = scene.create_shape({}, { 400.0f, 200.0f }, make_solid_style("#000000FF"));
        registry.emplace<ForegroundBlurGroup2DComponent>(island);
        const auto lens = scene.create_shape({ 20.0f, 20.0f }, { 62.0f, 32.0f },
            make_solid_style("#FFFFFF04"), Renderer2DPrimitive::eRoundedRectangle);
        registry.emplace<Parent2DComponent>(lens, Parent2DComponent { island });
        registry.emplace<LiquidGlassComponent>(lens, LiquidGlassComponent {
            GlassMaterial::eLiquid, LiquidGlassBackend::eEngineRenderer, true });
        const auto icon = scene.create_shape({ 30.0f, 30.0f }, { 12.0f, 12.0f }, make_solid_style("#FFFFFFFF"));
        registry.emplace<Parent2DComponent>(icon, Parent2DComponent { lens });
        double time = 1.0;
        for (const float radius : { 0.0f, 0.02f, 1.0f, 12.0f, 4.0f, 0.02f, 0.0f })
        {
            registry.get<ForegroundBlurGroup2DComponent>(island).radius = radius;
            scene.activate_dynamic(island, 1.0f);
            scene.mark_dirty(island);
            const auto plan = scene.build_render_plan(time += 0.02, 0u);
            bool lensEmitted = false, iconEmitted = false;
            for (const auto* batches : { &plan.blurs, &plan.cachedBlurs })
                for (const auto& batch : *batches)
                    if (batch.entity == lens && (batch.flags & eRenderer2DStyleLiquidGlass) != 0u)
                    {
                        lensEmitted = true;
                        passed &= expect(batch.foregroundBlurGroup == entt::null && batch.effect0.w > 0.99f,
                            "Island button optics must remain visible outside the temporary content blur during drag and spring release");
                    }
            for (const auto* batches : { &plan.shapes, &plan.cachedShapes })
                for (const auto& batch : *batches)
                    if (batch.entity == icon)
                    {
                        iconEmitted = true;
                        passed &= expect(batch.foregroundBlurGroup == (radius > 0.01f ? island : entt::null),
                            "Foreground icons inside a glass button must still receive the island's content blur");
                    }
            passed &= expect(lensEmitted && iconEmitted,
                "Both the optical surface and foreground icon must be emitted throughout the drag blur lifecycle");
        }
    }
    {
        Renderer2DScene scene;
        UiBuilder ui(scene);
        const auto surface = scene.create_shape({}, { 240.0f, 100.0f },
            make_solid_style("#223344FF"), Renderer2DPrimitive::eRoundedRectangle);
        auto material = ui_liquid_glass_options();
        material.sceneBackdrop = true;
        material.liquidBlur = 0.0f;
        material.liquidTint = { 0.0f, 0.4f, 0.9f, 0.8f };
        ui_apply_surface_material(ui, surface, material);
        const auto& paint = scene.registry().get<ShapeStyleComponent>(surface);
        passed &= expect(scene.registry().get<LiquidGlassComponent>(surface).sampleOwnPaint &&
            paint.color0.b > paint.color0.r && paint.color0.a >= 0.8f,
            "Tinted scene surfaces must retain their colour beneath their optical rims");
        ui_update_liquid_controls(scene, true, ui_liquid_glass_options(), false, entt::null, false);
        passed &= expect(scene.registry().get<LiquidGlassComponent>(surface).blur == 0.0f,
            "Updating panel controls must preserve independently authored preview optics");
        material.liquidTint.alpha = 0.2f;
        ui_apply_surface_material(ui, surface, material);
        passed &= expect(!scene.registry().get<LiquidGlassComponent>(surface).sampleOwnPaint &&
            scene.registry().get<ShapeStyleComponent>(surface).color0.a < 0.3f,
            "Clear scene surfaces must filter the backdrop before their translucent paint");
        const auto generation = scene.frame_generation();
        ui_apply_surface_material(ui, surface, material);
        passed &= expect(scene.frame_generation() == generation,
            "An unchanged surface material must retain its rendered frame");
    }
    {
        Renderer2DScene materialScene;
        UiBuilder ui(materialScene);
        Renderer2DFontAtlas font;
        UiSliderOptions options;
        options.hoveredTrackHeight = 14.0f;
        options.hoveredThumbScale = 1.3f;
        const auto handle = ui_create_slider(ui, ui.root(), UiAlignment::eTopLeft,
            { 20.0f, 20.0f }, { 240.0f, 7.0f }, 2, 0u, options);
        auto& registry = materialScene.registry();
        auto& slider = registry.get<SliderInputComponent>(handle.root);
        slider.hovered = slider.dragging = true;
        for (unsigned frame = 0u; frame < 12u; ++frame)
            ui_update_slider_smoothing(materialScene, font, 1.0 + frame / 60.0);
        passed &= expect(registry.get<ShapeComponent>(handle.knob).size == slider.thumbSize &&
            registry.get<ShapeComponent>(handle.root).size.y == slider.trackHeight,
            "hovering and dragging a non-glass slider must preserve its thumb and track size");
        ui_update_liquid_controls(materialScene, true, ui_liquid_glass_options());
        for (unsigned frame = 0u; frame < 12u; ++frame)
            ui_update_slider_smoothing(materialScene, font, 2.0 + frame / 60.0);
        passed &= expect(registry.get<ShapeComponent>(handle.knob).size.x > slider.thumbSize.x,
            "Liquid Glass slider thumbs must retain their grab expansion");
        ui_update_liquid_controls(materialScene, false, ui_liquid_glass_options());
        passed &= expect(!slider.liquidThumbEnabled && slider.liquidThumbScale == 1.0f &&
            registry.get<ShapeComponent>(handle.knob).size == slider.thumbSize &&
            registry.get<ShapeComponent>(handle.root).size.y == slider.trackHeight,
            "turning off Liquid Glass must immediately resize the actual slider geometry");
        materialScene.build_render_plan(0.0, 0u);
        ui_set_slider_value_from_point(materialScene, handle.root, { 150.0f, 23.0f });
        passed &= expect(slider.value > .4f && registry.get<ShapeComponent>(handle.knob).size == slider.thumbSize,
            "non-glass sliders must still scrub without stretching");
        const auto switchHandle = ui_create_switch(ui, ui.root(), UiAlignment::eTopLeft,
            { 20.0f, 70.0f }, { 64.0f, 28.0f }, 2, 0u);
        auto& toggle = registry.get<UiSwitchControlComponent>(switchHandle.root);
        ui_update_liquid_controls(materialScene, true, ui_liquid_glass_options());
        toggle.liquidThumbScale = 1.25f;
        registry.get<ShapeComponent>(toggle.knob).size = toggle.knobBaseSize * 1.25f;
        registry.get<Layout2DComponent>(toggle.knob).size = toggle.knobBaseSize * 1.25f;
        ui_update_liquid_controls(materialScene, false, ui_liquid_glass_options());
        passed &= expect(!toggle.liquidThumbEnabled && toggle.liquidThumbScale == 1.0f &&
            registry.get<ShapeComponent>(toggle.knob).size == toggle.knobBaseSize,
            "turning off Liquid Glass must restore the actual switch-thumb geometry immediately");
        materialScene.build_render_plan(0.0, 0u);
        const auto bounds = ui_entity_framebuffer_rect(registry, switchHandle.root);
        UiInputState input;
        UiPointerButtonInput pointer { true, { bounds.x + bounds.z * .5f, bounds.y + bounds.w * .5f },
            PointerButton::eLeft, 0, UiInputAction::ePress };
        ui_handle_pointer_button(materialScene, font, input, pointer);
        pointer.action = UiInputAction::eRelease;
        ui_handle_pointer_button(materialScene, font, input, pointer);
        passed &= expect(toggle.checked && registry.get<ShapeComponent>(toggle.knob).size == toggle.knobBaseSize,
            "switches must remain clickable without stretching when Liquid Glass is off");
    }
    {
        Renderer2DScene transitionScene;
        const auto surface = transitionScene.create_shape({}, { 80.0f, 40.0f });
        DisplayTransition2DComponent transition;
        transition.durationSeconds = .1f;
        transition.toOpacity = .5f;
        transition.removeWhenComplete = false;
        transitionScene.play_display_transition(surface, transition, 0.0);
        passed &= expect(!transitionScene.display_transition_finished(surface),
            "An unpresented transition must remain pending");
        transitionScene.build_render_plan(2.0, 0u);
        transitionScene.build_render_plan(2.05, 0u);
        passed &= expect(!transitionScene.display_transition_finished(surface),
            "Transition completion must follow the renderer clock rather than a particular opacity");
        transitionScene.build_render_plan(2.11, 0u);
        passed &= expect(transitionScene.display_transition_finished(surface),
            "Completed transitions must be observable without deleting the animated entity");
    }
    passed &= lost_button_releases_recover_capture();
    passed &= dropdowns_dismiss_globally();
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
        registry.emplace<ButtonVisualComponent>(light,
            ui_fixed_colour_button_visual(registry.get<ShapeStyleComponent>(light)));
        StretchDynamicsOptions stretch {};
        stretch.enabled = false;
        stretch.grabScale = 1.04f;
        stretch.consumeClickAfterDrag = true;
        stretch.returnDampingFraction = 0.54f;
        ui_enable_stretch_dynamics(lightsScene, light, stretch);
        auto optics = ui_liquid_glass_options();
        optics.liquidRefraction = 0.85f;
        optics.liquidTint = { 0.0f, 0.0f, 0.0f, 0.25f };
        ui_update_liquid_controls(lightsScene, true, optics);
        const auto glass = registry.get<ShapeStyleComponent>(light).color0;
#if defined(__APPLE__)
        const auto lightTint = registry.get<SystemBackdropComponent>(light).region.liquidTint;
        passed &= expect(glass.a == 0.0f && lightTint.red > lightTint.green * 2.0f &&
            lightTint.red > lightTint.blue * 2.0f &&
            !registry.any_of<ShadowComponent>(light) &&
            registry.get<LiquidGlassComponent>(light).backend == LiquidGlassBackend::eMacOSNative,
            "native traffic light glass must receive its original colour without duplicate shader paint");
#else
        passed &= expect(registry.get<LiquidGlassComponent>(light).refraction == 0.85f &&
            glass.r > glass.g * 2.0f && glass.r > glass.b * 2.0f && glass.a < 1.0f,
            "traffic light lenses must follow the optical settings and retain their colour");
        const auto& lightPaint = registry.get<ShapeStyleComponent>(light);
        passed &= expect(lightPaint.fill == Renderer2DFill::eLinearGradient &&
            lightPaint.color0.r < lightPaint.color1.r && lightPaint.color0.a >= 0.94f &&
            lightPaint.outlineWidth == 0.0f,
            "traffic light glass must retain saturated curved colour beneath its optical rim");
        const auto& shadow = registry.get<ShadowComponent>(light);
        passed &= expect(shadow.outsideOnly && shadow.spread == 0.0f && shadow.opacity <= 0.06f,
            "shared glass surfaces must have only a faint outside shadow");
#endif
        lightsScene.build_render_plan(0.0, 0u);
#if defined(_WIN32)
        bool paintedGlassContour = false;
        const auto lightPlan = lightsScene.build_render_plan(0.0, 0u);
        for (const auto* batches : { &lightPlan.shapes, &lightPlan.cachedShapes })
            for (const auto& batch : *batches)
                if (batch.entity == light)
                    paintedGlassContour = (batch.flags & eRenderer2DStyleLiquidPaintedSurface) != 0u &&
                        (batch.flags & eRenderer2DStyleLiquidOuterStroke) == 0u && batch.effect0.y == 0.0f;
        passed &= expect(paintedGlassContour,
            "Painted glass must retain its optical material without a separate stroke");
#endif
        UiInputState state {};
        const Renderer2DFontAtlas atlas;
        const glm::vec2 point { 27.0f, 27.0f };
        ui_handle_pointer_button(lightsScene, atlas, state,
            UiPointerButtonInput { true, point, PointerButton::eLeft, 0, UiInputAction::ePress });
        ui_update_stretch_dynamics_settling(lightsScene, state, 0.1);
        for (unsigned frame = 1u; frame <= 24u; ++frame)
            ui_update_stretch_dynamics_settling(lightsScene, state, 0.1 + frame / 120.0);
        passed &= expect(actions == 0 && registry.get<StretchDynamicsComponent>(light).scale.x > 1.03f && registry.get<StretchDynamicsComponent>(light).scale.x < 1.05f,
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

    for (const auto colour : { "#FF5F57FF", "#FEBC2EFF", "#28C840FF", "#80808094" })
    {
        Renderer2DScene scene;
        auto& registry = scene.registry();
        const auto authored = make_solid_style(colour);
        const auto light = scene.create_shape({}, { 14.0f, 14.0f }, authored,
            Renderer2DPrimitive::eEllipse);
        registry.emplace<ButtonInputComponent>(light);
        registry.emplace<ButtonVisualComponent>(light, ui_fixed_colour_button_visual(authored));
        registry.emplace<UiLiquidControlSurface>(light, UiLiquidControlSurface { false, false, true });
        for (const bool afterLiquid : { false, true })
        {
            if (afterLiquid) ui_update_liquid_controls(scene, true, ui_liquid_glass_options());
            ui_update_liquid_controls(scene, false, ui_liquid_glass_options());
            for (int state = 0; state < 4; ++state)
            {
                auto& button = registry.get<ButtonInputComponent>(light);
                button.hovered = state == 1;
                button.leftPressed = state == 2;
                button.enabled = state != 3;
                ui_update_button_visual(scene, light);
                const auto& paint = registry.get<ShapeStyleComponent>(light);
                passed &= expect(paint.color0 == authored.color0 && paint.color1 == authored.color1 &&
                    paint.outlineColor == authored.outlineColor,
                    "traffic light idle, hover, press and disabled states must preserve their authored colour before and after liquid mode");
            }
        }
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
    const glm::vec2 rowPoint { 120.0f, 20.0f };
    const entt::entity rowHit = scene.entity_at(rowPoint);
    ui_update_input_hover_from_hit(scene, fontAtlas, inputState, true, rowPoint, rowHit);
    passed &= expect(
        inputState.hoveredButton == row &&
            registry.get<ButtonInputComponent>(row).hovered,
        "a nested row should take hover ownership from its menu parent");
    passed &= expect(ui_hit_blocks_window_action(scene, inputState, rowPoint, rowHit),
        "a transparent row must block window dragging when hover and chrome share its hit");

    {
        Renderer2DScene sharedScene;
        auto& sharedRegistry = sharedScene.registry();
        const auto back = sharedScene.create_shape({ 0.0f, 0.0f }, { 100.0f, 100.0f }, visibleStyle);
        const auto front = sharedScene.create_shape({ 0.0f, 0.0f }, { 100.0f, 100.0f }, transparentStyle);
        for (const auto entity : { back, front })
        {
            sharedRegistry.get<Transform2DComponent>(entity).origin = { 0.0f, 0.0f };
            sharedRegistry.emplace<ButtonInputComponent>(entity);
        }
        sharedRegistry.emplace<HitRegion2DComponent>(front);
        sharedRegistry.get<RenderLayer2DComponent>(front).order = 10u;
        const glm::vec2 point { 40.0f, 40.0f };
        const auto hit = sharedScene.entity_at(point);
        UiInputState state;
        const auto hover = ui_update_input_hover_from_hit(sharedScene, fontAtlas, state, true, point, hit);
        passed &= expect(hit == front && hover.buttonOwner == front && state.hoveredButton == front &&
            ui_hit_blocks_window_action(sharedScene, state, point, hit),
            "one hit must give transparent front controls the same hover and window-action ownership");
        sharedRegistry.get<ButtonInputComponent>(front).enabled = false;
        sharedRegistry.emplace<TextInputComponent>(front);
        passed &= expect(!ui_hit_blocks_window_action(sharedScene, state, point, hit),
            "a disabled button must retain its existing priority over another input family on the same hit");
        sharedRegistry.get<ButtonInputComponent>(front).enabled = true;
        sharedRegistry.remove<TextInputComponent>(front);

        for (const auto member : { &UiInputState::pressedInputEntity, &UiInputState::pointerInputCapture,
            &UiInputState::activeSlider, &UiInputState::activeScrollBar })
        {
            UiInputState captured;
            captured.*member = front;
            passed &= expect(ui_hit_blocks_window_action(sharedScene, captured, { 200.0f, 200.0f }, entt::null),
                "active pointer capture must block window actions even when no entity is under the pointer");
        }
        ui_update_input_hover_from_hit(sharedScene, fontAtlas, state, true, point, entt::null);
        passed &= expect(state.hoveredButton == front,
            "a temporarily empty supplied hit must preserve a previous owner that still contains the pointer");

        const auto generation = sharedScene.frame_generation();
        sharedRegistry.get<RenderLayer2DComponent>(front).visible = false;
        passed &= expect(sharedScene.frame_generation() == generation,
            "the fresh-query regression must mutate the registry without advancing scene generation");
        ui_update_input_hover(sharedScene, fontAtlas, state, true, point);
        passed &= expect(state.hoveredButton == back,
            "ordinary hover must query again at the same point after a direct registry visibility change");
        sharedRegistry.get<RenderLayer2DComponent>(front).visible = true;
        ui_update_input_hover(sharedScene, fontAtlas, state, true, point);
        passed &= expect(state.hoveredButton == front,
            "ordinary hover must observe restored visibility without retaining a hit from an earlier update");
        ui_update_input_hover_from_hit(sharedScene, fontAtlas, state, false, point, front);
        passed &= expect(state.hoveredButton == entt::null,
            "an absent pointer must clear hover even if a caller supplies an old hit");
    }

    {
        Renderer2DScene dragScene;
        auto& dragRegistry = dragScene.registry();
        const auto panel = dragScene.create_shape({ 0.0f, 0.0f }, { 100.0f, 100.0f }, visibleStyle);
        const auto child = dragScene.create_shape({ 0.0f, 0.0f }, { 20.0f, 20.0f }, visibleStyle);
        const auto target = dragScene.create_shape({ 140.0f, 0.0f }, { 20.0f, 20.0f }, visibleStyle);
        for (const auto entity : { panel, child, target })
            dragRegistry.get<Transform2DComponent>(entity).origin = { 0.0f, 0.0f };
        dragRegistry.emplace<Parent2DComponent>(child, Parent2DComponent { panel });
        dragRegistry.get<RenderLayer2DComponent>(child).order = 10u;
        dragRegistry.get<ShapeComponent>(panel).draggable = true;
        const glm::vec2 point { 3.0f, 15.0f };
        const auto hit = dragScene.entity_at(point);
        UiInputState state;
        passed &= expect(hit == child && ui_draggable_parent_from_hit(dragRegistry, hit) == panel &&
            dragScene.draggable_parent_at(point) == panel && ui_hit_blocks_window_action(dragScene, state, point, hit),
            "a child hit must preserve draggable ancestry and block the surrounding native window action");
        dragRegistry.emplace<DragHandle2DComponent>(child, DragHandle2DComponent { target, true });
        passed &= expect(ui_draggable_parent_from_hit(dragRegistry, hit) == target &&
            dragScene.draggable_parent_at(point) == target,
            "an explicit drag-handle target must retain priority over a draggable parent");
        dragScene.destroy_entity(target);
        passed &= expect(ui_draggable_parent_from_hit(dragRegistry, hit) == entt::null &&
            dragScene.draggable_parent_at(point) == entt::null,
            "a removed drag target must not silently fall back to an ancestor");
        dragRegistry.get<DragHandle2DComponent>(child).enabled = false;
        passed &= expect(ui_draggable_parent_from_hit(dragRegistry, hit) == panel,
            "a disabled drag handle must allow its draggable ancestor to own the hit");
        dragRegistry.get<RenderLayer2DComponent>(panel).visible = false;
        passed &= expect(ui_draggable_parent_from_hit(dragRegistry, hit) == entt::null,
            "hidden draggable ancestry must not claim a supplied child hit");
        dragRegistry.get<RenderLayer2DComponent>(panel).visible = true;
        dragRegistry.get<ShapeComponent>(panel).draggable = false;
        dragRegistry.remove<DragHandle2DComponent>(child);
        dragRegistry.emplace<ResizablePanelComponent>(panel);
        const auto hover = ui_update_input_hover_from_hit(dragScene, fontAtlas, state, true, point, hit);
        passed &= expect(ui_hit_blocks_window_action(dragScene, state, point, hit) &&
            state.hoveredResizePanel == panel && hover.resizeEdges == ePanelResizeLeft,
            "a supplied child hit must preserve parent resize edges and window-action blocking");
        dragScene.destroy_entity(child);
        dragRegistry.get<ShapeStyleComponent>(panel) = transparentStyle;
        passed &= expect(dragScene.entity_at(point) == entt::null &&
            ui_hit_blocks_window_action(dragScene, state, point, entt::null),
            "transparent resizable panels must retain their edge fallback when the shared hit is empty");
    }

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
        const auto groupEntity = sliderScene.create_shape({}, { 600.0f * dpi, 200.0f * dpi },
            make_solid_style("#00000000"));
        sliderRegistry.emplace_or_replace<Parent2DComponent>(sliderHandle.root, groupEntity);
        auto& group = sliderRegistry.emplace<InteractiveVisual2DComponent>(groupEntity);
        group.scaleOrigin = { 300.0f * dpi, 0.0f };
        group.offset = { 31.0f, 7.0f };
        for (const float factor : { 0.25f, 0.5f, 0.75f, 1.03f })
        {
            group.scale = glm::vec2(factor);
            const auto visibleTrack = ui_entity_visual_framebuffer_rect(sliderRegistry, sliderHandle.root);
            for (const float fraction : { 0.0f, 0.25f, 0.75f, 1.0f })
            {
                ui_set_slider_value_from_point(sliderScene, sliderHandle.root,
                    { visibleTrack.x + inset * factor +
                        (visibleTrack.z - inset * factor * 2.0f) * fraction, visibleTrack.y });
                if (std::abs(slider.value - fraction * 100.0f) >= 0.01f)
                    std::cerr << "Scaled slider: dpi=" << dpi << " factor=" << factor
                        << " fraction=" << fraction << " value=" << slider.value
                        << " authored-width=" << track.z << " visual-width=" << visibleTrack.z
                        << " inset=" << inset << '\n';
                passed &= expect(std::abs(slider.value - fraction * 100.0f) < 0.01f,
                    "Slider input must follow its visually scaled menu, including thumb travel and translation");
            }
        }
    }

    for (float dpi : { 1.0f, 1.5f, 2.0f })
    {
        Renderer2DScene insetScene;
        LayoutScale insetScale {};
        insetScale.factor = glm::vec2(dpi);
        insetScale.logicalSize = { 600.0f, 200.0f };
        UiBuilder insetUi(insetScene, insetScale);
        UiSliderOptions options;
        options.insetThumb = true;
        options.thumbDiameter = 30.0f;
        const auto control = ui_create_slider(insetUi, insetUi.root(), UiAlignment::eTopLeft,
            { 20.0f, 50.0f }, { 255.0f, 34.0f }, 2, 0u, options);
        auto& registry = insetScene.registry();
        auto& slider = registry.get<SliderInputComponent>(control.root);
        for (const float value : { 0.0f, .5f, 1.0f })
        {
            slider.value = slider.visualValue = value;
            insetScene.build_render_plan(0.0, 0u);
            ui_update_slider_visual(insetScene, fontAtlas, control.root);
            insetScene.build_render_plan(0.0, 0u);
            const auto track = ui_entity_framebuffer_rect(registry, control.root);
            const auto thumb = ui_entity_framebuffer_rect(registry, control.knob);
            passed &= expect(std::abs(thumb.w - 30.0f * dpi) < .02f &&
                std::abs(thumb.y - track.y - 2.0f * dpi) < .02f,
                "Inset colour thumbs must honour their smaller diameter at every DPI");
            if (value != .5f)
            {
                const float gap = value == 0.0f ? thumb.x - track.x : track.x + track.z - thumb.x - thumb.z;
                passed &= expect(std::abs(gap - 2.0f * dpi) < .02f,
                    "Inset colour thumbs must have matching horizontal and vertical endpoint gaps");
            }
            ui_set_slider_value_from_point(insetScene, control.root,
                { thumb.x + thumb.z * .5f, thumb.y + thumb.w * .5f });
            passed &= expect(std::abs(slider.value - value) < .001f,
                "Inset colour slider input must match its rendered thumb position");
        }
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
        ui_update_slider_smoothing(glassScene, fontAtlas, 0.5);
        for (unsigned frame = 1u; frame <= 24u; ++frame)
            ui_update_slider_smoothing(glassScene, fontAtlas, 0.5 + frame / 120.0);
        ui_update_liquid_controls(glassScene, true, optics);
        auto plan = glassScene.build_render_plan(0.0, 0u);
        const auto containsOptics = [](const auto& batches) {
            return std::any_of(batches.begin(), batches.end(), [](const auto& batch) {
                return (batch.flags & eRenderer2DStyleLiquidGlass) != 0u;
            });
        };
#if defined(__APPLE__)
        passed &= expect(glassRegistry.all_of<LiquidGlassComponent, SystemBackdropComponent>(slider.thumbEntity) &&
            !containsOptics(plan.blurs) && !containsOptics(plan.cachedBlurs) &&
            glassRegistry.get<LiquidGlassComponent>(slider.thumbEntity).backend == LiquidGlassBackend::eMacOSNative,
            "a dragged macOS thumb must use native glass without a shader optical pass");
#else
        passed &= expect(glassRegistry.any_of<LiquidGlassComponent>(slider.thumbEntity) &&
            (containsOptics(plan.blurs) || containsOptics(plan.cachedBlurs)) &&
            !glassRegistry.any_of<SystemBackdropComponent>(slider.thumbEntity),
            "only a dragged thumb must emit portable optical drawing");
#endif
        const auto thumbHasOutsideStroke = [&](const auto& batches) {
            return std::any_of(batches.begin(), batches.end(), [&](const auto& batch) {
                return batch.entity == slider.thumbEntity &&
                    (batch.flags & eRenderer2DStyleLiquidOuterStroke) != 0u;
            });
        };
        const auto& sliderThumbStyle =
            glassRegistry.get<ShapeStyleComponent>(slider.thumbEntity);
        passed &= expect(sliderThumbStyle.outlineWidth == 0.0f &&
            sliderThumbStyle.outlineColor.a == 0.0f &&
            sliderThumbStyle.edgeSoftness == 0.5f &&
#if defined(__APPLE__)
            !glassRegistry.any_of<ShadowComponent>(slider.thumbEntity) &&
#else
            glassRegistry.any_of<ShadowComponent>(slider.thumbEntity) &&
            glassRegistry.get<ShadowComponent>(slider.thumbEntity).blurRadius >= 8.0f &&
            glassRegistry.get<ShadowComponent>(slider.thumbEntity).opacity <= 0.12f &&
            glassRegistry.get<ShadowComponent>(slider.thumbEntity).spread <= 0.75f &&
#endif
            !thumbHasOutsideStroke(plan.shapes) && !thumbHasOutsideStroke(plan.cachedShapes),
            "configured slider thumbs must have a soft shadow without gaining an inside outline in normal or liquid rendering");
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
            const auto fill = ui_entity_framebuffer_rect(glassRegistry, slider.fillEntity);
            passed &= expect(std::abs(fill.x + fill.z - (thumb.x + thumb.z * 0.5f)) < 0.01f,
                "a native thumb's transparent Metal paint must not change the slider fill's inset travel");
        }
        slider.dragging = false;
        slider.hovered = true;
        ui_update_liquid_controls(glassScene, true, optics);
        passed &= expect(glassRegistry.any_of<LiquidGlassComponent>(slider.thumbEntity),
            "A released thumb must retain its optical material while its spring returns");
        for (int frame = 1; frame <= 120; ++frame)
        {
            ui_update_slider_smoothing(glassScene, fontAtlas, 2.0 + frame / 120.0);
            ui_update_liquid_controls(glassScene, true, optics);
        }
        passed &= expect(std::abs(slider.liquidThumbScale - 1.0f) < 0.0001f,
            "the release spring must settle to normal size");
        passed &= expect(!glassRegistry.any_of<LiquidGlassComponent>(slider.thumbEntity) &&
            glassRegistry.get<ShapeStyleComponent>(slider.thumbEntity).color0 == glm::vec4(1.0f),
            "A settled thumb must restore its resting paint and release its optical material");
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
        passed &= expect(
#if defined(__APPLE__)
            focused.fill == Renderer2DFill::eSolid && focused.color0 == glm::vec4(0.0f) &&
            focused.color1 == focused.color0 &&
#else
            focused.fill == Renderer2DFill::eLinearGradient &&
            focused.color0.r > 0.95f && focused.color0.a >= 0.55f && focused.color1.a >= 0.48f &&
            registry.get<LiquidGlassComponent>(search.root).sampleOwnPaint &&
            registry.get<LiquidGlassComponent>(nav.root).sampleOwnPaint &&
#endif
            focused.outlineWidth == 0.0f &&
            registry.any_of<LiquidGlassComponent>(search.root) &&
            registry.any_of<LiquidGlassComponent>(nav.root) &&
            !registry.any_of<LiquidGlassComponent>(nav.label) &&
            !registry.any_of<LiquidGlassComponent>(nav.caret),
            "Light search and navigation controls must preserve their bright finish beneath one shared optical surface");
        optics.liquidRimAnimated = false;
        const auto authoredLens = controlsScene.create_shape({ 180.0f, 20.0f }, { 90.0f, 32.0f });
        auto authoredOptions = ui_liquid_glass_options();
        authoredOptions.sceneBackdrop = true;
        UiBuilder authoredUi(controlsScene);
        ui_apply_glass_material(authoredUi, authoredLens, authoredOptions);
        optics.liquidRefraction = 0.95f;
        optics.liquidBlur = 0.8f;
        optics.liquidTint = { 0.0f, 0.0f, 0.0f, 0.8f };
        ui_update_liquid_controls(controlsScene, true, optics);
        passed &= expect(!registry.get<LiquidGlassComponent>(authoredLens).rimAnimated &&
            registry.get<LiquidGlassComponent>(authoredLens).refraction == optics.liquidRefraction &&
            !registry.get<LiquidGlassComponent>(search.root).rimAnimated,
            "Authored lenses and managed controls must share live optical preferences");
        optics.liquidRimAnimated = true;
        ui_update_liquid_controls(controlsScene, true, optics);
        passed &= expect(registry.get<LiquidGlassComponent>(authoredLens).rimAnimated &&
            registry.get<LiquidGlassComponent>(search.root).rimAnimated,
            "Enabling rim animation must reach existing authored and managed lenses");
        const auto unchangedOpticsGeneration = controlsScene.frame_generation();
        ui_update_liquid_controls(controlsScene, true, optics);
        passed &= expect(controlsScene.frame_generation() == unchangedOpticsGeneration,
            "Unchanged optical preferences must not invalidate retained paint");
#if defined(__APPLE__)
        passed &= expect(registry.get<SystemBackdropComponent>(search.root).region.liquidTint.alpha == 0.8f &&
            registry.get<ShapeStyleComponent>(search.root).color0.a == 0.0f &&
            !registry.get<LiquidGlassComponent>(search.root).sceneBackdrop,
            "native controls must pass tint to AppKit while keeping shader lens paint transparent");
#else
        passed &= expect(registry.get<LiquidGlassComponent>(search.root).blur == 0.8f &&
            registry.get<LiquidGlassComponent>(nav.root).refraction == 0.95f &&
            registry.get<ShapeStyleComponent>(search.root).color1.a >= 0.8f &&
            registry.get<ShapeStyleComponent>(nav.root).color0.r < 0.20f,
            "all shared controls must respond to the live optical and tint preferences");
#endif
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
        for (const auto thumbSize : { glm::vec2(24.0f, 18.0f), glm::vec2(20.0f) })
        {
            Renderer2DScene hoverScene;
            LayoutScale scale {};
            scale.factor = glm::vec2(factor);
            scale.logicalSize = { 400.0f, 200.0f };
            UiBuilder ui(hoverScene, scale);
            UiSliderOptions options {};
            options.hoveredTrackHeight = 12.0f;
            options.thumbSize = thumbSize;
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
            slider.hovered = false;
            slider.dragging = true;
            ui_update_slider_smoothing(hoverScene, fontAtlas, 1.0);
            for (unsigned frame = 1u; frame <= 120u; ++frame)
            {
                ui_update_slider_smoothing(hoverScene, fontAtlas, 1.0 + frame / 120.0);
                ui_update_liquid_controls(hoverScene, true, ui_liquid_glass_options());
            }
            hoverScene.build_render_plan(2.0, 0u);
            const auto held = ui_entity_framebuffer_rect(registry, handle.knob);
            const auto& heldShape = registry.get<ShapeComponent>(handle.knob);
            passed &= expect(held.z - held.w >= (thumbSize.x - thumbSize.y) * factor - 0.01f &&
                std::abs(heldShape.cornerRadius * 2.0f - std::min(held.z, held.w)) < 0.01f,
                "A stationary held slider must preserve its capsule's straight centre and circular ends at every DPI");
            if (thumbSize.x == thumbSize.y)
                passed &= expect(std::abs(held.z - held.w) < 0.01f,
                    "A round slider thumb must remain round during stationary expansion");
            passed &= expect(glm::length(glm::vec2(before) + glm::vec2(before.z, before.w) * 0.5f -
                glm::vec2(held) - glm::vec2(held.z, held.w) * 0.5f) < 0.01f,
                "Stationary slider expansion must stay centred over the resting thumb");
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
        auto tintedOptics = optics;
        tintedOptics.liquidTint = { 0.2f, 0.3f, 0.4f, 0.8f };
        const auto beforeTint = nativeScene.frame_generation();
        ui_update_liquid_controls(nativeScene, true, tintedOptics, false, search.root);
        passed &= expect(nativeScene.frame_generation() > beforeTint &&
            registry.get<SystemBackdropComponent>(search.root).region.liquidTint.alpha == 0.8f,
            "changing only a native lens tint must schedule a frame and update its published material");
        const auto afterTint = nativeScene.frame_generation();
        ui_update_liquid_controls(nativeScene, true, tintedOptics, false, search.root);
        passed &= expect(nativeScene.frame_generation() == afterTint,
            "an unchanged native tint must retain the lens without continuously scheduling frames");
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
#if defined(__APPLE__)
        passed &= expect(!hasNativeCap && registry.any_of<SystemBackdropComponent>(nav.root) &&
            registry.get<LiquidGlassComponent>(nav.root).backend == LiquidGlassBackend::eMacOSNative &&
            !registry.any_of<ShapeCutout2DComponent>(parent),
            "a macOS lens must use AppKit above the existing cap paint without a shader pass or backdrop hole");
#else
        passed &= expect(hasNativeCap && !registry.any_of<SystemBackdropComponent>(nav.root),
            "a shared scene lens must sample the island cap without opening a desktop backdrop hole");
#endif
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
        thumbScene.build_render_plan(0.0, 0u);
        const auto seekTrack = ui_entity_framebuffer_rect(thumbScene.registry(), media.root);
        const float seekInset = slider.thumbSize.x * 1.32f * 0.5f;
        const glm::vec2 seekPoint { seekTrack.x + seekInset + (seekTrack.z - 2.0f * seekInset) * 0.25f,
            seekTrack.y + seekTrack.w * 0.5f };
        ui_set_slider_value_from_point(thumbScene, media.root, seekPoint);
        passed &= expect(std::abs(slider.value - 0.25f) < 0.0001f,
            "Wider visible capsules must preserve the existing pointer mapping of hidden media seek thumbs");
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


        switchRegistry.emplace<ScrollInputComponent>(switchRoot);
        auto& control = switchRegistry.get<UiSwitchControlComponent>(toggle.root);
        const auto& switchShadow = switchRegistry.get<ShadowComponent>(toggle.knob);
        passed &= expect(switchShadow.outsideOnly && switchShadow.spread == 0.0f &&
            switchShadow.blurRadius == 8.0f && switchShadow.opacity <= 0.12f,
            "switch thumbs must share the slider thumb outer shadow");
        UiInputState gestureState {};
        auto optics = ui_liquid_glass_options();
        optics.liquidBlur = 1.0f;
        optics.liquidTint = { 0.1f, 0.2f, 0.9f, 1.0f };
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


            gestureState.activeStretchDynamics = entt::null;
            switchRegistry.get<StretchDynamicsComponent>(toggle.root).dragging = false;
            ui_update_liquid_controls(switchScene, true, optics);
            ui_update_switch_animations(switchScene, time + 0.1);
            ui_update_stretch_dynamics_settling(switchScene, gestureState, time + 0.1);
            ui_update_switch_drag(switchScene, gestureState, true, true, point);
            ui_update_switch_animations(switchScene, time + 0.2);
            ui_update_liquid_controls(switchScene, true, optics);
            const glm::vec2 expandedKnobSize =
                switchRegistry.get<ShapeComponent>(toggle.knob).size;
            switchScene.build_render_plan(time + 0.2, 0u);
            const glm::vec4 expandedKnobRect =
                ui_entity_visual_framebuffer_rect(switchRegistry, toggle.knob);
            passed &= expect(control.dragging && control.liquidThumbScale > 1.0f &&
                switchRegistry.any_of<LiquidGlassComponent>(toggle.knob) &&
                !switchRegistry.any_of<LiquidGlassComponent>(toggle.root),
                "a grabbed switch knob inside a scroll view must expand with glass while its track stays normal");
            passed &= expect(expandedKnobSize.y >= control.knobBaseSize.y * 1.8f &&
                expandedKnobRect.w >= control.knobBaseSize.y * 1.8f &&
                expandedKnobRect.w <= control.knobBaseSize.y * 1.91f,
                "A dragged switch thumb must lift to roughly 185% height with bounded spring overshoot");
            passed &= expect(expandedKnobSize.x - expandedKnobSize.y >=
                control.knobBaseSize.x - control.knobBaseSize.y &&
                glm::length(glm::vec2(expandedKnobRect) + glm::vec2(expandedKnobRect.z, expandedKnobRect.w) * 0.5f - point) < 0.01f,
                "A stationary held switch must retain its capsule centre section and expand about its resting centre");
            const auto& glass = switchRegistry.get<LiquidGlassComponent>(toggle.knob);
            const auto& paint = switchRegistry.get<ShapeStyleComponent>(toggle.knob);
            passed &= expect(glass.preserveBackdropColour && glass.blur == 0.0f &&
                glass.refraction > 0.0f && glass.refraction < 0.25f &&
                paint.color0.a == 0.0f && paint.color1.a == 0.0f,
                "Switch thumbs must stay clear without panel tint, diffusion or backdrop darkening");
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
            {
                ui_update_switch_animations(switchScene, time + 0.2 + frame / 120.0);
                ui_update_liquid_controls(switchScene, true, optics);
            }
            passed &= expect(!control.dragging && control.liquidThumbScale == 1.0f &&
                !switchRegistry.any_of<LiquidGlassComponent>(toggle.knob) &&
                switchRegistry.get<ShapeStyleComponent>(toggle.knob).color0 == glm::vec4(1.0f) &&
                switchRegistry.get<ShapeComponent>(toggle.knob).size == control.knobBaseSize,
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

    {
        Renderer2DScene opticalScene;
        auto& registry = opticalScene.registry();
        const auto lens = opticalScene.create_shape({ 100.0f, 100.0f }, { 40.0f, 30.0f });
        registry.emplace<LiquidGlassComponent>(lens, LiquidGlassComponent {
            GlassMaterial::eLiquid, LiquidGlassBackend::eEngineRenderer,
            true, 0.7f, 0.15f, 0.6f, 0.35f });
        const auto generation = opticalScene.frame_generation();
        const auto cacheGeneration = opticalScene.cache_generation();
        passed &= expect(opticalScene.requires_continuous_redraw(0.0) &&
            opticalScene.requires_continuous_redraw(1.0) &&
            opticalScene.frame_generation() == generation,
            "direct SDK liquid rims must animate without synthetic scene mutations or a control updater");
        const auto hasLens = [&](const auto& batches) {
            return std::any_of(batches.begin(), batches.end(), [&](const auto& batch) {
                return batch.entity == lens &&
                    (batch.flags & eRenderer2DStyleLiquidGlass) != 0u;
            });
        };
        const auto first = opticalScene.build_render_plan(0.0, 0u);
        const auto retained = opticalScene.build_render_plan(1.0, cacheGeneration);
        passed &= expect(hasLens(first.blurs) && !hasLens(first.cachedBlurs) &&
            hasLens(retained.blurs) && !retained.rebuildCachedLayer &&
            opticalScene.frame_generation() == generation,
            "a statically declared liquid rim must stay in dynamic batches while the surrounding cache remains retained");
        commit_scene_bounds(opticalScene, { { lens, { 80u, 80u, 40u, 30u } } });
        passed &= expect(scene_damage_pending(opticalScene, lens, 1.0) &&
            scene_damage_pending(opticalScene, lens, 2.0) &&
            opticalScene.frame_generation() == generation &&
            opticalScene.cache_generation() == cacheGeneration,
            "committing a retained image must leave animated rim pixels damaged without changing scene or text generations");

        auto& style = registry.get<ShapeStyleComponent>(lens);
        style.opacity = 0.0f;
        passed &= expect(!opticalScene.requires_continuous_redraw(1.0) &&
            !scene_damage_pending(opticalScene, lens, 1.0),
            "zero-opacity portable rims must not request continuous rendering");
        const auto hiddenPlan = opticalScene.build_render_plan(1.0, cacheGeneration);
        passed &= expect(!hasLens(hiddenPlan.blurs) && !hasLens(hiddenPlan.cachedBlurs),
            "a zero-opacity lens must emit no optical pixels when another animation draws the scene");
        style.opacity = 0.4f;
        const auto translucentPlan = opticalScene.build_render_plan(1.0, cacheGeneration);
        const auto translucentLens = std::find_if(translucentPlan.blurs.begin(),
            translucentPlan.blurs.end(), [&](const auto& batch) {
                return batch.entity == lens &&
                    (batch.flags & eRenderer2DStyleLiquidGlass) != 0u;
            });
        passed &= expect(translucentLens != translucentPlan.blurs.end() &&
            std::abs(translucentLens->effect0.w - 0.4f) < 0.0001f,
            "portable optical coverage must follow the same surface opacity used to schedule its rim");
        style.opacity = 1.0f;
        auto& liquid = registry.get<LiquidGlassComponent>(lens);
        liquid.rim = 0.0f;
        passed &= expect(!opticalScene.requires_continuous_redraw(1.0) &&
            !scene_damage_pending(opticalScene, lens, 1.0),
            "a portable lens with its travelling rim disabled must retain its image");
        liquid.rim = 0.6f;
        liquid.rimAnimated = false;
        passed &= expect(!opticalScene.requires_continuous_redraw(1.0) &&
            !scene_damage_pending(opticalScene, lens, 1.0),
            "A static nonzero rim must retain its image without time-driven damage");
        const auto staticPlan = opticalScene.build_render_plan(1000.0, 0u);
        const auto staticLens = std::find_if(staticPlan.cachedBlurs.begin(), staticPlan.cachedBlurs.end(),
            [lens](const auto& batch) { return batch.entity == lens &&
                (batch.flags & eRenderer2DStyleLiquidGlass) != 0u; });
        passed &= expect(staticLens != staticPlan.cachedBlurs.end() &&
            (staticLens->flags & eRenderer2DStyleLiquidStaticRim) != 0u &&
            ((staticLens->packedData >> 21u) & 255u) == 0u,
            "A static rim must carry a stable shader flag and phase through long uptime");
        liquid.rimAnimated = true;
        liquid.sceneBackdrop = false;
        liquid.backend = LiquidGlassBackend::eWindowsCompositionShader;
        passed &= expect(!opticalScene.requires_continuous_redraw(1.0) &&
            !scene_damage_pending(opticalScene, lens, 1.0),
            "native compositor rims must not force portable renderer work");
        liquid.sceneBackdrop = true;
        liquid.backend = LiquidGlassBackend::eEngineRenderer;
        registry.get<Transform2DComponent>(lens).rotation3DRadians.x = 0.1f;
        passed &= expect(!opticalScene.requires_continuous_redraw(1.0) &&
            !scene_damage_pending(opticalScene, lens, 1.0),
            "unsupported transformed scene lenses must not request invisible optical work");
        registry.get<Transform2DComponent>(lens).rotation3DRadians.x = 0.0f;
        const auto clippingParent = opticalScene.create_shape({ 0.0f, 0.0f }, { 20.0f, 20.0f });
        registry.emplace<Mask2DComponent>(clippingParent, Mask2DComponent { true, false });
        registry.emplace<Parent2DComponent>(lens, Parent2DComponent { clippingParent });
        commit_scene_bounds(opticalScene, {});
        passed &= expect(!opticalScene.requires_continuous_redraw(1.0) &&
            !scene_damage_pending(opticalScene, lens, 1.0),
            "a fully clipped liquid rim must let the renderer sleep");
        registry.remove<Mask2DComponent>(clippingParent);
        registry.get<RenderLayer2DComponent>(clippingParent).visible = false;
        passed &= expect(!opticalScene.requires_continuous_redraw(1.0) &&
            !scene_damage_pending(opticalScene, lens, 1.0),
            "a liquid rim beneath a hidden ancestor must let the renderer sleep");
    }

    {
        Renderer2DScene scene;
        const auto lens = scene.create_shape({ 20.0f, 20.0f }, { 100.0f, 40.0f });
        auto& registry = scene.registry();
        registry.emplace<LiquidGlassComponent>(lens, LiquidGlassComponent {
            GlassMaterial::eLiquid, LiquidGlassBackend::eEngineRenderer,
            true, 0.7f, 0.15f, 0.6f, 0.35f });
        const auto phase = [&](double time) {
            const auto plan = scene.build_render_plan(time, 0u);
            for (const auto& batch : plan.blurs)
                if (batch.entity == lens && (batch.flags & eRenderer2DStyleLiquidGlass) != 0u)
                    return (batch.packedData >> 21u) & 255u;
            return std::numeric_limits<uint32_t>::max();
        };
        const auto generation = scene.frame_generation();
        const auto firstPhase = phase(0.0);
        passed &= expect(firstPhase == phase(0.001) &&
            !scene.requires_continuous_redraw_since(0.001, 0.0, generation) &&
            scene.requires_continuous_redraw_since(0.02, 0.0, generation),
            "a portable rim must submit only when its exact packed shader phase changes");



        for (const auto rate : { 60u, 144u, 200u })
        {
            for (const double origin : { 0.0, 8.2, 1000000.0 })
            {
                double submitted = origin;
                uint32_t submittedPhase = phase(submitted);
                for (uint32_t tick = 1u; tick <= rate; ++tick)
                {
                    const double time = origin + static_cast<double>(tick) / rate;
                    const auto nextPhase = phase(time);
                    const auto originalPacking = static_cast<uint32_t>(std::round(std::clamp(
                        static_cast<float>(std::fmod(time * 0.12, 1.0)), 0.0f, 1.0f) * 255.0f));
                    const bool redraw = scene.requires_continuous_redraw_since(time, submitted, generation);
                    passed &= expect(nextPhase == originalPacking && redraw == (nextPhase != submittedPhase),
                        "retained optical frames must match the existing shader input at every host cadence");
                    if (redraw)
                    {
                        submitted = time;
                        submittedPhase = nextPhase;
                    }
                }
            }
        }
        passed &= expect(phase(8.33) == 255u && phase(8.34) == 0u &&
            scene.requires_continuous_redraw_since(8.34, 8.33, generation),
            "the optical phase wrap must publish its new shader input");
        passed &= expect(scene.requires_continuous_redraw_since(0.001, -1.0, generation) &&
            scene.requires_continuous_redraw_since(0.001, 0.002, generation) &&
            scene.requires_continuous_redraw_since(0.001, std::numeric_limits<double>::quiet_NaN(), generation),
            "unknown or reset renderer clocks must not suppress the first optical frame");
        scene.mark_dirty(lens);
        passed &= expect(scene.requires_continuous_redraw_since(0.001, 0.0, generation),
            "a changed scene must redraw even within an unchanged optical phase");
        const auto currentGeneration = scene.frame_generation();
        registry.emplace<DisplayTransition2DComponent>(lens);
        passed &= expect(scene.requires_continuous_redraw_since(0.001, 0.0, currentGeneration),
            "a CPU-driven display transition must redraw within an unchanged optical phase");
        auto& retainedTransition = registry.get<DisplayTransition2DComponent>(lens);
        retainedTransition.hasStarted = true;
        retainedTransition.startSeconds = 0.0;
        retainedTransition.durationSeconds = 0.0095f;
        retainedTransition.removeWhenComplete = false;
        retainedTransition.destroyEntityTreeOnComplete = false;
        passed &= expect(scene.requires_continuous_redraw_since(0.01, 0.009, currentGeneration) &&
            !scene.requires_continuous_redraw_since(0.011, 0.01, currentGeneration),
            "a retained transition must publish its final pose once within an unchanged optical phase");
        registry.remove<DisplayTransition2DComponent>(lens);

        Media2DHandle handle;
        handle.id = 1u;
        handle.drawable = true;
        const auto player = scene.create_media({}, { 20.0f, 20.0f }, handle);
        auto& media = registry.get<Media2DComponent>(player);
        media.animated = true;
        media.playing = true;
        passed &= expect(scene.requires_continuous_redraw_since(0.001, 0.0, scene.frame_generation()),
            "animated media must redraw within an unchanged optical phase");
        media.playing = false;
        auto& cache = registry.get_or_emplace<Renderer2DCacheComponent>(player);
        cache.mode = Renderer2DCacheMode::eTimed;
        cache.pendingActiveSeconds = 0.1f;
        const auto timedGeneration = scene.frame_generation();
        passed &= expect(scene.requires_continuous_redraw_since(0.001, 0.0, timedGeneration),
            "pending timed cache activation must redraw within an unchanged optical phase");
        cache.pendingActiveSeconds = 0.0f;
        cache.wasActive = true;
        cache.activeUntilSeconds = 0.0005;
        passed &= expect(scene.requires_continuous_redraw_since(0.001, 0.0, timedGeneration),
            "timed cache expiry must redraw within an unchanged optical phase");
        cache.wasActive = false;
        cache.activeUntilSeconds = 1.0;
        cache.lastDynamicSeconds = -1.0;
        passed &= expect(scene.requires_continuous_redraw_since(0.001, 0.0, timedGeneration),
            "due active cache ticks must redraw within an unchanged optical phase");
        cache.activeUntilSeconds = 0.0;
        cache.idleTickRate = 1.0f;
        passed &= expect(scene.requires_continuous_redraw_since(0.001, 0.0, timedGeneration),
            "due idle cache ticks must redraw within an unchanged optical phase");
        cache.idleTickRate = 0.0f;
        passed &= expect(!scene.requires_continuous_redraw_since(0.001, 0.0, timedGeneration),
            "settled media and cache policies must allow identical optical frames to be retained");
    }

#if defined(_WIN32)
    for (const float tone : { 0.0f, 0.5f, 1.0f })
    {
        Renderer2DScene scene;
        UiBuilder ui(scene);
        const auto clear = scene.create_shape({ 20,20 }, { 80,40 },
            make_solid_style("#00000000", "#00000000", 0.0f, 1.0f));
        ui_set_liquid_stroke_surface(scene, clear, glm::vec4(tone,tone,tone,1),
            glm::vec4(tone,tone,tone,1));
        ui_apply_glass_material(ui, clear, ui_liquid_glass_options());
        const auto plan=scene.build_render_plan(1.0,0);
        bool emitted=false;
        for (const auto* list : { &plan.shapes, &plan.cachedShapes })
            for (const auto& batch : *list)
                if (batch.entity == clear)
                {
                    emitted=true;
                    passed &= expect((batch.flags&eRenderer2DStyleLiquidOuterStroke)==0u && batch.effect0.y==0.0f,
                        "A clear native lens must not emit a separate stroke");
                }
        passed &= expect(!emitted && scene.registry().all_of<LiquidGlassComponent>(clear),
            "A clear native lens must retain its optical material without an extra painted shape");
        const auto generation=scene.frame_generation();
        for (unsigned frame=0;frame<120;++frame)
            ui_set_liquid_stroke_surface(scene, clear, glm::vec4(tone,tone,tone,1),
                glm::vec4(tone,tone,tone,1));
        passed &= expect(scene.frame_generation()==generation,
            "Unchanged surface contrast must not schedule repeated frames");
    }
#endif
    for (const float dpi : { 1.0f, 1.5f, 2.0f })
        for (const glm::vec2 direction : { glm::vec2(1, 0), glm::vec2(-1, 0), glm::vec2(0, 1) })
        {
            Renderer2DScene scene;
            UiBuilder ui(scene);
            UiLiquidMergeOptions options;
            options.firstCenter = { 120.0f * dpi, 80.0f * dpi };
            options.secondCenter = options.firstCenter + direction * (42.0f * dpi);
            options.diameter = 40.0f * dpi;
            options.amount = 1.0f;
            const auto connection = ui_create_liquid_merge(ui, options);
            options.amount = 0.5f;
            ui_update_liquid_merge(scene, connection, options);
            const auto& shape = scene.registry().get<ShapeComponent>(connection);
            const auto& paint = scene.registry().get<ShapeStyleComponent>(connection);
            const auto& transform = scene.registry().get<Transform2DComponent>(connection);
            passed &= expect(shape.primitive == Renderer2DPrimitive::eLiquidBridge &&
                paint.color0.a == 1.0f && paint.opacity == 1.0f &&
                std::abs(transform.rotationRadians - std::atan2(direction.y, direction.x)) < 0.0001f,
                "Generic liquid objects must deform at full paint opacity in every direction");
            const auto generation = scene.frame_generation();
            for (unsigned frame = 0; frame < 120; ++frame)
                ui_update_liquid_merge(scene, connection, options);
            passed &= expect(scene.frame_generation() == generation &&
                !scene.requires_continuous_redraw(1.0),
                "A stationary liquid connection must retain its frame without an animation loop");
        }
    return passed ? 0 : 1;
}

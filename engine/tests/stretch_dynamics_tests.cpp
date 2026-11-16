// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/interactions.h>
#include <vibranceUI/ui/liquid_controls.h>

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "stretch dynamics test failed: " << message << '\n';
        }
        return condition;
    }

    bool close(float left, float right, float tolerance = 0.001f)
    {
        return std::abs(left - right) <= tolerance;
    }
}

int main()
{
    bool materialChecks = true;
    {
        Renderer2DScene materialScene;
        UiBuilder ui(materialScene);
        auto& registry = materialScene.registry();
        const auto surface = materialScene.create_shape({ 10.0f, 10.0f },
            { 120.0f, 40.0f }, make_solid_style("#334455FF"));
        registry.get<Transform2DComponent>(surface).origin = {};
        registry.emplace<Parent2DComponent>(surface, ui.root());
        StretchDynamicsOptions stretch;
        stretch.grabScale = 1.15f;
        ui_enable_stretch_dynamics(materialScene, surface, stretch);
        UiInputState input;
        materialChecks &= expect(!ui_begin_stretch_dynamics(materialScene, input, surface, { 30.0f, 30.0f }),
            "solid objects must not start optical stretching");
        registry.emplace<UiLiquidControlSurface>(surface);
        ui_update_liquid_controls(materialScene, true, ui_liquid_glass_options());
        materialChecks &= expect(ui_begin_stretch_dynamics(materialScene, input, surface, { 30.0f, 30.0f }),
            "an active Liquid Glass object must retain its stretch gesture");
        ui_update_stretch_dynamics_drag(materialScene, input, true, true, { 70.0f, 30.0f });
        ui_update_stretch_dynamics_settling(materialScene, input, 0.1);
        ui_update_stretch_dynamics_settling(materialScene, input, 0.2);
        materialChecks &= expect(registry.get<InteractiveVisual2DComponent>(surface).scale.x > 1.0f,
            "Liquid Glass must still deform while dragged");
        ui_update_liquid_controls(materialScene, false, ui_liquid_glass_options());
        materialChecks &= expect(registry.get<InteractiveVisual2DComponent>(surface).scale == glm::vec2(1.0f) &&
            !registry.get<StretchDynamicsComponent>(surface).settling,
            "disabling Liquid Glass mid-drag must restore the authored geometry immediately");
        ui_update_stretch_dynamics_settling(materialScene, input, 1.0);
        materialChecks &= expect(input.activeStretchDynamics == entt::null &&
            !ui_begin_stretch_dynamics(materialScene, input, surface, { 30.0f, 30.0f }),
            "a solid object must not restart stretching or keep an optical spring alive");
        const auto untouched = materialScene.create_shape({}, { 80.0f, 30.0f });
        ui_enable_stretch_dynamics(materialScene, untouched);
        ui_update_liquid_controls(materialScene, true, ui_liquid_glass_options());
        materialChecks &= expect(!ui_begin_stretch_dynamics(materialScene, input, untouched, { 20.0f, 15.0f }),
            "enabling glass elsewhere must not deform an unaffected object");
        bool released = false;
        StretchDynamicsOptions functional;
        functional.inwardDirection = { -1.0f, 0.0f };
        functional.inwardReleaseDistance = 20.0f;
        functional.inwardBlurRadius = 10.0f;
        functional.onInwardRelease = [&](const StretchDynamicsEvent&) { released = true; };
        ui_enable_stretch_dynamics(materialScene, untouched, functional);
        materialChecks &= expect(ui_begin_stretch_dynamics(materialScene, input, untouched, { 60.0f, 15.0f }),
            "functional drag actions must remain usable without glass");
        ui_update_stretch_dynamics_drag(materialScene, input, true, true, { 10.0f, 15.0f });
        const auto visual = registry.get<InteractiveVisual2DComponent>(untouched);
        materialChecks &= expect(visual.scale == glm::vec2(1.0f) && visual.blurRadius == 0.0f,
            "a functional gesture must not stretch or blur a solid object");
        ui_release_stretch_dynamics(materialScene, input, true, { 10.0f, 15.0f });
        materialChecks &= expect(released, "material changes must not remove functional release actions");
    }
    Renderer2DScene scene;
    entt::registry& registry = scene.registry();
    const entt::entity panel = scene.create_shape(
        { 20.0f, 30.0f },
        { 100.0f, 50.0f },
        ShapeStyleComponent {},
        Renderer2DPrimitive::eRoundedRectangle);
    Transform2DComponent& panelTransform =
        registry.get<Transform2DComponent>(panel);
    panelTransform.origin = { 0.0f, 0.0f };

    const entt::entity child = scene.create_shape(
        { 45.0f, 42.0f },
        { 20.0f, 10.0f },
        ShapeStyleComponent {},
        Renderer2DPrimitive::eRoundedRectangle);
    registry.emplace<Parent2DComponent>(child, Parent2DComponent { panel });
    registry.get<Transform2DComponent>(child).origin = { 0.0f, 0.0f };

    ShapeStyleComponent transparentViewportStyle {};
    transparentViewportStyle.opacity = 0.0f;
    const entt::entity scrollViewport = scene.create_shape(
        { 70.0f, 55.0f },
        { 20.0f, 12.0f },
        transparentViewportStyle,
        Renderer2DPrimitive::eRectangle);
    registry.emplace<Parent2DComponent>(
        scrollViewport,
        Parent2DComponent { panel });
    registry.emplace<ScrollInputComponent>(scrollViewport);
    registry.emplace<HitRegion2DComponent>(scrollViewport);
    ui_set_stretch_dynamics_blocker(scene, scrollViewport);
    registry.get<Transform2DComponent>(scrollViewport).origin = { 0.0f, 0.0f };

    bool inwardReleased = false;
    StretchDynamicsOptions options = {};
    options.requireLiquidGlass = false;
    options.dragResistance = 0.5f;
    options.maximumStretch = 0.5f;
    options.maximumCompression = 0.5f;
    options.dragDeadZone = 0.0f;
    options.stretchSelf = true;
    options.verticalAnchor = StretchDynamicsAnchor::eMinimum;
    options.inwardDirection = { 0.0f, -1.0f };
    options.inwardReleaseDistance = 30.0f;
    options.inwardBlurRadius = 12.0f;
    options.inwardBlurSelf = false;
    options.onInwardRelease = [&inwardReleased](const StretchDynamicsEvent& event) {
        inwardReleased = event.inwardReleaseArmed;
    };
    ui_enable_stretch_dynamics(scene, panel, std::move(options));

    UiInputState state = {};
    bool passed = materialChecks;
    const Renderer2DFontAtlas fontAtlas;
    passed &= expect(
        scene.entity_at({ 75.0f, 60.0f }) == scrollViewport,
        "the nested scroll viewport should be the front-most hit");
    ui_handle_pointer_button(
        scene,
        fontAtlas,
        state,
        UiPointerButtonInput {
            true,
            { 75.0f, 60.0f },
            PointerButton::eLeft,
            0,
            UiInputAction::ePress,
            {}
        });
    passed &= expect(
        state.activeStretchDynamics == entt::null,
        "a scroll viewport should keep the parent stretch gesture inactive");
    passed &= expect(
        ui_begin_stretch_dynamics(scene, state, panel, { 110.0f, 35.0f }),
        "an enabled entity should begin stretching on press");
    passed &= expect(
        ui_update_stretch_dynamics_drag(
            scene,
            state,
            true,
            true,
            { 110.0f, 75.0f }),
        "a top-edge press should be able to stretch the anchored surface downward");
    const glm::vec4 downwardPanelRect =
        ui_entity_visual_framebuffer_rect(registry, panel);
    passed &= expect(
        close(downwardPanelRect.y, 30.0f) &&
            close(downwardPanelRect.w, 70.0f),
        "downward stretching must preserve the authored top edge");
    passed &= expect(
        ui_update_stretch_dynamics_drag(
            scene,
            state,
            true,
            true,
            { 60.0f, 35.0f }),
        "an active stretch should follow pointer movement");

    const StretchDynamicsComponent& compressedDynamics =
        registry.get<StretchDynamicsComponent>(panel);
    passed &= expect(
        close(compressedDynamics.scale.x, 0.75f) &&
            close(compressedDynamics.scaleOrigin.x, 20.0f),
        "dragging a right-edge press left should compress inward from the fixed left edge");
    passed &= expect(
        ui_update_stretch_dynamics_drag(
            scene,
            state,
            true,
            true,
            { 160.0f, -5.0f }),
        "the grabbed edge should continue following a reversed drag");

    const StretchDynamicsComponent& dynamics =
        registry.get<StretchDynamicsComponent>(panel);
    passed &= expect(
        close(dynamics.scale.x, 1.25f) &&
            close(dynamics.scale.y, 0.6f),
        "outward and inward movement should become bounded per-axis deformation");
    passed &= expect(
        close(dynamics.scaleOrigin.x, 20.0f) &&
            close(dynamics.scaleOrigin.y, 30.0f),
        "a minimum vertical anchor should keep the top fixed even for a top-edge press");
    passed &= expect(
        ui_update_stretch_dynamics_drag(
            scene,
            state,
            true,
            true,
            { 60.0f, -5.0f }),
        "a reversed drag should transfer to the opposite horizontal edge");
    const StretchDynamicsComponent& reversedDynamics =
        registry.get<StretchDynamicsComponent>(panel);
    const glm::vec4 reversedPanelRect =
        ui_entity_visual_framebuffer_rect(registry, panel);
    passed &= expect(
        close(reversedDynamics.scale.x, 1.25f) &&
            close(reversedDynamics.scaleOrigin.x, 120.0f) &&
            close(reversedPanelRect.x, -5.0f) &&
            close(reversedPanelRect.x + reversedPanelRect.z, 120.0f),
        "crossing through rest after an outward drag should stretch the left edge leftward");
    passed &= expect(
        ui_update_stretch_dynamics_drag(
            scene,
            state,
            true,
            true,
            { 160.0f, -5.0f }),
        "the same press should be able to transfer back to the right edge");
    passed &= expect(
        close(dynamics.inwardProgress, 1.0f) &&
            close(dynamics.blurRadius, 12.0f),
        "an inward threshold should arm and reach its configured blur");

    const glm::vec4 authoredChildRect =
        ui_entity_framebuffer_rect(registry, child);
    passed &= expect(
        close(authoredChildRect.x, 45.0f) &&
            close(authoredChildRect.y, 42.0f) &&
            close(authoredChildRect.z, 20.0f) &&
            close(authoredChildRect.w, 10.0f),
        "interactive deformation should not feed back into authored layout bounds");
    const glm::vec4 childRect =
        ui_entity_visual_framebuffer_rect(registry, child);
    passed &= expect(
        close(childRect.x, 51.25f) &&
            close(childRect.y, 37.2f) &&
            close(childRect.z, 25.0f) &&
            close(childRect.w, 6.0f),
        "descendant position and size should inherit the parent deformation");
    const glm::vec4 panelRect =
        ui_entity_visual_framebuffer_rect(registry, panel);
    passed &= expect(
        close(panelRect.x, 20.0f) && close(panelRect.y, 30.0f) &&
            close(panelRect.z, 125.0f) && close(panelRect.w, 30.0f),
        "the interface shell should stretch with its content while its top remains anchored");
    const glm::vec2 childEffects =
        renderer2d_interactive_visual_effects(registry, child);
    passed &= expect(
        close(childEffects.x, 12.0f),
        "descendants should inherit the inward foreground blur");
    passed &= expect(
        close(renderer2d_interactive_visual_effects(registry, panel).x, 0.0f),
        "a stretching shell should be able to exclude itself from inward blur");

    passed &= expect(
        ui_release_stretch_dynamics(scene, state, true, { 160.0f, -5.0f }),
        "releasing beyond the inward threshold should consume the release");
    passed &= expect(
        inwardReleased && state.activeStretchDynamics == entt::null &&
            registry.get<StretchDynamicsComponent>(panel).settling,
        "inward release should invoke its callback and begin spring settling");

    LayoutScale scale = {};
    scale.factor = { 1.0f, 1.0f };
    scale.contentScale = scale.factor;
    UiBuilder ui(scene, scale);
    const entt::entity root = ui.root();
    UiIconButtonOptions buttonOptions = {};
    buttonOptions.stretchDynamics = StretchDynamicsOptions {};
    const UiControlHandle button = ui_create_icon_button(
        ui,
        root,
        UiAlignment::eTopLeft,
        { 0.0f, 0.0f },
        { 32.0f, 32.0f },
        1,
        0u,
        std::move(buttonOptions));
    passed &= expect(
        registry.all_of<StretchDynamicsComponent, InteractiveVisual2DComponent>(
            button.root),
        "icon-button options should expose the engine stretch capability");

    {
        float reference = 0.0f;
        float referenceVelocity = 0.0f;
        for (int rate : { 30, 60, 144 })
        {
            float value = 1.10f;
            float velocity = 0.0f;
            for (int step = 0; step < rate / 6; ++step)
                value = ui_stretch_dynamics_spring_step(value, velocity, 1.0f,
                    1.0 / rate, 0.28f, 0.54f);
            passed &= expect(value < 1.0f,
                "a released stretch must visibly bounce past its resting size");
            if (rate == 30) { reference = value; referenceVelocity = velocity; }
            else passed &= expect(std::abs(value - reference) < 0.00001f &&
                std::abs(velocity - referenceVelocity) < 0.0001f,
                "release bounce must agree at different display rates");
        }
        auto& dynamics = registry.get<StretchDynamicsComponent>(panel);
        dynamics.options.consumeClickAfterDrag = true;
        dynamics.options.inwardReleaseDistance = 0.0f;
        ui_begin_stretch_dynamics(scene, state, panel, { 60.0f, 45.0f });
        ui_update_stretch_dynamics_drag(scene, state, true, true, { 90.0f, 45.0f });
        passed &= expect(ui_release_stretch_dynamics(scene, state, true, { 90.0f, 45.0f }),
            "a real stretch drag must consume its release click so the return bounce remains visible");
    }

    {
        Renderer2DScene boundsScene;
        auto& registry = boundsScene.registry();
        const auto parent = boundsScene.create_shape({ 0.0f, 0.0f }, { 100.0f, 60.0f }, make_solid_style("#202020FF"));
        const auto child = boundsScene.create_shape({ 20.0f, 20.0f }, { 40.0f, 20.0f }, make_solid_style("#FFFFFF10"));
        registry.get<Transform2DComponent>(parent).origin = glm::vec2(0.0f);
        registry.get<Transform2DComponent>(child).origin = glm::vec2(0.0f);
        registry.emplace<Parent2DComponent>(child, Parent2DComponent { parent });
        StretchDynamicsOptions options {};
        options.requireLiquidGlass = false;
        options.constrainToParent = true;
        options.maximumStretch = 3.0f;
        options.dragResistance = 10.0f;
        ui_enable_stretch_dynamics(boundsScene, child, options);
        UiInputState input {};
        ui_begin_stretch_dynamics(boundsScene, input, child, { 50.0f, 35.0f });
        ui_update_stretch_dynamics_drag(boundsScene, input, true, true, { 500.0f, 500.0f });
        const auto visual = ui_entity_visual_framebuffer_rect(registry, child);
        passed &= expect(visual.x >= 0.0f && visual.y >= 0.0f &&
            visual.x + visual.z <= 100.01f && visual.y + visual.w <= 60.01f,
            "a constrained glass control must never stretch beyond its enclosing interface");
    }

    {
        float reference = 0.0f;
        for (const unsigned rate : { 60u, 120u, 240u })
        {
            Renderer2DScene fluidScene;
            const auto surface = fluidScene.create_shape({}, { 100.0f, 30.0f }, make_solid_style("#FFFFFF20"));
            StretchDynamicsOptions fluid;
            fluid.requireLiquidGlass = false;
            fluid.dragResponse = 0.10f;
            fluid.grabScale = 1.15f;
            fluid.returnDampingFraction = 0.78f;
            auto& dynamics = ui_enable_stretch_dynamics(fluidScene, surface, fluid);
            UiInputState input;
            ui_begin_stretch_dynamics(fluidScene, input, surface, { 50.0f, 15.0f });
            passed &= expect(dynamics.scale == glm::vec2(1.0f),
                "A fluid glass press must animate from its current size without an instantaneous expansion");
            ui_update_stretch_dynamics_settling(fluidScene, input, 1.0);
            for (unsigned frame = 1u; frame <= rate / 10u; ++frame)
                ui_update_stretch_dynamics_settling(fluidScene, input, 1.0 + double(frame) / rate);
            if (rate == 60u) reference = dynamics.scale.x;
            passed &= expect(dynamics.scale.x > 1.14f && dynamics.scale.x < 1.16f &&
                std::abs(dynamics.scale.x - reference) < 0.0001f,
                "The fluid glass grab must follow the same spring at each display rate");
            const auto previous = dynamics.scale;
            ui_update_stretch_dynamics_drag(fluidScene, input, true, true, { 80.0f, 15.0f });
            passed &= expect(dynamics.scale == previous && dynamics.targetScale.x > dynamics.scale.x,
                "Pointer events must update the deformation target without snapping the rendered shape");
            ui_update_stretch_dynamics_settling(fluidScene, input, 1.12);
            ui_release_stretch_dynamics(fluidScene, input, true, { 80.0f, 15.0f });
            ui_update_stretch_dynamics_settling(fluidScene, input, 1.14);
            const auto releaseScale = dynamics.scale;
            const auto releaseVelocity = dynamics.scaleVelocity;
            ui_begin_stretch_dynamics(fluidScene, input, surface, { 50.0f, 15.0f });
            passed &= expect(dynamics.scale == releaseScale && dynamics.scaleVelocity == releaseVelocity,
                "Regrabbing a returning control must preserve its shape and momentum");
            ui_release_stretch_dynamics(fluidScene, input, true, { 50.0f, 15.0f });
            for (unsigned frame = 0u; frame <= rate; ++frame)
                ui_update_stretch_dynamics_settling(fluidScene, input, 2.0 + double(frame) / rate);
            passed &= expect(dynamics.scale == glm::vec2(1.0f) && !dynamics.settling,
                "A fluid glass control must return fully to rest after interruption");
        }
    }
    {
        Renderer2DScene liquidScene;
        auto& registry = liquidScene.registry();
        const auto lens = liquidScene.create_shape({ 40.0f, 40.0f }, { 180.0f, 80.0f },
            make_solid_style("#FFFFFF10"), Renderer2DPrimitive::eRoundedRectangle);
        registry.get<Transform2DComponent>(lens).origin = {};
        registry.emplace<LiquidGlassComponent>(lens).material = GlassMaterial::eLiquid;
        StretchDynamicsOptions options;
        options.consumeClickAfterDrag = true;
        options.returnDampingFraction = 0.64f;
        auto& dynamics = ui_enable_stretch_dynamics(liquidScene, lens, options);
        UiInputState input;
        const glm::vec2 press { 100.0f, 80.0f };
        ui_begin_stretch_dynamics(liquidScene, input, lens, press);
        passed &= expect(dynamics.scale == glm::vec2(1.0f),
            "A liquid press must lift from the existing geometry without snapping");
        ui_update_stretch_dynamics_settling(liquidScene, input, 1.0);
        for (unsigned frame = 1u; frame <= 30u; ++frame)
            ui_update_stretch_dynamics_settling(liquidScene, input, 1.0 + frame / 120.0);
        passed &= expect(dynamics.scale.x > 1.01f && dynamics.scale.y > 1.035f && dynamics.scale.y < 1.045f &&
            registry.all_of<LiquidGlassContact2DComponent>(lens),
            "Pressing a lens must lift its cap and form a feathered contact light");
        const auto beforePull = dynamics.scale;
        ui_update_stretch_dynamics_drag(liquidScene, input, true, true, press + glm::vec2(90.0f, 0.0f));
        passed &= expect(dynamics.scale == beforePull && dynamics.targetOffset.x > 0.0f &&
            dynamics.targetScale.x > beforePull.x && dynamics.targetScale.y < beforePull.y,
            "A pull must move and elongate the lens towards the pointer without snapping");
        for (unsigned frame = 1u; frame <= 30u; ++frame)
            ui_update_stretch_dynamics_settling(liquidScene, input, 1.25 + frame / 120.0);
        const auto rect = ui_entity_visual_framebuffer_rect(registry, lens);
        const auto& contact = registry.get<LiquidGlassContact2DComponent>(lens);
        passed &= expect(contact.radius >= std::min(rect.z, rect.w) * 2.3f && contact.opacity <= 0.16f,
            "Small lenses must use a broad, low-intensity feather rather than a bright contact blob");
        passed &= expect(close(contact.point.x * rect.z + rect.x, press.x + 90.0f, 0.01f),
            "The feathered light must stay under the pointer as the lens deforms");
        const auto plan = liquidScene.build_render_plan(1.5, 0u);
        unsigned highlights = 0u;
        for (const auto& batch : plan.shapes)
            if (batch.entity == lens && (batch.flags & eRenderer2DStyleLiquidContact) != 0u) ++highlights;
        passed &= expect(highlights == 1u,
            "One clipped contact light must be emitted for an active lens");
        const auto offsetBeforeReverse = dynamics.offset;
        ui_update_stretch_dynamics_drag(liquidScene, input, true, true, press - glm::vec2(90.0f, 0.0f));
        passed &= expect(dynamics.offset == offsetBeforeReverse && dynamics.targetOffset.x < 0.0f,
            "Reversing a pull must change the target without swapping fixed edges or teleporting the lens");
        const float moderatePull = std::abs(dynamics.targetOffset.x);
        ui_update_stretch_dynamics_drag(liquidScene, input, true, true, press + glm::vec2(100000.0f, 0.0f));
        passed &= expect(dynamics.targetOffset.x > moderatePull && dynamics.targetOffset.x < 80.0f * 0.08f &&
            std::isfinite(dynamics.targetScale.x),
            "Very large pulls must approach a finite rubber-band limit continuously");
        passed &= expect(ui_release_stretch_dynamics(liquidScene, input, true, press),
            "A stretched button must consume its drag release instead of activating its click");
        ui_update_stretch_dynamics_settling(liquidScene, input, 1.52);
        const auto returningScale = dynamics.scale;
        const auto returningOffset = dynamics.offset;
        const auto returningVelocity = dynamics.offsetVelocity;
        ui_begin_stretch_dynamics(liquidScene, input, lens, press);
        passed &= expect(dynamics.scale == returningScale && dynamics.offset == returningOffset &&
            dynamics.offsetVelocity == returningVelocity,
            "Regrabbing a lens must retain both its shape and translational momentum");
        ui_release_stretch_dynamics(liquidScene, input, true, press);
        for (unsigned frame = 1u; frame <= 240u; ++frame)
            ui_update_stretch_dynamics_settling(liquidScene, input, 1.52 + frame / 120.0);
        passed &= expect(dynamics.scale == glm::vec2(1.0f) && dynamics.offset == glm::vec2(0.0f) &&
            !dynamics.settling && !registry.any_of<LiquidGlassContact2DComponent>(lens),
            "Released glass must finish its return and remove all contact lighting");
        const auto generation = liquidScene.frame_generation();
        ui_update_stretch_dynamics_settling(liquidScene, input, 4.0);
        passed &= expect(generation == liquidScene.frame_generation(),
            "A settled lens must not keep requesting redraws");
    }
    {
        glm::vec2 referenceScale {}, referenceOffset {};
        float referenceContact = 0.0f;
        for (const unsigned rate : { 30u, 60u, 120u, 240u })
        {
            Renderer2DScene scene;
            const auto lens = scene.create_shape({}, { 80.0f, 80.0f });
            scene.registry().get<Transform2DComponent>(lens).origin = {};
            scene.registry().emplace<LiquidGlassComponent>(lens).material = GlassMaterial::eLiquid;
            auto& dynamics = ui_enable_stretch_dynamics(scene, lens);
            UiInputState input;
            ui_begin_stretch_dynamics(scene, input, lens, { 40.0f, 40.0f });
            ui_update_stretch_dynamics_drag(scene, input, true, true, { 85.0f, 62.0f });
            ui_update_stretch_dynamics_settling(scene, input, 1.0);
            for (unsigned frame = 1u; frame <= rate / 5u; ++frame)
                ui_update_stretch_dynamics_settling(scene, input, 1.0 + double(frame) / rate);
            if (rate == 30u)
            {
                referenceScale = dynamics.scale;
                referenceOffset = dynamics.offset;
                referenceContact = dynamics.contact;
            }
            passed &= expect(glm::length(dynamics.scale - referenceScale) < 0.0001f &&
                glm::length(dynamics.offset - referenceOffset) < 0.0001f &&
                std::abs(dynamics.contact - referenceContact) < 0.0001f,
                "Liquid lift, pull and contact lighting must agree from 30 to 240 Hz");
            scene.registry().get<LiquidGlassComponent>(lens).material = GlassMaterial::eOff;
            ui_update_stretch_dynamics_settling(scene, input, 1.3);
            passed &= expect(dynamics.offset == glm::vec2(0.0f) && dynamics.scale == glm::vec2(1.0f) &&
                !scene.registry().any_of<LiquidGlassContact2DComponent>(lens) && input.activeStretchDynamics == entt::null,
                "Disabling glass during capture must clear translation, deformation and light immediately");
        }
    }
    return passed ? 0 : 1;
}

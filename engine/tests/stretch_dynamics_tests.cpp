#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/interactions.h>

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
    bool passed = true;
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

    return passed ? 0 : 1;
}

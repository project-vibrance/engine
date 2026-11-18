// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/ui/modal_lifecycle.h>
#include <vibranceUI/engine.h>
#include <vibranceUI/ui/input.h>

namespace
{
bool within(const entt::registry& registry, entt::entity entity, entt::entity root)
{
    for (std::size_t depth = 0u; registry.valid(entity) && depth < 256u; ++depth)
    {
        if (entity == root) return true;
        const auto* parent = registry.try_get<Parent2DComponent>(entity);
        if (!parent || parent->parent == entity) break;
        entity = parent->parent;
    }
    return false;
}

template <typename Component>
void block_inputs(Renderer2DScene& scene, entt::entity dim, entt::entity card, bool all,
    const std::function<bool(entt::entity)>& allow,
    std::vector<std::function<void(entt::registry&)>>& restore)
{
    auto& registry = scene.registry();
    for (const auto entity : registry.view<Component>())
    {
        if (!all && (within(registry, entity, card) || within(registry, entity, dim) || (allow && allow(entity)))) continue;
        if constexpr (std::is_same_v<Component, KeyboardShortcutComponent>)
            if (!all && within(registry, card, entity)) continue;
        auto& input = registry.get<Component>(entity);
        const bool enabled = input.enabled;
        bool focused = false;
        if constexpr (std::is_same_v<Component, TextInputComponent>) focused = input.focused;
        restore.emplace_back([entity, enabled, focused](entt::registry& target) {
            if (auto* value = target.try_get<Component>(entity))
            {
                value->enabled = enabled;
                if constexpr (std::is_same_v<Component, TextInputComponent>) value->focused = focused;
            }
        });
        input.enabled = false;
        if constexpr (requires { input.hovered; }) input.hovered = false;
        if constexpr (requires { input.focused; }) input.focused = false;
        if constexpr (requires { input.dragging; }) input.dragging = false;
        if constexpr (std::is_same_v<Component, ButtonInputComponent>)
            input.leftPressed = input.rightPressed = false;
        scene.mark_dirty(entity);
    }
}

void block_all(Renderer2DScene& scene, entt::entity dim, entt::entity card, bool all,
    const std::function<bool(entt::entity)>& allow,
    std::vector<std::function<void(entt::registry&)>>& restore)
{
    block_inputs<ButtonInputComponent>(scene, dim, card, all, allow, restore);
    block_inputs<TextInputComponent>(scene, dim, card, all, allow, restore);
    block_inputs<DropTargetComponent>(scene, dim, card, all, allow, restore);
    block_inputs<ScrollInputComponent>(scene, dim, card, all, allow, restore);
    block_inputs<ScrollBarInputComponent>(scene, dim, card, all, allow, restore);
    block_inputs<SliderInputComponent>(scene, dim, card, all, allow, restore);
    block_inputs<ResizablePanelComponent>(scene, dim, card, all, allow, restore);
    block_inputs<DragHandle2DComponent>(scene, dim, card, all, allow, restore);
    block_inputs<KeyboardShortcutComponent>(scene, dim, card, all, allow, restore);
    block_inputs<OutsidePointerInputComponent>(scene, dim, card, all, allow, restore);
    block_inputs<UiHoverRegionComponent>(scene, dim, card, all, allow, restore);
}
}

void UiModalLifecycle::show(Renderer2DScene& scene, entt::entity dimEntity, entt::entity cardEntity,
    bool animate, const std::function<bool(entt::entity)>& allowBackgroundInput)
{
    reset();
    dim = dimEntity;
    card = cardEntity;
    if (!scene.registry().valid(dim) || !scene.registry().valid(card)) return;
    block_all(scene, dim, card, false, allowBackgroundInput, inputRestorations);
    if (!animate) return;
    DisplayTransition2DComponent entrance;
    entrance.durationSeconds = .18f;
    entrance.fromOpacity = 0.0f;
    entrance.toOpacity = 1.0f;
    entrance.curve = DisplayTransitionCurve2D::eEaseOut;
    scene.play_display_transition(dim, entrance, 0.0);
    entrance.inheritToChildren = true;
    entrance.durationSeconds = .24f;
    entrance.fromScale = glm::vec2(.86f);
    entrance.toScale = glm::vec2(1.0f);
    scene.play_display_transition(card, entrance, 0.0);
}

bool UiModalLifecycle::begin_exit(Renderer2DScene& scene)
{
    if (closing) return true;
    if (!scene.registry().valid(dim) || !scene.registry().valid(card)) return false;
    closing = true;
    awaitingPresentation = false;
    block_all(scene, dim, card, true, {}, inputRestorations);
    DisplayTransition2DComponent exit;
    exit.durationSeconds = .18f;
    exit.fromOpacity = 1.0f;
    exit.toOpacity = 0.0f;
    exit.removeWhenComplete = false;
    exit.curve = DisplayTransitionCurve2D::eEaseIn;
    scene.play_display_transition(dim, exit, 0.0);
    exit.inheritToChildren = true;
    exit.durationSeconds = .20f;
    exit.fromScale = glm::vec2(1.0f);
    exit.toScale = glm::vec2(.88f);
    scene.play_display_transition(card, exit, 0.0);
    return true;
}

bool UiModalLifecycle::exit_ready(Engine& engine)
{
    if (!closing) return false;
    auto& scene = engine.renderer2d_scene();
    if (!scene.registry().valid(dim) || !scene.registry().valid(card)) return true;
    if (!awaitingPresentation)
    {
        if (!scene.display_transition_finished(dim) || !scene.display_transition_finished(card)) return false;
        const auto dimVisual = scene.last_resolved_shape_visual_state(dim);
        const auto cardVisual = scene.last_resolved_shape_visual_state(card);
        if (!dimVisual || !cardVisual || dimVisual->opacity > 0.0f || cardVisual->opacity > 0.0f) return false;
        awaitingPresentation = true;
    }
    return !engine.poll_pending_presentation();
}

void UiModalLifecycle::restore_input(Renderer2DScene& scene)
{
    for (auto action = inputRestorations.rbegin(); action != inputRestorations.rend(); ++action) (*action)(scene.registry());
    if (!inputRestorations.empty()) scene.mark_dirty();
    reset();
}

void UiModalLifecycle::reset()
{
    dim = card = entt::null;
    closing = awaitingPresentation = false;
    inputRestorations.clear();
}

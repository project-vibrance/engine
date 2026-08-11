#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <limits>
#include <string>
#include <vector>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/renderer2d_components.h>

enum class PointerButton
{
    eLeft,
    eRight,
    eMiddle,
    eOther
};

struct InputModifiers
{
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool super = false;

    bool any() const
    {
        return shift || control || alt || super;
    }
};

struct PointerInputEvent
{
    // Event positions are already converted to framebuffer space before dispatch
    entt::entity target = entt::null;
    glm::vec2 framebufferPosition { 0.0f };
    PointerButton button = PointerButton::eOther;
    int platformButton = 0;
    InputModifiers modifiers {};
};

struct ScrollInputEvent
{
    entt::entity target = entt::null;
    glm::vec2 framebufferPosition { 0.0f };
    double offsetX = 0.0;
    double offsetY = 0.0;
    InputModifiers modifiers {};
};

struct FileDropInputEvent
{
    entt::entity target = entt::null;
    glm::vec2 framebufferPosition { 0.0f };
    std::vector<std::filesystem::path> paths;
};

struct SliderInputEvent
{
    entt::entity target = entt::null;
    glm::vec2 framebufferPosition { 0.0f };
    float value = 0.0f;
    float normalizedValue = 0.0f;
};

struct ButtonInputComponent
{
    // Behaviour state for anything clickable, including transparent hit areas
    bool enabled = true;
    bool hovered = false;
    bool leftPressed = false;
    bool rightPressed = false;
    std::function<void(const PointerInputEvent&)> onPress;
    std::function<void(const PointerInputEvent&)> onRelease;
    std::function<void(const PointerInputEvent&)> onClick;
    std::function<void(const PointerInputEvent&)> onRightClick;
    std::function<void(bool)> onHoverChanged;
};

struct ButtonVisualComponent
{
    ShapeStyleComponent idle;
    ShapeStyleComponent hovered;
    ShapeStyleComponent pressed;
    ShapeStyleComponent disabled;
    bool hasDisabled = false;
};

struct TextInputComponent
{
    // Text fields keep both the full value and the visible byte offset
    entt::entity textEntity = entt::null;
    entt::entity caretEntity = entt::null;
    std::string value;
    std::string placeholder;
    std::size_t maxBytes = 256u;
    std::size_t displayStartByte = 0u;
    float textPaddingRight = 10.0f;
    float caretWidth = 1.5f;
    float caretHeight = 16.0f;
    float caretBlinkPeriodSeconds = 1.0f;
    bool enabled = true;
    bool focused = false;
    bool multiline = false;
    std::function<void(const std::string&)> onChanged;
};

struct TextInputVisualComponent
{
    ShapeStyleComponent idle;
    ShapeStyleComponent focused;
    ShapeStyleComponent disabled;
    TextStyleComponent valueText;
    TextStyleComponent placeholderText;
    ShapeStyleComponent caretStyle;
    bool hasDisabled = false;
    std::size_t maxDisplayCharacters = 28u;
};

struct DropTargetComponent
{
    bool enabled = true;
    bool hovered = false;
    std::vector<std::filesystem::path> lastDroppedPaths;
    std::function<void(const FileDropInputEvent&)> onDrop;
};

struct ScrollInputComponent
{
    // Scroll offset is owned by the viewport, while callers move their content
    bool enabled = true;
    bool hovered = false;
    float offset = 0.0f;
    float minOffset = 0.0f;
    float maxOffset = 0.0f;
    float step = 48.0f;
    std::function<void(const ScrollInputEvent&)> onScroll;
};

struct ScrollBarInputComponent
{
    entt::entity scrollTarget = entt::null;
    entt::entity thumb = entt::null;
    bool enabled = true;
    bool hovered = false;
    bool dragging = false;
    bool wakeRequested = true;
    bool fadeWhenIdle = true;
    float dragGrabOffsetY = 0.0f;
    float visibleOpacity = 1.0f;
    float hiddenOpacity = 0.0f;
    float idleDelaySeconds = 0.85f;
    float fadeDurationSeconds = 0.22f;
    double lastActiveSeconds = -1000000.0;
};

struct SliderInputComponent
{
    entt::entity maskEntity = entt::null;
    entt::entity fillEntity = entt::null;
    entt::entity thumbEntity = entt::null;
    entt::entity labelEntity = entt::null;
    bool enabled = true;
    bool hovered = false;
    bool dragging = false;
    // When true the slider visually dims when `enabled == false`
    // Set to false to keep the slider visuals bright even when disabled
    bool dimWhenDisabled = true;
    float value = 0.0f;
    float visualValue = 0.0f;
    float minValue = 0.0f;
    float maxValue = 1.0f;
    float step = 0.0f;
    float normalisedStep = 0.0f;
    float trackHeight = 0.0f;
    float hoveredTrackHeight = 0.0f;
    glm::vec2 thumbSize { 0.0f };
    glm::vec2 hoveredThumbSize { 0.0f };
    float hoverExpansion = 0.0f;
    float hoverExpansionRate = 20.0f;
    // Smooth scrubbing lets the visual thumb chase the real value without delaying callbacks
    bool smoothScrubbing = false;
    float visualSmoothingRate = 24.0f;
    double lastVisualUpdateSeconds = 0.0;
    std::function<void(const SliderInputEvent&)> onChanged;
    // Fired once when pointer scrubbing ends. Use this for expensive or
    // externally observable operations such as media seeking.
    std::function<void(const SliderInputEvent&)> onCommitted;
};

struct DisabledVisualComponent
{
    bool dimWhenDisabled = true;
    bool useUnavailableCursor = false;
};

enum PanelResizeEdgeFlags : uint32_t
{
    ePanelResizeNone = 0u,
    ePanelResizeLeft = 1u << 0u,
    ePanelResizeRight = 1u << 1u,
    ePanelResizeTop = 1u << 2u,
    ePanelResizeBottom = 1u << 3u,
    ePanelResizeAll = ePanelResizeLeft | ePanelResizeRight | ePanelResizeTop | ePanelResizeBottom
};

struct ResizablePanelComponent
{
    bool enabled = true;
    bool hovered = false;
    float edgeThickness = 10.0f;
    glm::vec2 minSize { 180.0f, 120.0f };
    glm::vec2 maxSize { 1000000.0f, 1000000.0f };
    uint32_t enabledEdges = ePanelResizeAll;
    uint32_t activeEdges = ePanelResizeNone;
};

struct KeyboardShortcutComponent
{
    int key = 0;
    bool shift = false;
    bool control = false;
    bool alt = false;
    bool super = false;
    bool requireExactModifiers = true;
    bool enabled = true;
    std::function<void()> onTriggered;
};

struct UiInputState
{
    // One shared state object resolves pointer capture, hover, focus and dragging
    entt::entity draggedPanel = entt::null;
    entt::entity resizingPanel = entt::null;
    entt::entity activeSlider = entt::null;
    entt::entity activeScrollBar = entt::null;
    entt::entity pressedInputEntity = entt::null;
    entt::entity pointerInputCapture = entt::null;
    entt::entity focusedTextInput = entt::null;
    entt::entity hoveredButton = entt::null;
    entt::entity hoveredDropTarget = entt::null;
    entt::entity hoveredScrollTarget = entt::null;
    entt::entity hoveredScrollBar = entt::null;
    entt::entity hoveredResizePanel = entt::null;
    entt::entity hoveredSlider = entt::null;
    glm::vec2 lastPanelDragPoint { 0.0f };
    glm::vec2 resizeStartPoint { 0.0f };
    glm::vec2 resizeStartMin { 0.0f };
    glm::vec2 resizeStartSize { 0.0f };
    glm::vec2 resizeStartLayoutOffset { 0.0f };
    uint32_t activeResizeEdges = ePanelResizeNone;

    void clear_capture()
    {
        activeSlider = entt::null;
        activeScrollBar = entt::null;
        pressedInputEntity = entt::null;
        pointerInputCapture = entt::null;
        draggedPanel = entt::null;
        resizingPanel = entt::null;
        activeResizeEdges = ePanelResizeNone;
    }

    void clear_hover()
    {
        hoveredButton = entt::null;
        hoveredDropTarget = entt::null;
        hoveredScrollTarget = entt::null;
        hoveredResizePanel = entt::null;
        hoveredSlider = entt::null;
    }

    void clear_focus()
    {
        focusedTextInput = entt::null;
    }
};

// Filters high-rate pointer jitter before it churns cached UI layers
constexpr float kUiPointerMovementEpsilon = 0.25f;
constexpr float kUiPointerMovementEpsilonSquared = kUiPointerMovementEpsilon * kUiPointerMovementEpsilon;

inline bool ui_has_meaningful_delta(glm::vec2 delta)
{
    return glm::dot(delta, delta) > kUiPointerMovementEpsilonSquared;
}

inline bool ui_has_meaningful_delta(glm::vec2 delta, float epsilon)
{
    return glm::dot(delta, delta) > epsilon * epsilon;
}

inline void ui_set_timed_moving_cache(Renderer2DScene& scene, entt::entity entity)
{
    entt::registry& registry = scene.registry();
    if (entity == entt::null || !registry.valid(entity))
    {
        return;
    }

    Renderer2DCacheComponent& cache = registry.get_or_emplace<Renderer2DCacheComponent>(entity);
    if (cache.mode != Renderer2DCacheMode::eStatic)
    {
        return;
    }

    cache.mode = Renderer2DCacheMode::eTimed;
    cache.idleTickRate = 0.0f;
    cache.activeTickRate = 60.0f;
    cache.propagateToChildren = true;
    cache.restoreStaticWhenIdle = true;
}

inline void ui_mark_moving_entity_dirty(
    Renderer2DScene& scene,
    entt::entity entity,
    float activeSeconds = 1.0f / 30.0f)
{
    if (entity == entt::null || !scene.registry().valid(entity))
    {
        return;
    }

    ui_set_timed_moving_cache(scene, entity);
    scene.activate_dynamic(entity, std::max(activeSeconds, 1.0f / 60.0f));
    scene.mark_dirty(entity);
}

inline void ui_mark_direct_manipulation_dirty(
    Renderer2DScene& scene,
    entt::entity entity,
    float activeSeconds = 1.0f / 30.0f)
{
    if (entity == entt::null || !scene.registry().valid(entity))
    {
        return;
    }

    // Pointer-driven movement explicitly marks every visible state. It does
    // not need a time-based tick in between pointer samples; leaving one active
    // submits duplicate frames while a thumb is merely held in place.
    entt::registry& registry = scene.registry();
    Renderer2DCacheComponent& cache =
        registry.get_or_emplace<Renderer2DCacheComponent>(entity);
    // Direct pointer input can arrive at 500-1000 Hz. A 144 Hz visible ceiling
    // remains fluid on high-refresh displays while preventing an expensive
    // scroll subtree from saturating low-end GPUs. Displays below it retain
    // their native cadence, and non-interactive animation is unaffected.
    cache.activeFrameRateLimit = 144u;
    if (cache.mode == Renderer2DCacheMode::eStatic)
    {
        cache.mode = Renderer2DCacheMode::eTimed;
        cache.idleTickRate = 0.0f;
        cache.activeTickRate = 0.0f;
        cache.propagateToChildren = true;
        cache.restoreStaticWhenIdle = true;
    }
    scene.activate_dynamic(entity, std::max(activeSeconds, 1.0f / 60.0f));
    scene.mark_dirty(entity);
}

inline bool ui_shortcut_matches(const KeyboardShortcutComponent& shortcut, int key, const InputModifiers& modifiers)
{
    if (!shortcut.enabled || shortcut.key != key)
    {
        return false;
    }

    if (shortcut.requireExactModifiers)
    {
        return shortcut.shift == modifiers.shift &&
            shortcut.control == modifiers.control &&
            shortcut.alt == modifiers.alt &&
            shortcut.super == modifiers.super;
    }

    return (!shortcut.shift || modifiers.shift) &&
        (!shortcut.control || modifiers.control) &&
        (!shortcut.alt || modifiers.alt) &&
        (!shortcut.super || modifiers.super);
}

template<typename Component>
entt::entity ui_component_owner(const entt::registry& registry, entt::entity entity)
{
    entt::entity current = entity;
    for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
    {
        if (registry.all_of<Component>(current))
        {
            return current;
        }

        const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
        if (!parent || parent->parent == entt::null || parent->parent == current)
        {
            break;
        }
        current = parent->parent;
    }
    return entt::null;
}

inline bool ui_entity_visible_by_hierarchy(const entt::registry& registry, entt::entity entity)
{
    entt::entity current = entity;
    for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
    {
        if (const RenderLayer2DComponent* layer = registry.try_get<RenderLayer2DComponent>(current);
            layer && !layer->visible)
        {
            return false;
        }

        const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
        if (!parent || parent->parent == entt::null || parent->parent == current)
        {
            break;
        }
        current = parent->parent;
    }
    return true;
}

inline bool ui_has_input_component(const entt::registry& registry, entt::entity entity)
{
    return registry.all_of<ButtonInputComponent>(entity) ||
        registry.all_of<TextInputComponent>(entity) ||
        registry.all_of<DropTargetComponent>(entity) ||
        registry.all_of<ScrollInputComponent>(entity) ||
        registry.all_of<ScrollBarInputComponent>(entity) ||
        registry.all_of<SliderInputComponent>(entity);
}

inline bool ui_point_in_rect(glm::vec4 rect, glm::vec2 point)
{
    return rect.z > 0.0f &&
        rect.w > 0.0f &&
        point.x >= rect.x &&
        point.y >= rect.y &&
        point.x <= rect.x + rect.z &&
        point.y <= rect.y + rect.w;
}

inline glm::vec2 ui_entity_framebuffer_size(const entt::registry& registry, entt::entity entity)
{
    const Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
    if (!transform)
    {
        return glm::vec2(0.0f);
    }

    glm::vec2 size(0.0f);
    if (const ShapeComponent* shape = registry.try_get<ShapeComponent>(entity))
    {
        size = shape->size;
    }
    else if (const Media2DComponent* media = registry.try_get<Media2DComponent>(entity))
    {
        size = media->size;
    }
    else if (const Model3DComponent* model = registry.try_get<Model3DComponent>(entity))
    {
        size = model->size;
    }
    else if (const TextComponent* text = registry.try_get<TextComponent>(entity))
    {
        size = {
            std::max(text->bounds.x, static_cast<float>(text->text.size()) * text->fontSize * 0.55f),
            std::max(text->bounds.y, text->fontSize * 1.25f)
        };
    }

    return glm::max(size * transform->scale, glm::vec2(0.0f));
}

inline glm::vec4 ui_entity_framebuffer_rect(const entt::registry& registry, entt::entity entity)
{
    const Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
    if (!transform)
    {
        return glm::vec4(0.0f);
    }

    const glm::vec2 size = ui_entity_framebuffer_size(registry, entity);
    const glm::vec2 minPosition = transform->position - transform->origin * size;
    return { minPosition.x, minPosition.y, size.x, size.y };
}

inline uint32_t ui_panel_resize_edges_for(glm::vec4 rect, glm::vec2 point, const ResizablePanelComponent& resize)
{
    const float thickness = std::max(resize.edgeThickness, 1.0f);
    const glm::vec4 expanded {
        rect.x - thickness,
        rect.y - thickness,
        rect.z + thickness * 2.0f,
        rect.w + thickness * 2.0f
    };
    if (!ui_point_in_rect(expanded, point))
    {
        return ePanelResizeNone;
    }

    uint32_t edges = ePanelResizeNone;
    if ((resize.enabledEdges & ePanelResizeLeft) != 0u && std::abs(point.x - rect.x) <= thickness)
    {
        edges |= ePanelResizeLeft;
    }
    if ((resize.enabledEdges & ePanelResizeRight) != 0u && std::abs(point.x - (rect.x + rect.z)) <= thickness)
    {
        edges |= ePanelResizeRight;
    }
    if ((resize.enabledEdges & ePanelResizeTop) != 0u && std::abs(point.y - rect.y) <= thickness)
    {
        edges |= ePanelResizeTop;
    }
    if ((resize.enabledEdges & ePanelResizeBottom) != 0u && std::abs(point.y - (rect.y + rect.w)) <= thickness)
    {
        edges |= ePanelResizeBottom;
    }
    return edges;
}

inline bool ui_render_layer_above(const entt::registry& registry, entt::entity candidate, entt::entity current)
{
    if (current == entt::null)
    {
        return true;
    }

    const RenderLayer2DComponent* candidateLayer = registry.try_get<RenderLayer2DComponent>(candidate);
    const RenderLayer2DComponent* currentLayer = registry.try_get<RenderLayer2DComponent>(current);
    const bool candidateAlwaysOnTop = candidateLayer && candidateLayer->alwaysOnTop;
    const bool currentAlwaysOnTop = currentLayer && currentLayer->alwaysOnTop;
    const int32_t candidateLayerValue = candidateLayer ? candidateLayer->layer : 0;
    const uint32_t candidateOrderValue = candidateLayer ? candidateLayer->order : 0u;
    const int32_t currentLayerValue = currentLayer ? currentLayer->layer : 0;
    const uint32_t currentOrderValue = currentLayer ? currentLayer->order : 0u;

    if (candidateAlwaysOnTop != currentAlwaysOnTop)
    {
        return candidateAlwaysOnTop;
    }
    if (candidateLayerValue != currentLayerValue)
    {
        return candidateLayerValue > currentLayerValue;
    }
    if (candidateOrderValue != currentOrderValue)
    {
        return candidateOrderValue > currentOrderValue;
    }
    return entt::to_integral(candidate) > entt::to_integral(current);
}

inline entt::entity ui_input_owner_from_hit(const entt::registry& registry, entt::entity hit)
{
    entt::entity current = hit;
    for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
    {
        if (ui_has_input_component(registry, current))
        {
            return current;
        }

        const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
        if (!parent || parent->parent == entt::null || parent->parent == current)
        {
            break;
        }
        current = parent->parent;
    }
    return entt::null;
}

inline void ui_set_entity_framebuffer_size(entt::registry& registry, entt::entity entity, glm::vec2 size)
{
    Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
    if (!transform)
    {
        return;
    }

    const glm::vec2 safeScale {
        std::abs(transform->scale.x) > 0.0001f ? transform->scale.x : 1.0f,
        std::abs(transform->scale.y) > 0.0001f ? transform->scale.y : 1.0f
    };
    const glm::vec2 localSize = glm::max(size / safeScale, glm::vec2(0.0f));
    if (ShapeComponent* shape = registry.try_get<ShapeComponent>(entity))
    {
        shape->size = localSize;
    }
    else if (Media2DComponent* media = registry.try_get<Media2DComponent>(entity))
    {
        media->size = localSize;
    }
    else if (Model3DComponent* model = registry.try_get<Model3DComponent>(entity))
    {
        model->size = localSize;
    }
}

inline glm::vec2 ui_clamp_drag_delta_to_bounds(
    const entt::registry& registry,
    entt::entity entity,
    glm::vec2 delta,
    glm::vec2 bounds)
{
    const Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
    if (!transform || bounds.x <= 0.0f || bounds.y <= 0.0f)
    {
        return delta;
    }

    const glm::vec2 size = ui_entity_framebuffer_size(registry, entity);
    const glm::vec2 currentMin = transform->position - transform->origin * size;
    const glm::vec2 proposedMin = currentMin + delta;
    const glm::vec2 minAllowed = glm::min(glm::vec2(0.0f), bounds - size);
    const glm::vec2 maxAllowed = glm::max(glm::vec2(0.0f), bounds - size);
    const glm::vec2 clampedMin = glm::clamp(proposedMin, minAllowed, maxAllowed);
    return clampedMin - currentMin;
}

inline entt::entity ui_input_owner_at(Renderer2DScene& scene, glm::vec2 point)
{
    return ui_input_owner_from_hit(scene.registry(), scene.entity_at(point));
}

inline entt::entity ui_resizable_panel_at_from_hit(
    Renderer2DScene& scene,
    entt::entity hit,
    glm::vec2 point,
    uint32_t& edges)
{
    edges = ePanelResizeNone;
    entt::registry& registry = scene.registry();
    if (hit != entt::null)
    {
        entt::entity current = hit;
        for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
        {
            if (const ResizablePanelComponent* resize = registry.try_get<ResizablePanelComponent>(current);
                resize && resize->enabled && ui_entity_visible_by_hierarchy(registry, current))
            {
                const uint32_t candidateEdges = ui_panel_resize_edges_for(ui_entity_framebuffer_rect(registry, current), point, *resize);
                if (candidateEdges != ePanelResizeNone)
                {
                    edges = candidateEdges;
                    return current;
                }
            }

            const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent || parent->parent == entt::null || parent->parent == current)
            {
                break;
            }
            current = parent->parent;
        }

        return entt::null;
    }

    entt::entity topmost = entt::null;
    uint32_t topmostEdges = ePanelResizeNone;
    auto view = registry.view<const Transform2DComponent, const ResizablePanelComponent>();
    view.each([&](
        entt::entity entity,
        const Transform2DComponent&,
        const ResizablePanelComponent& resize) {
        if (!resize.enabled)
        {
            return;
        }
        if (!ui_entity_visible_by_hierarchy(registry, entity))
        {
            return;
        }

        const uint32_t candidateEdges = ui_panel_resize_edges_for(ui_entity_framebuffer_rect(registry, entity), point, resize);
        if (candidateEdges == ePanelResizeNone || !ui_render_layer_above(registry, entity, topmost))
        {
            return;
        }

        topmost = entity;
        topmostEdges = candidateEdges;
    });

    edges = topmostEdges;
    return topmost;
}

inline entt::entity ui_resizable_panel_at(Renderer2DScene& scene, glm::vec2 point, uint32_t& edges)
{
    return ui_resizable_panel_at_from_hit(scene, scene.entity_at(point), point, edges);
}

inline bool ui_append_codepoint(std::string& value, unsigned int codepoint, std::size_t maxBytes)
{
    if (codepoint < 32u || codepoint == 127u || value.size() >= maxBytes)
    {
        return false;
    }

    const char appended = codepoint < 128u ? static_cast<char>(codepoint) : '?';
    value.push_back(appended);
    if (value.size() > maxBytes)
    {
        value.resize(maxBytes);
    }
    return true;
}

inline bool ui_erase_previous_codepoint(std::string& value)
{
    if (value.empty())
    {
        return false;
    }

    do
    {
        const unsigned char byte = static_cast<unsigned char>(value.back());
        value.pop_back();
        if ((byte & 0x80u) == 0u || (byte & 0xC0u) == 0xC0u)
        {
            break;
        }
    }
    while (!value.empty());
    return true;
}

inline float ui_slider_normalized_value(const SliderInputComponent& slider)
{
    return (slider.maxValue > slider.minValue) ?
        (slider.value - slider.minValue) / (slider.maxValue - slider.minValue) : 0.0f;
}

inline float ui_slider_visual_normalized_value(const SliderInputComponent& slider)
{
    const float value = slider.smoothScrubbing ? slider.visualValue : slider.value;
    return (slider.maxValue > slider.minValue) ?
        (value - slider.minValue) / (slider.maxValue - slider.minValue) : 0.0f;
}

inline bool ui_set_slider_value_from_point(Renderer2DScene& scene, entt::entity entity, glm::vec2 point)
{
    if (entity == entt::null)
    {
        return false;
    }

    entt::registry& registry = scene.registry();
    SliderInputComponent* slider = registry.try_get<SliderInputComponent>(entity);
    if (!slider || !slider->enabled)
    {
        return false;
    }

    const glm::vec4 rect = ui_entity_framebuffer_rect(registry, entity);
    if (rect.z <= 0.0f)
    {
        return false;
    }

    float normalized = (point.x - rect.x) / rect.z;
    normalized = glm::clamp(normalized, 0.0f, 1.0f);

    if (slider->normalisedStep > 0.0f)
    {
        const float snap = std::clamp(slider->normalisedStep, 0.0001f, 1.0f);
        normalized = std::round(normalized / snap) * snap;
        normalized = glm::clamp(normalized, 0.0f, 1.0f);
    }

    float newValue = slider->minValue + normalized * (slider->maxValue - slider->minValue);
    if (slider->normalisedStep <= 0.0f && slider->step > 0.0f)
    {
        const float steps = std::round((newValue - slider->minValue) / slider->step);
        newValue = slider->minValue + steps * slider->step;
    }
    newValue = std::clamp(newValue, slider->minValue, slider->maxValue);

    const float prev = slider->value;
    slider->value = newValue;
    const bool changed = std::abs(prev - newValue) > 1e-6f;
    if (changed && !slider->smoothScrubbing)
    {
        slider->visualValue = newValue;
    }
    return changed;
}

inline void ui_bring_panel_to_front(Renderer2DScene& scene, entt::entity panel)
{
    entt::registry& registry = scene.registry();
    RenderLayer2DComponent* panelLayer = registry.try_get<RenderLayer2DComponent>(panel);
    if (!panelLayer)
    {
        return;
    }

    int32_t topLayer = panelLayer->layer;
    uint32_t topOrder = panelLayer->order;
    auto panelView = registry.view<const ShapeComponent, const LayoutRect2DComponent, const RenderLayer2DComponent>();
    panelView.each([&](
        entt::entity entity,
        const ShapeComponent&,
        const LayoutRect2DComponent&,
        const RenderLayer2DComponent& layer) {
        if (entity == panel || registry.all_of<Parent2DComponent>(entity) || !layer.visible)
        {
            return;
        }

        if (layer.layer > topLayer || (layer.layer == topLayer && layer.order > topOrder))
        {
            topLayer = layer.layer;
            topOrder = layer.order;
        }
    });

    if (panelLayer->layer > topLayer ||
        (panelLayer->layer == topLayer && panelLayer->order >= topOrder))
    {
        return;
    }

    panelLayer->layer = topLayer;
    panelLayer->order = topOrder == std::numeric_limits<uint32_t>::max() ? topOrder : topOrder + 1u;
    ui_mark_moving_entity_dirty(scene, panel);
}

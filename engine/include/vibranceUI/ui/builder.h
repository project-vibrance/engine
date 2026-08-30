#pragma once

#include <algorithm>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>
#include <entt/entt.hpp>
#include <glm/glm.hpp>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/ui/input.h>
#include <vibranceUI/ui/layout.h>
#include <vibranceUI/ui/text.h>

class UiPlacement;

class UiBuilder
{
public:
    UiBuilder(Renderer2DScene& scene, const LayoutScale& scale = {}, bool activateTimedAnimations = true)
        : scene_(scene),
        registry_(scene.registry()),
        scale_(scale),
        activateTimedAnimations_(activateTimedAnimations)
    {
    }

    Renderer2DScene& scene()
    {
        return scene_;
    }

    entt::registry& registry()
    {
        return registry_;
    }

    const LayoutScale& scale() const
    {
        return scale_;
    }

    // Compact factories create renderer objects without also forcing a
    // placement strategy. Chain place(...) afterwards to author layout in
    // logical pixels.
    entt::entity root(
        int32_t layer = 0,
        uint32_t order = 0)
    {
        const glm::vec2 safeContentScale = glm::max(
            scale_.contentScale,
            glm::vec2(0.25f));
        ShapeStyleComponent transparentStyle = {};
        transparentStyle.set_color(glm::vec4(0.0f));
        transparentStyle.outlineColor = glm::vec4(0.0f);
        transparentStyle.opacity = 0.0f;
        entt::entity entity = scene_.create_shape(
            glm::vec2(0.0f),
            glm::max(scale_.logicalSize * safeContentScale, glm::vec2(1.0f)),
            transparentStyle,
            Renderer2DPrimitive::eRectangle);
        if (Transform2DComponent* transform =
                registry_.try_get<Transform2DComponent>(entity))
        {
            transform->position = glm::vec2(0.0f);
            transform->origin = glm::vec2(0.0f);
        }
        set_layer(entity, layer, order);
        return entity;
    }

    // A non-painting, input-transparent parent for positioning several freely
    // placed entities as one measured composition. Place and size the group,
    // then place children relative to it; unrelated window controls remain
    // free to use the original page or window root.
    entt::entity layout_group(
        int32_t layer = 0,
        uint32_t order = 0)
    {
        ShapeStyleComponent transparentStyle = {};
        transparentStyle.set_color(glm::vec4(0.0f));
        transparentStyle.outlineColor = glm::vec4(0.0f);
        transparentStyle.opacity = 0.0f;
        entt::entity entity = scene_.create_shape(
            glm::vec2(0.0f),
            glm::vec2(1.0f),
            transparentStyle,
            Renderer2DPrimitive::eRectangle);
        set_layer(entity, layer, order);
        registry_.emplace_or_replace<InputTransparent2DComponent>(entity);
        return entity;
    }

    entt::entity block(
        const ShapeStyleComponent& style,
        float cornerRadius = 0.0f,
        Renderer2DPrimitive primitive =
            Renderer2DPrimitive::eRoundedRectangle)
    {
        entt::entity entity = scene_.create_shape(
            glm::vec2(0.0f),
            glm::vec2(1.0f),
            style,
            primitive);
        set_shape(entity, cornerRadius);
        return entity;
    }

    entt::entity block(
        std::string_view color,
        float cornerRadius = 0.0f,
        Renderer2DPrimitive primitive =
            Renderer2DPrimitive::eRoundedRectangle)
    {
        ShapeStyleComponent style = {};
        style.set_color(color);
        style.outlineColor = glm::vec4(0.0f);
        return block(style, cornerRadius, primitive);
    }

    entt::entity text(
        std::string value,
        const Renderer2DFontAtlas& fontAtlas,
        float fontSize,
        const TextStyleComponent& style)
    {
        entt::entity entity = scene_.create_text(
            std::move(value),
            glm::vec2(0.0f),
            scaled_scalar(fontSize, scale_),
            style);
        apply_font_layout(fontAtlas, registry_, entity);
        return entity;
    }

    entt::entity text(
        std::string value,
        const Renderer2DFontAtlas& fontAtlas,
        float fontSize = 16.0f,
        std::string_view color = "#FFFFFFFF")
    {
        TextStyleComponent style = {};
        style.set_color(color);
        style.shadowColor = glm::vec4(0.0f);
        style.effectColor = glm::vec4(0.0f);
        return text(
            std::move(value),
            fontAtlas,
            fontSize,
            style);
    }

    entt::entity media(
        Media2DHandle handle,
        glm::vec2 logicalSize = glm::vec2(1.0f),
        Media2DFit fit = Media2DFit::eContain)
    {
        if (!handle.valid())
        {
            return entt::null;
        }
        return scene_.create_media(
            glm::vec2(0.0f),
            scaled_size(logicalSize.x, logicalSize.y, scale_),
            handle,
            fit);
    }

    UiPlacement place(entt::entity entity);

    RenderLayer2DComponent& set_layer(
        entt::entity entity,
        int32_t layer,
        uint32_t order,
        bool alwaysOnTop = false)
    {
        // Layer and order are the normal z-index, while alwaysOnTop escapes stacks
        RenderLayer2DComponent& renderLayer = registry_.get<RenderLayer2DComponent>(entity);
        renderLayer.layer = layer;
        renderLayer.order = order;
        renderLayer.alwaysOnTop = alwaysOnTop;
        return renderLayer;
    }

    ShapeComponent& set_shape(entt::entity entity, float cornerRadius, bool sdfEdges = true)
    {
        ShapeComponent& shape = registry_.get<ShapeComponent>(entity);
        shape.set_corner_radius(scaled_scalar(cornerRadius, scale_));
        shape.sdfEdges = sdfEdges;
        return shape;
    }

    ShapeComponent& set_shape_squircle(entt::entity entity, float amount = 1.0f, float power = 4.0f)
    {
        // Squircle amount blends from rounded rectangle to the softer iOS-style curve
        ShapeComponent& shape = registry_.get<ShapeComponent>(entity);
        shape.squircleAmount = std::clamp(amount, 0.0f, 1.0f);
        shape.squirclePower = std::clamp(power, 2.0f, 5.0f);
        return shape;
    }

    ShapeComponent& set_shape_corners(
        entt::entity entity,
        float topLeft,
        float topRight,
        float bottomRight,
        float bottomLeft,
        bool sdfEdges = true)
    {
        ShapeComponent& shape = registry_.get<ShapeComponent>(entity);
        shape.set_corner_radii(
            scaled_scalar(topLeft, scale_),
            scaled_scalar(topRight, scale_),
            scaled_scalar(bottomRight, scale_),
            scaled_scalar(bottomLeft, scale_));
        shape.sdfEdges = sdfEdges;
        return shape;
    }

    ShapeComponent& set_shape_left_corners(entt::entity entity, float radius, bool sdfEdges = true)
    {
        return set_shape_corners(entity, radius, 0.0f, 0.0f, radius, sdfEdges);
    }

    ShapeComponent& set_shape_right_corners(entt::entity entity, float radius, bool sdfEdges = true)
    {
        return set_shape_corners(entity, 0.0f, radius, radius, 0.0f, sdfEdges);
    }

    ShapeComponent& set_shape_top_corners(entt::entity entity, float radius, bool sdfEdges = true)
    {
        return set_shape_corners(entity, radius, radius, 0.0f, 0.0f, sdfEdges);
    }

    ShapeComponent& set_shape_bottom_corners(entt::entity entity, float radius, bool sdfEdges = true)
    {
        return set_shape_corners(entity, 0.0f, 0.0f, radius, radius, sdfEdges);
    }

    DragHandle2DComponent& set_drag_handle(entt::entity entity, entt::entity target)
    {
        return registry_.emplace_or_replace<DragHandle2DComponent>(entity, DragHandle2DComponent { target, true });
    }

    ShadowComponent& add_shadow(entt::entity entity, glm::vec2 offset, float blurRadius, float opacity)
    {
        ShadowComponent shadow = {};
        shadow.set_color("#00000075");
        shadow.offset = scaled_offset(offset.x, offset.y, scale_);
        shadow.blurRadius = scaled_scalar(blurRadius, scale_);
        shadow.spread = scaled_scalar(2.0f, scale_);
        shadow.opacity = opacity;
        return registry_.emplace_or_replace<ShadowComponent>(entity, shadow);
    }

    PanelBlurComponent& add_panel_blur(
        entt::entity entity,
        glm::vec2 offset,
        float radius,
        float featherRadius,
        float spread,
        float opacity,
        uint32_t passes = 1u)
    {
        PanelBlurComponent blur = {};
        blur.offset = scaled_offset(offset.x, offset.y, scale_);
        blur.radius = scaled_scalar(radius, scale_);
        blur.passes = std::max(passes, 1u);
        blur.spread = scaled_scalar(spread, scale_);
        blur.featherRadius = scaled_scalar(featherRadius, scale_);
        blur.opacity = std::clamp(opacity, 0.0f, 1.0f);
        return registry_.emplace_or_replace<PanelBlurComponent>(entity, blur);
    }

    Renderer2DCacheComponent& set_timed_cache(
        entt::entity entity,
        float idleTickRate,
        float activeTickRate,
        float activeSeconds)
    {
        Renderer2DCacheComponent& cache = registry_.get_or_emplace<Renderer2DCacheComponent>(entity);
        cache.mode = Renderer2DCacheMode::eTimed;
        cache.idleTickRate = idleTickRate;
        cache.activeTickRate = activeTickRate;
        cache.restoreStaticWhenIdle = false;
        if (activateTimedAnimations_)
        {
            scene_.activate_dynamic(entity, activeSeconds);
        }
        scene_.mark_dirty(entity);
        return cache;
    }

    Renderer2DCacheComponent& set_dynamic_cache(entt::entity entity, bool propagateToChildren = true)
    {
        // Dynamic cache is for controls that animate, scroll, drag, or change every frame
        Renderer2DCacheComponent& cache = registry_.get_or_emplace<Renderer2DCacheComponent>(entity);
        cache.mode = Renderer2DCacheMode::eDynamic;
        cache.idleTickRate = 0.0f;
        cache.activeTickRate = 0.0f;
        cache.pendingActiveSeconds = 0.0f;
        cache.activeUntilSeconds = 0.0;
        cache.wasActive = true;
        cache.propagateToChildren = propagateToChildren;
        cache.restoreStaticWhenIdle = false;
        return cache;
    }

    bool play_display_transition(
        entt::entity entity,
        double currentTimeSeconds,
        float durationSeconds = 0.24f,
        float fromOpacity = 0.0f,
        float fromBlurRadius = 0.0f,
        glm::vec2 fromScale = glm::vec2(1.0f),
        glm::vec2 toScale = glm::vec2(1.0f),
        DisplayTransitionCurve2D curve = DisplayTransitionCurve2D::eDefault)
    {
        // Display transitions animate a whole subtree when panels are shown or rebuilt
        DisplayTransition2DComponent transition = {};
        transition.durationSeconds = durationSeconds;
        transition.fromOpacity = fromOpacity;
        transition.toOpacity = 1.0f;
        transition.fromBlurRadius = scaled_scalar(fromBlurRadius, scale_);
        transition.toBlurRadius = 0.0f;
        transition.fromScale = fromScale;
        transition.toScale = toScale;
        transition.curve = curve;
        return scene_.play_display_transition(entity, transition, currentTimeSeconds);
    }

    bool dispose_panel(entt::entity& root)
    {
        if (root == entt::null)
        {
            return false;
        }

        if (!registry_.valid(root))
        {
            root = entt::null;
            return false;
        }

        const bool destroyed = scene_.destroy_entity_tree(root);
        root = entt::null;
        return destroyed;
    }

    bool set_subtree_input_enabled(entt::entity root, bool enabled)
    {
        if (root == entt::null || !registry_.valid(root))
        {
            return false;
        }

        bool changed = false;
        std::vector<entt::entity> stack;
        std::unordered_set<uint32_t> visited;
        stack.push_back(root);

        auto set_enabled = [&](auto* component) {
            if (component && component->enabled != enabled)
            {
                component->enabled = enabled;
                changed = true;
            }
        };

        while (!stack.empty())
        {
            const entt::entity current = stack.back();
            stack.pop_back();
            if (!registry_.valid(current))
            {
                continue;
            }

            const uint32_t key = static_cast<uint32_t>(entt::to_integral(current));
            if (visited.find(key) != visited.end())
            {
                continue;
            }
            visited.insert(key);

            if (ButtonInputComponent* button = registry_.try_get<ButtonInputComponent>(current))
            {
                set_enabled(button);
                button->hovered = false;
                button->leftPressed = false;
                button->rightPressed = false;
            }
            if (TextInputComponent* text = registry_.try_get<TextInputComponent>(current))
            {
                set_enabled(text);
                text->focused = false;
            }
            if (DropTargetComponent* drop = registry_.try_get<DropTargetComponent>(current))
            {
                set_enabled(drop);
                drop->hovered = false;
            }
            if (ScrollInputComponent* scroll = registry_.try_get<ScrollInputComponent>(current))
            {
                set_enabled(scroll);
                scroll->hovered = false;
            }
            if (ScrollBarInputComponent* scrollBar = registry_.try_get<ScrollBarInputComponent>(current))
            {
                set_enabled(scrollBar);
                scrollBar->hovered = false;
                scrollBar->dragging = false;
            }
            if (SliderInputComponent* slider = registry_.try_get<SliderInputComponent>(current))
            {
                set_enabled(slider);
                slider->hovered = false;
                slider->dragging = false;
            }
            if (ResizablePanelComponent* resize = registry_.try_get<ResizablePanelComponent>(current))
            {
                set_enabled(resize);
                resize->hovered = false;
                resize->activeEdges = ePanelResizeNone;
            }
            if (DragHandle2DComponent* drag = registry_.try_get<DragHandle2DComponent>(current))
            {
                set_enabled(drag);
            }
            set_enabled(registry_.try_get<KeyboardShortcutComponent>(current));

            auto childView = registry_.view<const Parent2DComponent>();
            childView.each([&](entt::entity child, const Parent2DComponent& parent) {
                if (parent.parent == current && child != current)
                {
                    stack.push_back(child);
                }
            });
        }

        if (changed)
        {
            scene_.mark_dirty();
        }
        return changed;
    }

    bool dispose_panel(
        entt::entity& root,
        const DisplayTransition2DComponent& transition,
        double currentTimeSeconds)
    {
        if (root == entt::null)
        {
            return false;
        }

        if (!registry_.valid(root))
        {
            root = entt::null;
            return false;
        }

        if (!transition.enabled || transition.durationSeconds <= 0.0f)
        {
            return dispose_panel(root);
        }

        DisplayTransition2DComponent exitTransition = transition;
        exitTransition.destroyEntityTreeOnComplete = true;
        exitTransition.destroyTarget = root;
        exitTransition.removeWhenComplete = false;

        const entt::entity target = root;
        set_subtree_input_enabled(target, false);
        const bool started = scene_.play_display_transition(target, exitTransition, currentTimeSeconds);
        if (started)
        {
            root = entt::null;
        }
        return started;
    }

    template<typename BuildFn>
    entt::entity show_disposable_panel(entt::entity& root, BuildFn&& build)
    {
        if (root != entt::null && registry_.valid(root))
        {
            return root;
        }

        root = std::forward<BuildFn>(build)(*this);
        return root;
    }

    template<typename BuildFn>
    entt::entity show_disposable_panel(
        entt::entity& root,
        BuildFn&& build,
        const DisplayTransition2DComponent& transition,
        double currentTimeSeconds)
    {
        root = show_disposable_panel(root, std::forward<BuildFn>(build));
        if (root != entt::null && registry_.valid(root) && transition.enabled)
        {
            scene_.play_display_transition(root, transition, currentTimeSeconds);
        }
        return root;
    }

    template<typename BuildFn>
    entt::entity replace_panel_content(entt::entity& root, BuildFn&& build)
    {
        dispose_panel(root);
        root = std::forward<BuildFn>(build)(*this);
        return root;
    }

    template<typename BuildFn>
    entt::entity replace_panel_content(
        entt::entity& root,
        BuildFn&& build,
        const DisplayTransition2DComponent& enterTransition,
        const DisplayTransition2DComponent& exitTransition,
        double currentTimeSeconds)
    {
        if (root != entt::null && registry_.valid(root))
        {
            dispose_panel(root, exitTransition, currentTimeSeconds);
        }
        else
        {
            root = entt::null;
        }

        root = std::forward<BuildFn>(build)(*this);
        if (root != entt::null && registry_.valid(root) && enterTransition.enabled)
        {
            scene_.play_display_transition(root, enterTransition, currentTimeSeconds);
        }
        return root;
    }

    template<typename BuildFn>
    entt::entity toggle_disposable_panel(entt::entity& root, BuildFn&& build)
    {
        if (root != entt::null && registry_.valid(root))
        {
            dispose_panel(root);
            return entt::null;
        }

        root = entt::null;
        return show_disposable_panel(root, std::forward<BuildFn>(build));
    }

    template<typename BuildFn>
    entt::entity toggle_disposable_panel(
        entt::entity& root,
        BuildFn&& build,
        const DisplayTransition2DComponent& transition,
        double currentTimeSeconds)
    {
        if (root != entt::null && registry_.valid(root))
        {
            dispose_panel(root);
            return entt::null;
        }

        root = entt::null;
        return show_disposable_panel(root, std::forward<BuildFn>(build), transition, currentTimeSeconds);
    }

    template<typename BuildFn>
    entt::entity toggle_disposable_panel(
        entt::entity& root,
        BuildFn&& build,
        const DisplayTransition2DComponent& enterTransition,
        const DisplayTransition2DComponent& exitTransition,
        double currentTimeSeconds)
    {
        if (root != entt::null && registry_.valid(root))
        {
            dispose_panel(root, exitTransition, currentTimeSeconds);
            return entt::null;
        }

        root = entt::null;
        return show_disposable_panel(root, std::forward<BuildFn>(build), enterTransition, currentTimeSeconds);
    }

    LayoutRect2DComponent& set_padding(
        entt::entity entity,
        float left,
        float top,
        float right,
        float bottom,
        bool resizeChildren = true,
        glm::vec2 childLayoutSize = glm::vec2(0.0f))
    {
        LayoutRect2DComponent layoutRect = {};
        layoutRect.padding = scaled_edges(left, top, right, bottom, scale_);
        layoutRect.resizeChildren = resizeChildren;
        layoutRect.childLayoutSize = childLayoutSize;
        return registry_.emplace_or_replace<LayoutRect2DComponent>(entity, layoutRect);
    }

    Layout2DComponent& attach_layout(
        entt::entity entity,
        entt::entity parent,
        glm::vec2 anchor,
        glm::vec2 pivot,
        glm::vec2 offset,
        glm::vec2 size = glm::vec2(0.0f))
    {
        registry_.emplace_or_replace<Parent2DComponent>(entity, Parent2DComponent { parent });

        Layout2DComponent layout = {};
        layout.anchorMin = anchor;
        layout.anchorMax = anchor;
        layout.pivot = pivot;
        layout.offset = offset;
        layout.size = size;
        return registry_.emplace_or_replace<Layout2DComponent>(entity, layout);
    }

    Layout2DComponent& attach_aligned(
        entt::entity entity,
        entt::entity parent,
        UiAlignment alignment,
        glm::vec2 offset,
        glm::vec2 size = glm::vec2(0.0f))
    {
        return attach_layout(
            entity,
            parent,
            ui_alignment_anchor(alignment),
            ui_alignment_pivot(alignment),
            offset,
            size);
    }

    Layout2DComponent& attach_stretch(
        entt::entity entity,
        entt::entity parent,
        glm::vec2 anchorMin,
        glm::vec2 anchorMax,
        glm::vec2 pivot,
        glm::vec4 margin,
        glm::vec2 offset = glm::vec2(0.0f),
        glm::vec2 size = glm::vec2(0.0f))
    {
        registry_.emplace_or_replace<Parent2DComponent>(entity, Parent2DComponent { parent });

        Layout2DComponent layout = {};
        layout.anchorMin = anchorMin;
        layout.anchorMax = anchorMax;
        layout.pivot = pivot;
        layout.margin = margin;
        layout.offset = offset;
        layout.size = size;
        return registry_.emplace_or_replace<Layout2DComponent>(entity, layout);
    }

    Layout2DComponent& attach_fill(
        entt::entity entity,
        entt::entity parent,
        glm::vec4 margin = glm::vec4(0.0f),
        glm::vec2 offset = glm::vec2(0.0f))
    {
        return attach_stretch(
            entity,
            parent,
            { 0.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.0f, 0.0f },
            scaled_edges(margin.x, margin.y, margin.z, margin.w, scale_),
            offset);
    }

    Grid2DComponent& set_grid(
        entt::entity entity,
        std::vector<GridTrack2D> columns,
        std::vector<GridTrack2D> rows,
        glm::vec2 gap = glm::vec2(0.0f))
    {
        Grid2DComponent grid = {};
        grid.columns = scale_tracks(std::move(columns));
        grid.rows = scale_tracks(std::move(rows));
        grid.gap = scaled_offset(gap.x, gap.y, scale_);
        return registry_.emplace_or_replace<Grid2DComponent>(entity, std::move(grid));
    }

    Grid2DComponent& set_grid(
        entt::entity entity,
        std::initializer_list<GridTrack2D> columns,
        std::initializer_list<GridTrack2D> rows,
        glm::vec2 gap = glm::vec2(0.0f))
    {
        return set_grid(
            entity,
            std::vector<GridTrack2D>(columns.begin(), columns.end()),
            std::vector<GridTrack2D>(rows.begin(), rows.end()),
            gap);
    }

    Grid2DComponent& set_horizontal_grid(
        entt::entity entity,
        std::initializer_list<float> columnFractions,
        float gap = 0.0f)
    {
        std::vector<GridTrack2D> columns;
        columns.reserve(columnFractions.size());
        for (float fraction : columnFractions)
        {
            columns.push_back(GridTrack2D::fraction(fraction));
        }
        return set_grid(entity, std::move(columns), { GridTrack2D::fraction(1.0f) }, { gap, 0.0f });
    }

    Grid2DComponent& set_vertical_grid(
        entt::entity entity,
        std::initializer_list<float> rowFractions,
        float gap = 0.0f)
    {
        std::vector<GridTrack2D> rows;
        rows.reserve(rowFractions.size());
        for (float fraction : rowFractions)
        {
            rows.push_back(GridTrack2D::fraction(fraction));
        }
        return set_grid(entity, { GridTrack2D::fraction(1.0f) }, std::move(rows), { 0.0f, gap });
    }

    GridCell2DComponent& set_grid_cell(
        entt::entity entity,
        uint32_t column,
        uint32_t row,
        uint32_t columnSpan = 1u,
        uint32_t rowSpan = 1u,
        glm::vec4 margin = glm::vec4(0.0f))
    {
        GridCell2DComponent cell = {};
        cell.column = column;
        cell.row = row;
        cell.columnSpan = std::max(columnSpan, 1u);
        cell.rowSpan = std::max(rowSpan, 1u);
        cell.margin = scaled_edges(margin.x, margin.y, margin.z, margin.w, scale_);
        return registry_.emplace_or_replace<GridCell2DComponent>(entity, cell);
    }

    Layout2DComponent& attach_grid_cell(
        entt::entity entity,
        entt::entity parent,
        uint32_t column,
        uint32_t row,
        uint32_t columnSpan,
        uint32_t rowSpan,
        UiAlignment alignment,
        glm::vec2 offset,
        glm::vec2 size = glm::vec2(0.0f),
        glm::vec4 margin = glm::vec4(0.0f))
    {
        set_grid_cell(entity, column, row, columnSpan, rowSpan, margin);
        return attach_aligned(entity, parent, alignment, offset, size);
    }

    Layout2DComponent& attach_grid_fill(
        entt::entity entity,
        entt::entity parent,
        uint32_t column,
        uint32_t row,
        uint32_t columnSpan = 1u,
        uint32_t rowSpan = 1u,
        glm::vec4 margin = glm::vec4(0.0f),
        glm::vec2 offset = glm::vec2(0.0f))
    {
        set_grid_cell(entity, column, row, columnSpan, rowSpan, margin);
        return attach_stretch(
            entity,
            parent,
            { 0.0f, 0.0f },
            { 1.0f, 1.0f },
            { 0.0f, 0.0f },
            glm::vec4(0.0f),
            offset);
    }

    Layout2DComponent& attach_grid_stretch_x(
        entt::entity entity,
        entt::entity parent,
        uint32_t column,
        uint32_t row,
        float height,
        uint32_t columnSpan = 1u,
        uint32_t rowSpan = 1u,
        glm::vec4 margin = glm::vec4(0.0f),
        glm::vec2 offset = glm::vec2(0.0f))
    {
        set_grid_cell(entity, column, row, columnSpan, rowSpan, margin);
        return attach_stretch(
            entity,
            parent,
            { 0.0f, 0.0f },
            { 1.0f, 0.0f },
            { 0.0f, 0.0f },
            glm::vec4(0.0f),
            scaled_offset(offset.x, offset.y, scale_),
            scaled_size(0.0f, height, scale_));
    }

    entt::entity create_grid_block(
        entt::entity parent,
        uint32_t column,
        uint32_t row,
        uint32_t columnSpan,
        uint32_t rowSpan,
        const ShapeStyleComponent& style,
        float cornerRadius,
        int32_t layer,
        uint32_t order,
        glm::vec4 margin = glm::vec4(0.0f),
        Renderer2DPrimitive primitive = Renderer2DPrimitive::eRoundedRectangle,
        bool dynamicCache = false)
    {
        entt::entity block = scene_.create_shape(
            { 0.0f, 0.0f },
            glm::vec2(1.0f),
            style,
            primitive);
        set_shape(block, cornerRadius);
        set_layer(block, layer, order);
        attach_grid_fill(block, parent, column, row, columnSpan, rowSpan, margin);
        if (dynamicCache)
        {
            set_dynamic_cache(block);
        }
        return block;
    }

    entt::entity create_grid_text(
        std::string value,
        const Renderer2DFontAtlas& fontAtlas,
        entt::entity parent,
        uint32_t column,
        uint32_t row,
        uint32_t columnSpan,
        uint32_t rowSpan,
        UiAlignment alignment,
        float fontSize,
        const TextStyleComponent& style,
        int32_t layer,
        uint32_t order,
        glm::vec2 offset = glm::vec2(0.0f),
        glm::vec4 margin = glm::vec4(0.0f),
        glm::vec2 size = glm::vec2(0.0f))
    {
        entt::entity text = scene_.create_text(
            std::move(value),
            { 0.0f, 0.0f },
            scaled_scalar(fontSize, scale_),
            style);
        apply_font_layout(fontAtlas, registry_, text);
        set_layer(text, layer, order);
        attach_grid_cell(
            text,
            parent,
            column,
            row,
            columnSpan,
            rowSpan,
            alignment,
            scaled_offset(offset.x, offset.y, scale_),
            size,
            margin);
        return text;
    }

    entt::entity create_grid_text(
        const Text& value,
        const Localisation& localisation,
        const Renderer2DFontAtlas& fontAtlas,
        entt::entity parent,
        uint32_t column,
        uint32_t row,
        uint32_t columnSpan,
        uint32_t rowSpan,
        UiAlignment alignment,
        float fontSize,
        const TextStyleComponent& style,
        int32_t layer,
        uint32_t order,
        glm::vec2 offset = glm::vec2(0.0f),
        glm::vec4 margin = glm::vec4(0.0f),
        glm::vec2 size = glm::vec2(0.0f))
    {
        entt::entity text = create_grid_text(
            localisation.resolve(value),
            fontAtlas,
            parent,
            column,
            row,
            columnSpan,
            rowSpan,
            alignment,
            fontSize,
            style,
            layer,
            order,
            offset,
            margin,
            size);
        registry_.emplace<LocalisedTextComponent>(text, value);
        return text;
    }

    entt::entity create_aligned_text(
        std::string value,
        const Renderer2DFontAtlas& fontAtlas,
        entt::entity parent,
        UiAlignment alignment,
        float fontSize,
        const TextStyleComponent& style,
        int32_t layer,
        uint32_t order,
        glm::vec2 offset = glm::vec2(0.0f),
        glm::vec2 size = glm::vec2(0.0f))
    {
        entt::entity text = scene_.create_text(
            std::move(value),
            { 0.0f, 0.0f },
            scaled_scalar(fontSize, scale_),
            style);
        apply_font_layout(fontAtlas, registry_, text);
        set_layer(text, layer, order);
        attach_aligned(text, parent, alignment, scaled_offset(offset.x, offset.y, scale_), size);
        return text;
    }

    entt::entity create_layout_text(
        std::string value,
        const Renderer2DFontAtlas& fontAtlas,
        entt::entity parent,
        glm::vec2 anchor,
        glm::vec2 pivot,
        glm::vec2 offset,
        float fontSize,
        const TextStyleComponent& style,
        int32_t layer,
        uint32_t order,
        glm::vec2 size = glm::vec2(0.0f))
    {
        entt::entity text = scene_.create_text(
            std::move(value),
            { 0.0f, 0.0f },
            scaled_scalar(fontSize, scale_),
            style);
        apply_font_layout(fontAtlas, registry_, text);
        set_layer(text, layer, order);
        attach_layout(text, parent, anchor, pivot, offset, size);
        return text;
    }

    entt::entity create_aligned_text(
        const Text& value,
        const Localisation& localisation,
        const Renderer2DFontAtlas& fontAtlas,
        entt::entity parent,
        UiAlignment alignment,
        float fontSize,
        const TextStyleComponent& style,
        int32_t layer,
        uint32_t order,
        glm::vec2 offset = glm::vec2(0.0f),
        glm::vec2 size = glm::vec2(0.0f))
    {
        entt::entity text = create_aligned_text(
            localisation.resolve(value),
            fontAtlas,
            parent,
            alignment,
            fontSize,
            style,
            layer,
            order,
            offset,
            size);
        registry_.emplace<LocalisedTextComponent>(text, value);
        return text;
    }

    entt::entity create_aligned_media(
        Media2DHandle media,
        entt::entity parent,
        UiAlignment alignment,
        glm::vec2 size,
        int32_t layer,
        uint32_t order,
        glm::vec2 offset = glm::vec2(0.0f),
        Media2DFit fit = Media2DFit::eContain)
    {
        if (!media.valid())
        {
            return entt::null;
        }

        entt::entity entity = scene_.create_media(
            { 0.0f, 0.0f },
            scaled_size(size.x, size.y, scale_),
            media,
            fit);
        set_layer(entity, layer, order);
        attach_aligned(entity, parent, alignment, scaled_offset(offset.x, offset.y, scale_), scaled_size(size.x, size.y, scale_));
        return entity;
    }

    entt::entity create_grid_media(
        Media2DHandle media,
        entt::entity parent,
        uint32_t column,
        uint32_t row,
        uint32_t columnSpan,
        uint32_t rowSpan,
        UiAlignment alignment,
        glm::vec2 size,
        int32_t layer,
        uint32_t order,
        glm::vec2 offset = glm::vec2(0.0f),
        glm::vec4 margin = glm::vec4(0.0f),
        Media2DFit fit = Media2DFit::eContain)
    {
        if (!media.valid())
        {
            return entt::null;
        }

        entt::entity entity = scene_.create_media(
            { 0.0f, 0.0f },
            scaled_size(size.x, size.y, scale_),
            media,
            fit);
        set_layer(entity, layer, order);
        attach_grid_cell(
            entity,
            parent,
            column,
            row,
            columnSpan,
            rowSpan,
            alignment,
            scaled_offset(offset.x, offset.y, scale_),
            scaled_size(size.x, size.y, scale_),
            margin);
        return entity;
    }

private:
    GridTrack2D scale_track(GridTrack2D track) const
    {
        if (track.unit == GridTrackUnit2D::ePixels)
        {
            track.value = scaled_scalar(track.value, scale_);
        }
        return track;
    }

    std::vector<GridTrack2D> scale_tracks(std::vector<GridTrack2D> tracks) const
    {
        for (GridTrack2D& track : tracks)
        {
            track = scale_track(track);
        }
        return tracks;
    }

    Renderer2DScene& scene_;
    entt::registry& registry_;
    LayoutScale scale_;
    bool activateTimedAnimations_ = true;
};

// Fluent, logical-pixel placement for entities created by either the compact
// factories above or the lower-level scene/control APIs.
class UiPlacement
{
public:
    UiPlacement(UiBuilder& builder, entt::entity entity) :
        builder_(&builder),
        entity_(entity)
    {
    }

    bool valid() const
    {
        return builder_ && entity_ != entt::null &&
            builder_->registry().valid(entity_);
    }

    entt::entity entity() const
    {
        return entity_;
    }

    operator entt::entity() const
    {
        return entity_;
    }

    UiPlacement& inside(entt::entity parent)
    {
        if (valid())
        {
            builder_->registry().emplace_or_replace<Parent2DComponent>(
                entity_,
                Parent2DComponent { parent });
        }
        return *this;
    }

    UiPlacement& at(UiAlignment alignment)
    {
        if (Layout2DComponent* value = layout())
        {
            value->anchorMin = ui_alignment_anchor(alignment);
            value->anchorMax = value->anchorMin;
            value->pivot = ui_alignment_pivot(alignment);
        }
        return *this;
    }

    UiPlacement& offset(float x, float y)
    {
        if (Layout2DComponent* value = layout())
        {
            value->offset = scaled_offset(x, y, builder_->scale());
        }
        return *this;
    }

    UiPlacement& offset(glm::vec2 value)
    {
        return offset(value.x, value.y);
    }

    UiPlacement& size(float width, float height)
    {
        if (Layout2DComponent* value = layout())
        {
            value->size = scaled_size(width, height, builder_->scale());
        }
        return *this;
    }

    UiPlacement& size(glm::vec2 value)
    {
        return size(value.x, value.y);
    }

    UiPlacement& fill(float margin = 0.0f)
    {
        return fill(glm::vec4(margin));
    }

    UiPlacement& fill(glm::vec4 margin)
    {
        if (Layout2DComponent* value = layout())
        {
            value->anchorMin = glm::vec2(0.0f);
            value->anchorMax = glm::vec2(1.0f);
            value->pivot = glm::vec2(0.0f);
            value->margin = scaled_edges(
                margin.x,
                margin.y,
                margin.z,
                margin.w,
                builder_->scale());
            value->offset = glm::vec2(0.0f);
            value->size = glm::vec2(0.0f);
        }
        return *this;
    }

    UiPlacement& grid(
        uint32_t column,
        uint32_t row,
        uint32_t columnSpan = 1u,
        uint32_t rowSpan = 1u,
        glm::vec4 margin = glm::vec4(0.0f))
    {
        if (valid())
        {
            builder_->set_grid_cell(
                entity_,
                column,
                row,
                columnSpan,
                rowSpan,
                margin);
        }
        return *this;
    }

    UiPlacement& layer(
        int32_t layer,
        uint32_t order = 0,
        bool alwaysOnTop = false)
    {
        if (valid())
        {
            builder_->set_layer(entity_, layer, order, alwaysOnTop);
        }
        return *this;
    }

    UiPlacement& padding(float all)
    {
        return padding(glm::vec4(all));
    }

    UiPlacement& padding(glm::vec4 edges)
    {
        if (valid())
        {
            builder_->set_padding(
                entity_,
                edges.x,
                edges.y,
                edges.z,
                edges.w);
        }
        return *this;
    }

    UiPlacement& corner_radius(float radius)
    {
        if (valid() &&
            builder_->registry().all_of<ShapeComponent>(entity_))
        {
            builder_->set_shape(entity_, radius);
        }
        return *this;
    }

    UiPlacement& visible(bool isVisible)
    {
        if (valid())
        {
            RenderLayer2DComponent& renderLayer =
                builder_->registry().get_or_emplace<
                    RenderLayer2DComponent>(entity_);
            renderLayer.visible = isVisible;
            builder_->scene().mark_dirty(entity_);
        }
        return *this;
    }

private:
    Layout2DComponent* layout()
    {
        if (!valid())
        {
            return nullptr;
        }
        return &builder_->registry().get_or_emplace<Layout2DComponent>(
            entity_);
    }

    UiBuilder* builder_ = nullptr;
    entt::entity entity_ = entt::null;
};

inline UiPlacement UiBuilder::place(entt::entity entity)
{
    return UiPlacement(*this, entity);
}

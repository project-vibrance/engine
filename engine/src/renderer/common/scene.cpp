#include "scene_helpers.h"
bool Renderer2DRenderPlan::empty() const
{
    return panelBlurs.empty() && shadows.empty() && blurs.empty() && shapes.empty() && media.empty() && texts.empty() &&
        models.empty() && cachedPanelBlurs.empty() && cachedShadows.empty() && cachedBlurs.empty() &&
        cachedShapes.empty() && cachedMedia.empty() && cachedTexts.empty();
}

void Renderer2DRenderPlan::clear()
{
    panelBlurs.clear();
    shadows.clear();
    blurs.clear();
    shapes.clear();
    media.clear();
    texts.clear();
    models.clear();
    cachedPanelBlurs.clear();
    cachedShadows.clear();
    cachedBlurs.clear();
    cachedShapes.clear();
    cachedMedia.clear();
    cachedTexts.clear();
    rebuildCachedLayer = false;
    usesHosted3D = false;
}

void Renderer2DRenderPlan::reserve(
    std::size_t shadowCount,
    std::size_t blurCount,
    std::size_t shapeCount,
    std::size_t mediaCount,
    std::size_t textCount,
    std::size_t modelCount)
{
    panelBlurs.reserve(blurCount);
    shadows.reserve(shadowCount);
    blurs.reserve(blurCount);
    shapes.reserve(shapeCount);
    media.reserve(mediaCount);
    texts.reserve(textCount);
    models.reserve(modelCount);
    cachedPanelBlurs.reserve(blurCount);
    cachedShadows.reserve(shadowCount);
    cachedBlurs.reserve(blurCount);
    cachedShapes.reserve(shapeCount);
    cachedMedia.reserve(mediaCount);
    cachedTexts.reserve(textCount);
}

entt::registry& Renderer2DScene::registry()
{
    return registry_;
}

const entt::registry& Renderer2DScene::registry() const
{
    return registry_;
}

entt::entity Renderer2DScene::create_entity()
{
    return registry_.create();
}

entt::entity Renderer2DScene::create_shape(
    glm::vec2 position,
    glm::vec2 size,
    const ShapeStyleComponent& style,
    Renderer2DPrimitive primitive,
    bool draggable)
{
    const entt::entity entity = registry_.create();

    Transform2DComponent& transform = registry_.emplace<Transform2DComponent>(entity);
    transform.position = position;

    ShapeComponent& shape = registry_.emplace<ShapeComponent>(entity);
    shape.primitive = primitive;
    shape.size = size;
    shape.draggable = draggable;

    registry_.emplace<ShapeStyleComponent>(entity, style);
    registry_.emplace<RenderLayer2DComponent>(entity);
    registry_.emplace<Renderer2DCacheComponent>(entity);
    mark_dirty();
    return entity;
}

entt::entity Renderer2DScene::create_text(
    std::string text,
    glm::vec2 position,
    float fontSize,
    const TextStyleComponent& style)
{
    const entt::entity entity = registry_.create();

    Transform2DComponent& transform = registry_.emplace<Transform2DComponent>(entity);
    transform.position = position;

    TextComponent& textComponent = registry_.emplace<TextComponent>(entity);
    textComponent.text = std::move(text);
    textComponent.fontSize = fontSize;
    textComponent.msdfPixelRange = std::max(style.outlineWidth + style.glowRadius, 4.0f);

    registry_.emplace<TextStyleComponent>(entity, style);
    registry_.emplace<RenderLayer2DComponent>(entity);
    registry_.emplace<Renderer2DCacheComponent>(entity);
    mark_dirty();
    return entity;
}

entt::entity Renderer2DScene::create_model(
    glm::vec2 position,
    glm::vec2 size,
    const Model3DComponent& model)
{
    const entt::entity entity = registry_.create();

    Transform2DComponent& transform = registry_.emplace<Transform2DComponent>(entity);
    transform.position = position;

    Model3DComponent modelComponent = model;
    modelComponent.size = size;
    registry_.emplace<Model3DComponent>(entity, modelComponent);
    registry_.emplace<RenderLayer2DComponent>(entity);

    Renderer2DCacheComponent& cache = registry_.emplace<Renderer2DCacheComponent>(entity);
    cache.mode = Renderer2DCacheMode::eDynamic;
    cache.restoreStaticWhenIdle = false;

    mark_dirty();
    return entity;
}

entt::entity Renderer2DScene::create_media(
    glm::vec2 position,
    glm::vec2 size,
    Media2DHandle media,
    Media2DFit fit)
{
    const entt::entity entity = registry_.create();

    Transform2DComponent& transform = registry_.emplace<Transform2DComponent>(entity);
    transform.position = position;

    Media2DComponent mediaComponent = {};
    mediaComponent.size = size;
    mediaComponent.fit = fit;
    mediaComponent.set_media(media);
    registry_.emplace<Media2DComponent>(entity, mediaComponent);
    registry_.emplace<RenderLayer2DComponent>(entity);

    Renderer2DCacheComponent& cache = registry_.emplace<Renderer2DCacheComponent>(entity);
    cache.mode = media.animated ? Renderer2DCacheMode::eDynamic : Renderer2DCacheMode::eStatic;

    mark_dirty();
    return entity;
}

bool Renderer2DScene::control_media(
    entt::entity entity,
    Media2DPlaybackCommand command,
    double value)
{
    Media2DComponent* media = media_component(registry_, entity);
    if (media == nullptr)
    {
        return false;
    }
    if (apply_media_playback_operation(*media, command, value))
    {
        mark_dirty(entity);
    }
    return true;
}

std::optional<Media2DPlaybackState> Renderer2DScene::media_playback_state(entt::entity entity) const
{
    const Media2DComponent* media = media_component(registry_, entity);
    if (media == nullptr)
    {
        return std::nullopt;
    }

    return Media2DPlaybackState {
        media->playbackSeconds,
        media->durationSeconds,
        media->currentFrame,
        media->frameCount,
        media->animated,
        media->playing,
        media->loop
    };
}

bool Renderer2DScene::enable_mask(entt::entity entity, bool useContentRect, float effectPadding)
{
    if (!registry_.valid(entity))
    {
        return false;
    }

    Mask2DComponent& mask = registry_.get_or_emplace<Mask2DComponent>(entity);
    mask.enabled = true;
    mask.useContentRect = useContentRect;
    mask.effectPadding = std::max(effectPadding, 0.0f);
    mark_dirty(entity);
    return true;
}

bool Renderer2DScene::disable_mask(entt::entity entity)
{
    if (!registry_.valid(entity) || !registry_.all_of<Mask2DComponent>(entity))
    {
        return false;
    }

    registry_.remove<Mask2DComponent>(entity);
    mark_dirty(entity);
    return true;
}

void Renderer2DScene::destroy_entity(entt::entity entity)
{
    if (registry_.valid(entity))
    {
        registry_.destroy(entity);
        mark_dirty();
    }
}

bool Renderer2DScene::destroy_entity_tree(entt::entity entity)
{
    const bool destroyed = destroy_entity_tree_now(registry_, entity);
    if (destroyed)
    {
        mark_dirty();
    }
    return destroyed;
}

void Renderer2DScene::clear()
{
    registry_.clear();
    mark_dirty();
}

void Renderer2DScene::mark_dirty()
{
    ++cacheGeneration_;
    ++frameGeneration_;
    fullDamagePending_ = true;
    damageEntities_.clear();
}

void Renderer2DScene::mark_dirty(entt::entity entity)
{
    if (!registry_.valid(entity))
    {
        return;
    }

    cache_or_default(registry_, entity);
    ++frameGeneration_;
    if (!fullDamagePending_ &&
        std::find(damageEntities_.begin(), damageEntities_.end(), entity) ==
            damageEntities_.end())
    {
        damageEntities_.push_back(entity);
    }
    if (affects_static_cache(registry_, entity))
    {
        ++cacheGeneration_;
    }
}

void Renderer2DScene::activate_dynamic(entt::entity entity, float durationSeconds)
{
    if (!registry_.valid(entity) || durationSeconds <= 0.0f)
    {
        return;
    }

    Renderer2DCacheComponent& cache = cache_or_default(registry_, entity);
    if (cache.mode == Renderer2DCacheMode::eStatic)
    {
        // Direct activation requests temporarily promote cached UI. This keeps
        // focus, typing, hover and similar updates off the static surface until idle.
        cache.mode = Renderer2DCacheMode::eTimed;
        cache.restoreStaticWhenIdle = true;
    }
    const bool alreadyBypassesStaticLayer = !affects_static_cache(registry_, entity);
    if (cache.pendingActiveSeconds >= durationSeconds)
    {
        cache.wasActive = true;
        if (!alreadyBypassesStaticLayer)
        {
            mark_dirty();
        }
        cache.detachedFromStaticLayer = true;
        return;
    }

    // Coalesce repeated activation requests queued before the next render plan
    cache.pendingActiveSeconds = durationSeconds;
    cache.wasActive = true;
    if (!alreadyBypassesStaticLayer)
    {
        mark_dirty();
    }
    cache.detachedFromStaticLayer = true;
}

bool Renderer2DScene::play_display_transition(
    entt::entity entity,
    const DisplayTransition2DComponent& transition,
    double currentTimeSeconds)
{
    if (!registry_.valid(entity))
    {
        return false;
    }

    DisplayTransition2DComponent activeTransition = transition;
    activeTransition.delaySeconds = std::max(activeTransition.delaySeconds, 0.0f);
    activeTransition.startSeconds = currentTimeSeconds;
    activeTransition.hasStarted = false;
    activeTransition.delayScheduled = false;
    activeTransition.durationSeconds = std::max(activeTransition.durationSeconds, 0.0f);
    registry_.emplace_or_replace<DisplayTransition2DComponent>(entity, activeTransition);

    const float activeSeconds = std::max(
        activeTransition.delaySeconds + activeTransition.durationSeconds,
        1.0f / 60.0f);
    activate_dynamic(entity, activeSeconds);
    mark_dirty(entity);
    return true;
}

bool Renderer2DScene::clear_display_transition(entt::entity entity)
{
    if (!registry_.valid(entity) || !registry_.all_of<DisplayTransition2DComponent>(entity))
    {
        return false;
    }

    registry_.remove<DisplayTransition2DComponent>(entity);
    mark_dirty(entity);
    return true;
}

uint64_t Renderer2DScene::cache_generation() const
{
    return cacheGeneration_;
}

uint64_t Renderer2DScene::frame_generation() const
{
    return frameGeneration_;
}

bool Renderer2DScene::damage_pending(
    entt::entity entity,
    double currentTimeSeconds) const
{
    if (fullDamagePending_)
    {
        return true;
    }
    entt::entity current = entity;
    for (uint32_t depth = 0u;
        depth < 64u && current != entt::null && registry_.valid(current);
        ++depth)
    {
        if (std::find(damageEntities_.begin(), damageEntities_.end(), current) !=
            damageEntities_.end())
        {
            return true;
        }
        if (const DisplayTransition2DComponent* transition =
                registry_.try_get<DisplayTransition2DComponent>(current);
            transition && transition->enabled &&
            !display_transition_complete(*transition, currentTimeSeconds))
        {
            return true;
        }
        const Parent2DComponent* parent =
            registry_.try_get<Parent2DComponent>(current);
        current = parent ? parent->parent : entt::null;
    }
    if (const Media2DComponent* media = registry_.try_get<Media2DComponent>(entity))
    {
        return media->animated && media->playing && media_visible(*media) &&
            is_visible(registry_, entity);
    }
    return false;
}

void Renderer2DScene::commit_presented_bounds(
    std::vector<std::pair<entt::entity, glm::uvec4>> bounds)
{
    presentedBounds_ = std::move(bounds);
    fullDamagePending_ = false;
    damageEntities_.clear();
}

bool Renderer2DScene::requires_continuous_redraw(
    double currentTimeSeconds) const
{
    const auto transitionView =
        registry_.view<const DisplayTransition2DComponent>();
    for (const entt::entity entity : transitionView)
    {
        const DisplayTransition2DComponent& transition =
            transitionView.get<const DisplayTransition2DComponent>(entity);
        if (!transition.enabled)
        {
            continue;
        }
        if (!display_transition_complete(transition, currentTimeSeconds) ||
            transition.removeWhenComplete ||
            transition.destroyEntityTreeOnComplete)
        {
            return true;
        }
    }

    const auto mediaView = registry_.view<const Media2DComponent>();
    for (const entt::entity entity : mediaView)
    {
        const Media2DComponent& media =
            mediaView.get<const Media2DComponent>(entity);
        if (media.animated && media.playing && media_visible(media) &&
            is_visible(registry_, entity))
        {
            return true;
        }
    }

    const auto cacheView = registry_.view<const Renderer2DCacheComponent>();
    for (const entt::entity entity : cacheView)
    {
        const Renderer2DCacheComponent& cache =
            cacheView.get<const Renderer2DCacheComponent>(entity);
        if (cache.mode != Renderer2DCacheMode::eTimed)
        {
            continue;
        }
        // A pending activation and the final expiry frame change cache
        // ownership. While the entity remains active, however, only request a
        // time-driven frame when its declared tick interval is due. Component
        // mutations still advance frameGeneration_ independently.
        if (cache.pendingActiveSeconds > 0.0f ||
            (cache.wasActive && cache.activeUntilSeconds <= currentTimeSeconds))
        {
            return true;
        }
        if (cache.activeUntilSeconds > currentTimeSeconds &&
            cache.activeTickRate > 0.0f &&
            (cache.lastDynamicSeconds < 0.0 ||
                currentTimeSeconds - cache.lastDynamicSeconds >=
                    1.0 / static_cast<double>(cache.activeTickRate)))
        {
            return true;
        }
        if (cache.activeUntilSeconds <= currentTimeSeconds &&
            cache.idleTickRate > 0.0f &&
            (cache.lastDynamicSeconds < 0.0 ||
                currentTimeSeconds - cache.lastDynamicSeconds >=
                    1.0 / static_cast<double>(cache.idleTickRate)))
        {
            return true;
        }
    }
    return false;
}

uint32_t Renderer2DScene::active_frame_rate_limit(
    double currentTimeSeconds) const
{
    uint32_t limit = 0u;
    const auto cacheView = registry_.view<const Renderer2DCacheComponent>();
    for (const entt::entity entity : cacheView)
    {
        const Renderer2DCacheComponent& cache =
            cacheView.get<const Renderer2DCacheComponent>(entity);
        const bool active = cache.pendingActiveSeconds > 0.0f ||
            cache.activeUntilSeconds > currentTimeSeconds;
        if (!active || cache.activeFrameRateLimit == 0u)
        {
            continue;
        }
        limit = limit == 0u ?
            cache.activeFrameRateLimit :
            std::min(limit, cache.activeFrameRateLimit);
    }
    return limit;
}

entt::entity Renderer2DScene::entity_at(glm::vec2 point)
{
    resolve_layouts(registry_);

    const ShapeStyleComponent defaultShapeStyle {};
    const TextStyleComponent defaultTextStyle {};
    Renderer2DHitCandidate topmost {};

    auto shapeView = registry_.view<const Transform2DComponent, const ShapeComponent>();
    for (auto entity : shapeView)
    {
        if (is_input_transparent(registry_, entity) || !is_visible(registry_, entity))
        {
            continue;
        }
        if (!point_in_rect(inherited_mask_clip_rect(registry_, entity), point))
        {
            continue;
        }

        const auto& transform = shapeView.get<const Transform2DComponent>(entity);
        const auto& shape = shapeView.get<const ShapeComponent>(entity);
        const ShapeStyleComponent* stylePtr = registry_.try_get<ShapeStyleComponent>(entity);
        const ShapeStyleComponent& style = stylePtr ? *stylePtr : defaultShapeStyle;
        const HitRegion2DComponent* hitRegion =
            registry_.try_get<HitRegion2DComponent>(entity);
        if (!shape_fill_visible(style) && !shape_outline_visible(style) &&
            (!hitRegion || !hitRegion->enabled))
        {
            continue;
        }

        const glm::vec4 rect = renderer2d_apply_interactive_visual_rect(
            registry_,
            entity,
            make_bounds(transform, shape.size));
        const float edgePadding = std::max(style.outlineWidth, 0.0f) + std::max(style.edgeSoftness, 0.0f);
        if (point_in_primitive(
            shape.primitive,
            expand_rect(rect, edgePadding),
            padded_corner_radii(shape, edgePadding),
            point))
        {
            consider_hit_candidate(topmost, registry_, entity, Renderer2DRenderOpType::eShape);
        }
    }

    auto mediaView = registry_.view<const Transform2DComponent, const Media2DComponent>();
    for (auto entity : mediaView)
    {
        const auto& media = mediaView.get<const Media2DComponent>(entity);
        if (is_input_transparent(registry_, entity) ||
            !media_visible(media) || !is_visible(registry_, entity))
        {
            continue;
        }
        if (!point_in_rect(inherited_mask_clip_rect(registry_, entity), point))
        {
            continue;
        }

        const auto& transform = mediaView.get<const Transform2DComponent>(entity);
        if (point_in_primitive(
            media.primitive,
            renderer2d_apply_interactive_visual_rect(
                registry_,
                entity,
                media_bounds_from_component(transform, media)),
            media.cornerRadius,
            point))
        {
            consider_hit_candidate(topmost, registry_, entity, Renderer2DRenderOpType::eMedia);
        }
    }

    auto modelView = registry_.view<const Transform2DComponent, const Model3DComponent>();
    for (auto entity : modelView)
    {
        const auto& model = modelView.get<const Model3DComponent>(entity);
        if (is_input_transparent(registry_, entity) ||
            !model.visible || !is_visible(registry_, entity) || !alpha_visible(model.materialColor.a))
        {
            continue;
        }

        const auto& transform = modelView.get<const Transform2DComponent>(entity);
        const glm::vec4 viewport = renderer2d_apply_interactive_visual_rect(
            registry_,
            entity,
            make_model_viewport(registry_, entity, transform, model));
        const glm::vec4 clipRect = make_model_clip_rect(registry_, entity, viewport, model);
        if (point_in_rect(intersect_rect(viewport, clipRect), point))
        {
            consider_hit_candidate(topmost, registry_, entity, Renderer2DRenderOpType::eModel3D);
        }
    }

    auto textView = registry_.view<const Transform2DComponent, const TextComponent>();
    for (auto entity : textView)
    {
        if (is_input_transparent(registry_, entity) || !is_visible(registry_, entity))
        {
            continue;
        }
        if (!point_in_rect(inherited_mask_clip_rect(registry_, entity), point))
        {
            continue;
        }

        const auto& transform = textView.get<const Transform2DComponent>(entity);
        const auto& text = textView.get<const TextComponent>(entity);
        const TextStyleComponent* stylePtr = registry_.try_get<TextStyleComponent>(entity);
        const TextStyleComponent& style = stylePtr ? *stylePtr : defaultTextStyle;
        if (!text_visible(style))
        {
            continue;
        }

        const glm::vec4 textBounds = text_bounds_from_component(transform, text);
        const float padding = text_hit_padding(style);
        bool hit = false;
        if (!text.glyphs.empty())
        {
            for (const MSDFGlyph& glyph : text.glyphs)
            {
                if (glyph.size.x <= 0.0f || glyph.size.y <= 0.0f)
                {
                    continue;
                }

                const glm::vec4 glyphRect =
                    renderer2d_apply_interactive_visual_rect(
                        registry_,
                        entity,
                        glm::vec4 {
                    textBounds.x + glyph.position.x * transform.scale.x,
                    textBounds.y + glyph.position.y * transform.scale.y,
                    glyph.size.x * transform.scale.x,
                    glyph.size.y * transform.scale.y
                        });
                if (point_in_rect(expand_rect(glyphRect, padding), point))
                {
                    hit = true;
                    break;
                }
            }
        }
        else
        {
            hit = point_in_rect(
                expand_rect(
                    renderer2d_apply_interactive_visual_rect(
                        registry_,
                        entity,
                        textBounds),
                    padding),
                point);
        }

        if (hit)
        {
            consider_hit_candidate(topmost, registry_, entity, Renderer2DRenderOpType::eText);
        }
    }

    return topmost.valid ? topmost.entity : entt::null;
}

bool Renderer2DScene::hit_test(glm::vec2 point)
{
    resolve_layouts(registry_);

    const ShapeStyleComponent defaultShapeStyle {};
    const TextStyleComponent defaultTextStyle {};

    auto shapeView = registry_.view<const Transform2DComponent, const ShapeComponent>();
    for (auto entity : shapeView)
    {
        if (is_input_transparent(registry_, entity) || !is_visible(registry_, entity))
        {
            continue;
        }
        if (!point_in_rect(inherited_mask_clip_rect(registry_, entity), point))
        {
            continue;
        }

        const auto& transform = shapeView.get<const Transform2DComponent>(entity);
        const auto& shape = shapeView.get<const ShapeComponent>(entity);
        const ShapeStyleComponent* stylePtr = registry_.try_get<ShapeStyleComponent>(entity);
        const ShapeStyleComponent& style = stylePtr ? *stylePtr : defaultShapeStyle;
        const HitRegion2DComponent* hitRegion =
            registry_.try_get<HitRegion2DComponent>(entity);

        const glm::vec4 rect = renderer2d_apply_interactive_visual_rect(
            registry_,
            entity,
            make_bounds(transform, shape.size));
        if (shape_fill_visible(style) || shape_outline_visible(style) ||
            (hitRegion && hitRegion->enabled))
        {
            const float edgePadding = std::max(style.outlineWidth, 0.0f) + std::max(style.edgeSoftness, 0.0f);
            if (point_in_primitive(
                shape.primitive,
                expand_rect(rect, edgePadding),
                padded_corner_radii(shape, edgePadding),
                point))
            {
                return true;
            }
        }

        if (const ShadowComponent* shadow = registry_.try_get<ShadowComponent>(entity);
            shadow && alpha_visible(shadow->color.a * shadow->opacity))
        {
            glm::vec4 shadowRect = rect;
            shadowRect.x += shadow->offset.x - shadow->spread;
            shadowRect.y += shadow->offset.y - shadow->spread;
            shadowRect.z += 2.0f * shadow->spread;
            shadowRect.w += 2.0f * shadow->spread;
            const float shadowPadding = std::max(shadow->blurRadius, 0.0f);
            if (point_in_primitive(
                shape.primitive,
                expand_rect(shadowRect, shadowPadding),
                padded_corner_radii(shape, shadow->spread + shadowPadding),
                point))
            {
                return true;
            }
        }

        if (const BlurComponent* blur = registry_.try_get<BlurComponent>(entity);
            blur && alpha_visible(blur->opacity) && point_in_primitive(shape.primitive, rect, shape.effective_corner_radii(), point))
        {
            return true;
        }
    }

    auto textView = registry_.view<const Transform2DComponent, const TextComponent>();
    for (auto entity : textView)
    {
        if (is_input_transparent(registry_, entity) || !is_visible(registry_, entity))
        {
            continue;
        }
        if (!point_in_rect(inherited_mask_clip_rect(registry_, entity), point))
        {
            continue;
        }

        const auto& transform = textView.get<const Transform2DComponent>(entity);
        const auto& text = textView.get<const TextComponent>(entity);
        const TextStyleComponent* stylePtr = registry_.try_get<TextStyleComponent>(entity);
        const TextStyleComponent& style = stylePtr ? *stylePtr : defaultTextStyle;
        if (!text_visible(style))
        {
            continue;
        }

        const glm::vec4 textBounds = text_bounds_from_component(transform, text);
        const float padding = text_hit_padding(style);
        if (!text.glyphs.empty())
        {
            for (const MSDFGlyph& glyph : text.glyphs)
            {
                if (glyph.size.x <= 0.0f || glyph.size.y <= 0.0f)
                {
                    continue;
                }

                const glm::vec4 glyphRect =
                    renderer2d_apply_interactive_visual_rect(
                        registry_,
                        entity,
                        glm::vec4 {
                    textBounds.x + glyph.position.x * transform.scale.x,
                    textBounds.y + glyph.position.y * transform.scale.y,
                    glyph.size.x * transform.scale.x,
                    glyph.size.y * transform.scale.y
                        });
                if (point_in_rect(expand_rect(glyphRect, padding), point))
                {
                    return true;
                }
            }
            continue;
        }

        if (point_in_rect(
            expand_rect(
                renderer2d_apply_interactive_visual_rect(
                    registry_,
                    entity,
                    textBounds),
                padding),
            point))
        {
            return true;
        }
    }

    auto mediaView = registry_.view<const Transform2DComponent, const Media2DComponent>();
    for (auto entity : mediaView)
    {
        const auto& media = mediaView.get<const Media2DComponent>(entity);
        if (is_input_transparent(registry_, entity) ||
            !media_visible(media) || !is_visible(registry_, entity))
        {
            continue;
        }
        if (!point_in_rect(inherited_mask_clip_rect(registry_, entity), point))
        {
            continue;
        }

        const auto& transform = mediaView.get<const Transform2DComponent>(entity);
        if (point_in_primitive(
            media.primitive,
            renderer2d_apply_interactive_visual_rect(
                registry_,
                entity,
                media_bounds_from_component(transform, media)),
            media.cornerRadius,
            point))
        {
            return true;
        }
    }

    auto modelView = registry_.view<const Transform2DComponent, const Model3DComponent>();
    for (auto entity : modelView)
    {
        const auto& model = modelView.get<const Model3DComponent>(entity);
        if (is_input_transparent(registry_, entity) ||
            !model.visible || !is_visible(registry_, entity) || !alpha_visible(model.materialColor.a))
        {
            continue;
        }

        const auto& transform = modelView.get<const Transform2DComponent>(entity);
        const glm::vec4 viewport = renderer2d_apply_interactive_visual_rect(
            registry_,
            entity,
            make_model_viewport(registry_, entity, transform, model));
        const glm::vec4 clipRect = make_model_clip_rect(registry_, entity, viewport, model);
        if (point_in_rect(intersect_rect(viewport, clipRect), point))
        {
            return true;
        }
    }

    return false;
}

bool Renderer2DScene::draggable_hit_test(glm::vec2 point)
{
    return draggable_parent_at(point) != entt::null;
}

entt::entity Renderer2DScene::draggable_parent_at(glm::vec2 point)
{
    entt::entity current = entity_at(point);
    for (uint32_t depth = 0; depth < 64u && current != entt::null && registry_.valid(current); ++depth)
    {
        if (!is_visible(registry_, current))
        {
            return entt::null;
        }

        if (const DragHandle2DComponent* handle = registry_.try_get<DragHandle2DComponent>(current);
            handle && handle->enabled)
        {
            const entt::entity target = handle->target != entt::null ? handle->target : current;
            return registry_.valid(target) ? target : entt::null;
        }

        if (const ShapeComponent* shape = registry_.try_get<ShapeComponent>(current);
            shape && shape->draggable)
        {
            return current;
        }

        const Parent2DComponent* parent = registry_.try_get<Parent2DComponent>(current);
        if (!parent || parent->parent == entt::null || parent->parent == current)
        {
            break;
        }
        current = parent->parent;
    }

    return entt::null;
}

Renderer2DRenderPlan Renderer2DScene::build_render_plan(double currentTimeSeconds, uint64_t rendererCacheGeneration)
{
    Renderer2DRenderPlan plan;
    build_render_plan(plan, currentTimeSeconds, rendererCacheGeneration);
    return plan;
}

std::optional<Renderer2DShapeVisualState>
Renderer2DScene::resolved_shape_visual_state(
    entt::entity entity,
    double currentTimeSeconds) const
{
    if (entity == entt::null || !registry_.valid(entity) ||
        !is_visible(registry_, entity))
    {
        return std::nullopt;
    }

    const Transform2DComponent* transform =
        registry_.try_get<Transform2DComponent>(entity);
    const ShapeComponent* shape = registry_.try_get<ShapeComponent>(entity);
    if (!transform || !shape)
    {
        return std::nullopt;
    }

    const DisplayTransitionEffect2D displayTransition =
        inherited_visual_effect(registry_, entity, currentTimeSeconds);
    const glm::vec2 interactiveEffects =
        renderer2d_interactive_visual_effects(registry_, entity);
    const ShapeStyleComponent* style =
        registry_.try_get<ShapeStyleComponent>(entity);
    const float styleOpacity = style ?
        std::clamp(style->opacity, 0.0f, 1.0f) : 1.0f;

    Renderer2DShapeVisualState state = {};
    state.rect = apply_display_transition_scale(
        renderer2d_apply_interactive_visual_rect(
            registry_,
            entity,
            make_bounds(*transform, shape->size)),
        displayTransition);
    state.opacity = std::clamp(
        styleOpacity * displayTransition.opacity * interactiveEffects.y,
        0.0f,
        1.0f);
    return state;
}

void Renderer2DScene::build_render_plan(
    Renderer2DRenderPlan& plan,
    double currentTimeSeconds,
    uint64_t rendererCacheGeneration)
{
    plan.clear();
    if (update_display_transition_activity(registry_, currentTimeSeconds))
    {
        mark_dirty();
    }
    resolve_layouts(registry_);

    const ShapeStyleComponent defaultShapeStyle {};
    const TextStyleComponent defaultTextStyle {};

    auto shapeView = registry_.view<const Transform2DComponent, const ShapeComponent>();
    auto textView = registry_.view<const Transform2DComponent, const TextComponent>();
    auto mediaView = registry_.view<const Transform2DComponent, Media2DComponent>();
    auto modelView = registry_.view<const Transform2DComponent, const Model3DComponent>();

    auto update_entity_cache_state = [&](entt::entity entity, Renderer2DCacheComponent& cache) {
        if (update_cache_activity(cache, currentTimeSeconds) ||
            should_refresh_idle_cache(cache, currentTimeSeconds))
        {
            mark_dirty();
        }
    };

    // Cache policy is also attached to non-drawable hierarchy roots (for example,
    // scroll content containers). Advance every policy exactly once so a timed
    // root cannot remain active forever merely because it has no render component.
    auto cacheView = registry_.view<Renderer2DCacheComponent>();
    cacheView.each([&](entt::entity entity, Renderer2DCacheComponent& cache) {
        update_entity_cache_state(entity, cache);
    });

    bool hasVisibleHostedModels = false;
    modelView.each([&](entt::entity entity, const Transform2DComponent& transform, const Model3DComponent& model) {
        const glm::vec4 viewport = renderer2d_apply_interactive_visual_rect(
            registry_,
            entity,
            make_model_viewport(registry_, entity, transform, model));
        const glm::vec4 clipRect = make_model_clip_rect(registry_, entity, viewport, model);
        if (model.visible && is_visible(registry_, entity) && !rect_empty(intersect_rect(viewport, clipRect)))
        {
            hasVisibleHostedModels = true;
        }
    });

    plan.usesHosted3D = hasVisibleHostedModels;
    plan.rebuildCachedLayer = rendererCacheGeneration != cacheGeneration_;
    plan.reserve(
        shapeView.size_hint(),
        shapeView.size_hint(),
        shapeView.size_hint(),
        mediaView.size_hint(),
        textView.size_hint(),
        modelView.size_hint());

    std::unordered_map<uint32_t, EntityDynamicState> dynamicMemo;
    dynamicMemo.reserve(shapeView.size_hint() + textView.size_hint() + mediaView.size_hint() + modelView.size_hint());
    std::vector<entt::entity> dynamicStack;
    dynamicStack.reserve(8);

    shapeView.each([&](entt::entity entity, const Transform2DComponent& transform, const ShapeComponent& shape)
    {
        if (!is_visible(registry_, entity))
        {
            return;
        }

        Renderer2DCacheComponent& cache = cache_or_default(registry_, entity);
        const RenderLayer2DKey layer = render_layer_key(registry_, entity);
        const bool dynamicBatch = is_entity_dynamic_by_hierarchy(
            registry_,
            entity,
            currentTimeSeconds,
            dynamicMemo,
            dynamicStack);
        const bool cachedBatch = !dynamicBatch && cache.mode != Renderer2DCacheMode::eDynamic && plan.rebuildCachedLayer;

        if (!dynamicBatch && !cachedBatch)
        {
            return;
        }

        if (dynamicBatch)
        {
            note_dynamic_emit(cache, currentTimeSeconds);
        }

        const ShapeStyleComponent* stylePtr = registry_.try_get<ShapeStyleComponent>(entity);
        const ShapeStyleComponent& style = stylePtr ? *stylePtr : defaultShapeStyle;
        const DisplayTransitionEffect2D displayTransition =
            inherited_visual_effect(registry_, entity, currentTimeSeconds);
        const glm::vec2 interactiveEffects =
            renderer2d_interactive_visual_effects(registry_, entity);
        const float visualOpacity =
            displayTransition.opacity * interactiveEffects.y;
        const bool foregroundVisible =
            alpha_visible(visualOpacity) &&
            (shape_fill_visible(style) || shape_outline_visible(style));

        Renderer2DBatch batch = {};
        batch.entity = entity;
        batch.stackLayer = layer.stackLayer;
        batch.stackOrder = layer.stackOrder;
        batch.layer = layer.layer;
        batch.order = layer.order;
        batch.alwaysOnTop = layer.alwaysOnTop;
        batch.primitive = shape.primitive;
        batch.rect = apply_display_transition_scale(
            renderer2d_apply_interactive_visual_rect(
                registry_,
                entity,
                make_bounds(transform, shape.size)),
            displayTransition);
        batch.clipRect = inherited_mask_clip_rect(registry_, entity);
        batch.uvRect = shape.primitive == Renderer2DPrimitive::eCircularProgress ?
            circular_progress_parameters(shape) :
            (shape.primitive == Renderer2DPrimitive::eLiquidBridge ?
                glm::vec4(
                    std::clamp(shape.notchAmount, 0.0f, 1.0f),
                    0.0f,
                    0.0f,
                    0.0f) :
                glm::vec4(
                    style.gradientStart.x,
                    style.gradientStart.y,
                    style.gradientEnd.x,
                    style.gradientEnd.y));
        batch.color0 = style.color0;
        batch.color1 = style.color1;
        batch.color2 = style.outlineColor;
        const float transitionShapeBlur = std::max(
            displayTransition.blurRadius,
            interactiveEffects.x);
        batch.effect0 = {
            shape.cornerRadius,
            style.outlineWidth,
            std::max(std::max(style.edgeSoftness, 0.5f), transitionShapeBlur),
            style.opacity * visualOpacity
        };
        const uint32_t transformFlags = transform2_5d_flags(transform);
        batch.effect1 = transformFlags == 0u ? shape_sdf_parameters(shape) : transform2_5d_effect(transform);
        batch.flags = shape_flags(shape, style) | transformFlags;
        if (transitionShapeBlur > 0.001f && transformFlags == 0u)
        {
            batch.flags |= eRenderer2DStyleBlur;
        }
        if ((batch.flags & eRenderer2DStyleCornerRadii) != 0u)
        {
            batch.packedData = pack_corner_radii(shape.effective_corner_radii());
        }
        if (transformFlags == 0u)
        {
            const ShapeMaskClip mask = shape_mask_for_surface(registry_, entity, currentTimeSeconds);
            if (mask.enabled && !rect_empty(mask.rect))
            {
                batch.effect1 = mask.rect;
                batch.flags |= eRenderer2DStyleShapeMask;
                if (mask.cutout) batch.flags |= eRenderer2DStyleShapeCutout;
                batch.flags &= ~eRenderer2DStyleCornerRadii;
                batch.packedData = pack_shape_mask_data(
                    mask.primitive,
                    mask.cornerRadius,
                    mask.squircleAmount,
                    mask.squirclePower,
                    mask.notchAmount,
                    mask.notchDepth);
            }
        }

        if (const ShadowComponent* shadow = registry_.try_get<ShadowComponent>(entity);
            shadow && alpha_visible(
                shadow->color.a * shadow->opacity * visualOpacity))
        {
            Renderer2DBatch shadowBatch = batch;
            const glm::vec4 sourceRect = shadowBatch.rect;
            shadowBatch.rect.x += shadow->offset.x - shadow->spread;
            shadowBatch.rect.y += shadow->offset.y - shadow->spread;
            shadowBatch.rect.z += 2.0f * shadow->spread;
            shadowBatch.rect.w += 2.0f * shadow->spread;
            shadowBatch.color0 = shadow->color;
            shadowBatch.color1 = sourceRect;
            shadowBatch.color2 = shape.effective_corner_radii();
            shadowBatch.effect0 = {
                shape.cornerRadius + shadow->spread,
                shadow->blurRadius,
                shadow->spread,
                shadow->opacity * visualOpacity
            };
            shadowBatch.effect1 = shape_sdf_parameters(shape);
            shadowBatch.flags |= eRenderer2DStyleShadow;
            if (shadow->outsideOnly)
            {
                shadowBatch.flags |= eRenderer2DStyleShadowOutsideOnly;
            }
            if (shadow->excludeShapeExtensions)
            {
                shadowBatch.flags |= eRenderer2DStyleShadowExcludeShapeExtensions;
            }
            shadowBatch.flags &= ~eRenderer2DStyleTransform2_5D;
            if ((shadowBatch.flags & eRenderer2DStyleCornerRadii) != 0u)
            {
                shadowBatch.packedData = pack_corner_radii(padded_corner_radii(shape, shadow->spread));
            }
            (cachedBatch ? plan.cachedShadows : plan.shadows).push_back(shadowBatch);
        }

        const BlurComponent* blur = registry_.try_get<BlurComponent>(entity);
        // Display-transition blur is a foreground primitive effect handled by
        // the shape pipeline above. Only explicit backdrop blur belongs here;
        // otherwise transparent layout roots create temporary rectangular panes.
        const bool explicitBlurVisible = blur &&
            alpha_visible(blur->opacity * visualOpacity);
        const bool styleBlurVisible = !blur &&
            style_backdrop_blur_visible(style) &&
            alpha_visible(visualOpacity);
        if (explicitBlurVisible || styleBlurVisible)
        {
            Renderer2DBatch blurBatch = batch;
            const float blurCornerRadius = blurBatch.effect0.x;
            const float blurEdgeSoftness = blurBatch.effect0.z;
            const float baseBlurRadius = blur ? blur->radius : style.backdropBlurRadius;
            const uint32_t blurPasses = blur ? blur->passes : style.backdropBlurPasses;
            const float baseBlurOpacity = blur ?
                blur->opacity :
                style.backdropBlurOpacity;
            const float blurRadius = baseBlurRadius;
            const float blurOpacity = baseBlurOpacity * visualOpacity;
            blurBatch.color2 = {
                style.gradientStart.x,
                style.gradientStart.y,
                style.gradientEnd.x,
                style.gradientEnd.y
            };
            blurBatch.uvRect = shape_sdf_parameters(shape);
            blurBatch.effect0 = {
                blurRadius,
                static_cast<float>(std::max(blurPasses, 1u)),
                style.backdropBlurReplaceSource ? 1.0f : 0.0f,
                blurOpacity
            };
            blurBatch.effect1 = {
                blurCornerRadius,
                blurEdgeSoftness,
                0.0f,
                0.0f
            };
            blurBatch.flags |= eRenderer2DStyleBlur;
            if (style.backdropBlurFollowsFillAlpha)
            {
                blurBatch.flags |= eRenderer2DStyleBlurFollowsFillAlpha;
            }
            if (style.backdropBlurClipToInheritedMask)
            {
                const ShapeMaskClip mask = shape_mask_for_surface(registry_, entity, currentTimeSeconds);
                if (mask.enabled && !rect_empty(mask.rect))
                {
                    blurBatch.uvRect = {
                        style.gradientStart.x,
                        style.gradientStart.y,
                        style.gradientEnd.x,
                        style.gradientEnd.y
                    };
                    blurBatch.effect1 = mask.rect;
                    blurBatch.color2 = shape_mask_payload(mask);
                    blurBatch.flags |= eRenderer2DStyleBlurInheritedShapeMask;
                    if (mask.cutout) blurBatch.flags |= eRenderer2DStyleShapeCutout;
                }
            }
            blurBatch.flags &= ~(
                eRenderer2DStyleTransform2_5D |
                eRenderer2DStyleCornerRadii |
                eRenderer2DStyleShapeMask);
            blurBatch.packedData = 0u;
            (cachedBatch ? plan.cachedBlurs : plan.blurs).push_back(blurBatch);
        }

        if (foregroundVisible)
        {
            (cachedBatch ? plan.cachedShapes : plan.shapes).push_back(batch);
        }
    });

    mediaView.each([&](entt::entity entity, const Transform2DComponent& transform, Media2DComponent& media)
    {
        update_media_playback(media, currentTimeSeconds);
        if (!media_visible(media) || !is_visible(registry_, entity))
        {
            return;
        }

        const DisplayTransitionEffect2D displayTransition =
            inherited_visual_effect(registry_, entity, currentTimeSeconds);
        const glm::vec2 interactiveEffects =
            renderer2d_interactive_visual_effects(registry_, entity);
        const float visualOpacity =
            displayTransition.opacity * interactiveEffects.y;
        if (!alpha_visible(visualOpacity))
        {
            return;
        }
        const glm::vec4 rect = apply_display_transition_scale(
            renderer2d_apply_interactive_visual_rect(
                registry_,
                entity,
                media_bounds_from_component(transform, media)),
            displayTransition);
        if (rect_empty(rect))
        {
            return;
        }

        Renderer2DCacheComponent& cache = cache_or_default(registry_, entity);
        if (media.animated && media.playing)
        {
            cache.mode = Renderer2DCacheMode::eDynamic;
            cache.restoreStaticWhenIdle = false;
        }

        const RenderLayer2DKey layer = render_layer_key(registry_, entity);
        const bool dynamicBatch = is_entity_dynamic_by_hierarchy(
            registry_,
            entity,
            currentTimeSeconds,
            dynamicMemo,
            dynamicStack);
        const bool cachedBatch = !dynamicBatch && cache.mode != Renderer2DCacheMode::eDynamic && plan.rebuildCachedLayer;

        if (!dynamicBatch && !cachedBatch)
        {
            return;
        }

        if (dynamicBatch)
        {
            note_dynamic_emit(cache, currentTimeSeconds);
        }

        Renderer2DBatch batch = {};
        batch.entity = entity;
        batch.stackLayer = layer.stackLayer;
        batch.stackOrder = layer.stackOrder;
        batch.layer = layer.layer;
        batch.order = layer.order;
        batch.alwaysOnTop = layer.alwaysOnTop;
        // The media pipeline is selected by the render operation. Preserve
        // the component's visual primitive in the push constants so its own
        // pixels, hit testing, and any inherited mask use the same geometry.
        batch.primitive = media.primitive;
        batch.mediaId = media.mediaId;
        batch.frameIndex = media.currentFrame;
        batch.rect = rect;
        batch.clipRect = inherited_mask_clip_rect(registry_, entity);
        batch.uvRect = media_uv_rect_from_component(media, rect);
        batch.color0 = media.tint;
        batch.color1 = media.tintFill == Renderer2DFill::eSolid ? media.tint : media.tintEnd;
        batch.color2 = { media.gradientStart.x, media.gradientStart.y, media.gradientEnd.x, media.gradientEnd.y };
        if (media.primitive == Renderer2DPrimitive::eBarVisualiser)
        {
            // Primitive payload is authored in the media component's pixel
            // space. Apply entity/display-transition scale so the analytic bar
            // mask remains locked to the projected artwork throughout scale-in
            // and scale-out animations.
            const float scaleX = rect.z /
                std::max(std::abs(media.size.x), 0.001f);
            const float scaleY = rect.w /
                std::max(std::abs(media.size.y), 0.001f);
            batch.color1 = media.primitiveData0 * scaleY;
            batch.color2 = {
                media.primitiveData1.x * scaleY,
                media.primitiveData1.y * scaleY,
                media.primitiveData1.z * scaleX,
                media.primitiveData1.w * scaleX
            };

            // The compound primitive occupies the full horizontal span but
            // only the tallest live bar vertically. Tightening the clip keeps
            // dispatch, retained-surface clearing, and compositor damage from
            // expanding to the visualiser's maximum height while the bars are
            // near their idle dot state.
            const float maximumBarHeight = std::clamp(
                std::max(
                    std::max(
                        std::max(batch.color1.x, batch.color1.y),
                        std::max(batch.color1.z, batch.color1.w)),
                    std::max(batch.color2.x, batch.color2.y)),
                0.0f,
                rect.w);
            if (maximumBarHeight > 0.001f)
            {
                const float barSpan = std::min(
                    batch.color2.z * 6.0f + batch.color2.w * 5.0f,
                    rect.z);
                batch.clipRect = intersect_rect(
                    batch.clipRect,
                    {
                        rect.x,
                        rect.y + (rect.w - maximumBarHeight) * 0.5f,
                        std::max(barSpan, 0.0f),
                        maximumBarHeight
                    });
            }
        }
        batch.effect0 = {
            media.cornerRadius,
            media.edgeSoftness,
            std::max(
                media.blurRadius,
                std::max(displayTransition.blurRadius, interactiveEffects.x)),
            media.opacity * visualOpacity
        };
        batch.effect1 = transform2_5d_effect(transform);
        batch.flags = media_flags(media) | transform2_5d_flags(transform);
        if (media.primitive == Renderer2DPrimitive::eBarVisualiser)
        {
            // color1/color2 carry geometry for this primitive, so a component
            // tint gradient cannot share those slots. The visualiser uses its
            // solid tint while the artwork itself still supplies all colour.
            batch.flags &= ~(
                eRenderer2DStyleGradient |
                eRenderer2DStyleRadialGradient);
        }
        if (batch.effect0.z > 0.0f)
        {
            batch.flags |= eRenderer2DStyleBlur;
        }
        if (const ShapeMaskClip mask = inherited_shape_mask_clip(registry_, entity);
            mask.enabled &&
            media.primitive != Renderer2DPrimitive::eBarVisualiser &&
            (batch.flags & eRenderer2DStyleTransform2_5D) == 0u &&
            (batch.flags & eRenderer2DStyleGradient) == 0u)
        {
            batch.effect1 = mask.rect;
            batch.color2 = shape_mask_payload(mask);
            batch.flags |= eRenderer2DStyleShapeMask;
        }
        batch.packedData = pack_media_color_adjustment(media);
        (cachedBatch ? plan.cachedMedia : plan.media).push_back(batch);
    });

    textView.each([&](entt::entity entity, const Transform2DComponent& transform, const TextComponent& text)
    {
        if (!is_visible(registry_, entity))
        {
            return;
        }
        // An empty string has no drawable fallback. Emitting the legacy
        // non-atlas text batch for it produces a small uninitialised glyph
        // rectangle, commonly visible behind icon-only controls.
        if (text.text.empty())
        {
            return;
        }

        Renderer2DCacheComponent& cache = cache_or_default(registry_, entity);
        const RenderLayer2DKey layer = render_layer_key(registry_, entity);
        const bool dynamicBatch = is_entity_dynamic_by_hierarchy(
            registry_,
            entity,
            currentTimeSeconds,
            dynamicMemo,
            dynamicStack);
        const bool cachedBatch = !dynamicBatch && cache.mode != Renderer2DCacheMode::eDynamic && plan.rebuildCachedLayer;

        if (!dynamicBatch && !cachedBatch)
        {
            return;
        }

        if (dynamicBatch)
        {
            note_dynamic_emit(cache, currentTimeSeconds);
        }

        auto& targetTexts = cachedBatch ? plan.cachedTexts : plan.texts;

        const TextStyleComponent* stylePtr = registry_.try_get<TextStyleComponent>(entity);
        const TextStyleComponent& style = stylePtr ? *stylePtr : defaultTextStyle;
        const TextEdgeFade2DComponent* edgeFade =
            registry_.try_get<TextEdgeFade2DComponent>(entity);
        const DisplayTransitionEffect2D displayTransition =
            inherited_visual_effect(registry_, entity, currentTimeSeconds);
        const glm::vec2 interactiveEffects =
            renderer2d_interactive_visual_effects(registry_, entity);
        const float visualOpacity =
            displayTransition.opacity * interactiveEffects.y;
        if (!text_visible(style) || !alpha_visible(visualOpacity))
        {
            return;
        }

        const glm::vec4 textBounds = text_bounds_from_component(transform, text);

        const auto apply_text_edge_fade = [edgeFade](Renderer2DBatch& batch) {
            if (!edgeFade || !edgeFade->enabled ||
                (edgeFade->leftWidth <= 0.0f &&
                    edgeFade->rightWidth <= 0.0f) ||
                rect_empty(batch.clipRect))
            {
                return;
            }
            const float glyphLeft = batch.rect.x;
            const float glyphRight = batch.rect.x + batch.rect.z;
            const float clipLeft = batch.clipRect.x;
            const float clipRight = batch.clipRect.x + batch.clipRect.z;
            const float leftOpaque = clipLeft +
                std::max(edgeFade->leftWidth, 0.0f);
            const float rightOpaque = clipRight -
                std::max(edgeFade->rightWidth, 0.0f);
            const bool intersectsLeft = edgeFade->leftWidth > 0.0f &&
                glyphRight > clipLeft && glyphLeft < leftOpaque;
            const bool intersectsRight = edgeFade->rightWidth > 0.0f &&
                glyphRight > rightOpaque && glyphLeft < clipRight;
            if (!intersectsLeft && !intersectsRight)
            {
                return;
            }

            uint32_t mode = kTextEdgeFadeLeft;
            float fadeStart = clipLeft + 1.0f;
            float fadeEnd = std::max(leftOpaque, fadeStart + 0.25f);
            if (intersectsRight && (!intersectsLeft ||
                glyphLeft + batch.rect.z * 0.5f >=
                    clipLeft + batch.clipRect.z * 0.5f))
            {
                mode = kTextEdgeFadeRight;
                fadeEnd = clipRight - 1.0f;
                fadeStart = std::min(rightOpaque, fadeEnd - 0.25f);
            }
            batch.packedData = pack_text_edge_fade(
                batch.packedData,
                fadeStart - glyphLeft,
                fadeEnd - glyphLeft,
                mode);
            batch.flags |= eRenderer2DStyleTextEdgeFade;
        };

        auto make_text_batch = [&]() {
            Renderer2DBatch batch = {};
            batch.entity = entity;
            batch.stackLayer = layer.stackLayer;
            batch.stackOrder = layer.stackOrder;
            batch.layer = layer.layer;
            batch.order = layer.order;
            batch.alwaysOnTop = layer.alwaysOnTop;
            batch.primitive = Renderer2DPrimitive::eGlyphRun;
            batch.clipRect = inherited_mask_clip_rect(registry_, entity);
            batch.color0 = style.color0;
            batch.color1 = style.color1;
            batch.color2 = style.effectColor;
            batch.shadowColor = style.shadowColor;
            batch.effect0 = {
                std::max(text.msdfPixelRange, 1.0f),
                style.outlineWidth,
                style.glowRadius,
                style.opacity * visualOpacity
            };
            batch.effect1 = {
                style.shadowOffset.x,
                style.shadowOffset.y,
                style.shadowBlur,
                std::max(
                    style.blurRadius,
                    std::max(displayTransition.blurRadius, interactiveEffects.x))
            };
            batch.flags = text_flags(text, style);
            batch.packedData = pack_text_weight_expansion(style.fontWeightExpansion);
            if (batch.effect1.w > 0.0f)
            {
                batch.flags |= eRenderer2DStyleBlur;
            }
            if (const ShapeMaskClip mask = inherited_shape_mask_clip(registry_, entity);
                mask.enabled &&
                (batch.flags & (eRenderer2DStyleOutline | eRenderer2DStyleShadow | eRenderer2DStyleGlow | eRenderer2DStyleBlur)) == 0u)
            {
                batch.effect1 = mask.rect;
                batch.color2 = shape_mask_payload(mask);
                batch.flags |= eRenderer2DStyleShapeMask;
            }
            return batch;
        };

        if (!text.glyphs.empty())
        {
            for (const MSDFGlyph& glyph : text.glyphs)
            {
                if (glyph.size.x <= 0.0f || glyph.size.y <= 0.0f)
                {
                    continue;
                }

                Renderer2DBatch batch = make_text_batch();
                batch.rect = {
                    textBounds.x + glyph.position.x * transform.scale.x,
                    textBounds.y + glyph.position.y * transform.scale.y,
                    glyph.size.x * transform.scale.x,
                    glyph.size.y * transform.scale.y
                };
                batch.rect = apply_display_transition_scale(
                    renderer2d_apply_interactive_visual_rect(
                        registry_,
                        entity,
                        batch.rect),
                    displayTransition);
                batch.uvRect = { glyph.uvMin.x, glyph.uvMin.y, glyph.uvMax.x, glyph.uvMax.y };
                batch.flags |= eRenderer2DStyleAtlasText;
                apply_text_edge_fade(batch);
                targetTexts.push_back(batch);
            }
            return;
        }

        Renderer2DBatch batch = make_text_batch();
        batch.rect = apply_display_transition_scale(
            renderer2d_apply_interactive_visual_rect(
                registry_,
                entity,
                textBounds),
            displayTransition);
        batch.uvRect = { style.gradientStart.x, style.gradientStart.y, style.gradientEnd.x, style.gradientEnd.y };
        apply_text_edge_fade(batch);
        targetTexts.push_back(batch);
    });

    modelView.each([&](entt::entity entity, const Transform2DComponent& transform, const Model3DComponent& model)
    {
        if (!model.visible || !is_visible(registry_, entity))
        {
            return;
        }

        const glm::vec4 viewport = renderer2d_apply_interactive_visual_rect(
            registry_,
            entity,
            make_model_viewport(registry_, entity, transform, model));
        const glm::vec4 clipRect = make_model_clip_rect(registry_, entity, viewport, model);
        if (rect_empty(viewport) || rect_empty(clipRect))
        {
            return;
        }

        Renderer2DCacheComponent& cache = cache_or_default(registry_, entity);
        note_dynamic_emit(cache, currentTimeSeconds);

        const RenderLayer2DKey layer = render_layer_key(registry_, entity);
        const DisplayTransitionEffect2D displayTransition =
            inherited_visual_effect(registry_, entity, currentTimeSeconds);
        const glm::vec2 interactiveEffects =
            renderer2d_interactive_visual_effects(registry_, entity);

        Renderer3DModelBatch batch = {};
        batch.entity = entity;
        batch.modelId = model.modelId;
        batch.firstTriangle = model.firstTriangle;
        batch.triangleCount = model.triangleCount;
        batch.stackLayer = layer.stackLayer;
        batch.stackOrder = layer.stackOrder;
        batch.layer = layer.layer;
        batch.order = layer.order;
        batch.alwaysOnTop = layer.alwaysOnTop;
        batch.viewportRect = apply_display_transition_scale(viewport, displayTransition);
        batch.clipRect = clipRect;
        batch.position = model.position;
        batch.rotationRadians = model.rotationRadians;
        batch.scale = model.scale;
        batch.cameraPosition = model.cameraPosition;
        batch.cameraTarget = model.cameraTarget;
        batch.lightDirection = model.lightDirection;
        batch.materialColor = model.materialColor;
        batch.materialColor.a *=
            displayTransition.opacity * interactiveEffects.y;
        batch.fieldOfViewRadians = model.fieldOfViewRadians;
        batch.nearPlane = model.nearPlane;
        batch.farPlane = model.farPlane;
        plan.models.push_back(batch);
    });

    sort_batches(plan.panelBlurs);
    sort_batches(plan.shadows);
    sort_batches(plan.blurs);
    sort_batches(plan.shapes);
    sort_batches(plan.media);
    sort_batches(plan.texts);
    sort_batches(plan.cachedPanelBlurs);
    sort_batches(plan.cachedShadows);
    sort_batches(plan.cachedBlurs);
    sort_batches(plan.cachedShapes);
    sort_batches(plan.cachedMedia);
    sort_batches(plan.cachedTexts);
}


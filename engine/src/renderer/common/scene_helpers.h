#pragma once
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/renderer/render_types.h>
#include <vibranceUI/renderer/dispatch_bounds.h>
#include <algorithm>
#include <cmath>
#include <unordered_set>
namespace
{
    constexpr uint32_t kTextPassAll = 0;
    constexpr uint32_t kTextPassUnderlay = 1;
    constexpr uint32_t kTextPassForeground = 2;
    constexpr uint32_t kTextPackedExpansionMask = 0xfffu;
    constexpr uint32_t kTextPackedExpansionBias = 2048u;
    constexpr float kTextPackedExpansionScale = 256.0f;
    constexpr uint32_t kTextPackedEdgeFadeMask = 0xffu;
    constexpr uint32_t kTextPackedEdgeFadeStartShift = 14u;
    constexpr uint32_t kTextPackedEdgeFadeEndShift = 22u;
    constexpr uint32_t kTextPackedEdgeFadeModeShift = 30u;
    constexpr uint32_t kTextPackedEdgeFadeModeMask = 0x3u;
    constexpr uint32_t kTextEdgeFadeLeft = 1u;
    constexpr uint32_t kTextEdgeFadeRight = 2u;
    constexpr int32_t kTextPackedEdgeFadeBias = 128;
    constexpr float kTextPackedEdgeFadeScale = 4.0f;
    constexpr uint32_t kDispatchOriginMask = 0xffffu;
    constexpr uint32_t kBlurUseStaticBackdrop = 1u << 31u;
    constexpr uint32_t kBlurUseExternalBackdrop = 1u << 30u;
    constexpr uint32_t kHostedModelDepthMax = 65534u;
    constexpr float kRenderer2DTwoPi = 6.28318530717958647692f;

    struct DispatchBounds
    {
        uint32_t x = 0;
        uint32_t y = 0;
        uint32_t width = 0;
        uint32_t height = 0;

        bool empty() const
        {
            return width == 0 || height == 0;
        }
    };

    bool dispatch_bounds_overlap(
        const DispatchBounds& left,
        const DispatchBounds& right)
    {
        if (left.empty() || right.empty())
        {
            return false;
        }
        return left.x < right.x + right.width &&
            right.x < left.x + left.width &&
            left.y < right.y + right.height &&
            right.y < left.y + left.height;
    }

    DispatchBounds union_dispatch_bounds(
        const DispatchBounds& left,
        const DispatchBounds& right)
    {
        if (left.empty())
        {
            return right;
        }
        if (right.empty())
        {
            return left;
        }
        const uint32_t x = std::min(left.x, right.x);
        const uint32_t y = std::min(left.y, right.y);
        const uint32_t rightEdge = std::max(
            left.x + left.width,
            right.x + right.width);
        const uint32_t bottomEdge = std::max(
            left.y + left.height,
            right.y + right.height);
        return { x, y, rightEdge - x, bottomEdge - y };
    }

    enum class DispatchBoundsMode
    {
        eExact,
        eShape,
        eShadow,
        eText,
        eTextUnderlay,
        eTextForeground
    };

    enum class Renderer2DRenderOpType
    {
        ePanelBlur,
        eShadow,
        eBlur,
        eShape,
        eMedia,
        eModel3D,
        eTextUnderlay,
        eText
    };

    struct Renderer2DRenderOp
    {
        Renderer2DRenderOpType type = Renderer2DRenderOpType::eShape;
        const Renderer2DBatch* batch = nullptr;
        const Renderer3DModelBatch* model = nullptr;
        entt::entity entity = entt::null;
        int32_t stackLayer = 0;
        uint32_t stackOrder = 0;
        int32_t layer = 0;
        uint32_t order = 0;
        bool alwaysOnTop = false;
    };

    struct RenderLayer2DKey
    {
        int32_t stackLayer = 0;
        uint32_t stackOrder = 0;
        int32_t layer = 0;
        uint32_t order = 0;
        bool alwaysOnTop = false;
    };

    glm::vec4 make_bounds(const Transform2DComponent& transform, glm::vec2 size)
    {
        const glm::vec2 scaledSize = glm::max(size * transform.scale, glm::vec2(0.0f));
        const glm::vec2 minPosition = transform.position - transform.origin * scaledSize;
        return { minPosition.x, minPosition.y, scaledSize.x, scaledSize.y };
    }

    bool is_visible(const entt::registry& registry, entt::entity entity)
    {
        entt::entity current = entity;
        for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
        {
            if (const auto* layer = registry.try_get<RenderLayer2DComponent>(current); layer && !layer->visible)
            {
                return false;
            }

            const auto* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent || parent->parent == current)
            {
                break;
            }
            current = parent->parent;
        }
        return true;
    }

    bool is_input_transparent(const entt::registry& registry, entt::entity entity)
    {
        const auto* inputTransparent =
            registry.try_get<InputTransparent2DComponent>(entity);
        return inputTransparent && inputTransparent->enabled;
    }

    uint32_t entity_key(entt::entity entity)
    {
        return static_cast<uint32_t>(entt::to_integral(entity));
    }

    std::vector<entt::entity> collect_entity_tree(entt::registry& registry, entt::entity entity)
    {
        // Destroy and cache operations work on whole UI subtrees, not just roots
        std::vector<entt::entity> entities;
        if (!registry.valid(entity))
        {
            return entities;
        }

        std::vector<entt::entity> stack;
        std::unordered_set<uint32_t> visited;
        stack.push_back(entity);

        while (!stack.empty())
        {
            const entt::entity current = stack.back();
            stack.pop_back();
            if (!registry.valid(current))
            {
                continue;
            }

            const uint32_t key = entity_key(current);
            if (visited.find(key) != visited.end())
            {
                continue;
            }

            visited.insert(key);
            entities.push_back(current);

            auto childView = registry.view<const Parent2DComponent>();
            childView.each([&](entt::entity child, const Parent2DComponent& parent) {
                if (parent.parent == current && child != current)
                {
                    stack.push_back(child);
                }
            });
        }

        return entities;
    }

    bool destroy_entity_tree_now(entt::registry& registry, entt::entity entity)
    {
        std::vector<entt::entity> entities = collect_entity_tree(registry, entity);
        for (auto it = entities.rbegin(); it != entities.rend(); ++it)
        {
            if (registry.valid(*it))
            {
                registry.destroy(*it);
            }
        }
        return !entities.empty();
    }

    entt::entity stack_root_for(const entt::registry& registry, entt::entity entity)
    {
        entt::entity root = entity;
        entt::entity current = entity;
        for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
        {
            const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent ||
                parent->parent == entt::null ||
                parent->parent == current ||
                !registry.valid(parent->parent))
            {
                break;
            }

            root = parent->parent;
            current = parent->parent;
        }
        return root;
    }

    RenderLayer2DKey render_layer_key(const entt::registry& registry, entt::entity entity)
    {
        static const RenderLayer2DComponent defaultLayer {};
        const entt::entity stackRoot = stack_root_for(registry, entity);
        const RenderLayer2DComponent* stackLayer = registry.try_get<RenderLayer2DComponent>(stackRoot);
        const RenderLayer2DComponent* localLayer = registry.try_get<RenderLayer2DComponent>(entity);
        if (!stackLayer)
        {
            stackLayer = &defaultLayer;
        }
        if (!localLayer)
        {
            localLayer = &defaultLayer;
        }

        return {
            stackLayer->layer,
            stackLayer->order,
            localLayer->layer,
            localLayer->order,
            stackLayer->alwaysOnTop || localLayer->alwaysOnTop
        };
    }

    RenderLayer2DKey render_layer_key(const Renderer2DBatch& batch)
    {
        return {
            batch.stackLayer,
            batch.stackOrder,
            batch.layer,
            batch.order,
            batch.alwaysOnTop
        };
    }

    RenderLayer2DKey render_layer_key(const Renderer3DModelBatch& batch)
    {
        return {
            batch.stackLayer,
            batch.stackOrder,
            batch.layer,
            batch.order,
            batch.alwaysOnTop
        };
    }

    bool render_layer_key_less(const RenderLayer2DKey& a, const RenderLayer2DKey& b)
    {
        if (a.alwaysOnTop != b.alwaysOnTop)
        {
            return b.alwaysOnTop;
        }
        if (a.stackLayer != b.stackLayer)
        {
            return a.stackLayer < b.stackLayer;
        }
        if (a.stackOrder != b.stackOrder)
        {
            return a.stackOrder < b.stackOrder;
        }
        if (a.layer != b.layer)
        {
            return a.layer < b.layer;
        }
        return a.order < b.order;
    }

    uint32_t pack_dispatch_origin(uint32_t x, uint32_t y)
    {
        const uint32_t packedX = std::min(x, kDispatchOriginMask);
        const uint32_t packedY = std::min(y, kDispatchOriginMask);
        return packedX | (packedY << 16u);
    }

    uint32_t pack_dispatch_extent(uint32_t width, uint32_t height)
    {
        const uint32_t packedWidth = std::min(width, kDispatchOriginMask);
        const uint32_t packedHeight = std::min(height, kDispatchOriginMask);
        return packedWidth | (packedHeight << 16u);
    }

    uint32_t pack_unorm8(float value)
    {
        return static_cast<uint32_t>(std::round(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    }

    uint32_t pack_range8(float value, float minValue, float maxValue)
    {
        if (maxValue <= minValue)
        {
            return 0u;
        }
        return pack_unorm8((value - minValue) / (maxValue - minValue));
    }

    uint32_t pack_text_weight_expansion(float expansionPixels)
    {
        const int32_t minValue = -static_cast<int32_t>(kTextPackedExpansionBias);
        const int32_t maxValue =
            static_cast<int32_t>(kTextPackedExpansionMask - kTextPackedExpansionBias);
        const int32_t scaled = static_cast<int32_t>(std::round(expansionPixels * kTextPackedExpansionScale));
        const uint32_t packed = static_cast<uint32_t>(
            std::clamp(scaled, minValue, maxValue) + static_cast<int32_t>(kTextPackedExpansionBias));
        return (packed & kTextPackedExpansionMask) << 2u;
    }

    float unpack_text_weight_expansion(uint32_t packedData)
    {
        const uint32_t packed = (packedData >> 2u) & kTextPackedExpansionMask;
        return static_cast<float>(static_cast<int32_t>(packed) - static_cast<int32_t>(kTextPackedExpansionBias)) /
            kTextPackedExpansionScale;
    }

    uint32_t pack_text_edge_fade(
        uint32_t packedData,
        float startOffset,
        float endOffset,
        uint32_t mode)
    {
        const auto packOffset = [](float offset) {
            const int32_t scaled = static_cast<int32_t>(std::round(
                offset * kTextPackedEdgeFadeScale));
            return static_cast<uint32_t>(std::clamp(
                scaled + kTextPackedEdgeFadeBias,
                0,
                static_cast<int32_t>(kTextPackedEdgeFadeMask)));
        };
        const uint32_t payloadMask =
            (kTextPackedEdgeFadeMask << kTextPackedEdgeFadeStartShift) |
            (kTextPackedEdgeFadeMask << kTextPackedEdgeFadeEndShift) |
            (kTextPackedEdgeFadeModeMask << kTextPackedEdgeFadeModeShift);
        return (packedData & ~payloadMask) |
            ((packOffset(startOffset) & kTextPackedEdgeFadeMask) <<
                kTextPackedEdgeFadeStartShift) |
            ((packOffset(endOffset) & kTextPackedEdgeFadeMask) <<
                kTextPackedEdgeFadeEndShift) |
            ((mode & kTextPackedEdgeFadeModeMask) <<
                kTextPackedEdgeFadeModeShift);
    }

    uint32_t pack_shape_mask_data(Renderer2DPrimitive primitive, float radius)
    {
        const uint32_t packedRadius = static_cast<uint32_t>(
            std::round(std::clamp(radius, 0.0f, 255.0f))) & 0xffu;
        const uint32_t packedPrimitive = static_cast<uint32_t>(primitive) & 0x0fu;
        const uint32_t packedAmount = 15u;
        const uint32_t packedPower = 10u;
        return packedRadius |
            (packedPrimitive << 8u) |
            (packedAmount << 12u) |
            (packedPower << 16u);
    }

    uint32_t pack_shape_mask_data(
        Renderer2DPrimitive primitive,
        float radius,
        float squircleAmount,
        float squirclePower,
        float notchAmount,
        float notchDepth)
    {
        const uint32_t packedRadius = static_cast<uint32_t>(
            std::round(std::clamp(radius, 0.0f, 255.0f))) & 0xffu;
        const uint32_t packedPrimitive = static_cast<uint32_t>(primitive) & 0x0fu;
        const uint32_t packedAmount = static_cast<uint32_t>(
            std::round(std::clamp(squircleAmount, 0.0f, 1.0f) * 15.0f)) & 0x0fu;
        const uint32_t packedPower = static_cast<uint32_t>(
            std::round(((std::clamp(squirclePower, 2.0f, 5.0f) - 2.0f) / 3.0f) * 15.0f)) & 0x0fu;
        const uint32_t packedNotchAmount = static_cast<uint32_t>(
            std::round(std::clamp(notchAmount, 0.0f, 1.0f) * 15.0f)) & 0x0fu;
        const uint32_t packedNotchDepth = static_cast<uint32_t>(
            std::round(std::clamp(notchDepth, 0.0f, 255.0f))) & 0xffu;
        return packedRadius |
            (packedPrimitive << 8u) |
            (packedAmount << 12u) |
            (packedPower << 16u) |
            (packedNotchAmount << 20u) |
            (packedNotchDepth << 24u);
    }

    uint32_t pack_corner_radii(glm::vec4 radii)
    {
        auto pack_radius = [](float radius) {
            return static_cast<uint32_t>(std::round(std::clamp(radius, 0.0f, 255.0f))) & 0xffu;
        };

        return pack_radius(radii.x) |
            (pack_radius(radii.y) << 8u) |
            (pack_radius(radii.z) << 16u) |
            (pack_radius(radii.w) << 24u);
    }

    glm::vec4 padded_corner_radii(const ShapeComponent& shape, float padding)
    {
        return glm::max(shape.effective_corner_radii() + glm::vec4(std::max(padding, 0.0f)), glm::vec4(0.0f));
    }

    glm::vec4 shape_sdf_parameters(const ShapeComponent& shape)
    {
        return {
            std::clamp(shape.squircleAmount, 0.0f, 1.0f),
            std::clamp(shape.squirclePower, 2.0f, 5.0f),
            std::clamp(shape.notchAmount, 0.0f, 1.0f),
            std::max(shape.notchDepth, 0.0f)
        };
    }

    glm::vec4 circular_progress_parameters(const ShapeComponent& shape)
    {
        return {
            std::clamp(shape.arcProgress, 0.0f, 1.0f),
            std::max(shape.arcThickness, 0.0f),
            shape.arcStartAngleRadians,
            shape.arcClockwise ? 1.0f : -1.0f
        };
    }

    float notched_squircle_flare_size(glm::vec2 size, float amount, float depth)
    {
        constexpr float morphThreshold = 0.85f;
        const float progress = std::clamp(
            (std::clamp(amount, 0.0f, 1.0f) - morphThreshold) / (1.0f - morphThreshold),
            0.0f,
            1.0f);
        const float depthTarget = depth > 0.5f ? depth : std::clamp(size.y * 0.62f, 0.0f, 92.0f);
        const float flareTarget = std::clamp(
            depthTarget * 0.18f,
            0.0f,
            size.y * 0.5f);
        const float sizeRatio = std::clamp((size.y * 0.5f) / std::max(flareTarget, 0.001f), 0.0f, 1.0f);
        const float flareT = sizeRatio * progress;
        return flareTarget * flareT * flareT;
    }

    float notched_squircle_dispatch_padding(const Renderer2DBatch& batch, glm::vec4 shapeParams)
    {
        if (batch.primitive != Renderer2DPrimitive::eNotchedSquircle ||
            (batch.flags & eRenderer2DStyleTransform2_5D) != 0u)
        {
            return 0.0f;
        }

        const glm::vec2 size { std::max(batch.rect.z, 1.0f), std::max(batch.rect.w, 1.0f) };
        return notched_squircle_flare_size(size, shapeParams.z, shapeParams.w) + 6.0f;
    }

    uint32_t workgroup_count(uint32_t pixelCount)
    {
        return (pixelCount + 7u) / 8u;
    }

    float transform_dispatch_padding(const Renderer2DBatch& batch)
    {
        if ((batch.flags & eRenderer2DStyleTransform2_5D) == 0u)
        {
            return 0.0f;
        }

        return std::sqrt(batch.rect.z * batch.rect.z + batch.rect.w * batch.rect.w) * 0.5f;
    }

    float dispatch_padding(const Renderer2DBatch& batch, DispatchBoundsMode mode)
    {
        const float transformPadding = transform_dispatch_padding(batch);
        switch (mode)
        {
        case DispatchBoundsMode::eShape:
            return transformPadding +
                std::max(batch.effect0.y, 0.0f) +
                std::max(batch.effect0.z, 0.0f) +
                notched_squircle_dispatch_padding(batch, batch.effect1);
        case DispatchBoundsMode::eShadow:
            return std::max(batch.effect0.y, 0.5f) + notched_squircle_dispatch_padding(batch, batch.effect1);
        case DispatchBoundsMode::eTextUnderlay:
        {
            const float weightPad = std::max(unpack_text_weight_expansion(batch.packedData), 0.0f);
            const float glowPad = std::max(batch.effect0.z, 0.0f);
            const bool shapeMaskedText = (batch.flags & eRenderer2DStyleShapeMask) != 0u;
            const float shadowPad = shapeMaskedText ? 0.0f :
                std::max(std::abs(batch.effect1.x), std::abs(batch.effect1.y)) +
                    std::max(batch.effect1.z, 0.0f);
            return std::max(std::max(glowPad, shadowPad), weightPad) + 2.0f;
        }
        case DispatchBoundsMode::eTextForeground:
        {
            const float weightPad = std::max(unpack_text_weight_expansion(batch.packedData), 0.0f);
            const float textBlurPad = (batch.flags & eRenderer2DStyleShapeMask) != 0u ?
                0.0f :
                std::max(batch.effect1.w, 0.0f);
            return std::max(
                std::max(std::max(batch.effect0.y, 0.0f), textBlurPad),
                weightPad) + 2.0f;
        }
        case DispatchBoundsMode::eText:
        {
            const float weightPad = std::max(unpack_text_weight_expansion(batch.packedData), 0.0f);
            const bool shapeMaskedText = (batch.flags & eRenderer2DStyleShapeMask) != 0u;
            const float textBlurPad = shapeMaskedText ? 0.0f : batch.effect1.w;
            const float glyphPad = std::max(std::max(std::max(batch.effect0.y, batch.effect0.z), textBlurPad),
                weightPad);
            const float shadowPad = shapeMaskedText ? 0.0f :
                std::max(std::abs(batch.effect1.x), std::abs(batch.effect1.y)) +
                    std::max(batch.effect1.z, 0.0f);
            return std::max(glyphPad, shadowPad) + 2.0f;
        }
        case DispatchBoundsMode::eExact:
        default:
            return transformPadding;
        }
    }

    glm::vec4 expand_and_clip_rect(glm::vec4 rect, float padding, glm::vec4 clipRect)
    {
        const float safePadding = std::max(padding, 0.0f);
        rect.x -= safePadding;
        rect.y -= safePadding;
        rect.z += safePadding * 2.0f;
        rect.w += safePadding * 2.0f;

        const float minX = std::max(rect.x, clipRect.x);
        const float minY = std::max(rect.y, clipRect.y);
        const float maxX = std::min(rect.x + rect.z, clipRect.x + clipRect.z);
        const float maxY = std::min(rect.y + rect.w, clipRect.y + clipRect.w);
        return {
            minX,
            minY,
            std::max(maxX - minX, 0.0f),
            std::max(maxY - minY, 0.0f)
        };
    }

    DispatchBounds make_dispatch_bounds(const auto& swapchain, glm::vec4 rect, float padding,
        bool coverWorkgroups = true)
    {
        const int32_t screenWidth = static_cast<int32_t>(swapchain.extent.width);
        const int32_t screenHeight = static_cast<int32_t>(swapchain.extent.height);
        if (screenWidth <= 0 || screenHeight <= 0 || rect.z <= 0.0f || rect.w <= 0.0f)
        {
            return {};
        }

        const int32_t minX = std::clamp(static_cast<int32_t>(std::floor(rect.x - padding)), 0, screenWidth);
        const int32_t minY = std::clamp(static_cast<int32_t>(std::floor(rect.y - padding)), 0, screenHeight);
        const int32_t maxX = std::clamp(static_cast<int32_t>(std::ceil(rect.x + rect.z + padding)), 0, screenWidth);
        const int32_t maxY = std::clamp(static_cast<int32_t>(std::ceil(rect.y + rect.w + padding)), 0, screenHeight);

        if (maxX <= minX || maxY <= minY)
        {
            return {};
        }

        const auto axisExtent = coverWorkgroups ?
            renderer2d_dispatch_axis_coverage : renderer2d_dispatch_axis_extent;
        return {
            static_cast<uint32_t>(minX),
            static_cast<uint32_t>(minY),
            axisExtent(
                static_cast<uint32_t>(minX),
                static_cast<uint32_t>(maxX - minX),
                static_cast<uint32_t>(screenWidth)),
            axisExtent(
                static_cast<uint32_t>(minY),
                static_cast<uint32_t>(maxY - minY),
                static_cast<uint32_t>(screenHeight))
        };
    }

    DispatchBounds make_dispatch_bounds(
        const auto& swapchain,
        const Renderer2DBatch& batch,
        DispatchBoundsMode mode,
        bool coverWorkgroups = true)
    {
        return make_dispatch_bounds(
            swapchain,
            expand_and_clip_rect(batch.rect, dispatch_padding(batch, mode), batch.clipRect),
            0.0f, coverWorkgroups);
    }

    DispatchBounds make_full_screen_bounds(const auto& swapchain)
    {
        return { 0u, 0u, swapchain.extent.width, swapchain.extent.height };
    }

    DispatchBounds align_dispatch_bounds_to_workgroups(
        const auto& swapchain,
        const DispatchBounds& bounds)
    {
        if (bounds.empty())
        {
            return {};
        }
        constexpr uint32_t groupSize = 8u;
        const uint32_t x = bounds.x - bounds.x % groupSize;
        const uint32_t y = bounds.y - bounds.y % groupSize;
        const uint32_t right = std::min(
            ((bounds.x + bounds.width + groupSize - 1u) / groupSize) * groupSize,
            swapchain.extent.width);
        const uint32_t bottom = std::min(
            ((bounds.y + bounds.height + groupSize - 1u) / groupSize) * groupSize,
            swapchain.extent.height);
        return { x, y, right - x, bottom - y };
    }

    DispatchBounds make_blur_dispatch_bounds(
        const auto& swapchain,
        const Renderer2DBatch& batch)
    {
        return make_dispatch_bounds(
            swapchain,
            expand_and_clip_rect(
                batch.rect,
                std::max(
                    std::max(batch.effect0.x, 0.0f),
                    std::max(batch.effect1.z, 0.0f)) +
                    notched_squircle_dispatch_padding(batch, batch.uvRect),
                batch.clipRect),
            0.0f);
    }

    Renderer2DCacheComponent& cache_or_default(entt::registry& registry, entt::entity entity)
    {
        return registry.get_or_emplace<Renderer2DCacheComponent>(entity);
    }

    bool cache_bypasses_static_layer_for_dirty(const Renderer2DCacheComponent& cache)
    {
        return cache.mode == Renderer2DCacheMode::eDynamic ||
            (cache.mode == Renderer2DCacheMode::eTimed && cache.detachedFromStaticLayer);
    }

    bool affects_static_cache(const entt::registry& registry, entt::entity entity)
    {
        entt::entity current = entity;
        for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
        {
            if (const Renderer2DCacheComponent* cache = registry.try_get<Renderer2DCacheComponent>(current);
                cache && cache_bypasses_static_layer_for_dirty(*cache) && (current == entity || cache->propagateToChildren))
            {
                return false;
            }

            const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent || parent->parent == current)
            {
                break;
            }
            current = parent->parent;
        }
        return true;
    }

    bool update_cache_activity(Renderer2DCacheComponent& cache, double currentTimeSeconds)
    {
        if (cache.pendingActiveSeconds > 0.0f)
        {
            cache.activeUntilSeconds = std::max(cache.activeUntilSeconds,
                currentTimeSeconds + static_cast<double>(cache.pendingActiveSeconds));
            cache.pendingActiveSeconds = 0.0f;
            cache.wasActive = true;
        }

        const bool active = cache.activeUntilSeconds > currentTimeSeconds;
        if (active)
        {
            cache.wasActive = true;
            return false;
        }

        if (cache.wasActive)
        {
            cache.wasActive = false;
            cache.detachedFromStaticLayer = false;
            cache.activeFrameRateLimit = 0u;
            if (cache.restoreStaticWhenIdle)
            {
                cache.mode = Renderer2DCacheMode::eStatic;
                cache.restoreStaticWhenIdle = false;
                cache.pendingActiveSeconds = 0.0f;
                cache.activeUntilSeconds = 0.0;
            }
            return true;
        }

        return false;
    }

    bool should_refresh_idle_cache(Renderer2DCacheComponent& cache, double currentTimeSeconds)
    {
        if (cache.mode != Renderer2DCacheMode::eTimed ||
            cache.activeUntilSeconds > currentTimeSeconds ||
            cache.idleTickRate <= 0.0f)
        {
            return false;
        }

        const double minInterval = 1.0 / static_cast<double>(cache.idleTickRate);
        if (cache.lastDynamicSeconds < 0.0 || currentTimeSeconds - cache.lastDynamicSeconds >= minInterval)
        {
            cache.lastDynamicSeconds = currentTimeSeconds;
            return true;
        }

        return false;
    }

    bool is_cache_dynamic_now(const Renderer2DCacheComponent& cache, double currentTimeSeconds)
    {
        return cache.mode == Renderer2DCacheMode::eDynamic ||
            (cache.mode == Renderer2DCacheMode::eTimed && cache.activeUntilSeconds > currentTimeSeconds);
    }

    void note_dynamic_emit(Renderer2DCacheComponent& cache, double currentTimeSeconds)
    {
        if (is_cache_dynamic_now(cache, currentTimeSeconds))
        {
            cache.lastDynamicSeconds = currentTimeSeconds;
        }
    }

    float display_transition_cycle_seconds(const DisplayTransition2DComponent& transition)
    {
        if (!transition.enabled || transition.durationSeconds <= 0.0f)
        {
            return 0.0f;
        }

        return transition.durationSeconds / std::max(transition.speed, 0.001f);
    }

    float display_transition_total_seconds(const DisplayTransition2DComponent& transition)
    {
        return display_transition_cycle_seconds(transition) *
            static_cast<float>(std::max(transition.repeatCount, 1u));
    }

    double display_transition_elapsed_seconds(
        const DisplayTransition2DComponent& transition,
        double currentTimeSeconds)
    {
        if (!transition.hasStarted)
        {
            return 0.0;
        }

        return std::max(currentTimeSeconds - transition.startSeconds, 0.0);
    }

    bool display_transition_complete(
        const DisplayTransition2DComponent& transition,
        double currentTimeSeconds)
    {
        if (!transition.enabled || transition.durationSeconds <= 0.0f)
        {
            return true;
        }

        if (!transition.hasStarted)
        {
            return false;
        }

        return display_transition_elapsed_seconds(transition, currentTimeSeconds) >=
            static_cast<double>(display_transition_total_seconds(transition));
    }

    struct EntityDynamicState
    {
        bool self = false;
        bool propagatesToChildren = false;
    };

    EntityDynamicState entity_dynamic_state(
        const entt::registry& registry,
        entt::entity entity,
        double currentTimeSeconds,
        std::unordered_map<uint32_t, EntityDynamicState>& memo,
        std::vector<entt::entity>& stack)
    {
        if (entity == entt::null || !registry.valid(entity))
        {
            return {};
        }

        const uint32_t key = entity_key(entity);
        if (const auto it = memo.find(key); it != memo.end())
        {
            return it->second;
        }

        if (std::find(stack.begin(), stack.end(), entity) != stack.end())
        {
            memo[key] = {};
            return {};
        }

        stack.push_back(entity);

        EntityDynamicState state {};
        if (const Renderer2DCacheComponent* cache = registry.try_get<Renderer2DCacheComponent>(entity))
        {
            const bool cacheDynamic = is_cache_dynamic_now(*cache, currentTimeSeconds);
            state.self = cacheDynamic;
            state.propagatesToChildren = cacheDynamic && cache->propagateToChildren;
        }

        if (const DisplayTransition2DComponent* transition = registry.try_get<DisplayTransition2DComponent>(entity))
        {
            const bool transitionDynamic = transition->enabled &&
                transition->durationSeconds > 0.0f &&
                !display_transition_complete(*transition, currentTimeSeconds);
            if (transitionDynamic)
            {
                state.self = true;
                state.propagatesToChildren = state.propagatesToChildren || transition->inheritToChildren;
            }
        }

        if (const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(entity);
            parent && parent->parent != entt::null && registry.valid(parent->parent))
        {
            const EntityDynamicState parentState = entity_dynamic_state(
                registry,
                parent->parent,
                currentTimeSeconds,
                memo,
                stack);
            if (parentState.propagatesToChildren)
            {
                state.self = true;
                state.propagatesToChildren = true;
            }
        }

        stack.pop_back();
        memo[key] = state;
        return state;
    }

    bool is_entity_dynamic_by_hierarchy(
        const entt::registry& registry,
        entt::entity entity,
        double currentTimeSeconds,
        std::unordered_map<uint32_t, EntityDynamicState>& memo,
        std::vector<entt::entity>& stack)
    {
        return entity_dynamic_state(registry, entity, currentTimeSeconds, memo, stack).self;
    }

    float cubic_bezier_axis(float t, float p1, float p2)
    {
        const float inv = 1.0f - t;
        return 3.0f * inv * inv * t * p1 + 3.0f * inv * t * t * p2 + t * t * t;
    }

    float cubic_bezier_axis_derivative(float t, float p1, float p2)
    {
        const float inv = 1.0f - t;
        return 3.0f * inv * inv * p1 +
            6.0f * inv * t * (p2 - p1) +
            3.0f * t * t * (1.0f - p2);
    }

    float cubic_bezier_curve(float progress, glm::vec4 controlPoints)
    {
        progress = std::clamp(progress, 0.0f, 1.0f);
        float t = progress;
        for (uint32_t i = 0; i < 6u; ++i)
        {
            const float x = cubic_bezier_axis(t, controlPoints.x, controlPoints.z) - progress;
            const float derivative = cubic_bezier_axis_derivative(t, controlPoints.x, controlPoints.z);
            if (std::abs(x) < 0.0001f || std::abs(derivative) < 0.0001f)
            {
                break;
            }
            t = std::clamp(t - x / derivative, 0.0f, 1.0f);
        }

        float minT = 0.0f;
        float maxT = 1.0f;
        for (uint32_t i = 0; i < 8u; ++i)
        {
            const float x = cubic_bezier_axis(t, controlPoints.x, controlPoints.z);
            if (std::abs(x - progress) < 0.0001f)
            {
                break;
            }
            if (x < progress)
            {
                minT = t;
            }
            else
            {
                maxT = t;
            }
            t = (minT + maxT) * 0.5f;
        }

        return cubic_bezier_axis(t, controlPoints.y, controlPoints.w);
    }

    float spring_response_value(float timeSeconds, float angularFrequency, float dampingRatio)
    {
        timeSeconds = std::max(timeSeconds, 0.0f);
        angularFrequency = std::max(angularFrequency, 0.001f);
        dampingRatio = std::max(dampingRatio, 0.001f);

        if (dampingRatio < 1.0f)
        {
            const float dampedFrequency = angularFrequency * std::sqrt(std::max(1.0f - dampingRatio * dampingRatio, 0.001f));
            const float envelope = std::exp(-dampingRatio * angularFrequency * timeSeconds);
            const float phase = dampedFrequency * timeSeconds;
            return 1.0f - envelope * (
                std::cos(phase) +
                (dampingRatio / std::sqrt(std::max(1.0f - dampingRatio * dampingRatio, 0.001f))) *
                    std::sin(phase));
        }

        if (std::abs(dampingRatio - 1.0f) < 0.001f)
        {
            return 1.0f - std::exp(-angularFrequency * timeSeconds) * (1.0f + angularFrequency * timeSeconds);
        }

        const float root = std::sqrt(std::max(dampingRatio * dampingRatio - 1.0f, 0.001f));
        const float r1 = -angularFrequency * (dampingRatio - root);
        const float r2 = -angularFrequency * (dampingRatio + root);
        const float c1 = r2 / (r1 - r2);
        const float c2 = -r1 / (r1 - r2);
        return 1.0f + c1 * std::exp(r1 * timeSeconds) + c2 * std::exp(r2 * timeSeconds);
    }

    float display_transition_curve(const DisplayTransition2DComponent& transition, float progress)
    {
        progress = std::clamp(progress, 0.0f, 1.0f);
        if (progress >= 1.0f)
        {
            return 1.0f;
        }

        switch (transition.curve)
        {
        case DisplayTransitionCurve2D::eDefault:
            return cubic_bezier_curve(progress, { 0.25f, 0.10f, 0.25f, 1.0f });
        case DisplayTransitionCurve2D::eEaseIn:
            return cubic_bezier_curve(progress, { 0.42f, 0.0f, 1.0f, 1.0f });
        case DisplayTransitionCurve2D::eEaseOut:
            return cubic_bezier_curve(progress, { 0.0f, 0.0f, 0.58f, 1.0f });
        case DisplayTransitionCurve2D::eEaseInOut:
            return cubic_bezier_curve(progress, { 0.42f, 0.0f, 0.58f, 1.0f });
        case DisplayTransitionCurve2D::eTimingCurve:
            return cubic_bezier_curve(progress, transition.timingCurve);
        case DisplayTransitionCurve2D::eInterpolatingSpring:
        {
            const float angularFrequency = std::sqrt(std::max(transition.springStiffness, 0.001f));
            const float dampingRatio = transition.springDamping / (2.0f * angularFrequency);
            return spring_response_value(
                progress * std::max(transition.durationSeconds, 0.001f),
                angularFrequency,
                dampingRatio);
        }
        case DisplayTransitionCurve2D::eInteractiveSpring:
        case DisplayTransitionCurve2D::eSpring:
        {
            const float angularFrequency = kRenderer2DTwoPi / std::max(transition.springResponse, 0.001f);
            const float effectiveDuration = std::max(
                transition.durationSeconds + transition.springBlendDuration,
                transition.springResponse);
            return spring_response_value(
                progress * effectiveDuration,
                angularFrequency,
                transition.springDampingFraction);
        }
        case DisplayTransitionCurve2D::eLinear:
        default:
            return progress;
        }
    }

    float display_transition_progress(const DisplayTransition2DComponent& transition, double currentTimeSeconds)
    {
        if (!transition.enabled)
        {
            return 1.0f;
        }

        if (transition.durationSeconds <= 0.0f)
        {
            return 1.0f;
        }

        if (!transition.hasStarted)
        {
            return 0.0f;
        }

        const float cycleSeconds = display_transition_cycle_seconds(transition);
        if (cycleSeconds <= 0.0f)
        {
            return 1.0f;
        }

        const double elapsed = display_transition_elapsed_seconds(transition, currentTimeSeconds);
        const float totalSeconds = display_transition_total_seconds(transition);
        if (elapsed >= static_cast<double>(totalSeconds))
        {
            if (transition.autoreverses && (std::max(transition.repeatCount, 1u) % 2u) == 0u)
            {
                return 0.0f;
            }
            return 1.0f;
        }

        const float cyclePosition = static_cast<float>(elapsed / static_cast<double>(cycleSeconds));
        const uint32_t cycleIndex = static_cast<uint32_t>(std::floor(cyclePosition));
        float progress = cyclePosition - static_cast<float>(cycleIndex);
        if (transition.autoreverses && (cycleIndex % 2u) == 1u)
        {
            progress = 1.0f - progress;
        }
        return std::clamp(progress, 0.0f, 1.0f);
    }

    struct DisplayTransitionEffect2D
    {
        float opacity = 1.0f;
        float blurRadius = 0.0f;
        glm::vec2 scale { 1.0f };
        glm::vec2 scaleOrigin { 0.0f };
        bool hasScale = false;
    };

    glm::vec2 display_transition_origin(const entt::registry& registry, entt::entity entity)
    {
        const Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
        if (!transform)
        {
            return glm::vec2(0.0f);
        }

        glm::vec2 size(0.0f);
        if (const ShapeComponent* shape = registry.try_get<ShapeComponent>(entity))
        {
            size = glm::max(shape->size * transform->scale, glm::vec2(0.0f));
        }
        else if (const Media2DComponent* media = registry.try_get<Media2DComponent>(entity))
        {
            size = glm::max(media->size * transform->scale, glm::vec2(0.0f));
        }
        else if (const Model3DComponent* model = registry.try_get<Model3DComponent>(entity))
        {
            size = glm::max(model->size * transform->scale, glm::vec2(0.0f));
        }
        else if (const TextComponent* text = registry.try_get<TextComponent>(entity))
        {
            size = {
                std::max(text->bounds.x, static_cast<float>(text->text.size()) * text->fontSize * 0.55f),
                std::max(text->bounds.y, text->fontSize * 1.25f)
            };
            size = glm::max(size * transform->scale, glm::vec2(0.0f));
        }

        return transform->position - transform->origin * size + size * 0.5f;
    }

    DisplayTransitionEffect2D display_transition_effect(
        const DisplayTransition2DComponent& transition,
        double currentTimeSeconds)
    {
        if (!transition.enabled)
        {
            return {};
        }

        const float progress = display_transition_curve(
            transition,
            display_transition_progress(transition, currentTimeSeconds));
        return {
            std::clamp(glm::mix(transition.fromOpacity, transition.toOpacity, progress), 0.0f, 1.0f),
            std::max(glm::mix(transition.fromBlurRadius, transition.toBlurRadius, progress), 0.0f),
            glm::max(glm::mix(transition.fromScale, transition.toScale, progress), glm::vec2(0.0f)),
            glm::vec2(0.0f),
            false
        };
    }

    DisplayTransitionEffect2D inherited_display_transition_effect(
        const entt::registry& registry,
        entt::entity entity,
        double currentTimeSeconds)
    {
        DisplayTransitionEffect2D effect = {};
        entt::entity current = entity;
        for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
        {
            if (const DisplayTransition2DComponent* transition = registry.try_get<DisplayTransition2DComponent>(current);
                transition && transition->enabled && (current == entity || transition->inheritToChildren))
            {
                const DisplayTransitionEffect2D currentEffect = display_transition_effect(*transition, currentTimeSeconds);
                effect.opacity *= currentEffect.opacity;
                effect.blurRadius = std::max(effect.blurRadius, currentEffect.blurRadius);
                if (std::abs(currentEffect.scale.x - 1.0f) > 0.0001f ||
                    std::abs(currentEffect.scale.y - 1.0f) > 0.0001f)
                {
                    effect.scale *= currentEffect.scale;
                    if (!effect.hasScale)
                    {
                        effect.scaleOrigin = display_transition_origin(registry, current);
                        effect.hasScale = true;
                    }
                }
            }

            const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent || parent->parent == entt::null || parent->parent == current)
            {
                break;
            }
            current = parent->parent;
        }
        return effect;
    }

    glm::vec4 apply_display_transition_scale(glm::vec4 rect, const DisplayTransitionEffect2D& transition)
    {
        if (!transition.hasScale)
        {
            return rect;
        }

        const glm::vec2 minPosition = transition.scaleOrigin +
            (glm::vec2(rect.x, rect.y) - transition.scaleOrigin) * transition.scale;
        const glm::vec2 size = glm::vec2(rect.z, rect.w) * transition.scale;
        return { minPosition.x, minPosition.y, size.x, size.y };
    }

    bool update_display_transition_activity(entt::registry& registry, double currentTimeSeconds)
    {
        std::vector<entt::entity> completed;
        std::vector<entt::entity> destroyTargets;
        bool changed = false;
        auto transitionView = registry.view<DisplayTransition2DComponent>();
        transitionView.each([&](entt::entity entity, DisplayTransition2DComponent& transition) {
            if (!transition.enabled)
            {
                return;
            }

            if (!transition.hasStarted)
            {
                if (!transition.delayScheduled)
                {
                    // Schedule delays from the renderer clock. Callers may use
                    // another monotonic clock (for example GLFW), whose epoch
                    // is not required to match steady_clock.
                    transition.startSeconds =
                        currentTimeSeconds +
                        static_cast<double>(transition.delaySeconds);
                    transition.delayScheduled = true;
                    changed = true;
                }
                if (currentTimeSeconds < transition.startSeconds)
                {
                    return;
                }
                transition.hasStarted = true;
                changed = true;
            }

            const float totalSeconds = display_transition_total_seconds(transition);
            const float elapsedSeconds = static_cast<float>(
                display_transition_elapsed_seconds(transition, currentTimeSeconds));

            if (display_transition_complete(transition, currentTimeSeconds))
            {
                if (transition.destroyEntityTreeOnComplete)
                {
                    destroyTargets.push_back(
                        transition.destroyTarget != entt::null ? transition.destroyTarget : entity);
                    return;
                }

                if (transition.removeWhenComplete)
                {
                    completed.push_back(entity);
                }
                return;
            }

            Renderer2DCacheComponent& cache = cache_or_default(registry, entity);
            const float remainingSeconds = std::max(
                totalSeconds - elapsedSeconds,
                1.0f / 60.0f);
            cache.activeUntilSeconds = std::max(
                cache.activeUntilSeconds,
                currentTimeSeconds + static_cast<double>(remainingSeconds));
            cache.wasActive = true;
        });

        for (entt::entity entity : completed)
        {
            if (registry.valid(entity) && registry.all_of<DisplayTransition2DComponent>(entity))
            {
                registry.remove<DisplayTransition2DComponent>(entity);
                changed = true;
            }
        }

        for (entt::entity entity : destroyTargets)
        {
            changed = destroy_entity_tree_now(registry, entity) || changed;
        }

        return changed;
    }

    struct LayoutResolvedRect
    {
        glm::vec2 position { 0.0f };
        glm::vec2 size { 0.0f };
    };

    glm::vec2 fallback_text_layout_size(const TextComponent& text)
    {
        return {
            std::max(static_cast<float>(text.text.size()) * text.fontSize * 0.55f, 0.0f),
            std::max(text.fontSize * 1.25f, 0.0f)
        };
    }

    glm::vec2 text_layout_size(const TextComponent& text)
    {
        if (text.bounds.x > 0.0f || text.bounds.y > 0.0f)
        {
            return glm::max(text.bounds, glm::vec2(0.0f));
        }

        return fallback_text_layout_size(text);
    }

    glm::vec2 entity_layout_size(const entt::registry& registry, entt::entity entity)
    {
        const Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
        if (!transform)
        {
            return { 0.0f, 0.0f };
        }

        if (const ShapeComponent* shape = registry.try_get<ShapeComponent>(entity))
        {
            return glm::max(shape->size * transform->scale, glm::vec2(0.0f));
        }

        if (const TextComponent* text = registry.try_get<TextComponent>(entity))
        {
            return glm::max(text_layout_size(*text) * transform->scale, glm::vec2(0.0f));
        }

        if (const Model3DComponent* model = registry.try_get<Model3DComponent>(entity))
        {
            const glm::vec2 size = glm::max(model->size, glm::vec2(0.0f));
            return glm::max(size * transform->scale, glm::vec2(0.0f));
        }

        if (const Media2DComponent* media = registry.try_get<Media2DComponent>(entity))
        {
            const glm::vec2 size = glm::max(media->size, glm::vec2(0.0f));
            return glm::max(size * transform->scale, glm::vec2(0.0f));
        }

        return { 0.0f, 0.0f };
    }

    glm::vec2 entity_base_layout_size(const entt::registry& registry, entt::entity entity)
    {
        if (const ShapeComponent* shape = registry.try_get<ShapeComponent>(entity))
        {
            return glm::max(shape->size, glm::vec2(0.0f));
        }

        if (const TextComponent* text = registry.try_get<TextComponent>(entity))
        {
            return text_layout_size(*text);
        }

        if (const Model3DComponent* model = registry.try_get<Model3DComponent>(entity))
        {
            return glm::max(model->size, glm::vec2(0.0f));
        }

        if (const Media2DComponent* media = registry.try_get<Media2DComponent>(entity))
        {
            return glm::max(media->size, glm::vec2(0.0f));
        }

        return { 0.0f, 0.0f };
    }

    LayoutResolvedRect entity_layout_bounds(const entt::registry& registry, entt::entity entity)
    {
        const Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
        if (!transform)
        {
            return {};
        }

        const glm::vec2 size = entity_layout_size(registry, entity);
        return {
            transform->position - transform->origin * size,
            size
        };
    }

    LayoutResolvedRect entity_content_rect(const entt::registry& registry, entt::entity entity)
    {
        LayoutResolvedRect rect = entity_layout_bounds(registry, entity);
        if (const LayoutRect2DComponent* layoutRect = registry.try_get<LayoutRect2DComponent>(entity))
        {
            rect.position.x += layoutRect->padding.x;
            rect.position.y += layoutRect->padding.y;
            rect.size.x = std::max(rect.size.x - layoutRect->padding.x - layoutRect->padding.z, 0.0f);
            rect.size.y = std::max(rect.size.y - layoutRect->padding.y - layoutRect->padding.w, 0.0f);
        }
        return rect;
    }

    glm::vec4 rect_from_layout(const LayoutResolvedRect& rect)
    {
        return { rect.position.x, rect.position.y, rect.size.x, rect.size.y };
    }

    bool rect_empty(glm::vec4 rect)
    {
        return rect.z <= 0.0f || rect.w <= 0.0f;
    }

    glm::vec4 expand_rect(glm::vec4 rect, float amount)
    {
        const float padding = std::max(amount, 0.0f);
        return {
            rect.x - padding,
            rect.y - padding,
            rect.z + padding * 2.0f,
            rect.w + padding * 2.0f
        };
    }

    glm::vec4 intersect_rect(glm::vec4 a, glm::vec4 b)
    {
        const float minX = std::max(a.x, b.x);
        const float minY = std::max(a.y, b.y);
        const float maxX = std::min(a.x + a.z, b.x + b.z);
        const float maxY = std::min(a.y + a.w, b.y + b.w);
        return {
            minX,
            minY,
            std::max(maxX - minX, 0.0f),
            std::max(maxY - minY, 0.0f)
        };
    }

    glm::vec4 unclipped_rect()
    {
        return {
            -1000000.0f,
            -1000000.0f,
            2000000.0f,
            2000000.0f
        };
    }

    glm::vec4 entity_mask_rect(const entt::registry& registry, entt::entity entity, const Mask2DComponent& mask)
    {
        return expand_rect(
            rect_from_layout(mask.useContentRect ? entity_content_rect(registry, entity) : entity_layout_bounds(registry, entity)),
            mask.effectPadding);
    }

    struct ShapeMaskClip
    {
        bool enabled = false;
        Renderer2DPrimitive primitive = Renderer2DPrimitive::eRectangle;
        glm::vec4 rect { 0.0f };
        float cornerRadius = 0.0f;
        float squircleAmount = 1.0f;
        float squirclePower = 4.0f;
        float notchAmount = 0.0f;
        float notchDepth = 0.0f;
        bool cutout = false;
    };

    glm::vec4 shape_mask_payload(const ShapeMaskClip& mask)
    {
        const uint32_t packedAmount = static_cast<uint32_t>(
            std::round(std::clamp(mask.squircleAmount, 0.0f, 1.0f) * 15.0f)) & 0x0fu;
        const uint32_t packedPower = static_cast<uint32_t>(
            std::round(((std::clamp(mask.squirclePower, 2.0f, 5.0f) - 2.0f) / 3.0f) * 15.0f)) & 0x0fu;
        const uint32_t packedNotchAmount = static_cast<uint32_t>(
            std::round(std::clamp(mask.notchAmount, 0.0f, 1.0f) * 15.0f)) & 0x0fu;
        const uint32_t packedNotchShape =
            packedAmount |
            (packedPower << 4u) |
            (packedNotchAmount << 8u);

        return {
            static_cast<float>(mask.primitive),
            std::max(mask.cornerRadius, 0.0f),
            mask.primitive == Renderer2DPrimitive::eNotchedSquircle ?
                static_cast<float>(packedNotchShape) :
                std::clamp(mask.squircleAmount, 0.0f, 1.0f),
            mask.primitive == Renderer2DPrimitive::eNotchedSquircle ?
                std::max(mask.notchDepth, 0.0f) :
                std::clamp(mask.squirclePower <= 0.001f ? 4.0f : mask.squirclePower, 2.0f, 5.0f)
        };
    }

    bool shape_mask_clip_needed(const ShapeComponent& shape)
    {
        if (shape.primitive == Renderer2DPrimitive::eRectangle)
        {
            return false;
        }
        if (shape.primitive == Renderer2DPrimitive::eRoundedRectangle &&
            shape.cornerRadius <= 0.001f &&
            !shape.customCornerRadii)
        {
            return false;
        }
        return true;
    }

    glm::vec4 inherited_mask_clip_rect(const entt::registry& registry, entt::entity entity)
    {
        glm::vec4 clip = unclipped_rect();
        entt::entity current = entity;
        for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
        {
            const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent || parent->parent == entt::null || parent->parent == current || !registry.valid(parent->parent))
            {
                break;
            }

            if (const Mask2DComponent* mask = registry.try_get<Mask2DComponent>(parent->parent);
                mask && mask->enabled)
            {
                clip = intersect_rect(
                    clip,
                    renderer2d_apply_interactive_visual_rect(
                        registry,
                        parent->parent,
                        entity_mask_rect(registry, parent->parent, *mask)));
                if (rect_empty(clip))
                {
                    break;
                }
            }

            current = parent->parent;
        }
        return clip;
    }

    ShapeMaskClip inherited_shape_mask_clip(const entt::registry& registry, entt::entity entity)
    {
        entt::entity current = entity;
        for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
        {
            const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent || parent->parent == entt::null || parent->parent == current || !registry.valid(parent->parent))
            {
                break;
            }

            const Mask2DComponent* mask = registry.try_get<Mask2DComponent>(parent->parent);
            const ShapeComponent* shape = registry.try_get<ShapeComponent>(parent->parent);
            if (mask && mask->enabled && shape && shape_mask_clip_needed(*shape))
            {
                return {
                    true,
                    shape->primitive,
                    renderer2d_apply_interactive_visual_rect(
                        registry,
                        parent->parent,
                        rect_from_layout(entity_layout_bounds(
                            registry,
                            parent->parent))),
                    shape->cornerRadius,
                    shape->squircleAmount,
                    shape->squirclePower,
                    shape->notchAmount,
                    shape->notchDepth
                };
            }

            current = parent->parent;
        }
        return {};
    }

    DisplayTransitionEffect2D inherited_scroll_edge_fade_effect(
        const entt::registry& registry,
        entt::entity entity)
    {
        entt::entity current = entity;
        entt::entity childOfCurrent = entity;
        for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
        {
            if (const ScrollEdgeFade2DComponent* fade = registry.try_get<ScrollEdgeFade2DComponent>(current);
                fade && fade->enabled)
            {
                glm::vec4 viewport = unclipped_rect();
                entt::entity viewportAncestor = current;
                for (uint32_t parentDepth = 0; parentDepth < 64u; ++parentDepth)
                {
                    const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(viewportAncestor);
                    if (!parent || parent->parent == entt::null || !registry.valid(parent->parent))
                    {
                        break;
                    }
                    viewportAncestor = parent->parent;
                    if (const Mask2DComponent* mask = registry.try_get<Mask2DComponent>(viewportAncestor);
                        mask && mask->enabled)
                    {
                        viewport = entity_mask_rect(registry, viewportAncestor, *mask);
                        break;
                    }
                }
                if (rect_empty(viewport))
                {
                    return {};
                }

                const entt::entity sampleEntity = current == entity ? entity : childOfCurrent;
                const float centerY = display_transition_origin(registry, sampleEntity).y;
                const auto smooth = [](float value) {
                    const float t = std::clamp(value, 0.0f, 1.0f);
                    return t * t * (3.0f - 2.0f * t);
                };
                float visibility = 1.0f;
                if (fade->topHeight > 0.0f)
                {
                    visibility = std::min(
                        visibility,
                        smooth((centerY - viewport.y) / fade->topHeight));
                }
                if (fade->bottomHeight > 0.0f)
                {
                    const float bottom = viewport.y + viewport.w;
                    visibility = std::min(
                        visibility,
                        smooth((bottom - centerY) / fade->bottomHeight));
                }

                DisplayTransitionEffect2D effect = {};
                effect.opacity = glm::mix(
                    std::clamp(fade->minimumOpacity, 0.0f, 1.0f),
                    1.0f,
                    visibility);
                effect.blurRadius = std::max(fade->maximumBlurRadius, 0.0f) * (1.0f - visibility);
                return effect;
            }

            const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent || parent->parent == entt::null || parent->parent == current)
            {
                break;
            }
            childOfCurrent = current;
            current = parent->parent;
        }
        return {};
    }

    DisplayTransitionEffect2D inherited_visual_effect(
        const entt::registry& registry,
        entt::entity entity,
        double currentTimeSeconds)
    {
        DisplayTransitionEffect2D effect = inherited_display_transition_effect(
            registry,
            entity,
            currentTimeSeconds);
        const DisplayTransitionEffect2D edgeFade = inherited_scroll_edge_fade_effect(registry, entity);
        effect.opacity *= edgeFade.opacity;
        effect.blurRadius = std::max(effect.blurRadius, edgeFade.blurRadius);
        return effect;
    }

    ShapeMaskClip shape_mask_for_surface(const entt::registry& registry, entt::entity entity, double now)
    {
        if (const auto* cutout = registry.try_get<ShapeCutout2DComponent>(entity);
            cutout && registry.valid(cutout->source))
        {
            if (const auto* shape = registry.try_get<ShapeComponent>(cutout->source))
            {
                return { true, shape->primitive,
                    apply_display_transition_scale(
                        renderer2d_apply_interactive_visual_rect(registry, cutout->source,
                            rect_from_layout(entity_layout_bounds(registry, cutout->source))),
                        inherited_visual_effect(registry, cutout->source, now)),
                    shape->cornerRadius, shape->squircleAmount, shape->squirclePower,
                    shape->notchAmount, shape->notchDepth, true };
            }
        }
        return inherited_shape_mask_clip(registry, entity);
    }

    glm::vec2 fallback_model_size(const entt::registry& registry, entt::entity entity, const Model3DComponent& model)
    {
        if (model.size.x > 0.0f && model.size.y > 0.0f)
        {
            return model.size;
        }

        if (const ShapeComponent* shape = registry.try_get<ShapeComponent>(entity))
        {
            return glm::max(shape->size, glm::vec2(0.0f));
        }

        if (const TextComponent* text = registry.try_get<TextComponent>(entity))
        {
            return text_layout_size(*text);
        }

        if (const Media2DComponent* media = registry.try_get<Media2DComponent>(entity))
        {
            return glm::max(media->size, glm::vec2(0.0f));
        }

        return { 160.0f, 160.0f };
    }

    bool point_in_rect(glm::vec4 rect, glm::vec2 point)
    {
        return !rect_empty(rect) &&
            point.x >= rect.x &&
            point.y >= rect.y &&
            point.x <= rect.x + rect.z &&
            point.y <= rect.y + rect.w;
    }

    bool point_in_ellipse(glm::vec4 rect, glm::vec2 point)
    {
        if (rect_empty(rect))
        {
            return false;
        }

        const glm::vec2 radius { rect.z * 0.5f, rect.w * 0.5f };
        if (radius.x <= 0.0f || radius.y <= 0.0f)
        {
            return false;
        }

        const glm::vec2 center { rect.x + radius.x, rect.y + radius.y };
        const glm::vec2 normalized = (point - center) / radius;
        return glm::dot(normalized, normalized) <= 1.0f;
    }

    bool point_in_rounded_rect(glm::vec4 rect, glm::vec4 cornerRadii, glm::vec2 point)
    {
        if (!point_in_rect(rect, point))
        {
            return false;
        }

        const glm::vec2 halfSize { rect.z * 0.5f, rect.w * 0.5f };
        const glm::vec2 center { rect.x + halfSize.x, rect.y + halfSize.y };
        const glm::vec2 p = point - center;
        float radius = p.y < 0.0f ?
            (p.x < 0.0f ? cornerRadii.x : cornerRadii.y) :
            (p.x < 0.0f ? cornerRadii.w : cornerRadii.z);
        radius = std::clamp(radius, 0.0f, std::min(rect.z, rect.w) * 0.5f);
        if (radius <= 0.0f)
        {
            return true;
        }

        const glm::vec2 corner = halfSize - glm::vec2(radius);
        const glm::vec2 q = glm::abs(p) - corner;
        const glm::vec2 outside = glm::max(q, glm::vec2(0.0f));
        const float signedDistance = glm::length(outside) + std::min(std::max(q.x, q.y), 0.0f) - radius;
        return signedDistance <= 0.0f;
    }

    bool point_in_rounded_rect(glm::vec4 rect, float cornerRadius, glm::vec2 point)
    {
        return point_in_rounded_rect(rect, glm::vec4(cornerRadius), point);
    }

    bool point_in_notched_squircle(glm::vec4 rect, glm::vec4 cornerRadii, glm::vec2 point)
    {
        const glm::vec2 size { std::max(rect.z, 1.0f), std::max(rect.w, 1.0f) };
        const glm::vec2 halfSize = size * 0.5f;
        const float radius = std::min(
            std::max(std::max(cornerRadii.x, cornerRadii.y), std::max(cornerRadii.z, cornerRadii.w)),
            std::min(halfSize.x, halfSize.y));
        const float flare = notched_squircle_flare_size(size, 1.0f, 0.0f);
        const float bottomRadius = std::clamp(radius, 0.0f, std::min(halfSize.x, halfSize.y));

        if (point_in_rounded_rect(rect, glm::vec4(0.0f, 0.0f, bottomRadius, bottomRadius), point))
        {
            return true;
        }
        if (flare <= 0.05f)
        {
            return false;
        }

        const glm::vec4 topFlareRect {
            rect.x - flare,
            rect.y,
            rect.z + flare * 2.0f,
            flare
        };
        if (!point_in_rect(topFlareRect, point))
        {
            return false;
        }

        const glm::vec2 leftCutoutCenter { rect.x - flare, rect.y + flare };
        const glm::vec2 rightCutoutCenter { rect.x + rect.z + flare, rect.y + flare };
        return glm::length(point - leftCutoutCenter) >= flare - 0.001f &&
            glm::length(point - rightCutoutCenter) >= flare - 0.001f;
    }

    bool point_in_primitive(Renderer2DPrimitive primitive, glm::vec4 rect, glm::vec4 cornerRadii, glm::vec2 point)
    {
        switch (primitive)
        {
        case Renderer2DPrimitive::eEllipse:
        case Renderer2DPrimitive::eCircularProgress:
            return point_in_ellipse(rect, point);
        case Renderer2DPrimitive::eSquircle:
        case Renderer2DPrimitive::eRoundedRectangle:
            return point_in_rounded_rect(rect, cornerRadii, point);
        case Renderer2DPrimitive::eNotchedSquircle:
            return point_in_notched_squircle(rect, cornerRadii, point);
        case Renderer2DPrimitive::eRectangle:
        default:
            return point_in_rect(rect, point);
        }
    }

    bool point_in_primitive(Renderer2DPrimitive primitive, glm::vec4 rect, float cornerRadius, glm::vec2 point)
    {
        return point_in_primitive(primitive, rect, glm::vec4(cornerRadius), point);
    }

    bool alpha_visible(float alpha)
    {
        return alpha > 0.001f;
    }

    bool shape_fill_visible(const ShapeStyleComponent& style)
    {
        if (style.fill == Renderer2DFill::eNone)
        {
            return false;
        }
        return alpha_visible(std::max(style.color0.a, style.color1.a) * style.opacity);
    }

    bool shape_outline_visible(const ShapeStyleComponent& style)
    {
        return style.outlineWidth > 0.0f && alpha_visible(style.outlineColor.a * style.opacity);
    }

    glm::vec4 text_bounds_from_component(
        const Transform2DComponent& transform,
        const TextComponent& text)
    {
        return make_bounds(transform, text_layout_size(text));
    }

    float text_hit_padding(const TextStyleComponent& style)
    {
        const float foregroundPad = std::max(
            std::max(style.outlineWidth, style.blurRadius),
            std::max(style.fontWeightExpansion, 0.0f));
        const float glowPad = std::max(style.glowRadius, 0.0f);
        const float shadowPad = std::max(std::abs(style.shadowOffset.x), std::abs(style.shadowOffset.y)) +
            std::max(style.shadowBlur, 0.0f);
        return std::max(std::max(foregroundPad, glowPad), shadowPad) + 2.0f;
    }

    bool text_visible(const TextStyleComponent& style)
    {
        const bool foregroundVisible = alpha_visible(std::max(style.color0.a, style.color1.a) * style.opacity);
        const bool glowVisible = style.glowRadius > 0.0f && alpha_visible(style.effectColor.a * style.opacity);
        const bool shadowVisible =
            (style.shadowBlur > 0.0f || glm::length(style.shadowOffset) > 0.0f) &&
            alpha_visible(style.shadowColor.a * style.opacity);
        return foregroundVisible || glowVisible || shadowVisible;
    }

    bool style_backdrop_blur_visible(const ShapeStyleComponent& style)
    {
        return style.backdropBlurRadius > 0.0f && alpha_visible(style.backdropBlurOpacity);
    }

    glm::vec2 media_source_size(const Media2DComponent& media)
    {
        if (media.sourcePixelSize.x > 0u && media.sourcePixelSize.y > 0u)
        {
            return {
                static_cast<float>(media.sourcePixelSize.x),
                static_cast<float>(media.sourcePixelSize.y)
            };
        }

        return glm::max(media.size, glm::vec2(1.0f));
    }

    glm::vec4 media_bounds_from_component(
        const Transform2DComponent& transform,
        const Media2DComponent& media)
    {
        glm::vec4 rect = make_bounds(transform, media.size);
        if (media.fit != Media2DFit::eContain || rect_empty(rect))
        {
            return rect;
        }

        const glm::vec2 sourceSize = media_source_size(media);
        const float sourceAspect = sourceSize.x / std::max(sourceSize.y, 0.0001f);
        const float targetAspect = rect.z / std::max(rect.w, 0.0001f);
        glm::vec2 drawSize = { rect.z, rect.w };
        if (targetAspect > sourceAspect)
        {
            drawSize.x = rect.w * sourceAspect;
        }
        else
        {
            drawSize.y = rect.z / sourceAspect;
        }

        rect.x += (rect.z - drawSize.x) * 0.5f;
        rect.y += (rect.w - drawSize.y) * 0.5f;
        rect.z = drawSize.x;
        rect.w = drawSize.y;
        return rect;
    }

    glm::vec4 media_uv_rect_from_component(const Media2DComponent& media, glm::vec4 drawRect)
    {
        glm::vec4 uv = media.uvRect;
        if (media.fit != Media2DFit::eCover || rect_empty(drawRect))
        {
            return uv;
        }

        const glm::vec2 sourceSize = media_source_size(media);
        const float sourceAspect = sourceSize.x / std::max(sourceSize.y, 0.0001f);
        const float targetAspect = drawRect.z / std::max(drawRect.w, 0.0001f);

        const glm::vec2 uvMin { uv.x, uv.y };
        const glm::vec2 uvMax { uv.z, uv.w };
        const glm::vec2 uvSize = uvMax - uvMin;
        if (targetAspect > sourceAspect)
        {
            const float visibleHeight = glm::clamp(sourceAspect / std::max(targetAspect, 0.0001f), 0.0f, 1.0f);
            const float yOffset = (1.0f - visibleHeight) * 0.5f;
            uv.y = uvMin.y + uvSize.y * yOffset;
            uv.w = uvMin.y + uvSize.y * (yOffset + visibleHeight);
        }
        else if (targetAspect < sourceAspect)
        {
            const float visibleWidth = glm::clamp(targetAspect / std::max(sourceAspect, 0.0001f), 0.0f, 1.0f);
            const float xOffset = (1.0f - visibleWidth) * 0.5f;
            uv.x = uvMin.x + uvSize.x * xOffset;
            uv.z = uvMin.x + uvSize.x * (xOffset + visibleWidth);
        }
        return uv;
    }

    bool media_visible(const Media2DComponent& media)
    {
        const float tintAlpha = media.tintFill == Renderer2DFill::eSolid
            ? media.tint.a
            : std::max(media.tint.a, media.tintEnd.a);
        return media.visible && media.drawable && media.mediaId != 0 && alpha_visible(media.opacity * tintAlpha);
    }

    uint32_t media_frame_for_time(const Media2DComponent& media)
    {
        if (!media.animated || media.frameCount <= 1u)
        {
            return 0u;
        }

        const bool hasFrameDurations = media.frameDurationsSeconds.size() >= media.frameCount;
        if (!hasFrameDurations && media.frameRate <= 0.0)
        {
            return 0u;
        }

        double duration = media.durationSeconds > 0.0
            ? media.durationSeconds
            : (media.frameRate > 0.0 ? static_cast<double>(media.frameCount) / media.frameRate : 0.0);
        if (hasFrameDurations && duration <= 0.0)
        {
            duration = 0.0;
            for (uint32_t frameIndex = 0; frameIndex < media.frameCount; ++frameIndex)
            {
                duration += std::max(media.frameDurationsSeconds[frameIndex], 0.0);
            }
        }
        if (duration <= 0.0)
        {
            return 0u;
        }

        double playback = media.playbackSeconds;
        if (media.loop)
        {
            playback = std::fmod(std::max(playback, 0.0), duration);
        }
        else
        {
            playback = std::clamp(playback, 0.0, duration);
        }

        if (hasFrameDurations)
        {
            double accumulated = 0.0;
            for (uint32_t frameIndex = 0; frameIndex < media.frameCount; ++frameIndex)
            {
                accumulated += std::max(media.frameDurationsSeconds[frameIndex], 0.0);
                if (playback < accumulated)
                {
                    return frameIndex;
                }
            }
            return media.frameCount - 1u;
        }

        const uint32_t frame = static_cast<uint32_t>(playback * media.frameRate);
        return std::min(frame, media.frameCount - 1u);
    }

    void update_media_playback(Media2DComponent& media, double currentTimeSeconds)
    {
        if (!media.animated || media.frameCount <= 1u || !media.playing)
        {
            media.lastPlaybackUpdateSeconds = currentTimeSeconds;
            media.currentFrame = media_frame_for_time(media);
            return;
        }

        if (media.lastPlaybackUpdateSeconds < 0.0)
        {
            media.lastPlaybackUpdateSeconds = currentTimeSeconds;
            media.currentFrame = media_frame_for_time(media);
            return;
        }

        const double deltaSeconds = std::max(0.0, currentTimeSeconds - media.lastPlaybackUpdateSeconds);
        media.lastPlaybackUpdateSeconds = currentTimeSeconds;
        media.playbackSeconds += deltaSeconds;

        if (!media.loop && media.durationSeconds > 0.0 && media.playbackSeconds >= media.durationSeconds)
        {
            media.playbackSeconds = media.durationSeconds;
            media.playing = false;
        }
        else if (media.loop && media.durationSeconds > 0.0)
        {
            media.playbackSeconds = std::fmod(media.playbackSeconds, media.durationSeconds);
        }

        media.currentFrame = media_frame_for_time(media);
    }

    Media2DComponent* media_component(entt::registry& registry, entt::entity entity)
    {
        return registry.valid(entity)
            ? registry.try_get<Media2DComponent>(entity)
            : nullptr;
    }

    const Media2DComponent* media_component(const entt::registry& registry, entt::entity entity)
    {
        return registry.valid(entity)
            ? registry.try_get<Media2DComponent>(entity)
            : nullptr;
    }

    bool apply_media_playback_operation(
        Media2DComponent& media,
        Media2DPlaybackCommand operation,
        double value = 0.0)
    {
        switch (operation)
        {
        case Media2DPlaybackCommand::ePlay:
            if (media.playing)
            {
                return false;
            }
            media.playing = true;
            media.lastPlaybackUpdateSeconds = -1.0;
            return true;
        case Media2DPlaybackCommand::ePause:
            if (!media.playing)
            {
                return false;
            }
            media.playing = false;
            media.lastPlaybackUpdateSeconds = -1.0;
            return true;
        case Media2DPlaybackCommand::eStop:
            media.playing = false;
            media.playbackSeconds = 0.0;
            media.lastPlaybackUpdateSeconds = -1.0;
            media.currentFrame = 0u;
            return true;
        case Media2DPlaybackCommand::eRestart:
            media.playing = true;
            media.playbackSeconds = 0.0;
            media.lastPlaybackUpdateSeconds = -1.0;
            media.currentFrame = 0u;
            return true;
        case Media2DPlaybackCommand::eSeek:
        {
            const double maximum = std::max(media.durationSeconds, 0.0);
            media.playbackSeconds = maximum > 0.0
                ? std::clamp(value, 0.0, maximum)
                : std::max(value, 0.0);
            media.lastPlaybackUpdateSeconds = -1.0;
            media.currentFrame = media_frame_for_time(media);
            return true;
        }
        case Media2DPlaybackCommand::eSetLooping:
            if (media.loop == (value != 0.0))
            {
                return false;
            }
            media.loop = value != 0.0;
            return true;
        }
        return false;
    }

    glm::vec4 make_model_viewport(
        const entt::registry& registry,
        entt::entity entity,
        const Transform2DComponent& transform,
        const Model3DComponent& model)
    {
        return make_bounds(transform, fallback_model_size(registry, entity, model));
    }

    glm::vec4 make_model_clip_rect(
        const entt::registry& registry,
        entt::entity entity,
        glm::vec4 viewport,
        const Model3DComponent& model)
    {
        glm::vec4 clip = model.clipToBounds ? viewport : unclipped_rect();

        if (!model.clipToParent)
        {
            return intersect_rect(clip, inherited_mask_clip_rect(registry, entity));
        }

        entt::entity current = entity;
        for (uint32_t depth = 0; depth < 64u && current != entt::null && registry.valid(current); ++depth)
        {
            const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(current);
            if (!parent || parent->parent == entt::null || parent->parent == current || !registry.valid(parent->parent))
            {
                break;
            }

            clip = intersect_rect(clip, rect_from_layout(entity_content_rect(registry, parent->parent)));
            if (rect_empty(clip))
            {
                break;
            }
            current = parent->parent;
        }

        return intersect_rect(clip, inherited_mask_clip_rect(registry, entity));
    }

    glm::vec2 entity_child_resize_scale(
        const entt::registry& registry,
        entt::entity parent,
        const LayoutResolvedRect& parentContentRect)
    {
        const LayoutRect2DComponent* layoutRect = registry.try_get<LayoutRect2DComponent>(parent);
        if (!layoutRect || !layoutRect->resizeChildren)
        {
            return { 1.0f, 1.0f };
        }

        const glm::vec2 referenceSize = layoutRect->childLayoutSize;
        if (referenceSize.x <= 0.0f && referenceSize.y <= 0.0f)
        {
            return { 1.0f, 1.0f };
        }

        glm::vec2 referenceContentSize = parentContentRect.size;
        if (referenceSize.x > 0.0f)
        {
            referenceContentSize.x = std::max(referenceSize.x - layoutRect->padding.x - layoutRect->padding.z, 0.0f);
        }
        if (referenceSize.y > 0.0f)
        {
            referenceContentSize.y = std::max(referenceSize.y - layoutRect->padding.y - layoutRect->padding.w, 0.0f);
        }

        return {
            referenceContentSize.x > 0.0001f ? parentContentRect.size.x / referenceContentSize.x : 1.0f,
            referenceContentSize.y > 0.0001f ? parentContentRect.size.y / referenceContentSize.y : 1.0f
        };
    }

    std::vector<float> resolve_grid_axis(const std::vector<GridTrack2D>& tracks, float availableSize, float gap)
    {
        const std::size_t trackCount = std::max<std::size_t>(tracks.size(), 1u);
        const float gapTotal = std::max(gap, 0.0f) * static_cast<float>(trackCount > 0u ? trackCount - 1u : 0u);
        float fixedTotal = 0.0f;
        float fractionTotal = 0.0f;

        for (std::size_t i = 0; i < trackCount; ++i)
        {
            const GridTrack2D track = tracks.empty() ? GridTrack2D::fraction(1.0f) : tracks[i];
            if (track.unit == GridTrackUnit2D::ePixels)
            {
                fixedTotal += std::max(track.value, 0.0f);
            }
            else
            {
                fractionTotal += std::max(track.value, 0.0f);
            }
        }

        const bool distributeFractionsEvenly = fractionTotal <= 0.0001f;
        if (distributeFractionsEvenly)
        {
            fractionTotal = static_cast<float>(trackCount);
        }

        const float fractionalSize = std::max(availableSize - fixedTotal - gapTotal, 0.0f);
        std::vector<float> sizes;
        sizes.reserve(trackCount);
        for (std::size_t i = 0; i < trackCount; ++i)
        {
            const GridTrack2D track = tracks.empty() ? GridTrack2D::fraction(1.0f) : tracks[i];
            if (track.unit == GridTrackUnit2D::ePixels)
            {
                sizes.push_back(std::max(track.value, 0.0f));
            }
            else
            {
                const float fraction = std::max(track.value, 0.0f);
                sizes.push_back(fractionalSize * (distributeFractionsEvenly ? 1.0f : fraction) / fractionTotal);
            }
        }
        return sizes;
    }

    float grid_axis_start(const std::vector<float>& sizes, std::size_t index, float gap)
    {
        float start = 0.0f;
        for (std::size_t i = 0; i < index && i < sizes.size(); ++i)
        {
            start += sizes[i] + gap;
        }
        return start;
    }

    float grid_axis_span_size(const std::vector<float>& sizes, std::size_t index, std::size_t span, float gap)
    {
        if (sizes.empty() || index >= sizes.size())
        {
            return 0.0f;
        }

        const std::size_t end = std::min(index + std::max<std::size_t>(span, 1u), sizes.size());
        float size = 0.0f;
        for (std::size_t i = index; i < end; ++i)
        {
            size += sizes[i];
        }
        size += gap * static_cast<float>(end > index ? end - index - 1u : 0u);
        return size;
    }

    LayoutResolvedRect grid_cell_content_rect(
        const entt::registry& registry,
        entt::entity parent,
        const GridCell2DComponent& cell,
        const LayoutResolvedRect& parentContentRect,
        glm::vec2 childResizeScale)
    {
        const Grid2DComponent* grid = registry.try_get<Grid2DComponent>(parent);
        if (!grid)
        {
            return parentContentRect;
        }

        const float columnGap = std::max(grid->gap.x, 0.0f);
        const float rowGap = std::max(grid->gap.y, 0.0f);
        const std::vector<float> columns = resolve_grid_axis(grid->columns, parentContentRect.size.x, columnGap);
        const std::vector<float> rows = resolve_grid_axis(grid->rows, parentContentRect.size.y, rowGap);
        if (columns.empty() || rows.empty())
        {
            return parentContentRect;
        }

        const std::size_t column = std::min<std::size_t>(cell.column, columns.size() - 1u);
        const std::size_t row = std::min<std::size_t>(cell.row, rows.size() - 1u);
        const std::size_t columnSpan = std::max<std::size_t>(cell.columnSpan, 1u);
        const std::size_t rowSpan = std::max<std::size_t>(cell.rowSpan, 1u);
        const glm::vec4 margin {
            cell.margin.x * childResizeScale.x,
            cell.margin.y * childResizeScale.y,
            cell.margin.z * childResizeScale.x,
            cell.margin.w * childResizeScale.y
        };

        const glm::vec2 minPosition {
            parentContentRect.position.x + grid_axis_start(columns, column, columnGap) + margin.x,
            parentContentRect.position.y + grid_axis_start(rows, row, rowGap) + margin.y
        };
        const glm::vec2 rawSize {
            grid_axis_span_size(columns, column, columnSpan, columnGap),
            grid_axis_span_size(rows, row, rowSpan, rowGap)
        };
        return {
            minPosition,
            glm::max(rawSize - glm::vec2(margin.x + margin.z, margin.y + margin.w), glm::vec2(0.0f))
        };
    }

    glm::vec4 scale_edges(glm::vec4 edges, glm::vec2 scale)
    {
        return {
            edges.x * scale.x,
            edges.y * scale.y,
            edges.z * scale.x,
            edges.w * scale.y
        };
    }

    void set_entity_layout_size(entt::registry& registry, entt::entity entity, glm::vec2 size)
    {
        ShapeComponent* shape = registry.try_get<ShapeComponent>(entity);
        Model3DComponent* model = registry.try_get<Model3DComponent>(entity);
        Media2DComponent* media = registry.try_get<Media2DComponent>(entity);
        Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
        if ((!shape && !model && !media) || !transform)
        {
            return;
        }

        const glm::vec2 safeScale {
            std::abs(transform->scale.x) > 0.0001f ? transform->scale.x : 1.0f,
            std::abs(transform->scale.y) > 0.0001f ? transform->scale.y : 1.0f
        };
        if (shape)
        {
            shape->size = glm::max(size / safeScale, glm::vec2(0.0f));
        }
        else if (model)
        {
            model->size = glm::max(size / safeScale, glm::vec2(0.0f));
        }
        else if (media)
        {
            media->size = glm::max(size / safeScale, glm::vec2(0.0f));
        }
    }

    bool has_entity_layout_size(const Layout2DComponent& layout)
    {
        return layout.size.x > 0.0f || layout.size.y > 0.0f;
    }

    bool is_layout_stretched_x(const Layout2DComponent& layout)
    {
        return std::abs(layout.anchorMax.x - layout.anchorMin.x) > 0.0001f;
    }

    bool is_layout_stretched_y(const Layout2DComponent& layout)
    {
        return std::abs(layout.anchorMax.y - layout.anchorMin.y) > 0.0001f;
    }

    void resolve_entity_layout(
        entt::registry& registry,
        entt::entity entity,
        std::vector<entt::entity>& stack,
        std::unordered_set<uint32_t>& resolved)
    {
        const uint32_t key = entity_key(entity);
        if (resolved.find(key) != resolved.end())
        {
            return;
        }

        if (std::find(stack.begin(), stack.end(), entity) != stack.end())
        {
            return;
        }

        Transform2DComponent* transform = registry.try_get<Transform2DComponent>(entity);
        const Parent2DComponent* parent = registry.try_get<Parent2DComponent>(entity);
        const Layout2DComponent* layout = registry.try_get<Layout2DComponent>(entity);
        if (!transform || !parent || !layout || parent->parent == entt::null || !registry.valid(parent->parent))
        {
            resolved.insert(key);
            return;
        }

        stack.push_back(entity);
        resolve_entity_layout(registry, parent->parent, stack, resolved);
        stack.pop_back();

        LayoutResolvedRect contentRect = entity_content_rect(registry, parent->parent);
        const glm::vec2 childResizeScale = entity_child_resize_scale(registry, parent->parent, contentRect);
        if (const GridCell2DComponent* gridCell = registry.try_get<GridCell2DComponent>(entity))
        {
            contentRect = grid_cell_content_rect(registry, parent->parent, *gridCell, contentRect, childResizeScale);
        }
        const glm::vec2 scaledOffset = layout->offset * childResizeScale;
        const glm::vec4 scaledMargin = scale_edges(layout->margin, childResizeScale);

        transform->scale = childResizeScale;

        glm::vec2 resolvedSize = entity_base_layout_size(registry, entity) * childResizeScale;

        if (has_entity_layout_size(*layout))
        {
            if (layout->size.x > 0.0f)
            {
                resolvedSize.x = layout->size.x * childResizeScale.x;
            }
            if (layout->size.y > 0.0f)
            {
                resolvedSize.y = layout->size.y * childResizeScale.y;
            }
        }

        if (is_layout_stretched_x(*layout))
        {
            const float minX = contentRect.size.x * layout->anchorMin.x + scaledMargin.x;
            const float maxX = contentRect.size.x * layout->anchorMax.x - scaledMargin.z;
            resolvedSize.x = std::max(maxX - minX, 0.0f);
        }
        if (is_layout_stretched_y(*layout))
        {
            const float minY = contentRect.size.y * layout->anchorMin.y + scaledMargin.y;
            const float maxY = contentRect.size.y * layout->anchorMax.y - scaledMargin.w;
            resolvedSize.y = std::max(maxY - minY, 0.0f);
        }

        set_entity_layout_size(registry, entity, resolvedSize);

        glm::vec2 minPosition { 0.0f };
        if (is_layout_stretched_x(*layout))
        {
            minPosition.x = contentRect.position.x + contentRect.size.x * layout->anchorMin.x +
                scaledMargin.x + scaledOffset.x;
        }
        else
        {
            const float anchorX = contentRect.position.x +
                contentRect.size.x * layout->anchorMin.x + scaledOffset.x;
            minPosition.x = anchorX - layout->pivot.x * resolvedSize.x;
        }

        if (is_layout_stretched_y(*layout))
        {
            minPosition.y = contentRect.position.y + contentRect.size.y * layout->anchorMin.y +
                scaledMargin.y + scaledOffset.y;
        }
        else
        {
            const float anchorY = contentRect.position.y + contentRect.size.y * layout->anchorMin.y +
                scaledOffset.y;
            minPosition.y = anchorY - layout->pivot.y * resolvedSize.y;
        }

        transform->origin = layout->pivot;
        transform->position = minPosition + layout->pivot * resolvedSize;
        resolved.insert(key);
    }

    void resolve_layouts(entt::registry& registry)
    {
        auto layoutView = registry.view<Transform2DComponent, const Parent2DComponent, const Layout2DComponent>();
        std::vector<entt::entity> stack;
        stack.reserve(8);
        std::unordered_set<uint32_t> resolved;
        resolved.reserve(layoutView.size_hint());
        layoutView.each([&](entt::entity entity, Transform2DComponent&, const Parent2DComponent&, const Layout2DComponent&) {
            resolve_entity_layout(registry, entity, stack, resolved);
        });

        auto visualView = registry.view<
            Transform2DComponent,
            const VisualTransform2DComponent,
            const Layout2DComponent>();
        visualView.each([&](
            entt::entity entity,
            Transform2DComponent& transform,
            const VisualTransform2DComponent& visual,
            const Layout2DComponent&) {
            const glm::vec2 baseSize = entity_layout_size(registry, entity);
            const glm::vec2 baseMin =
                transform.position - transform.origin * baseSize;
            transform.scale *= glm::max(visual.scale, glm::vec2(0.001f));
            transform.origin = glm::clamp(
                visual.pivot,
                glm::vec2(0.0f),
                glm::vec2(1.0f));
            transform.position =
                baseMin + baseSize * transform.origin + visual.offset;
        });
    }

    uint32_t shape_flags(const ShapeComponent& shape, const ShapeStyleComponent& style)
    {
        uint32_t flags = eRenderer2DStyleNone;
        if (style.fill == Renderer2DFill::eLinearGradient || style.fill == Renderer2DFill::eRadialGradient)
        {
            flags |= eRenderer2DStyleGradient;
        }
        if (style.fill == Renderer2DFill::eRadialGradient)
        {
            flags |= eRenderer2DStyleRadialGradient;
        }
        if (style.outlineWidth > 0.0f)
        {
            flags |= eRenderer2DStyleOutline;
        }
        if (shape.sdfEdges)
        {
            flags |= eRenderer2DStyleSdfEdges;
        }
        if (shape.customCornerRadii)
        {
            flags |= eRenderer2DStyleCornerRadii;
        }
        return flags;
    }

    uint32_t text_flags(const TextComponent& text, const TextStyleComponent& style)
    {
        uint32_t flags = text.useMsdf ? eRenderer2DStyleMsdfText : eRenderer2DStyleNone;
        if (style.fill == Renderer2DFill::eLinearGradient || style.fill == Renderer2DFill::eRadialGradient)
        {
            flags |= eRenderer2DStyleGradient;
        }
        if (style.outlineWidth > 0.0f)
        {
            flags |= eRenderer2DStyleOutline;
        }
        if (style.shadowColor.a > 0.0f &&
            (style.shadowBlur > 0.0f || glm::length(style.shadowOffset) > 0.0f))
        {
            flags |= eRenderer2DStyleShadow;
        }
        if (style.glowRadius > 0.0f)
        {
            flags |= eRenderer2DStyleGlow;
        }
        if (style.blurRadius > 0.0f)
        {
            flags |= eRenderer2DStyleBlur;
        }
        return flags;
    }

    uint32_t media_flags(const Media2DComponent& media)
    {
        uint32_t flags = eRenderer2DStyleMedia;
        if (media.tintFill == Renderer2DFill::eLinearGradient || media.tintFill == Renderer2DFill::eRadialGradient)
        {
            flags |= eRenderer2DStyleGradient;
        }
        if (media.tintFill == Renderer2DFill::eRadialGradient)
        {
            flags |= eRenderer2DStyleRadialGradient;
        }
        if (media.blurRadius > 0.0f)
        {
            flags |= eRenderer2DStyleBlur;
        }
        if (media.sampleBlurOncePerPixel && media.blurRadius > 0.0f)
        {
            flags |= eRenderer2DStyleMediaSingleBlurSample;
        }
        if (std::abs(media.brightness) > 0.0001f ||
            std::abs(media.contrast - 1.0f) > 0.0001f ||
            std::abs(media.exposure) > 0.0001f ||
            media.invert > 0.0001f)
        {
            flags |= eRenderer2DStyleMediaColorAdjust;
        }
        if (media.autoLiftBlack && media.sourceHasBlackBackground)
        {
            flags |= eRenderer2DStyleMediaAutoBlackLift;
        }
        if (media.tintAsMask)
        {
            flags |= eRenderer2DStyleMediaTintAsMask;
        }
        if (media.premultipliedAlpha)
        {
            flags |= eRenderer2DStyleMediaPremultipliedAlpha;
        }
        return flags;
    }

    uint32_t pack_media_color_adjustment(const Media2DComponent& media)
    {
        return pack_range8(media.brightness, -1.0f, 1.0f) |
            (pack_range8(media.contrast, 0.0f, 4.0f) << 8u) |
            (pack_range8(media.exposure, -4.0f, 4.0f) << 16u) |
            (pack_unorm8(media.invert) << 24u);
    }

    bool transform2_5d_visible(const Transform2DComponent& transform)
    {
        constexpr float epsilon = 0.0001f;
        return std::abs(transform.rotationRadians) > epsilon ||
            std::abs(transform.rotation3DRadians.x) > epsilon ||
            std::abs(transform.rotation3DRadians.y) > epsilon;
    }

    uint32_t transform2_5d_flags(const Transform2DComponent& transform)
    {
        return transform2_5d_visible(transform) ? eRenderer2DStyleTransform2_5D : eRenderer2DStyleNone;
    }

    glm::vec4 transform2_5d_effect(const Transform2DComponent& transform)
    {
        return {
            transform.rotationRadians,
            transform.rotation3DRadians.x,
            transform.rotation3DRadians.y,
            transform.perspective
        };
    }

    void sort_batches(std::vector<Renderer2DBatch>& batches)
    {
        if (batches.size() < 2)
        {
            return;
        }

        std::sort(batches.begin(), batches.end(),
            [](const Renderer2DBatch& a, const Renderer2DBatch& b) {
                const RenderLayer2DKey left = render_layer_key(a);
                const RenderLayer2DKey right = render_layer_key(b);
                if (render_layer_key_less(left, right))
                {
                    return true;
                }
                if (render_layer_key_less(right, left))
                {
                    return false;
                }
                return entity_key(a.entity) < entity_key(b.entity);
            });
    }

    uint32_t render_op_phase(Renderer2DRenderOpType type)
    {
        switch (type)
        {
        case Renderer2DRenderOpType::ePanelBlur:
            return 0u;
        case Renderer2DRenderOpType::eShadow:
            return 1u;
        case Renderer2DRenderOpType::eBlur:
            return 2u;
        case Renderer2DRenderOpType::eShape:
            return 3u;
        case Renderer2DRenderOpType::eMedia:
            return 4u;
        case Renderer2DRenderOpType::eModel3D:
            return 5u;
        case Renderer2DRenderOpType::eTextUnderlay:
            return 6u;
        case Renderer2DRenderOpType::eText:
        default:
            return 7u;
        }
    }

    struct Renderer2DHitCandidate
    {
        entt::entity entity = entt::null;
        RenderLayer2DKey layer {};
        uint32_t phase = 0u;
        uint32_t entityKey = 0u;
        bool valid = false;
    };

    bool hit_candidate_above(
        const Renderer2DHitCandidate& candidate,
        const RenderLayer2DKey& layer,
        uint32_t phase,
        uint32_t entityKey)
    {
        if (!candidate.valid)
        {
            return true;
        }
        if (layer.alwaysOnTop != candidate.layer.alwaysOnTop)
        {
            return layer.alwaysOnTop;
        }
        if (layer.stackLayer != candidate.layer.stackLayer)
        {
            return layer.stackLayer > candidate.layer.stackLayer;
        }
        if (layer.stackOrder != candidate.layer.stackOrder)
        {
            return layer.stackOrder > candidate.layer.stackOrder;
        }
        if (layer.layer != candidate.layer.layer)
        {
            return layer.layer > candidate.layer.layer;
        }
        if (layer.order != candidate.layer.order)
        {
            return layer.order > candidate.layer.order;
        }
        if (phase != candidate.phase)
        {
            return phase > candidate.phase;
        }
        return entityKey > candidate.entityKey;
    }

    void consider_hit_candidate(
        Renderer2DHitCandidate& candidate,
        const entt::registry& registry,
        entt::entity entity,
        Renderer2DRenderOpType type)
    {
        const RenderLayer2DKey layer = render_layer_key(registry, entity);
        const uint32_t phase = render_op_phase(type);
        const uint32_t key = entity_key(entity);
        if (!hit_candidate_above(candidate, layer, phase, key))
        {
            return;
        }

        candidate.entity = entity;
        candidate.layer = layer;
        candidate.phase = phase;
        candidate.entityKey = key;
        candidate.valid = true;
    }

    void add_render_ops(
        std::vector<Renderer2DRenderOp>& ops,
        const std::vector<Renderer2DBatch>& batches,
        Renderer2DRenderOpType type)
    {
        for (const Renderer2DBatch& batch : batches)
        {
            ops.push_back({
                type,
                &batch,
                nullptr,
                batch.entity,
                batch.stackLayer,
                batch.stackOrder,
                batch.layer,
                batch.order,
                batch.alwaysOnTop
            });
        }
    }

    void add_model_ops(
        std::vector<Renderer2DRenderOp>& ops,
        const std::vector<Renderer3DModelBatch>& models)
    {
        for (const Renderer3DModelBatch& model : models)
        {
            ops.push_back({
                Renderer2DRenderOpType::eModel3D,
                nullptr,
                &model,
                model.entity,
                model.stackLayer,
                model.stackOrder,
                model.layer,
                model.order,
                model.alwaysOnTop
            });
        }
    }

    void sort_render_ops(std::vector<Renderer2DRenderOp>& ops)
    {
        if (ops.size() < 2)
        {
            return;
        }

        std::sort(ops.begin(), ops.end(),
            [](const Renderer2DRenderOp& a, const Renderer2DRenderOp& b) {
                const RenderLayer2DKey left {
                    a.stackLayer,
                    a.stackOrder,
                    a.layer,
                    a.order,
                    a.alwaysOnTop
                };
                const RenderLayer2DKey right {
                    b.stackLayer,
                    b.stackOrder,
                    b.layer,
                    b.order,
                    b.alwaysOnTop
                };
                if (render_layer_key_less(left, right))
                {
                    return true;
                }
                if (render_layer_key_less(right, left))
                {
                    return false;
                }
                const uint32_t leftPhase = render_op_phase(a.type);
                const uint32_t rightPhase = render_op_phase(b.type);
                if (leftPhase != rightPhase)
                {
                    return leftPhase < rightPhase;
                }
                const uint32_t leftKey = entity_key(a.entity);
                const uint32_t rightKey = entity_key(b.entity);
                if (leftKey != rightKey)
                {
                    return leftKey < rightKey;
                }
                return false;
            });
    }

    Renderer2DPushConstants make_push_constants(
        const Renderer2DBatch& batch,
        uint32_t pass = 0,
        uint32_t dispatchOriginX = 0,
        uint32_t dispatchOriginY = 0)
    {
        Renderer2DPushConstants constants = {};
        constants.rect = batch.rect;
        constants.uvRect = batch.uvRect;
        constants.color0 = batch.color0;
        constants.color1 = batch.color1;
        constants.color2 = batch.color2;
        constants.effect0 = batch.effect0;
        constants.effect1 = batch.effect1;
        constants.data = {
            static_cast<uint32_t>(batch.primitive),
            batch.flags,
            pass | batch.packedData,
            pack_dispatch_origin(dispatchOriginX, dispatchOriginY)
        };
        return constants;
    }

}

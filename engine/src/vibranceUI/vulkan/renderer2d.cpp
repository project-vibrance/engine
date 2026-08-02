#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <vibranceUI/renderer/renderer2d.h>
#include <vibranceUI/renderer/renderer3d.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace
{
    constexpr uint32_t kDepthPass = 0;
    constexpr uint32_t kColorPass = 1;
    constexpr uint32_t kTextPassAll = 0;
    constexpr uint32_t kTextPassUnderlay = 1;
    constexpr uint32_t kTextPassForeground = 2;
    constexpr uint32_t kTextPackedExpansionMask = 0xfffu;
    constexpr uint32_t kTextPackedExpansionBias = 2048u;
    constexpr float kTextPackedExpansionScale = 256.0f;
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

    RasterPushConstants make_orthographic_constants(const Swapchain& swapchain)
    {
        const float width = static_cast<float>(std::max(swapchain.extent.width, 1u));
        const float height = static_cast<float>(std::max(swapchain.extent.height, 1u));
        const float aspect = width / height;

        RasterPushConstants constants = {};
        if (aspect >= 1.0f)
        {
            constants.worldToClip = glm::ortho(-aspect, aspect, -1.0f, 1.0f, 0.0f, 1.0f);
        }
        else
        {
            constants.worldToClip = glm::ortho(-1.0f, 1.0f, -1.0f / aspect, 1.0f / aspect, 0.0f, 1.0f);
        }
        constants.worldToClip[1][1] *= -1.0f;
        return constants;
    }

    void insert_compute_memory_barrier(vk::CommandBuffer commandBuffer)
    {
        vk::MemoryBarrier barrier = {};
        barrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
        barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

        commandBuffer.pipelineBarrier(
            vk::PipelineStageFlagBits::eComputeShader,
            vk::PipelineStageFlagBits::eComputeShader,
            vk::DependencyFlags(),
            barrier,
            nullptr,
            nullptr
        );
    }

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
            std::min(size.y * 0.5f, size.x * 0.18f));
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

    DispatchBounds make_dispatch_bounds(const Swapchain& swapchain, glm::vec4 rect, float padding)
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

        return {
            static_cast<uint32_t>(minX),
            static_cast<uint32_t>(minY),
            static_cast<uint32_t>(maxX - minX),
            static_cast<uint32_t>(maxY - minY)
        };
    }

    DispatchBounds make_dispatch_bounds(
        const Swapchain& swapchain,
        const Renderer2DBatch& batch,
        DispatchBoundsMode mode)
    {
        return make_dispatch_bounds(
            swapchain,
            expand_and_clip_rect(batch.rect, dispatch_padding(batch, mode), batch.clipRect),
            0.0f);
    }

    DispatchBounds make_full_screen_bounds(const Swapchain& swapchain)
    {
        return { 0u, 0u, swapchain.extent.width, swapchain.extent.height };
    }

    DispatchBounds make_blur_dispatch_bounds(
        const Swapchain& swapchain,
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
                clip = intersect_rect(clip, entity_mask_rect(registry, parent->parent, *mask));
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
                    rect_from_layout(entity_layout_bounds(registry, parent->parent)),
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
            const float anchorX = contentRect.position.x + contentRect.size.x * layout->anchorMin.x +
                scaledOffset.x;
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

    void dispatch_bounds(vk::CommandBuffer commandBuffer, const DispatchBounds& bounds)
    {
        if (!bounds.empty())
        {
            commandBuffer.dispatch(workgroup_count(bounds.width), workgroup_count(bounds.height), 1);
        }
    }

    bool bind_pipeline(
        vk::CommandBuffer commandBuffer,
        PipelineType pipelineType,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines)
    {
        const auto pipelineIt = pipelines.find(pipelineType);
        if (pipelineIt == pipelines.end() || !pipelineIt->second)
        {
            return false;
        }

        commandBuffer.bindPipeline(vk::PipelineBindPoint::eCompute, pipelineIt->second);
        return true;
    }

    void bind_frame_set(
        vk::CommandBuffer commandBuffer,
        PipelineType pipelineType,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope frameScope = DescriptorScope::eFrame)
    {
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
            0, 1, &descriptorSets[frameScope], 0, nullptr);
    }

    void bind_post_set(
        vk::CommandBuffer commandBuffer,
        PipelineType pipelineType,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope postScope = DescriptorScope::ePost)
    {
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
            1, 1, &descriptorSets[postScope], 0, nullptr);
    }

    void clear_frame_surface(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        DescriptorScope frameScope,
        glm::uvec4 clearRect = glm::uvec4(0u))
    {
        const PipelineType pipelineType = PipelineType::eClear;
        if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
        {
            return;
        }

        bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
        DispatchBounds bounds = make_full_screen_bounds(swapchain);
        if (clearRect.z > 0u && clearRect.w > 0u)
        {
            const uint32_t x = std::min(clearRect.x, swapchain.extent.width);
            const uint32_t y = std::min(clearRect.y, swapchain.extent.height);
            bounds = {
                x,
                y,
                std::min(clearRect.z, swapchain.extent.width - x),
                std::min(clearRect.w, swapchain.extent.height - y)
            };
        }
        if (bounds.empty())
        {
            return;
        }
        const glm::uvec4 clearConstants { 0u, bounds.x, bounds.y, 0u };
        commandBuffer.pushConstants(
            pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute,
            0u,
            sizeof(clearConstants),
            &clearConstants);
        dispatch_bounds(commandBuffer, bounds);
        insert_compute_memory_barrier(commandBuffer);
    }

    void record_batches(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        PipelineType pipelineType,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        const std::vector<Renderer2DBatch>& batches,
        DispatchBoundsMode dispatchMode,
        DescriptorScope frameScope)
    {
        if (batches.empty() || !bind_pipeline(commandBuffer, pipelineType, pipelines))
        {
            return;
        }

        bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);

        for (const Renderer2DBatch& batch : batches)
        {
            const DispatchBounds bounds = make_dispatch_bounds(swapchain, batch, dispatchMode);
            if (bounds.empty())
            {
                continue;
            }

            Renderer2DPushConstants constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
            commandBuffer.pushConstants(pipelineLayouts[pipelineType],
                vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
            dispatch_bounds(commandBuffer, bounds);
            insert_compute_memory_barrier(commandBuffer);
        }
    }

    void record_legacy_triangle_mesh(
        vk::CommandBuffer commandBuffer,
        Swapchain& swapchain,
        std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
        std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
        std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
        uint32_t firstTriangle,
        uint32_t triangleCount)
    {
        if (triangleCount == 0 || !bind_pipeline(commandBuffer, PipelineType::eRasteriseBig, pipelines))
        {
            return;
        }

        PipelineType pipelineType = PipelineType::eRasteriseBig;
        bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts);
        commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
            1, 1, &descriptorSets[DescriptorScope::eDrawCall], 0, nullptr);

        RasterPushConstants constants = make_orthographic_constants(swapchain);
        constants.firstTriangle = firstTriangle;
        constants.triangleCount = triangleCount;

        const uint32_t workgroupCountX = workgroup_count(swapchain.extent.width);
        const uint32_t workgroupCountY = workgroup_count(swapchain.extent.height);
        constants.pass = kDepthPass;
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        commandBuffer.dispatch(workgroupCountX, workgroupCountY, triangleCount);

        insert_compute_memory_barrier(commandBuffer);

        constants.pass = kColorPass;
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        commandBuffer.dispatch(workgroupCountX, workgroupCountY, triangleCount);

        insert_compute_memory_barrier(commandBuffer);
    }
}

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
        // Permanently dynamic entities are redrawn when their data changes;
        // mark_dirty() advances frameGeneration_ for that purpose. They have
        // no expiration timer, so only timed cache promotions need this
        // continuous tail frame.
        if (cache.mode == Renderer2DCacheMode::eTimed &&
            (cache.pendingActiveSeconds > 0.0f ||
                cache.activeUntilSeconds > currentTimeSeconds ||
                cache.wasActive))
        {
            return true;
        }
        if (cache.mode == Renderer2DCacheMode::eTimed &&
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

entt::entity Renderer2DScene::entity_at(glm::vec2 point)
{
    resolve_layouts(registry_);

    const ShapeStyleComponent defaultShapeStyle {};
    const TextStyleComponent defaultTextStyle {};
    Renderer2DHitCandidate topmost {};

    auto shapeView = registry_.view<const Transform2DComponent, const ShapeComponent>();
    for (auto entity : shapeView)
    {
        if (!is_visible(registry_, entity))
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
        if (!shape_fill_visible(style) && !shape_outline_visible(style))
        {
            continue;
        }

        const glm::vec4 rect = make_bounds(transform, shape.size);
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
        if (!media_visible(media) || !is_visible(registry_, entity))
        {
            continue;
        }
        if (!point_in_rect(inherited_mask_clip_rect(registry_, entity), point))
        {
            continue;
        }

        const auto& transform = mediaView.get<const Transform2DComponent>(entity);
        if (point_in_primitive(
            Renderer2DPrimitive::eRoundedRectangle,
            media_bounds_from_component(transform, media),
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
        if (!model.visible || !is_visible(registry_, entity) || !alpha_visible(model.materialColor.a))
        {
            continue;
        }

        const auto& transform = modelView.get<const Transform2DComponent>(entity);
        const glm::vec4 viewport = make_model_viewport(registry_, entity, transform, model);
        const glm::vec4 clipRect = make_model_clip_rect(registry_, entity, viewport, model);
        if (point_in_rect(intersect_rect(viewport, clipRect), point))
        {
            consider_hit_candidate(topmost, registry_, entity, Renderer2DRenderOpType::eModel3D);
        }
    }

    auto textView = registry_.view<const Transform2DComponent, const TextComponent>();
    for (auto entity : textView)
    {
        if (!is_visible(registry_, entity))
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

                const glm::vec4 glyphRect {
                    textBounds.x + glyph.position.x * transform.scale.x,
                    textBounds.y + glyph.position.y * transform.scale.y,
                    glyph.size.x * transform.scale.x,
                    glyph.size.y * transform.scale.y
                };
                if (point_in_rect(expand_rect(glyphRect, padding), point))
                {
                    hit = true;
                    break;
                }
            }
        }
        else
        {
            hit = point_in_rect(expand_rect(textBounds, padding), point);
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
        if (!is_visible(registry_, entity))
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

        const glm::vec4 rect = make_bounds(transform, shape.size);
        if (shape_fill_visible(style) || shape_outline_visible(style))
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
        if (!is_visible(registry_, entity))
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

                const glm::vec4 glyphRect {
                    textBounds.x + glyph.position.x * transform.scale.x,
                    textBounds.y + glyph.position.y * transform.scale.y,
                    glyph.size.x * transform.scale.x,
                    glyph.size.y * transform.scale.y
                };
                if (point_in_rect(expand_rect(glyphRect, padding), point))
                {
                    return true;
                }
            }
            continue;
        }

        if (point_in_rect(expand_rect(textBounds, padding), point))
        {
            return true;
        }
    }

    auto mediaView = registry_.view<const Transform2DComponent, const Media2DComponent>();
    for (auto entity : mediaView)
    {
        const auto& media = mediaView.get<const Media2DComponent>(entity);
        if (!media_visible(media) || !is_visible(registry_, entity))
        {
            continue;
        }
        if (!point_in_rect(inherited_mask_clip_rect(registry_, entity), point))
        {
            continue;
        }

        const auto& transform = mediaView.get<const Transform2DComponent>(entity);
        if (point_in_primitive(
            Renderer2DPrimitive::eRoundedRectangle,
            media_bounds_from_component(transform, media),
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
        if (!model.visible || !is_visible(registry_, entity) || !alpha_visible(model.materialColor.a))
        {
            continue;
        }

        const auto& transform = modelView.get<const Transform2DComponent>(entity);
        const glm::vec4 viewport = make_model_viewport(registry_, entity, transform, model);
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

    auto update_entity_cache_state = [&](entt::entity entity) {
        Renderer2DCacheComponent& cache = cache_or_default(registry_, entity);
        if (update_cache_activity(cache, currentTimeSeconds) ||
            should_refresh_idle_cache(cache, currentTimeSeconds))
        {
            mark_dirty();
        }
    };

    shapeView.each([&](entt::entity entity, const Transform2DComponent&, const ShapeComponent&) {
        update_entity_cache_state(entity);
    });
    textView.each([&](entt::entity entity, const Transform2DComponent&, const TextComponent&) {
        update_entity_cache_state(entity);
    });
    mediaView.each([&](entt::entity entity, const Transform2DComponent&, Media2DComponent&) {
        update_entity_cache_state(entity);
    });
    modelView.each([&](entt::entity entity, const Transform2DComponent&, const Model3DComponent&) {
        update_entity_cache_state(entity);
    });

    bool hasVisibleHostedModels = false;
    modelView.each([&](entt::entity entity, const Transform2DComponent& transform, const Model3DComponent& model) {
        const glm::vec4 viewport = make_model_viewport(registry_, entity, transform, model);
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

        Renderer2DBatch batch = {};
        batch.entity = entity;
        batch.stackLayer = layer.stackLayer;
        batch.stackOrder = layer.stackOrder;
        batch.layer = layer.layer;
        batch.order = layer.order;
        batch.alwaysOnTop = layer.alwaysOnTop;
        batch.primitive = shape.primitive;
        batch.rect = apply_display_transition_scale(make_bounds(transform, shape.size), displayTransition);
        batch.clipRect = inherited_mask_clip_rect(registry_, entity);
        batch.uvRect = shape.primitive == Renderer2DPrimitive::eCircularProgress ?
            circular_progress_parameters(shape) :
            glm::vec4(style.gradientStart.x, style.gradientStart.y, style.gradientEnd.x, style.gradientEnd.y);
        batch.color0 = style.color0;
        batch.color1 = style.color1;
        batch.color2 = style.outlineColor;
        const float transitionShapeBlur = std::max(displayTransition.blurRadius, 0.0f);
        batch.effect0 = {
            shape.cornerRadius,
            style.outlineWidth,
            std::max(std::max(style.edgeSoftness, 0.5f), transitionShapeBlur),
            style.opacity * displayTransition.opacity
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
            const ShapeMaskClip mask = inherited_shape_mask_clip(registry_, entity);
            if (mask.enabled && !rect_empty(mask.rect))
            {
                batch.effect1 = mask.rect;
                batch.flags |= eRenderer2DStyleShapeMask;
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

        if (const ShadowComponent* shadow = registry_.try_get<ShadowComponent>(entity))
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
                shadow->opacity * displayTransition.opacity
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
        if (blur || style_backdrop_blur_visible(style))
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
            const float blurOpacity = baseBlurOpacity * displayTransition.opacity;
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
                0.0f,
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
                const ShapeMaskClip mask = inherited_shape_mask_clip(registry_, entity);
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
                }
            }
            blurBatch.flags &= ~(
                eRenderer2DStyleTransform2_5D |
                eRenderer2DStyleCornerRadii |
                eRenderer2DStyleShapeMask);
            blurBatch.packedData = 0u;
            (cachedBatch ? plan.cachedBlurs : plan.blurs).push_back(blurBatch);
        }

        (cachedBatch ? plan.cachedShapes : plan.shapes).push_back(batch);
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
        const glm::vec4 rect = apply_display_transition_scale(
            media_bounds_from_component(transform, media),
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
        batch.primitive = Renderer2DPrimitive::eMedia;
        batch.mediaId = media.mediaId;
        batch.frameIndex = media.currentFrame;
        batch.rect = rect;
        batch.clipRect = inherited_mask_clip_rect(registry_, entity);
        batch.uvRect = media_uv_rect_from_component(media, rect);
        batch.color0 = media.tint;
        batch.color1 = media.tintFill == Renderer2DFill::eSolid ? media.tint : media.tintEnd;
        batch.color2 = { media.gradientStart.x, media.gradientStart.y, media.gradientEnd.x, media.gradientEnd.y };
        batch.effect0 = {
            media.cornerRadius,
            media.edgeSoftness,
            std::max(media.blurRadius, displayTransition.blurRadius),
            media.opacity * displayTransition.opacity
        };
        batch.effect1 = transform2_5d_effect(transform);
        batch.flags = media_flags(media) | transform2_5d_flags(transform);
        if (batch.effect0.z > 0.0f)
        {
            batch.flags |= eRenderer2DStyleBlur;
        }
        if (const ShapeMaskClip mask = inherited_shape_mask_clip(registry_, entity);
            mask.enabled &&
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
        const DisplayTransitionEffect2D displayTransition =
            inherited_visual_effect(registry_, entity, currentTimeSeconds);

        const glm::vec4 textBounds = text_bounds_from_component(transform, text);

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
                style.opacity * displayTransition.opacity
            };
            batch.effect1 = {
                style.shadowOffset.x,
                style.shadowOffset.y,
                style.shadowBlur,
                std::max(style.blurRadius, displayTransition.blurRadius)
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
                batch.rect = apply_display_transition_scale(batch.rect, displayTransition);
                batch.uvRect = { glyph.uvMin.x, glyph.uvMin.y, glyph.uvMax.x, glyph.uvMax.y };
                batch.flags |= eRenderer2DStyleAtlasText;
                targetTexts.push_back(batch);
            }
            return;
        }

        Renderer2DBatch batch = make_text_batch();
        batch.rect = apply_display_transition_scale(textBounds, displayTransition);
        batch.uvRect = { style.gradientStart.x, style.gradientStart.y, style.gradientEnd.x, style.gradientEnd.y };
        targetTexts.push_back(batch);
    });

    modelView.each([&](entt::entity entity, const Transform2DComponent& transform, const Model3DComponent& model)
    {
        if (!model.visible || !is_visible(registry_, entity))
        {
            return;
        }

        const glm::vec4 viewport = make_model_viewport(registry_, entity, transform, model);
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
        batch.materialColor.a *= displayTransition.opacity;
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

void ShadowPipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    DescriptorScope frameScope) const
{
    record_batches(commandBuffer, swapchain, PipelineType::eShadow2D, pipelines, descriptorSets, pipelineLayouts, batches,
        DispatchBoundsMode::eShadow, frameScope);
}

void ShadowPipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    DescriptorScope frameScope) const
{
    if (!bind_pipeline(commandBuffer, PipelineType::eShadow2D, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, PipelineType::eShadow2D, descriptorSets, pipelineLayouts, frameScope);
    const DispatchBounds bounds = make_dispatch_bounds(swapchain, batch, DispatchBoundsMode::eShadow);
    if (bounds.empty())
    {
        return;
    }

    Renderer2DPushConstants constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    commandBuffer.pushConstants(pipelineLayouts[PipelineType::eShadow2D],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void BlurPipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    DescriptorScope frameScope,
    DescriptorScope postScope) const
{
    for (const Renderer2DBatch& batch : batches)
    {
        record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, batch, frameScope, postScope,
            frameScope == DescriptorScope::eFrame, false);
    }
}

void BlurPipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    DescriptorScope frameScope,
    DescriptorScope postScope,
    bool includeStaticBackdrop,
    bool includeExternalBackdrop) const
{
    const PipelineType pipelineType = PipelineType::eBlur2D;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    const DispatchBounds bounds = make_blur_dispatch_bounds(swapchain, batch);
    if (bounds.empty())
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, postScope);

    Renderer2DPushConstants constants = {};
    constants.rect = {
        static_cast<float>(bounds.x),
        static_cast<float>(bounds.y),
        static_cast<float>(bounds.width),
        static_cast<float>(bounds.height)
    };
    constants.data = {
        static_cast<uint32_t>(Renderer2DPrimitive::eClear),
        eRenderer2DStyleClear,
        0u,
        pack_dispatch_origin(bounds.x, bounds.y)
    };
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);

    const uint32_t passCount = std::max(1u, static_cast<uint32_t>(std::round(std::max(batch.effect0.y, 1.0f))));
    Renderer2DBatch passBatch = batch;
    const float targetOpacity = std::clamp(batch.effect0.w, 0.0f, 1.0f);
    passBatch.effect0.w = passCount > 1u
        ? 1.0f - std::pow(1.0f - targetOpacity, 1.0f / static_cast<float>(passCount))
        : targetOpacity;
    for (uint32_t pass = 0; pass < passCount; ++pass)
    {
        uint32_t passData = pass;
        if (includeStaticBackdrop)
        {
            passData |= kBlurUseStaticBackdrop;
        }
        if (includeExternalBackdrop)
        {
            passData |= kBlurUseExternalBackdrop;
        }

        constants = make_push_constants(passBatch, passData, bounds.x, bounds.y);
        constants.data.y |= eRenderer2DStyleBlurHorizontal;
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        dispatch_bounds(commandBuffer, bounds);
        insert_compute_memory_barrier(commandBuffer);

        constants = make_push_constants(passBatch, passData, bounds.x, bounds.y);
        constants.data.y |= eRenderer2DStyleBlurVertical;
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        dispatch_bounds(commandBuffer, bounds);
        insert_compute_memory_barrier(commandBuffer);
    }

    constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    constants.data.y |= eRenderer2DStyleBlurComposite;
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void ShapePipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    DescriptorScope frameScope) const
{
    record_batches(commandBuffer, swapchain, PipelineType::eShape2D, pipelines, descriptorSets, pipelineLayouts, batches,
        DispatchBoundsMode::eShape, frameScope);
}

void ShapePipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    DescriptorScope frameScope) const
{
    if (!bind_pipeline(commandBuffer, PipelineType::eShape2D, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, PipelineType::eShape2D, descriptorSets, pipelineLayouts, frameScope);
    const DispatchBounds bounds = make_dispatch_bounds(swapchain, batch, DispatchBoundsMode::eShape);
    if (bounds.empty())
    {
        return;
    }

    Renderer2DPushConstants constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    commandBuffer.pushConstants(pipelineLayouts[PipelineType::eShape2D],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void MediaPipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
    DescriptorScope frameScope) const
{
    for (const Renderer2DBatch& batch : batches)
    {
        record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, batch, mediaAssets, frameScope);
    }
}

void MediaPipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
    DescriptorScope frameScope) const
{
    if (mediaAssets == nullptr || batch.mediaId == 0)
    {
        return;
    }

    const auto mediaIt = mediaAssets->find(batch.mediaId);
    if (mediaIt == mediaAssets->end() || !mediaIt->second.drawable || !mediaIt->second.descriptorSet)
    {
        return;
    }
    const Media2DAsset& asset = mediaIt->second;

    const PipelineType pipelineType = PipelineType::eMedia2D;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    const DispatchBounds bounds = make_dispatch_bounds(swapchain, batch, DispatchBoundsMode::eExact);
    if (bounds.empty())
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
    vk::DescriptorSet mediaSet = asset.descriptorSet;
    if (!asset.frameDescriptorSets.empty())
    {
        const uint32_t frameIndex = std::min<uint32_t>(
            batch.frameIndex,
            static_cast<uint32_t>(asset.frameDescriptorSets.size() - 1u));
        mediaSet = asset.frameDescriptorSets[frameIndex];
    }
    if (!mediaSet)
    {
        return;
    }
    commandBuffer.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipelineLayouts[pipelineType],
        1, 1, &mediaSet, 0, nullptr);

    Renderer2DPushConstants constants = make_push_constants(batch, 0u, bounds.x, bounds.y);
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void TextPipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    DescriptorScope frameScope,
    DescriptorScope postScope) const
{
    for (const Renderer2DBatch& batch : batches)
    {
        if ((batch.flags & (eRenderer2DStyleShadow | eRenderer2DStyleGlow)) != 0u)
        {
            record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, batch, frameScope,
                postScope, kTextPassUnderlay);
        }
    }

    for (const Renderer2DBatch& batch : batches)
    {
        record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, batch, frameScope, postScope,
            kTextPassForeground);
    }
}

void TextPipeline::record_batch(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const Renderer2DBatch& batch,
    DescriptorScope frameScope,
    DescriptorScope postScope,
    uint32_t textPass) const
{
    const PipelineType pipelineType = PipelineType::eTextMSDF;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, postScope);

    auto record_text_draw = [&](const Renderer2DBatch& drawBatch, uint32_t drawPass, DispatchBoundsMode boundsMode) {
        const DispatchBounds bounds = make_dispatch_bounds(swapchain, drawBatch, boundsMode);
        if (bounds.empty())
        {
            return;
        }

        Renderer2DPushConstants constants = make_push_constants(drawBatch, drawPass, bounds.x, bounds.y);
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        dispatch_bounds(commandBuffer, bounds);
        insert_compute_memory_barrier(commandBuffer);
    };

    if (textPass == kTextPassUnderlay)
    {
        if ((batch.flags & eRenderer2DStyleShadow) != 0u)
        {
            Renderer2DBatch shadowBatch = batch;
            shadowBatch.color2 = batch.shadowColor;
            shadowBatch.effect0.z = 0.0f;
            shadowBatch.flags &= ~eRenderer2DStyleGlow;
            record_text_draw(shadowBatch, textPass, DispatchBoundsMode::eTextUnderlay);
        }

        if ((batch.flags & eRenderer2DStyleGlow) != 0u)
        {
            Renderer2DBatch glowBatch = batch;
            glowBatch.effect1.x = 0.0f;
            glowBatch.effect1.y = 0.0f;
            glowBatch.effect1.z = 0.0f;
            glowBatch.flags &= ~eRenderer2DStyleShadow;
            record_text_draw(glowBatch, textPass, DispatchBoundsMode::eTextUnderlay);
        }
        return;
    }

    const DispatchBoundsMode boundsMode =
        textPass == kTextPassForeground ? DispatchBoundsMode::eTextForeground :
        DispatchBoundsMode::eText;
    record_text_draw(batch, textPass, boundsMode);
}

void CompositePipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    bool useExternalBackdropUnderlay,
    bool writeNativeSurface,
    bool writeCompositionSurface,
    glm::uvec4 contentRect) const
{
    const PipelineType pipelineType = PipelineType::eComposite2D;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts);
    constexpr uint32_t useExternalBackdropFlag = 1u << 0u;
    constexpr uint32_t writeCompositionSurfaceFlag = 1u << 1u;
    constexpr uint32_t writeNativeSurfaceFlag = 1u << 2u;
    const uint32_t flags =
        (useExternalBackdropUnderlay ? useExternalBackdropFlag : 0u) |
        (writeCompositionSurface ? writeCompositionSurfaceFlag : 0u) |
        (writeNativeSurface ? writeNativeSurfaceFlag : 0u);
    DispatchBounds bounds = {};
    if (writeNativeSurface || useExternalBackdropUnderlay)
    {
        bounds = make_full_screen_bounds(swapchain);
    }
    else if (contentRect.z > 0u && contentRect.w > 0u)
    {
        const uint32_t x = std::min(contentRect.x, swapchain.extent.width);
        const uint32_t y = std::min(contentRect.y, swapchain.extent.height);
        bounds = {
            x,
            y,
            std::min(contentRect.z, swapchain.extent.width - x),
            std::min(contentRect.w, swapchain.extent.height - y)
        };
    }
    if (bounds.empty())
    {
        return;
    }

    const glm::uvec4 constants { flags, bounds.x, bounds.y, 0u };
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void Hosted3DCompositePipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    DescriptorScope frameScope,
    DescriptorScope postScope,
    glm::uvec4 dispatchRect) const
{
    const PipelineType pipelineType = PipelineType::eCompositeHosted3D;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, postScope);
    const DispatchBounds bounds =
        dispatchRect.z > 0u && dispatchRect.w > 0u
        ? DispatchBounds {
            std::min(dispatchRect.x, swapchain.extent.width),
            std::min(dispatchRect.y, swapchain.extent.height),
            std::min(dispatchRect.z, swapchain.extent.width - std::min(dispatchRect.x, swapchain.extent.width)),
            std::min(dispatchRect.w, swapchain.extent.height - std::min(dispatchRect.y, swapchain.extent.height))
        }
        : make_full_screen_bounds(swapchain);
    if (bounds.empty())
    {
        return;
    }

    const glm::uvec4 constants { bounds.x, bounds.y, bounds.width, bounds.height };
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    insert_compute_memory_barrier(commandBuffer);
}

void Renderer2D::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    uint32_t firstTriangle,
    uint32_t triangleCount,
    Renderer2DScene& scene,
    double currentTimeSeconds,
    Renderer3D* renderer3D,
    const Camera* camera,
    uint32_t defaultFirstTriangle3D,
    uint32_t defaultTriangleCount3D,
    StorageBuffer* vertexBuffer,
    std::unordered_map<uint32_t, Model3DAsset>* modelAssets,
    std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
    StorageImage* dynamicRenderTarget,
    StorageImage* staticRenderTarget,
    StorageImage* hosted3DResolveTarget,
    vk::RenderPass hosted3DRenderPass,
    vk::Framebuffer hosted3DFramebuffer,
    bool hosted3DUsesResolveAttachment,
    bool externalBackdropAvailable) const
{
    record_legacy_triangle_mesh(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, firstTriangle, triangleCount);

    scene.build_render_plan(renderPlanCache, currentTimeSeconds, cachedLayerGeneration);

    (void)camera;

    auto record_layered_ops = [&](
        const std::vector<Renderer2DBatch>& panelBlurs,
        const std::vector<Renderer2DBatch>& shadows,
        const std::vector<Renderer2DBatch>& blurs,
        const std::vector<Renderer2DBatch>& shapes,
        const std::vector<Renderer2DBatch>& media,
        const std::vector<Renderer2DBatch>& texts,
        const std::vector<Renderer3DModelBatch>& models,
        DescriptorScope frameScope,
        DescriptorScope blurPostScope,
        DescriptorScope textPostScope,
        bool includeStaticBackdrop,
        bool includeExternalBackdrop) {
        std::vector<Renderer2DRenderOp> ops;
        ops.reserve(panelBlurs.size() + shadows.size() + blurs.size() + shapes.size() + media.size() +
            texts.size() * 2u + models.size());
        add_render_ops(ops, panelBlurs, Renderer2DRenderOpType::ePanelBlur);
        add_render_ops(ops, shadows, Renderer2DRenderOpType::eShadow);
        add_render_ops(ops, blurs, Renderer2DRenderOpType::eBlur);
        add_render_ops(ops, shapes, Renderer2DRenderOpType::eShape);
        add_render_ops(ops, media, Renderer2DRenderOpType::eMedia);
        add_model_ops(ops, models);
        for (const Renderer2DBatch& batch : texts)
        {
            if ((batch.flags & (eRenderer2DStyleShadow | eRenderer2DStyleGlow)) != 0u)
            {
                ops.push_back({
                    Renderer2DRenderOpType::eTextUnderlay,
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
            ops.push_back({
                Renderer2DRenderOpType::eText,
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
        sort_render_ops(ops);

        uint32_t modelOrdinal = 0;
        for (const Renderer2DRenderOp& op : ops)
        {
            if (op.type == Renderer2DRenderOpType::eModel3D)
            {
                if (renderer3D != nullptr && op.model != nullptr)
                {
                    const uint32_t depthLayer = kHostedModelDepthMax - std::min(modelOrdinal, kHostedModelDepthMax);
                    StorageImage* modelCompositeTarget =
                        frameScope == DescriptorScope::eUICache ? staticRenderTarget : dynamicRenderTarget;

                    Model3DAsset* modelAsset = nullptr;
                    StorageBuffer* modelVertexBuffer = vertexBuffer;
                    uint32_t modelDefaultFirstTriangle = defaultFirstTriangle3D;
                    uint32_t modelDefaultTriangleCount = defaultTriangleCount3D;
                    if (op.model->modelId != 0)
                    {
                        modelVertexBuffer = nullptr;
                        if (modelAssets != nullptr)
                        {
                            const auto modelIt = modelAssets->find(op.model->modelId);
                            if (modelIt != modelAssets->end())
                            {
                                modelAsset = &modelIt->second;
                                modelVertexBuffer = &modelAsset->buffer;
                                modelDefaultFirstTriangle = modelVertexBuffer->firstTriangle3D;
                                modelDefaultTriangleCount = modelVertexBuffer->triangleCount3D > 0
                                    ? modelVertexBuffer->triangleCount3D
                                    : modelVertexBuffer->triangleCount;
                            }
                        }
                    }

                    if (modelVertexBuffer == nullptr || modelCompositeTarget == nullptr)
                    {
                        ++modelOrdinal;
                        continue;
                    }
                    const bool useHostedGraphics =
                        modelAsset != nullptr &&
                        hosted3DResolveTarget != nullptr &&
                        static_cast<bool>(hosted3DFramebuffer);

                    const bool recordedHostedModel = renderer3D->record_model(
                        commandBuffer,
                        swapchain,
                        pipelines,
                        descriptorSets,
                        pipelineLayouts,
                        frameScope,
                        *op.model,
                        modelDefaultFirstTriangle,
                        modelDefaultTriangleCount,
                        depthLayer,
                        modelVertexBuffer,
                        modelAsset,
                        useHostedGraphics ? hosted3DResolveTarget : nullptr,
                        hosted3DRenderPass,
                        useHostedGraphics ? hosted3DFramebuffer : vk::Framebuffer {},
                        hosted3DUsesResolveAttachment);
                    if (recordedHostedModel && useHostedGraphics)
                    {
                        const DispatchBounds compositeBounds = make_dispatch_bounds(
                            swapchain,
                            intersect_rect(op.model->viewportRect, op.model->clipRect),
                            0.0f);
                        if (compositeBounds.empty())
                        {
                            ++modelOrdinal;
                            continue;
                        }

                        hosted3DCompositePipeline.record(
                            commandBuffer,
                            swapchain,
                            pipelines,
                            descriptorSets,
                            pipelineLayouts,
                            frameScope,
                            blurPostScope,
                            {
                                compositeBounds.x,
                                compositeBounds.y,
                                compositeBounds.width,
                                compositeBounds.height
                            });
                    }
                    ++modelOrdinal;
                }
                continue;
            }

            if (op.batch == nullptr)
            {
                continue;
            }

            switch (op.type)
            {
            case Renderer2DRenderOpType::ePanelBlur:
                blurPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, blurPostScope, includeStaticBackdrop, includeExternalBackdrop);
                break;
            case Renderer2DRenderOpType::eShadow:
                shadowPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope);
                break;
            case Renderer2DRenderOpType::eBlur:
                blurPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, blurPostScope, includeStaticBackdrop, includeExternalBackdrop);
                break;
            case Renderer2DRenderOpType::eShape:
                shapePipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope);
                break;
            case Renderer2DRenderOpType::eMedia:
                mediaPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, mediaAssets, frameScope);
                break;
            case Renderer2DRenderOpType::eModel3D:
                break;
            case Renderer2DRenderOpType::eTextUnderlay:
                textPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, textPostScope, kTextPassUnderlay);
                break;
            case Renderer2DRenderOpType::eText:
                textPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, textPostScope, kTextPassForeground);
                break;
            }
        }
    };

    auto include_dynamic_layer = [](
        const RenderLayer2DKey& layer,
        bool& hasLayer,
        RenderLayer2DKey& lowestLayer) {
        if (!hasLayer || render_layer_key_less(layer, lowestLayer))
        {
            lowestLayer = layer;
            hasLayer = true;
        }
    };

    bool hasDynamicLayer = false;
    RenderLayer2DKey lowestDynamicLayer {};
    auto include_dynamic_batches = [&](const std::vector<Renderer2DBatch>& batches) {
        for (const Renderer2DBatch& batch : batches)
        {
            include_dynamic_layer(render_layer_key(batch), hasDynamicLayer, lowestDynamicLayer);
        }
    };
    include_dynamic_batches(renderPlanCache.panelBlurs);
    include_dynamic_batches(renderPlanCache.shadows);
    include_dynamic_batches(renderPlanCache.blurs);
    include_dynamic_batches(renderPlanCache.shapes);
    include_dynamic_batches(renderPlanCache.media);
    include_dynamic_batches(renderPlanCache.texts);
    for (const Renderer3DModelBatch& model : renderPlanCache.models)
    {
        include_dynamic_layer(render_layer_key(model), hasDynamicLayer, lowestDynamicLayer);
    }

    auto append_cached_overlays = [&](
        std::vector<Renderer2DBatch>& target,
        const std::vector<Renderer2DBatch>& cached) {
        if (!hasDynamicLayer)
        {
            return;
        }

        for (const Renderer2DBatch& batch : cached)
        {
            const RenderLayer2DKey layer = render_layer_key(batch);
            if (render_layer_key_less(lowestDynamicLayer, layer))
            {
                target.push_back(batch);
            }
        }
    };

    auto store_cached_layer_plan = [&]() {
        cachedLayerPlanCache.cachedPanelBlurs = renderPlanCache.cachedPanelBlurs;
        cachedLayerPlanCache.cachedShadows = renderPlanCache.cachedShadows;
        cachedLayerPlanCache.cachedBlurs = renderPlanCache.cachedBlurs;
        cachedLayerPlanCache.cachedShapes = renderPlanCache.cachedShapes;
        cachedLayerPlanCache.cachedMedia = renderPlanCache.cachedMedia;
        cachedLayerPlanCache.cachedTexts = renderPlanCache.cachedTexts;
    };

    const bool cachedCutoffMatches =
        cachedLayerHasDynamicCutoff == hasDynamicLayer &&
        (!hasDynamicLayer ||
            (cachedLayerCutoffStackLayer == lowestDynamicLayer.stackLayer &&
                cachedLayerCutoffStackOrder == lowestDynamicLayer.stackOrder &&
                cachedLayerCutoffLayer == lowestDynamicLayer.layer &&
                cachedLayerCutoffOrder == lowestDynamicLayer.order &&
                cachedLayerCutoffAlwaysOnTop == lowestDynamicLayer.alwaysOnTop));
    const bool rebuildForDynamicCutoff = !cachedCutoffMatches;
    const bool rebuildCachedLayer = renderPlanCache.rebuildCachedLayer || rebuildForDynamicCutoff;

    if (rebuildCachedLayer)
    {
        // A visibility-only dynamic transition (such as a blinking caret) does
        // not invalidate static entities. Reuse the last complete static plan
        // while rebuilding its cutoff, unless the scene itself changed.
        const Renderer2DRenderPlan& staticPlan = renderPlanCache.rebuildCachedLayer ?
            renderPlanCache :
            cachedLayerPlanCache;
        std::vector<Renderer2DBatch> cachedPanelBlurs = staticPlan.cachedPanelBlurs;
        std::vector<Renderer2DBatch> cachedShadows = staticPlan.cachedShadows;
        std::vector<Renderer2DBatch> cachedBlurs = staticPlan.cachedBlurs;
        std::vector<Renderer2DBatch> cachedShapes = staticPlan.cachedShapes;
        std::vector<Renderer2DBatch> cachedMedia = staticPlan.cachedMedia;
        std::vector<Renderer2DBatch> cachedTexts = staticPlan.cachedTexts;

        if (hasDynamicLayer)
        {
            // Higher cached batches are replayed with the moving subtree to preserve
            // z-order. Remove them from the static surface first so they are blended once.
            auto remove_replayed_overlays = [&](std::vector<Renderer2DBatch>& batches) {
                batches.erase(
                    std::remove_if(
                        batches.begin(),
                        batches.end(),
                        [&](const Renderer2DBatch& batch) {
                            return render_layer_key_less(lowestDynamicLayer, render_layer_key(batch));
                        }),
                    batches.end());
            };
            remove_replayed_overlays(cachedPanelBlurs);
            remove_replayed_overlays(cachedShadows);
            remove_replayed_overlays(cachedBlurs);
            remove_replayed_overlays(cachedShapes);
            remove_replayed_overlays(cachedMedia);
            remove_replayed_overlays(cachedTexts);
        }

        clear_frame_surface(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts, DescriptorScope::eUICache);
        record_layered_ops(
            cachedPanelBlurs,
            cachedShadows,
            cachedBlurs,
            cachedShapes,
            cachedMedia,
            cachedTexts,
            {},
            DescriptorScope::eUICache,
            DescriptorScope::eUICachePost,
            DescriptorScope::ePost,
            false,
            externalBackdropAvailable);
        if (renderPlanCache.rebuildCachedLayer)
        {
            store_cached_layer_plan();
            cachedLayerGeneration = scene.cache_generation();
        }
        cachedLayerHasDynamicCutoff = hasDynamicLayer;
        if (hasDynamicLayer)
        {
            cachedLayerCutoffStackLayer = lowestDynamicLayer.stackLayer;
            cachedLayerCutoffStackOrder = lowestDynamicLayer.stackOrder;
            cachedLayerCutoffLayer = lowestDynamicLayer.layer;
            cachedLayerCutoffOrder = lowestDynamicLayer.order;
            cachedLayerCutoffAlwaysOnTop = lowestDynamicLayer.alwaysOnTop;
        }
    }

    if (hasDynamicLayer)
    {
        // Cached chrome above moving content is replayed so static cache does not flatten z-order
        std::vector<Renderer2DBatch> panelBlurs = renderPlanCache.panelBlurs;
        std::vector<Renderer2DBatch> shadows = renderPlanCache.shadows;
        std::vector<Renderer2DBatch> blurs = renderPlanCache.blurs;
        std::vector<Renderer2DBatch> shapes = renderPlanCache.shapes;
        std::vector<Renderer2DBatch> media = renderPlanCache.media;
        std::vector<Renderer2DBatch> texts = renderPlanCache.texts;

        append_cached_overlays(panelBlurs, cachedLayerPlanCache.cachedPanelBlurs);
        append_cached_overlays(shadows, cachedLayerPlanCache.cachedShadows);
        append_cached_overlays(blurs, cachedLayerPlanCache.cachedBlurs);
        append_cached_overlays(shapes, cachedLayerPlanCache.cachedShapes);
        append_cached_overlays(media, cachedLayerPlanCache.cachedMedia);
        append_cached_overlays(texts, cachedLayerPlanCache.cachedTexts);

        record_layered_ops(
            panelBlurs,
            shadows,
            blurs,
            shapes,
            media,
            texts,
            renderPlanCache.models,
            DescriptorScope::eFrame,
            DescriptorScope::ePost,
            DescriptorScope::ePost,
            true,
            externalBackdropAvailable);
    }
    else
    {
        record_layered_ops(
            renderPlanCache.panelBlurs,
            renderPlanCache.shadows,
            renderPlanCache.blurs,
            renderPlanCache.shapes,
            renderPlanCache.media,
            renderPlanCache.texts,
            renderPlanCache.models,
            DescriptorScope::eFrame,
            DescriptorScope::ePost,
            DescriptorScope::ePost,
            true,
            externalBackdropAvailable);
    }

    DispatchBounds visibleBounds = {};
    auto include_bounds = [&](DispatchBounds& target, const DispatchBounds& bounds) {
        if (bounds.empty())
        {
            return;
        }
        if (target.empty())
        {
            target = bounds;
            return;
        }
        const uint32_t left = std::min(target.x, bounds.x);
        const uint32_t top = std::min(target.y, bounds.y);
        const uint32_t right = std::max(
            target.x + target.width,
            bounds.x + bounds.width);
        const uint32_t bottom = std::max(
            target.y + target.height,
            bounds.y + bounds.height);
        target = { left, top, right - left, bottom - top };
    };
    auto include_batches = [&](DispatchBounds& target,
                               const std::vector<Renderer2DBatch>& batches,
                               DispatchBoundsMode mode) {
        for (const Renderer2DBatch& batch : batches)
        {
            include_bounds(target, make_dispatch_bounds(swapchain, batch, mode));
        }
    };
    auto include_blurs = [&](DispatchBounds& target,
                             const std::vector<Renderer2DBatch>& batches) {
        for (const Renderer2DBatch& batch : batches)
        {
            include_bounds(target, make_blur_dispatch_bounds(swapchain, batch));
        }
    };
    auto include_plan = [&](DispatchBounds& target, const Renderer2DRenderPlan& plan) {
        include_blurs(target, plan.panelBlurs);
        include_batches(target, plan.shadows, DispatchBoundsMode::eShadow);
        include_blurs(target, plan.blurs);
        include_batches(target, plan.shapes, DispatchBoundsMode::eShape);
        include_batches(target, plan.media, DispatchBoundsMode::eExact);
        include_batches(target, plan.texts, DispatchBoundsMode::eText);
        for (const Renderer3DModelBatch& model : plan.models)
        {
            include_bounds(target, make_dispatch_bounds(
                swapchain,
                intersect_rect(model.viewportRect, model.clipRect),
                2.0f));
        }
    };

    if (triangleCount > 0u)
    {
        visibleBounds = make_full_screen_bounds(swapchain);
    }
    else
    {
        include_plan(visibleBounds, renderPlanCache);
        include_blurs(visibleBounds, renderPlanCache.cachedPanelBlurs);
        include_batches(visibleBounds, renderPlanCache.cachedShadows, DispatchBoundsMode::eShadow);
        include_blurs(visibleBounds, renderPlanCache.cachedBlurs);
        include_batches(visibleBounds, renderPlanCache.cachedShapes, DispatchBoundsMode::eShape);
        include_batches(visibleBounds, renderPlanCache.cachedMedia, DispatchBoundsMode::eExact);
        include_batches(visibleBounds, renderPlanCache.cachedTexts, DispatchBoundsMode::eText);
    }
    contentBounds = {
        visibleBounds.x,
        visibleBounds.y,
        visibleBounds.width,
        visibleBounds.height
    };
    std::vector<std::pair<entt::entity, glm::uvec4>> currentEntityBounds;
    auto include_entity_bounds = [&](entt::entity entity, const DispatchBounds& bounds) {
        if (entity == entt::null || bounds.empty())
        {
            return;
        }
        auto existing = std::find_if(
            currentEntityBounds.begin(),
            currentEntityBounds.end(),
            [&](const auto& entry) { return entry.first == entity; });
        if (existing == currentEntityBounds.end())
        {
            currentEntityBounds.emplace_back(
                entity,
                glm::uvec4 { bounds.x, bounds.y, bounds.width, bounds.height });
            return;
        }
        const glm::uvec4 previous = existing->second;
        const uint32_t x = std::min(previous.x, bounds.x);
        const uint32_t y = std::min(previous.y, bounds.y);
        const uint32_t right = std::max(
            previous.x + previous.z,
            bounds.x + bounds.width);
        const uint32_t bottom = std::max(
            previous.y + previous.w,
            bounds.y + bounds.height);
        existing->second = { x, y, right - x, bottom - y };
    };
    auto record_batch_bounds = [&](const std::vector<Renderer2DBatch>& batches,
                                   DispatchBoundsMode mode) {
        for (const Renderer2DBatch& batch : batches)
        {
            include_entity_bounds(
                batch.entity,
                make_dispatch_bounds(swapchain, batch, mode));
        }
    };
    auto record_blur_bounds = [&](const std::vector<Renderer2DBatch>& batches) {
        for (const Renderer2DBatch& batch : batches)
        {
            include_entity_bounds(
                batch.entity,
                make_blur_dispatch_bounds(swapchain, batch));
        }
    };
    auto record_plan_bounds = [&](const Renderer2DRenderPlan& plan) {
        record_blur_bounds(plan.panelBlurs);
        record_batch_bounds(plan.shadows, DispatchBoundsMode::eShadow);
        record_blur_bounds(plan.blurs);
        record_batch_bounds(plan.shapes, DispatchBoundsMode::eShape);
        record_batch_bounds(plan.media, DispatchBoundsMode::eExact);
        record_batch_bounds(plan.texts, DispatchBoundsMode::eText);
        for (const Renderer3DModelBatch& model : plan.models)
        {
            include_entity_bounds(
                model.entity,
                make_dispatch_bounds(
                    swapchain,
                    intersect_rect(model.viewportRect, model.clipRect),
                    2.0f));
        }
    };
    record_plan_bounds(renderPlanCache);
    record_blur_bounds(renderPlanCache.cachedPanelBlurs);
    record_batch_bounds(renderPlanCache.cachedShadows, DispatchBoundsMode::eShadow);
    record_blur_bounds(renderPlanCache.cachedBlurs);
    record_batch_bounds(renderPlanCache.cachedShapes, DispatchBoundsMode::eShape);
    record_batch_bounds(renderPlanCache.cachedMedia, DispatchBoundsMode::eExact);
    record_batch_bounds(renderPlanCache.cachedTexts, DispatchBoundsMode::eText);

    DispatchBounds trackedDamage = {};
    if (triangleCount > 0u || rebuildCachedLayer || scene.fullDamagePending_)
    {
        trackedDamage = visibleBounds;
        for (const auto& bounds : scene.presentedBounds_)
        {
            include_bounds(
                trackedDamage,
                DispatchBounds {
                    bounds.second.x,
                    bounds.second.y,
                    bounds.second.z,
                    bounds.second.w });
        }
    }
    else
    {
        auto include_tracked = [&](const auto& bounds) {
            if (scene.damage_pending(bounds.first, currentTimeSeconds))
            {
                include_bounds(
                    trackedDamage,
                    DispatchBounds {
                        bounds.second.x,
                        bounds.second.y,
                        bounds.second.z,
                        bounds.second.w });
            }
        };
        for (const auto& bounds : currentEntityBounds)
        {
            include_tracked(bounds);
        }
        for (const auto& bounds : scene.presentedBounds_)
        {
            include_tracked(bounds);
        }
    }
    damageBounds = {
        trackedDamage.x,
        trackedDamage.y,
        trackedDamage.width,
        trackedDamage.height
    };
    scene.commit_presented_bounds(std::move(currentEntityBounds));
}

void Renderer2D::record_composite(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    bool useExternalBackdropUnderlay,
    bool writeNativeSurface,
    bool writeCompositionSurface,
    glm::uvec4 contentRect) const
{
    compositePipeline.record(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
        useExternalBackdropUnderlay,
        writeNativeSurface,
        writeCompositionSurface,
        contentRect);
}

glm::uvec4 Renderer2D::content_bounds() const
{
    return contentBounds;
}

glm::uvec4 Renderer2D::damage_bounds() const
{
    return damageBounds;
}

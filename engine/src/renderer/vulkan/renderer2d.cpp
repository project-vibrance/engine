#include <vibranceUI/renderer/renderer2d.h>
#include <vibranceUI/renderer/renderer3d.h>
#include "../common/scene_helpers.h"
namespace {
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
    DescriptorScope frameScope,
    bool synchronize) const
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
    if (synchronize)
    {
        insert_compute_memory_barrier(commandBuffer);
    }
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
    DescriptorScope frameScope,
    bool synchronize) const
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
    if (synchronize)
    {
        insert_compute_memory_barrier(commandBuffer);
    }
}

void MediaPipeline::record(
    vk::CommandBuffer commandBuffer,
    Swapchain& swapchain,
    std::unordered_map<PipelineType, vk::Pipeline>& pipelines,
    std::unordered_map<DescriptorScope, vk::DescriptorSet>& descriptorSets,
    std::unordered_map<PipelineType, vk::PipelineLayout>& pipelineLayouts,
    const std::vector<Renderer2DBatch>& batches,
    std::unordered_map<uint32_t, Media2DAsset>* mediaAssets,
    DescriptorScope frameScope,
    DescriptorScope postScope) const
{
    for (const Renderer2DBatch& batch : batches)
    {
        record_batch(
            commandBuffer,
            swapchain,
            pipelines,
            descriptorSets,
            pipelineLayouts,
            batch,
            mediaAssets,
            frameScope,
            postScope);
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
    DescriptorScope frameScope,
    DescriptorScope postScope,
    bool synchronize) const
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
    commandBuffer.bindDescriptorSets(
        vk::PipelineBindPoint::eCompute,
        pipelineLayouts[pipelineType],
        2,
        1,
        &descriptorSets[postScope],
        0,
        nullptr);

    // Media primitive values occupy four bits. Carry the exact dispatch extent
    // in the remaining bits so the shader can reject spare invocations in the
    // final 8x8 workgroup before running an expensive source filter. A zero
    // extent is the fallback for an unusually large (>16K) batch.
    constexpr uint32_t kMediaDispatchExtentMask = 0x3fffu;
    const auto make_media_constants = [&] (
        const DispatchBounds& dispatchBounds,
        uint32_t extraFlags) {
        Renderer2DPushConstants constants = make_push_constants(
            batch,
            0u,
            dispatchBounds.x,
            dispatchBounds.y);
        constants.data.y |= extraFlags;
        if (dispatchBounds.width <= kMediaDispatchExtentMask &&
            dispatchBounds.height <= kMediaDispatchExtentMask)
        {
            constants.data.x =
                (static_cast<uint32_t>(batch.primitive) & 0x0fu) |
                ((dispatchBounds.width & kMediaDispatchExtentMask) << 4u) |
                ((dispatchBounds.height & kMediaDispatchExtentMask) << 18u);
        }
        return constants;
    };

    const uint32_t blurRadius = static_cast<uint32_t>(
        std::clamp(std::lround(batch.effect0.z), 0l, 32l));
    const bool useSeparableBlur =
        blurRadius > 0u &&
        (batch.flags & eRenderer2DStyleMediaSingleBlurSample) != 0u &&
        (batch.flags & eRenderer2DStyleTransform2_5D) == 0u;
    if (useSeparableBlur)
    {
        const uint32_t horizontalTop =
            bounds.y > blurRadius ? bounds.y - blurRadius : 0u;
        const uint32_t horizontalBottom = std::min(
            bounds.y + bounds.height + blurRadius,
            swapchain.extent.height);
        const DispatchBounds horizontalBounds {
            bounds.x,
            horizontalTop,
            bounds.width,
            horizontalBottom - horizontalTop
        };
        Renderer2DPushConstants horizontalConstants = make_media_constants(
            horizontalBounds,
            eRenderer2DStyleMediaBlurHorizontal);
        commandBuffer.pushConstants(
            pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute,
            0,
            sizeof(horizontalConstants),
            &horizontalConstants);
        dispatch_bounds(commandBuffer, horizontalBounds);
        insert_compute_memory_barrier(commandBuffer);
    }

    Renderer2DPushConstants constants = make_media_constants(
        bounds,
        useSeparableBlur ? eRenderer2DStyleMediaBlurVertical : 0u);
    commandBuffer.pushConstants(pipelineLayouts[pipelineType],
        vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
    dispatch_bounds(commandBuffer, bounds);
    if (synchronize || useSeparableBlur)
    {
        // A following media batch may reuse overlapping horizontal scratch
        // even when its final destination is disjoint. Always finish vertical
        // scratch reads before allowing the next horizontal writer to begin.
        insert_compute_memory_barrier(commandBuffer);
    }
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
    uint32_t textPass,
    bool synchronize) const
{
    const PipelineType pipelineType = PipelineType::eTextMSDF;
    if (!bind_pipeline(commandBuffer, pipelineType, pipelines))
    {
        return;
    }

    bind_frame_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, frameScope);
    bind_post_set(commandBuffer, pipelineType, descriptorSets, pipelineLayouts, postScope);

    auto record_text_draw = [&](const Renderer2DBatch& drawBatch, uint32_t drawPass, DispatchBoundsMode boundsMode) {
        const DispatchBounds bounds = make_dispatch_bounds(swapchain, drawBatch, boundsMode, false);
        if (bounds.empty())
        {
            return;
        }

        Renderer2DPushConstants constants = make_push_constants(drawBatch, drawPass, bounds.x, bounds.y);
        // Text does not use data.x for a primitive type. Carry the exact
        // dispatch extent there so the shader can reject spare invocations in
        // the final 8x8 workgroup instead of drawing beyond a scrolling mask.
        constants.data.x = pack_dispatch_extent(bounds.width, bounds.height);
        commandBuffer.pushConstants(pipelineLayouts[pipelineType],
            vk::ShaderStageFlagBits::eCompute, 0, sizeof(constants), &constants);
        dispatch_bounds(commandBuffer, bounds);
        if (synchronize)
        {
            insert_compute_memory_barrier(commandBuffer);
        }
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
    Renderer2DScene& scene,
    double currentTimeSeconds,
    Renderer3D* renderer3D,
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
    scene.build_render_plan(renderPlanCache, currentTimeSeconds, cachedLayerGeneration);

    const auto collect_dynamic_entities = [](const Renderer2DRenderPlan& plan) {
        std::vector<uint32_t> entities;
        const auto append_batches = [&](const std::vector<Renderer2DBatch>& batches) {
            for (const Renderer2DBatch& batch : batches)
            {
                if (batch.entity != entt::null)
                {
                    entities.push_back(entity_key(batch.entity));
                }
            }
        };
        append_batches(plan.panelBlurs);
        append_batches(plan.shadows);
        append_batches(plan.blurs);
        append_batches(plan.shapes);
        append_batches(plan.media);
        append_batches(plan.texts);
        for (const Renderer3DModelBatch& model : plan.models)
        {
            if (model.entity != entt::null)
            {
                entities.push_back(entity_key(model.entity));
            }
        }
        std::sort(entities.begin(), entities.end());
        entities.erase(std::unique(entities.begin(), entities.end()), entities.end());
        return entities;
    };

    std::vector<uint32_t> currentDynamicEntities =
        collect_dynamic_entities(renderPlanCache);
    if (currentDynamicEntities != cachedDynamicEntities &&
        !renderPlanCache.rebuildCachedLayer)
    {
        // Cache modes are public components and can be changed directly. Never
        // trust a missed mark_dirty() to leave old pixels in the static image:
        // an ownership-set change always rebuilds the static layer before the
        // same entity can be emitted dynamically (or restored to static).
        scene.build_render_plan(renderPlanCache, currentTimeSeconds, 0u);
        currentDynamicEntities = collect_dynamic_entities(renderPlanCache);
    }
    cachedDynamicEntities = std::move(currentDynamicEntities);

    // Every in-flight slot owns a retained dynamic image. On first use it has no
    // trustworthy contents, so clear it completely. Afterwards, clear exactly
    // the workgroup-aligned envelope written by this same slot on its previous
    // use. The envelope is accumulated from the actual dispatch bounds below,
    // including multi-pass blur, shadow, text, masks, replayed overlays and 3D.
    // This removes stale pixels without clearing an entire ultrawide surface for
    // a small island visualiser update.
    if (!dynamicSurfaceInitialized ||
        (dynamicSurfaceBounds.z > 0u && dynamicSurfaceBounds.w > 0u))
    {
        clear_frame_surface(
            commandBuffer,
            swapchain,
            pipelines,
            descriptorSets,
            pipelineLayouts,
            DescriptorScope::eFrame,
            dynamicSurfaceInitialized ? dynamicSurfaceBounds : glm::uvec4(0u));
    }
    dynamicSurfaceInitialized = true;

    DispatchBounds currentDynamicSurfaceBounds = {};
    const auto include_dynamic_surface_bounds = [&](const DispatchBounds& bounds) {
        if (bounds.empty())
        {
            return;
        }
        if (currentDynamicSurfaceBounds.empty())
        {
            currentDynamicSurfaceBounds = bounds;
            return;
        }
        const uint32_t left = std::min(currentDynamicSurfaceBounds.x, bounds.x);
        const uint32_t top = std::min(currentDynamicSurfaceBounds.y, bounds.y);
        const uint32_t right = std::max(
            currentDynamicSurfaceBounds.x + currentDynamicSurfaceBounds.width,
            bounds.x + bounds.width);
        const uint32_t bottom = std::max(
            currentDynamicSurfaceBounds.y + currentDynamicSurfaceBounds.height,
            bounds.y + bounds.height);
        currentDynamicSurfaceBounds = {
            left,
            top,
            right - left,
            bottom - top
        };
    };

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

        // Most UI primitives write independent rectangles into the same storage image. Vulkan
        // does not require a memory dependency between disjoint writes, so defer the compute
        // barrier until a later primitive overlaps one of them or a multi-pass effect needs the
        // accumulated image. This is especially important for text, where a marquee can otherwise
        // emit one full pipeline barrier per glyph on every submitted frame.
        std::vector<DispatchBounds> pendingWrites;
        pendingWrites.reserve(ops.size());

        const auto flush_pending_writes = [&]() {
            if (pendingWrites.empty())
            {
                return;
            }
            insert_compute_memory_barrier(commandBuffer);
            pendingWrites.clear();
        };

        const auto prepare_simple_write = [&](const DispatchBounds& bounds) {
            if (bounds.empty())
            {
                return false;
            }
            // Static surfaces live for many frames and across cache ownership
            // handoffs. Build them deterministically: an underestimated optical
            // bound must never turn two storage-image writes into an unordered
            // race whose missing pixels then remain cached. Dynamic surfaces
            // keep the cheaper overlap-aware batching on every motion frame.
            const bool overlapsPendingWrite =
                frameScope == DescriptorScope::eUICache ||
                std::any_of(
                    pendingWrites.begin(),
                    pendingWrites.end(),
                    [&](const DispatchBounds& pending) {
                        return dispatch_bounds_overlap(pending, bounds);
                    });
            if (overlapsPendingWrite)
            {
                flush_pending_writes();
            }
            pendingWrites.push_back(bounds);
            return true;
        };

        uint32_t modelOrdinal = 0;
        for (const Renderer2DRenderOp& op : ops)
        {
            if (op.type == Renderer2DRenderOpType::eModel3D)
            {
                flush_pending_writes();
                if (renderer3D != nullptr && op.model != nullptr)
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(make_dispatch_bounds(
                            swapchain,
                            intersect_rect(op.model->viewportRect, op.model->clipRect),
                            2.0f));
                    }
                    const uint32_t depthLayer = kHostedModelDepthMax - std::min(modelOrdinal, kHostedModelDepthMax);
                    StorageImage* modelCompositeTarget =
                        frameScope == DescriptorScope::eUICache ? staticRenderTarget : dynamicRenderTarget;

                    Model3DAsset* modelAsset = nullptr;
                    StorageBuffer* modelVertexBuffer = nullptr;
                    uint32_t modelDefaultFirstTriangle = 0u;
                    uint32_t modelDefaultTriangleCount = 0u;
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
                                modelDefaultFirstTriangle = 0u;
                                modelDefaultTriangleCount = modelVertexBuffer->triangleCount;
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
                flush_pending_writes();
                if (frameScope == DescriptorScope::eFrame)
                {
                    include_dynamic_surface_bounds(
                        make_blur_dispatch_bounds(swapchain, *op.batch));
                }
                blurPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, blurPostScope, includeStaticBackdrop, includeExternalBackdrop);
                break;
            case Renderer2DRenderOpType::eShadow:
            {
                const DispatchBounds bounds = make_dispatch_bounds(
                    swapchain, *op.batch, DispatchBoundsMode::eShadow);
                if (prepare_simple_write(bounds))
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(bounds);
                    }
                    shadowPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                        *op.batch, frameScope, false);
                }
                break;
            }
            case Renderer2DRenderOpType::eBlur:
                flush_pending_writes();
                if (frameScope == DescriptorScope::eFrame)
                {
                    include_dynamic_surface_bounds(
                        make_blur_dispatch_bounds(swapchain, *op.batch));
                }
                blurPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, blurPostScope, includeStaticBackdrop, includeExternalBackdrop);
                break;
            case Renderer2DRenderOpType::eShape:
            {
                const DispatchBounds bounds = make_dispatch_bounds(
                    swapchain, *op.batch, DispatchBoundsMode::eShape);
                if (prepare_simple_write(bounds))
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(bounds);
                    }
                    shapePipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                        *op.batch, frameScope, false);
                }
                break;
            }
            case Renderer2DRenderOpType::eMedia:
            {
                const DispatchBounds bounds = make_dispatch_bounds(
                    swapchain, *op.batch, DispatchBoundsMode::eExact);
                if (prepare_simple_write(bounds))
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(bounds);
                    }
                    mediaPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                        *op.batch, mediaAssets, frameScope, blurPostScope, false);
                }
                break;
            }
            case Renderer2DRenderOpType::eModel3D:
                break;
            case Renderer2DRenderOpType::eTextUnderlay:
                flush_pending_writes();
                if (frameScope == DescriptorScope::eFrame)
                {
                    include_dynamic_surface_bounds(make_dispatch_bounds(
                        swapchain, *op.batch, DispatchBoundsMode::eTextUnderlay));
                }
                textPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                    *op.batch, frameScope, textPostScope, kTextPassUnderlay);
                break;
            case Renderer2DRenderOpType::eText:
            {
                const DispatchBounds bounds = make_dispatch_bounds(
                    swapchain, *op.batch, DispatchBoundsMode::eTextForeground);
                if (prepare_simple_write(bounds))
                {
                    if (frameScope == DescriptorScope::eFrame)
                    {
                        include_dynamic_surface_bounds(bounds);
                    }
                    textPipeline.record_batch(commandBuffer, swapchain, pipelines, descriptorSets, pipelineLayouts,
                        *op.batch, frameScope, textPostScope, kTextPassForeground, false);
                }
                break;
            }
            }
        }
        flush_pending_writes();
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

    DispatchBounds dynamicCoverageBounds = {};
    auto include_dynamic_coverage = [&](const std::vector<Renderer2DBatch>& batches) {
        for (const Renderer2DBatch& batch : batches)
        {
            dynamicCoverageBounds = union_dispatch_bounds(
                dynamicCoverageBounds,
                make_dispatch_bounds(swapchain, batch.clipRect, 0.0f));
        }
    };
    include_dynamic_coverage(renderPlanCache.panelBlurs);
    include_dynamic_coverage(renderPlanCache.shadows);
    include_dynamic_coverage(renderPlanCache.blurs);
    include_dynamic_coverage(renderPlanCache.shapes);
    include_dynamic_coverage(renderPlanCache.media);
    include_dynamic_coverage(renderPlanCache.texts);
    for (const Renderer3DModelBatch& model : renderPlanCache.models)
    {
        dynamicCoverageBounds = union_dispatch_bounds(
            dynamicCoverageBounds,
            make_dispatch_bounds(swapchain, model.clipRect, 0.0f));
    }
    dynamicCoverageBounds = align_dispatch_bounds_to_workgroups(
        swapchain,
        dynamicCoverageBounds);

    auto cached_batch_overlays_dynamic = [&] (
        const Renderer2DBatch& batch,
        DispatchBoundsMode mode,
        bool blurBounds) {
        if (!hasDynamicLayer ||
            !render_layer_key_less(lowestDynamicLayer, render_layer_key(batch)))
        {
            return false;
        }
        const DispatchBounds bounds = blurBounds ?
            make_blur_dispatch_bounds(swapchain, batch) :
            make_dispatch_bounds(swapchain, batch, mode);
        return dispatch_bounds_overlap(dynamicCoverageBounds, bounds);
    };

    auto append_cached_overlays = [&](
        std::vector<Renderer2DBatch>& target,
        const std::vector<Renderer2DBatch>& cached,
        DispatchBoundsMode mode,
        bool blurBounds = false) {
        if (!hasDynamicLayer)
        {
            return;
        }

        for (const Renderer2DBatch& batch : cached)
        {
            if (cached_batch_overlays_dynamic(batch, mode, blurBounds))
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
                cachedLayerCutoffAlwaysOnTop == lowestDynamicLayer.alwaysOnTop &&
                cachedLayerCutoffBounds.x == dynamicCoverageBounds.x &&
                cachedLayerCutoffBounds.y == dynamicCoverageBounds.y &&
                cachedLayerCutoffBounds.z == dynamicCoverageBounds.width &&
                cachedLayerCutoffBounds.w == dynamicCoverageBounds.height));
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
            auto remove_replayed_overlays = [&] (
                std::vector<Renderer2DBatch>& batches,
                DispatchBoundsMode mode,
                bool blurBounds = false) {
                batches.erase(
                    std::remove_if(
                        batches.begin(),
                        batches.end(),
                        [&](const Renderer2DBatch& batch) {
                            return cached_batch_overlays_dynamic(batch, mode, blurBounds);
                        }),
                    batches.end());
            };
            remove_replayed_overlays(cachedPanelBlurs, DispatchBoundsMode::eExact, true);
            remove_replayed_overlays(cachedShadows, DispatchBoundsMode::eShadow);
            remove_replayed_overlays(cachedBlurs, DispatchBoundsMode::eExact, true);
            remove_replayed_overlays(cachedShapes, DispatchBoundsMode::eShape);
            remove_replayed_overlays(cachedMedia, DispatchBoundsMode::eExact);
            remove_replayed_overlays(cachedTexts, DispatchBoundsMode::eText);
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
            cachedLayerCutoffBounds = {
                dynamicCoverageBounds.x,
                dynamicCoverageBounds.y,
                dynamicCoverageBounds.width,
                dynamicCoverageBounds.height };
        }
        else
        {
            cachedLayerCutoffBounds = glm::uvec4(0u);
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

        append_cached_overlays(
            panelBlurs,
            cachedLayerPlanCache.cachedPanelBlurs,
            DispatchBoundsMode::eExact,
            true);
        append_cached_overlays(
            shadows,
            cachedLayerPlanCache.cachedShadows,
            DispatchBoundsMode::eShadow);
        append_cached_overlays(
            blurs,
            cachedLayerPlanCache.cachedBlurs,
            DispatchBoundsMode::eExact,
            true);
        append_cached_overlays(
            shapes,
            cachedLayerPlanCache.cachedShapes,
            DispatchBoundsMode::eShape);
        append_cached_overlays(
            media,
            cachedLayerPlanCache.cachedMedia,
            DispatchBoundsMode::eExact);
        append_cached_overlays(
            texts,
            cachedLayerPlanCache.cachedTexts,
            DispatchBoundsMode::eText);

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

    include_plan(visibleBounds, renderPlanCache);
    // renderPlanCache only carries static batches on a cache rebuild. The
    // retained plan is the authoritative visible static layer on later frames
    // and must remain part of the content bounds. Otherwise a newly acquired
    // Composition buffer can be cleared and populated with only the dynamic
    // right-hand page, leaving fixed navigation/search chrome transparent.
    include_blurs(visibleBounds, cachedLayerPlanCache.cachedPanelBlurs);
    include_batches(visibleBounds, cachedLayerPlanCache.cachedShadows, DispatchBoundsMode::eShadow);
    include_blurs(visibleBounds, cachedLayerPlanCache.cachedBlurs);
    include_batches(visibleBounds, cachedLayerPlanCache.cachedShapes, DispatchBoundsMode::eShape);
    include_batches(visibleBounds, cachedLayerPlanCache.cachedMedia, DispatchBoundsMode::eExact);
    include_batches(visibleBounds, cachedLayerPlanCache.cachedTexts, DispatchBoundsMode::eText);
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
    record_blur_bounds(cachedLayerPlanCache.cachedPanelBlurs);
    record_batch_bounds(cachedLayerPlanCache.cachedShadows, DispatchBoundsMode::eShadow);
    record_blur_bounds(cachedLayerPlanCache.cachedBlurs);
    record_batch_bounds(cachedLayerPlanCache.cachedShapes, DispatchBoundsMode::eShape);
    record_batch_bounds(cachedLayerPlanCache.cachedMedia, DispatchBoundsMode::eExact);
    record_batch_bounds(cachedLayerPlanCache.cachedTexts, DispatchBoundsMode::eText);

    DispatchBounds trackedDamage = {};
    if (rebuildCachedLayer || scene.fullDamagePending_)
    {
        trackedDamage = make_full_screen_bounds(swapchain);
    }
    else
    {
        const auto include_entity_rect = [&](const auto& bounds) {
            include_bounds(
                trackedDamage,
                DispatchBounds {
                    bounds.second.x,
                    bounds.second.y,
                    bounds.second.z,
                    bounds.second.w });
        };
        const auto same_rect = [](glm::uvec4 left, glm::uvec4 right) {
            return left.x == right.x && left.y == right.y &&
                left.z == right.z && left.w == right.w;
        };

        // Every Renderer2D instance owns one in-flight frame slot. Compare its
        // current entity envelope with what that same slot presented last time;
        // a scene-global previous-bounds list can be consumed by another slot
        // first and leave moved/destroyed pixels behind when buffers rotate.
        for (const auto& current : currentEntityBounds)
        {
            const auto previous = std::find_if(
                presentedBounds.begin(),
                presentedBounds.end(),
                [&](const auto& entry) { return entry.first == current.first; });
            if (previous == presentedBounds.end())
            {
                include_entity_rect(current);
            }
            else if (!same_rect(current.second, previous->second))
            {
                include_entity_rect(current);
                include_entity_rect(*previous);
            }
        }
        for (const auto& previous : presentedBounds)
        {
            const auto current = std::find_if(
                currentEntityBounds.begin(),
                currentEntityBounds.end(),
                [&](const auto& entry) { return entry.first == previous.first; });
            if (current == currentEntityBounds.end())
            {
                include_entity_rect(previous);
            }
        }

        auto include_tracked = [&](const auto& bounds) {
            if (scene.damage_pending(bounds.first, currentTimeSeconds))
            {
                include_entity_rect(bounds);
            }
        };
        for (const auto& bounds : currentEntityBounds)
        {
            include_tracked(bounds);
        }
    for (const auto& bounds : presentedBounds)
    {
        include_tracked(bounds);
    }
    }

    // Compute shaders dispatch in 8x8 groups. Retained clears, damage copies,
    // and compositor updates must own the same complete workgroups; tracking an
    // exact pixel rect can otherwise leave a one-pixel edge from an older slot.
    const DispatchBounds alignedDamage = align_dispatch_bounds_to_workgroups(
        swapchain,
        trackedDamage);
    damageBounds = {
        alignedDamage.x,
        alignedDamage.y,
        alignedDamage.width,
        alignedDamage.height };
    const DispatchBounds alignedDynamicSurface =
        align_dispatch_bounds_to_workgroups(swapchain, currentDynamicSurfaceBounds);
    dynamicSurfaceBounds = {
        alignedDynamicSurface.x,
        alignedDynamicSurface.y,
        alignedDynamicSurface.width,
        alignedDynamicSurface.height };
    presentedBounds = currentEntityBounds;
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

void Renderer2D::invalidate_dynamic_surface()
{
    dynamicSurfaceInitialized = false;
    cachedLayerGeneration = 0u;
    cachedLayerHasDynamicCutoff = false;
    cachedLayerCutoffBounds = glm::uvec4(0u);
    cachedDynamicEntities.clear();
    presentedBounds.clear();
    renderPlanCache.clear();
    cachedLayerPlanCache.clear();
    dynamicSurfaceBounds = glm::uvec4(0u);
    contentBounds = glm::uvec4(0u);
    damageBounds = glm::uvec4(0u);
}

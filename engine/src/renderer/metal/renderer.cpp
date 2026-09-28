#include "device.h"
#include "mesh.h"
#include "frame_damage.h"
#include "../common/scene_helpers.h"
#include <vibranceUI/renderer/renderer.h>
#include <vibranceUI/core/logger.h>
#include <vibranceUI/ui/text.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/ext/matrix_clip_space.hpp>
#include <chrono>
#include <mutex>
#include <thread>
#include <set>
#include <sstream>
#include <stdexcept>

using namespace vibrance::metal;
namespace
{
std::mutex liveMutex;
std::vector<Engine *> liveEngines;
std::string sharedLocale = "en_us";
using Clock = std::chrono::steady_clock;
struct FrameBatch
{
    bool active = false;
    uint32_t rate = 60;
};
thread_local FrameBatch frameBatch;
std::string resource_file_cache_version(const std::filesystem::path &path)
{
    std::error_code error;
    const std::uintmax_t size = std::filesystem::file_size(path, error);
    const std::uintmax_t safeSize = error ? 0u : size;
    error.clear();
    const auto writeTime = std::filesystem::last_write_time(path, error);
    // libc++ may expose the filesystem clock count as a 128-bit integer,
    // which has no std::to_string overload. The cache stamp only needs a
    // stable, process-independent scalar representation.
    const std::int64_t writeTicks =
        error ? 0 : static_cast<std::int64_t>(writeTime.time_since_epoch().count());
    return std::to_string(safeSize) + "@" + std::to_string(writeTicks);
}

} // namespace
struct Engine::Impl
{
    Device device;
    void *window;
    Owned<CA::MetalLayer> layer;
    RendererExtent extent;
    Renderer2DScene scene;
    Renderer2DRenderPlan plan;
    std::vector<Renderer2DRenderOp> ops;
    FrameDamage frameDamage;
    std::vector<FrameDamage::Item> paintItems;
    DispatchBounds damageBounds;
    uint64_t fontTextureId = 0;
    std::vector<Renderer2DPushConstants> textConstants;
    std::vector<DispatchBounds> textBounds;
    uint32_t textWidth = 0, textHeight = 0;
    Renderer2DFontAtlas font;
    AudioEngine audio;
    Localisation localisation;
    MetalTextureUpload upload;
    std::filesystem::path fontPath;
    Renderer2DFontAtlasLoadOptions fontOptions;
    std::unordered_set<uint32_t> attemptedGlyphs;
    std::unordered_map<entt::entity, std::string> checkedText;
    uint64_t checkedTextGeneration = UINT64_MAX;
    std::unordered_map<uint32_t, Media2DAsset> media;
    std::unordered_map<uint32_t, MetalModel> models;
    std::unordered_map<uint32_t, Owned<MTL::Buffer>> modelBuffers;
    std::unordered_map<std::string, uint32_t> modelCache, mediaCache;
    uint32_t nextMedia = 1, nextModel = 1;
    Owned<MTL::Texture> colour, temporary, blur, depth, hosted, hostedDepth, hostedMSAA;
    Owned<MTL::RenderPipelineState> modelPipeline;
    Owned<MTL::DepthStencilState> depthState;
    std::unordered_map<std::string, Owned<MTL::SamplerState>> modelSamplers;
    std::unique_ptr<StorageImage> backdrop, fallbackFont;
    uint32_t cap, frameRate, samples = 1;
    RendererPresentMode preference;
    Clock::time_point start = Clock::now(), nextFrame = start;
    Clock::time_point lastPresentation = start;
    double previousSample = 0;
    uint32_t frameCount = 0;
    int fps = 0;
    bool valid = false;
    uint64_t presentedGeneration = UINT64_MAX;

    explicit Impl(const EngineCreateInfo &info)
        : device(info.preferMetal4), window(info.nativeWindowHandle),
          audio(info.enableAudio), cap(info.maxRenderPixels),
          frameRate(info.targetFrameRate), preference(info.presentMode)
    {
        auto pool = own(NS::AutoreleasePool::alloc()->init());
        if (info.renderBackend != RenderBackend::eMetal)
            throw std::runtime_error("This macOS engine uses the native Metal renderer");
        layer = own(attach_surface(window, device.gpu.get(), info.transparentFramebuffer));
        if (!layer)
            throw std::runtime_error("Metal requires a Cocoa window on the main thread");
        layer->setDisplaySyncEnabled(preference != RendererPresentMode::eImmediate);
        upload = [this](auto w, auto h, auto bytes, bool srgb, bool mipmaps) {
            return device.upload(w, h, bytes, srgb, mipmaps);
        };
        const std::array<unsigned char, 4> zero{};
        fallbackFont = upload(1, 1, zero, false, false);
        for (uint32_t count : {8u, 4u, 2u})
            if (count <= info.msaaSamples && device.gpu->supportsTextureSampleCount(count))
            {
                samples = count;
                break;
            }
        for (const char *name :
             {"shape_2d_comp", "shadow_2d_comp", "blur_2d_comp", "text_msdf_batch_comp", "media_2d_comp",
              "clear_screen_comp", "composite_2d_comp", "hosted_3d_composite_comp"})
            device.pipeline(name);
        models.emplace(0, make_metal_triangle(upload));
        resize(info.framebufferWidth, info.framebufferHeight);
        valid = true;
        Logger::fetch_logger()->info(std::string("Native Metal ") + (device.metal4 ? "4" : "3") +
                                     " renderer: " + device.gpu->name()->utf8String());
    }
    ~Impl()
    {
        device.flush();
        detach_surface(window, layer.get());
    }
    void resize(uint32_t w, uint32_t h)
    {
        if (cap && uint64_t(w) * h > cap)
        {
            const double scale = std::sqrt(double(cap) / (double(w) * h));
            w = std::max(1u, uint32_t(w * scale));
            h = std::max(1u, uint32_t(h * scale));
        }
        // Hosts report the framebuffer size on every tick, including idle ticks.
        // Preserve the attachments and the presented scene until it really changes.
        if (extent.width == w && extent.height == h)
            return;
        auto pool = own(NS::AutoreleasePool::alloc()->init());
        device.flush();
        extent = {w, h};
        frameDamage.invalidate();
        layer->setDrawableSize(CGSizeMake(w, h));
        if (!w || !h)
        {
            colour.reset();
            temporary.reset();
            blur.reset();
            depth.reset();
            hosted.reset();
            hostedDepth.reset();
            hostedMSAA.reset();
            return;
        }
        colour = device.target(w, h);
        // Most UI windows never use backdrop or media blur. Allocate scratch
        // only when a render operation needs it.
        temporary.reset();
        blur.reset();
        // clear() only clears colour (its depth flag is always zero). Keep a
        // valid binding for the shared shader without another full-size target.
        depth = device.target(1, 1, MTL::PixelFormatR32Uint);
        // UI-only windows do not need 3D colour and depth attachments.
        hosted.reset();
        hostedDepth.reset();
        hostedMSAA.reset();
        scene.mark_dirty();
    }
    void ensure_model_targets()
    {
        if (hosted)
            return;
        hosted = device.target(extent.width, extent.height);
        auto depthDescriptor =
            MTL::TextureDescriptor::texture2DDescriptor(
                MTL::PixelFormatDepth32Float, extent.width, extent.height, false);
        depthDescriptor->setUsage(MTL::TextureUsageRenderTarget);
        depthDescriptor->setStorageMode(MTL::StorageModePrivate);
        if (samples > 1)
        {
            depthDescriptor->setTextureType(MTL::TextureType2DMultisample);
            depthDescriptor->setSampleCount(samples);
        }
        hostedDepth = own(device.gpu->newTexture(depthDescriptor));
        if (samples > 1)
        {
            auto descriptor =
                MTL::TextureDescriptor::texture2DDescriptor(
                    MTL::PixelFormatRGBA8Unorm, extent.width, extent.height, false);
            descriptor->setTextureType(MTL::TextureType2DMultisample);
            descriptor->setSampleCount(samples);
            descriptor->setUsage(MTL::TextureUsageRenderTarget);
            descriptor->setStorageMode(MTL::StorageModePrivate);
            hostedMSAA = own(device.gpu->newTexture(descriptor));
        }
        if (!hostedDepth || (samples > 1 && !hostedMSAA))
            throw std::runtime_error("Cannot allocate Metal 3D attachments");
    }
    void clear(MTL::Texture *image, DispatchBounds bounds)
    {
        glm::uvec4 constants(0, bounds.x, bounds.y, 0);
        device.dispatch("clear_screen_comp", &constants, sizeof(constants), bounds.width, bounds.height,
                        {image, depth.get()});
    }
    DispatchBounds clipped_to_damage(DispatchBounds bounds) const
    {
        const auto left = std::max(bounds.x, damageBounds.x);
        const auto top = std::max(bounds.y, damageBounds.y);
        const auto right = std::min(bounds.x + bounds.width, damageBounds.x + damageBounds.width);
        const auto bottom = std::min(bounds.y + bounds.height, damageBounds.y + damageBounds.height);
        return right > left && bottom > top ? DispatchBounds{left, top, right - left, bottom - top}
                                            : DispatchBounds{};
    }
    DispatchBounds operation_bounds(const Renderer2DRenderOp &op) const
    {
        if (op.model)
            return make_dispatch_bounds(*this, intersect_rect(op.model->viewportRect, op.model->clipRect), 0,
                                        false);
        const auto &batch = *op.batch;
        switch (op.type)
        {
        case Renderer2DRenderOpType::ePanelBlur:
        case Renderer2DRenderOpType::eBlur:
            return make_blur_dispatch_bounds(*this, batch);
        case Renderer2DRenderOpType::eShadow:
            return make_dispatch_bounds(*this, batch, DispatchBoundsMode::eShadow);
        case Renderer2DRenderOpType::eTextUnderlay:
            if (!(batch.flags & (eRenderer2DStyleShadow | eRenderer2DStyleGlow)))
                return {};
            return make_dispatch_bounds(*this, batch, DispatchBoundsMode::eText, false);
        case Renderer2DRenderOpType::eText:
            return make_dispatch_bounds(*this, batch, DispatchBoundsMode::eTextForeground, false);
        case Renderer2DRenderOpType::eShape:
            return make_dispatch_bounds(*this, batch, DispatchBoundsMode::eShape);
        default:
            return make_dispatch_bounds(*this, batch, DispatchBoundsMode::eExact);
        }
    }
    void dispatch(const char *name, const Renderer2DBatch &batch, DispatchBounds bounds, uint32_t pass,
                  std::initializer_list<MTL::Texture *> textures)
    {
        // The media horizontal pass fills scratch pixels needed by the vertical
        // pass, including samples just outside the damaged colour rectangle.
        if (!(std::string_view(name) == "media_2d_comp" &&
              (batch.flags & eRenderer2DStyleMediaBlurHorizontal)))
            bounds = clipped_to_damage(bounds);
        if (bounds.empty())
            return;
        auto constants = make_push_constants(batch, pass, bounds.x, bounds.y);
        if (std::string_view(name) == "text_msdf_comp")
        {
            // Compute writes blend with the destination. Only disjoint glyphs
            // may run together; overlapping edges, shadows and glows stay ordered.
            if (textConstants.size() == 64 ||
                std::any_of(textBounds.begin(), textBounds.end(),
                            [&](const auto &other) { return dispatch_bounds_overlap(other, bounds); }))
                flush_text();
            constants.data.x = pack_dispatch_extent(bounds.width, bounds.height);
            textConstants.push_back(constants);
            textBounds.push_back(bounds);
            textWidth = std::max(textWidth, bounds.width);
            textHeight = std::max(textHeight, bounds.height);
            return;
        }
        flush_text();
        device.dispatch(name, &constants, sizeof(constants), bounds.width, bounds.height, textures);
    }
    void flush_text()
    {
        if (textConstants.empty())
            return;
        device.dispatch("text_msdf_batch_comp", textConstants.data(),
                        textConstants.size() * sizeof(Renderer2DPushConstants), textWidth, textHeight,
                        {texture(font.image() ? font.image() : fallbackFont.get()), colour.get()},
                        uint32_t(textConstants.size()));
        textConstants.clear();
        textBounds.clear();
        textWidth = textHeight = 0;
    }
    MTL::SamplerState *model_sampler(const MetalModelSampler &settings)
    {
        const auto key = std::to_string(settings.minFilter) + "|" + std::to_string(settings.magFilter) + "|" +
                         std::to_string(settings.wrapS) + "|" + std::to_string(settings.wrapT);
        if (auto found = modelSamplers.find(key); found != modelSamplers.end())
            return found->second.get();
        auto descriptor = own(MTL::SamplerDescriptor::alloc()->init());
        const auto filter = [](uint32_t value) {
            return value == 9728 || value == 9984 || value == 9986 ? MTL::SamplerMinMagFilterNearest
                                                                   : MTL::SamplerMinMagFilterLinear;
        };
        const auto wrap = [](uint32_t value) {
            return value == 33071   ? MTL::SamplerAddressModeClampToEdge
                   : value == 33648 ? MTL::SamplerAddressModeMirrorRepeat
                                    : MTL::SamplerAddressModeRepeat;
        };
        descriptor->setMinFilter(filter(settings.minFilter));
        descriptor->setMagFilter(filter(settings.magFilter));
        descriptor->setMipFilter(settings.minFilter == 9984 || settings.minFilter == 9985
                                     ? MTL::SamplerMipFilterNearest
                                 : settings.minFilter >= 9986 ? MTL::SamplerMipFilterLinear
                                                              : MTL::SamplerMipFilterNotMipmapped);
        descriptor->setSAddressMode(wrap(settings.wrapS));
        descriptor->setTAddressMode(wrap(settings.wrapT));
        auto sampler = own(device.gpu->newSamplerState(descriptor.get()));
        if (!sampler)
            throw std::runtime_error("Cannot allocate model sampler");
        return modelSamplers.emplace(key, std::move(sampler)).first->second.get();
    }
    void draw_model(const Renderer3DModelBatch &batch)
    {
        ensure_model_targets();
        flush_text();
        const auto found = models.find(batch.modelId);
        if (found == models.end() || batch.viewportRect.z <= 0 || batch.viewportRect.w <= 0)
            return;
        const auto clipped = intersect_rect(batch.viewportRect, batch.clipRect);
        const auto bounds = make_dispatch_bounds(*this, clipped, 0.0f, false);
        if (bounds.empty())
            return;
        device.flush();
        if (!modelPipeline)
        {
            auto vertex = device.function("model_3d_vert");
            auto fragment = device.function("model_3d_frag");
            auto descriptor = own(MTL::RenderPipelineDescriptor::alloc()->init());
            descriptor->setVertexFunction(vertex.get());
            descriptor->setFragmentFunction(fragment.get());
            descriptor->setRasterSampleCount(samples);
            auto colourAttachment = descriptor->colorAttachments()->object(0);
            colourAttachment->setPixelFormat(MTL::PixelFormatRGBA8Unorm);
            colourAttachment->setBlendingEnabled(true);
            colourAttachment->setSourceRGBBlendFactor(MTL::BlendFactorSourceAlpha);
            colourAttachment->setDestinationRGBBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
            colourAttachment->setSourceAlphaBlendFactor(MTL::BlendFactorOne);
            colourAttachment->setDestinationAlphaBlendFactor(MTL::BlendFactorOneMinusSourceAlpha);
            descriptor->setDepthAttachmentPixelFormat(MTL::PixelFormatDepth32Float);
            auto vd = MTL::VertexDescriptor::vertexDescriptor();
            const std::array<size_t, 8> offsets{offsetof(Vertex, pos),       offsetof(Vertex, color),
                                                offsetof(Vertex, normal),    offsetof(Vertex, uv),
                                                offsetof(Vertex, tangent),   offsetof(Vertex, material),
                                                offsetof(Vertex, material2), offsetof(Vertex, material3)};
            for (size_t i = 0; i < 8; ++i)
            {
                auto attribute = vd->attributes()->object(i);
                attribute->setBufferIndex(1);
                attribute->setOffset(offsets[i]);
                attribute->setFormat(i < 3    ? MTL::VertexFormatFloat3
                                     : i == 3 ? MTL::VertexFormatFloat2
                                              : MTL::VertexFormatFloat4);
            }
            vd->layouts()->object(1)->setStride(sizeof(Vertex));
            descriptor->setVertexDescriptor(vd);
            NS::Error *error = nullptr;
            modelPipeline = own(device.gpu->newRenderPipelineState(descriptor.get(), &error));
            if (!modelPipeline)
                throw std::runtime_error(error->localizedDescription()->utf8String());
            auto dd = own(MTL::DepthStencilDescriptor::alloc()->init());
            dd->setDepthCompareFunction(MTL::CompareFunctionLess);
            dd->setDepthWriteEnabled(true);
            depthState = own(device.gpu->newDepthStencilState(dd.get()));
        }
        const auto &model = found->second;
        if (!modelBuffers.contains(batch.modelId))
            modelBuffers[batch.modelId] =
                own(device.gpu->newBuffer(model.vertices.data(), model.vertices.size() * sizeof(Vertex),
                                          MTL::ResourceStorageModeShared));
        auto pass = MTL::RenderPassDescriptor::renderPassDescriptor();
        auto attachment = pass->colorAttachments()->object(0);
        attachment->setTexture(samples > 1 ? hostedMSAA.get() : hosted.get());
        attachment->setLoadAction(MTL::LoadActionClear);
        attachment->setClearColor(MTL::ClearColor(0, 0, 0, 0));
        attachment->setStoreAction(samples > 1 ? MTL::StoreActionMultisampleResolve : MTL::StoreActionStore);
        if (samples > 1)
            attachment->setResolveTexture(hosted.get());
        pass->depthAttachment()->setTexture(hostedDepth.get());
        pass->depthAttachment()->setLoadAction(MTL::LoadActionClear);
        pass->depthAttachment()->setClearDepth(1);
        pass->depthAttachment()->setStoreAction(MTL::StoreActionDontCare);
        auto cb = device.queue->commandBuffer();
        auto encoder = cb->renderCommandEncoder(pass);
        encoder->setRenderPipelineState(modelPipeline.get());
        encoder->setDepthStencilState(depthState.get());
        encoder->setViewport(MTL::Viewport{batch.viewportRect.x, batch.viewportRect.y, batch.viewportRect.z,
                                           batch.viewportRect.w, 0, 1});
        encoder->setScissorRect(MTL::ScissorRect{bounds.x, bounds.y, bounds.width, bounds.height});
        glm::mat4 matrix = glm::translate(glm::mat4(1), batch.position);
        matrix = glm::rotate(matrix, batch.rotationRadians.x, glm::vec3(1, 0, 0));
        matrix = glm::rotate(matrix, batch.rotationRadians.y, glm::vec3(0, 1, 0));
        matrix = glm::rotate(matrix, batch.rotationRadians.z, glm::vec3(0, 0, 1));
        matrix = glm::scale(matrix, batch.scale);
        const auto normal = std::abs(glm::determinant(glm::mat3(matrix))) > 1e-6f
                                ? glm::transpose(glm::inverse(glm::mat3(matrix)))
                                : glm::mat3(1);
        const auto target = glm::length(batch.cameraTarget - batch.cameraPosition) > 0.0001f
                                ? batch.cameraTarget
                                : batch.cameraPosition + glm::vec3(0, 0, -1);
        auto projection = glm::perspectiveRH_ZO(
            std::clamp(batch.fieldOfViewRadians, 0.01f, 3.0f), batch.viewportRect.z / batch.viewportRect.w,
            std::max(batch.nearPlane, 0.0001f), std::max(batch.farPlane, batch.nearPlane + 0.0001f));
        Model3DPushConstants constants;
        constants.worldToClip =
            projection * glm::lookAt(batch.cameraPosition, target, glm::vec3(0, 1, 0)) * matrix;
        constants.normalToWorld0 = glm::vec4(normal[0], batch.lightDirection.x);
        constants.normalToWorld1 = glm::vec4(normal[1], batch.lightDirection.y);
        constants.normalToWorld2 = glm::vec4(normal[2], batch.lightDirection.z);
        constants.materialColor = batch.materialColor;
        encoder->setVertexBytes(&constants, sizeof(constants), 0);
        encoder->setFragmentBytes(&constants, sizeof(constants), 0);
        encoder->setVertexBuffer(modelBuffers[batch.modelId].get(), 0, 1);
        for (const auto &range : model.ranges)
        {
            for (size_t i = 0; i < 5; ++i)
            {
                encoder->setFragmentTexture(texture(range.textures[i].get()), i);
                encoder->setFragmentSamplerState(model_sampler(range.samplers[i]), i);
            }
            const auto first = std::max(range.firstVertex, batch.firstTriangle * 3);
            const auto end = std::min(range.firstVertex + range.vertexCount,
                                      batch.triangleCount ? (batch.firstTriangle + batch.triangleCount) * 3
                                                          : uint32_t(model.vertices.size()));
            if (end > first)
                encoder->drawPrimitives(MTL::PrimitiveTypeTriangle, first, end - first);
        }
        encoder->endEncoding();
        cb->commit();
        cb->waitUntilCompleted();
        if (cb->status() == MTL::CommandBufferStatusError)
            throw std::runtime_error("Metal hosted 3D submission failed");
        glm::uvec4 constants2(bounds.x, bounds.y, bounds.width, bounds.height);
        device.dispatch("hosted_3d_composite_comp", &constants2, sizeof(constants2), bounds.width,
                        bounds.height, {colour.get(), hosted.get()});
    }
    void render()
    {
        const bool drawableAvailable = valid && extent.width && extent.height;
        // CAMetalLayer presentation does not pace the CPU reliably when the
        // glass view presents inside a Core Animation transaction. A zero
        // target rate therefore uses a 60 Hz energy-safe default on macOS.
        if (!frameBatch.active)
        {
            const uint32_t pollRate = frameRate ? frameRate : 60u;
            std::this_thread::sleep_until(nextFrame);
            nextFrame = Clock::now() +
                std::chrono::nanoseconds(1000000000 / pollRate);
        }
        if (!drawableAvailable)
            return;
        const double now = std::chrono::duration<double>(Clock::now() - start).count();
        if (presentedGeneration == scene.frame_generation() && !scene.requires_continuous_redraw(now))
            return;
        auto pool = own(NS::AutoreleasePool::alloc()->init());
        scene.build_render_plan(plan, now, 0); // Replay both layers in their exact shared stacking order.
        ops.clear();
        auto add = [&](const auto &panels, const auto &shadows, const auto &blurs, const auto &shapes,
                       const auto &media, const auto &texts) {
            add_render_ops(ops, panels, Renderer2DRenderOpType::ePanelBlur);
            add_render_ops(ops, shadows, Renderer2DRenderOpType::eShadow);
            add_render_ops(ops, blurs, Renderer2DRenderOpType::eBlur);
            add_render_ops(ops, shapes, Renderer2DRenderOpType::eShape);
            add_render_ops(ops, media, Renderer2DRenderOpType::eMedia);
            add_render_ops(ops, texts, Renderer2DRenderOpType::eTextUnderlay);
            add_render_ops(ops, texts, Renderer2DRenderOpType::eText);
        };
        add(plan.panelBlurs, plan.shadows, plan.blurs, plan.shapes, plan.media, plan.texts);
        add(plan.cachedPanelBlurs, plan.cachedShadows, plan.cachedBlurs, plan.cachedShapes, plan.cachedMedia,
            plan.cachedTexts);
        add_model_ops(ops, plan.models);
        sort_render_ops(ops);
        paintItems.clear();
        bool hasBlur = false;
        for (const auto &op : ops)
        {
            const auto bounds = operation_bounds(op);
            paintItems.push_back({op.batch ? *op.batch : Renderer2DBatch{},
                                  {bounds.x, bounds.y, bounds.width, bounds.height},
                                  uint32_t(op.type),
                                  op.type == Renderer2DRenderOpType::eBlur ||
                                      op.type == Renderer2DRenderOpType::ePanelBlur});
            hasBlur = hasBlur || paintItems.back().backdropEffect;
        }
        // Models have their own camera/material state and cross-queue rendering.
        // Backdrop blur reads earlier layers beyond an individual primitive.
        const auto atlasId = texture(font.image() ? font.image() : fallbackFont.get())->gpuResourceID()._impl;
        if (!plan.models.empty() || atlasId != fontTextureId)
            frameDamage.invalidate();
        fontTextureId = atlasId;
        const auto damage = frameDamage.update(
            paintItems, extent.width, extent.height, !plan.models.empty());
        damageBounds = {damage.x, damage.y, damage.z, damage.w};
        if (damageBounds.empty())
        {
            presentedGeneration = scene.frame_generation();
            return;
        }
        clear(colour.get(), damageBounds);
        if (hasBlur)
        {
            if (!temporary)
                temporary = device.target(extent.width, extent.height);
            if (!blur)
                blur = device.target(extent.width, extent.height);
            // The vertical pass samples at most 48 pixels outside its blur
            // footprint. Clear that halo as well, so scratch pixels from a
            // previous frame cannot leak into an edge sample.
            constexpr uint32_t halo = 50;
            const uint32_t left = damageBounds.x > halo ? damageBounds.x - halo : 0;
            const uint32_t top = damageBounds.y > halo ? damageBounds.y - halo : 0;
            const uint32_t right = std::min(extent.width, damageBounds.x + damageBounds.width + halo);
            const uint32_t bottom = std::min(extent.height, damageBounds.y + damageBounds.height + halo);
            const DispatchBounds scratch{left, top, right - left, bottom - top};
            clear(temporary.get(), scratch);
            clear(blur.get(), scratch);
        }
        for (size_t opIndex = 0; opIndex < ops.size(); ++opIndex)
        {
            const auto &op = ops[opIndex];
            const auto b = paintItems[opIndex].bounds;
            if (clipped_to_damage({b.x, b.y, b.z, b.w}).empty())
                continue;
            if (op.model)
            {
                draw_model(*op.model);
                continue;
            }
            auto batch = *op.batch;
            switch (op.type)
            {
            case Renderer2DRenderOpType::ePanelBlur:
            case Renderer2DRenderOpType::eBlur: {
                auto bounds = make_blur_dispatch_bounds(*this, batch);
                // The Metal path never populates a separate static layer.
                auto bindings = {colour.get(), colour.get(), backdrop ? texture(backdrop.get()) : colour.get(),
                                 temporary.get(), blur.get()};
                Renderer2DBatch clearBatch;
                clearBatch.rect = {bounds.x, bounds.y, bounds.width, bounds.height};
                clearBatch.primitive = Renderer2DPrimitive::eClear;
                clearBatch.flags = eRenderer2DStyleClear;
                dispatch("blur_2d_comp", clearBatch, bounds, 0, bindings);
                auto passBatch = batch;
                const uint32_t passes = std::max(1u, uint32_t(std::round(std::max(batch.effect0.y, 1.0f))));
                passBatch.effect0.w =
                    1.0f - std::pow(1.0f - std::clamp(batch.effect0.w, 0.0f, 1.0f), 1.0f / passes);
                for (uint32_t i = 0; i < passes; ++i)
                {
                    passBatch.flags = batch.flags | eRenderer2DStyleBlurHorizontal;
                    dispatch("blur_2d_comp", passBatch, bounds, i | (backdrop ? kBlurUseExternalBackdrop : 0),
                             bindings);
                    passBatch.flags = batch.flags | eRenderer2DStyleBlurVertical;
                    dispatch("blur_2d_comp", passBatch, bounds, i | (backdrop ? kBlurUseExternalBackdrop : 0),
                             bindings);
                }
                batch.flags |= eRenderer2DStyleBlurComposite;
                dispatch("blur_2d_comp", batch, bounds, 0, bindings);
                break;
            }
            case Renderer2DRenderOpType::eShape:
                dispatch("shape_2d_comp", batch,
                         make_dispatch_bounds(*this, batch, DispatchBoundsMode::eShape), 0, {colour.get()});
                break;
            case Renderer2DRenderOpType::eShadow:
                dispatch("shadow_2d_comp", batch,
                         make_dispatch_bounds(*this, batch, DispatchBoundsMode::eShadow), 0, {colour.get()});
                break;
            case Renderer2DRenderOpType::eTextUnderlay:
                if (batch.flags & eRenderer2DStyleShadow)
                {
                    auto shadow = batch;
                    shadow.color2 = batch.shadowColor;
                    shadow.effect0.z = 0;
                    shadow.flags &= ~eRenderer2DStyleGlow;
                    dispatch("text_msdf_comp", shadow,
                             make_dispatch_bounds(*this, shadow, DispatchBoundsMode::eTextUnderlay, false), 1,
                             {texture(font.image() ? font.image() : fallbackFont.get()), colour.get()});
                }
                if (batch.flags & eRenderer2DStyleGlow)
                {
                    batch.effect1.x = batch.effect1.y = batch.effect1.z = 0;
                    batch.flags &= ~eRenderer2DStyleShadow;
                    dispatch("text_msdf_comp", batch,
                             make_dispatch_bounds(*this, batch, DispatchBoundsMode::eTextUnderlay, false), 1,
                             {texture(font.image() ? font.image() : fallbackFont.get()), colour.get()});
                }
                break;
            case Renderer2DRenderOpType::eText:
                dispatch("text_msdf_comp", batch,
                         make_dispatch_bounds(*this, batch, DispatchBoundsMode::eTextForeground, false), 2,
                         {texture(font.image() ? font.image() : fallbackFont.get()), colour.get()});
                break;
            case Renderer2DRenderOpType::eMedia: {
                auto it = media.find(batch.mediaId);
                if (it == media.end() || !it->second.drawable || it->second.frameImages.empty())
                    break;
                const auto &asset = it->second;
                auto source = texture(
                    asset.frameImages[std::min<size_t>(batch.frameIndex, asset.frameImages.size() - 1)]);
                const auto bounds = make_dispatch_bounds(*this, batch, DispatchBoundsMode::eExact);
                const uint32_t radius = uint32_t(std::clamp(std::lround(batch.effect0.z), 0l, 32l));
                if (!temporary && ((batch.flags & (eRenderer2DStyleMediaBlurHorizontal |
                                                     eRenderer2DStyleMediaBlurVertical)) ||
                                   (radius && (batch.flags & eRenderer2DStyleMediaSingleBlurSample))))
                    temporary = device.target(extent.width, extent.height);
                if (radius && (batch.flags & eRenderer2DStyleMediaSingleBlurSample) &&
                    !(batch.flags & eRenderer2DStyleTransform2_5D))
                {
                    auto horizontal = batch;
                    horizontal.flags |= eRenderer2DStyleMediaBlurHorizontal;
                    const uint32_t y = bounds.y > radius ? bounds.y - radius : 0;
                    dispatch("media_2d_comp", horizontal,
                             {bounds.x, y, bounds.width,
                              std::min(bounds.y + bounds.height + radius, extent.height) - y},
                             0, {source, temporary.get(), colour.get()});
                    batch.flags |= eRenderer2DStyleMediaBlurVertical;
                }
                dispatch("media_2d_comp", batch, bounds, 0,
                         {source, temporary ? temporary.get() : colour.get(), colour.get()});
                break;
            }
            default:
                break;
            }
        }
        flush_text();
        // Acquire the drawable after encoding the offscreen work. The first
        // dispatch may wait for the previous submission to finish, and holding
        // a CAMetalLayer drawable through that wait needlessly delays its
        // return to Core Animation on high-refresh displays.
        auto drawable = layer->nextDrawable();
        if (!drawable)
        {
            device.submit();
            frameDamage.invalidate();
            return;
        }
        if (device.metal4)
            device.queue4->wait(drawable);
        // Core Animation consumes premultiplied colour.
        glm::uvec4 composite(2u | 8u | (backdrop ? 1u : 0u), 0, 0, 0);
        device.dispatch("composite_2d_comp", &composite, sizeof(composite), extent.width, extent.height,
                        {colour.get(), colour.get(), backdrop ? texture(backdrop.get()) : colour.get(),
                         temporary ? temporary.get() : colour.get(), drawable->texture()});
        device.submit(drawable, surface_presents_with_transaction(layer.get()));
        ++frameCount;
        presentedGeneration = scene.frame_generation();
        lastPresentation = Clock::now();
    }
};

Engine::Engine(const EngineCreateInfo &info) : impl(std::make_unique<Impl>(info))
{
    if (!info.defaultRenderer2DFontPath.empty())
        load_renderer2d_font(info.defaultRenderer2DFontPath, info.defaultRenderer2DFontOptions);
    else
    {
        const auto path = renderer2d_primary_font_for_locale(std::filesystem::path{}, shared_locale());
        if (!path.empty())
            load_renderer2d_font(path);
    }
    std::lock_guard lock(liveMutex);
    liveEngines.push_back(this);
}
Engine::~Engine()
{
    std::lock_guard lock(liveMutex);
    std::erase(liveEngines, this);
}
void Engine::begin_externally_paced_frame(uint32_t frameRate)
{
    frameBatch.rate = std::max(frameRate, 1u);
    frameBatch.active = true;
}
void Engine::end_externally_paced_frame()
{
    frameBatch.active = false;
}
double Engine::idle_event_wait_seconds()
{
    std::lock_guard lock(liveMutex);
    const auto now = Clock::now();
    for (const auto engine : liveEngines)
    {
        const auto &state = *engine->impl;
        if (!state.valid || !state.extent.width || !state.extent.height)
            continue;
        // CPU-driven springs also need the active cadence between rendered
        // frames, even when no time-driven scene component is present.
        if (now - state.lastPresentation < std::chrono::milliseconds(250) ||
            state.presentedGeneration != state.scene.frame_generation() ||
            state.scene.requires_continuous_redraw(std::chrono::duration<double>(now - state.start).count()))
            return 0.0;
    }
    // Pointer pass-through overlays may not receive mouse events, so retain a
    // bounded fallback poll. Normal keyboard/mouse events wake the wait early.
    return 0.05;
}
void Engine::draw()
{
    if (!impl->fontPath.empty() && impl->checkedTextGeneration != impl->scene.frame_generation())
    {
        std::string preload;
        auto &registry = impl->scene.registry();
        registry.view<TextComponent>().each([&](entt::entity entity, const TextComponent &text) {
            const auto found = impl->checkedText.find(entity);
            if (found != impl->checkedText.end() && found->second == text.text)
                return;
            impl->checkedText.insert_or_assign(entity, text.text);
            bool added = false;
            for (auto code : impl->font.missing_codepoints(text.text))
                added = impl->attemptedGlyphs.insert(code).second || added;
            if (added)
                preload += '\n' + text.text;
        });
        std::erase_if(impl->checkedText, [&](const auto &entry) {
            return !registry.valid(entry.first) || !registry.all_of<TextComponent>(entry.first);
        });
        if (!preload.empty())
        {
            auto options = impl->fontOptions;
            options.preloadText += preload;
            options.requireRequestedGlyphs = false;
            auto attempted = std::move(impl->attemptedGlyphs);
            load_renderer2d_font(impl->fontPath, options);
            impl->attemptedGlyphs = std::move(attempted);
        }
        impl->checkedTextGeneration = impl->scene.frame_generation();
    }
    try
    {
        impl->render();
    }
    catch (const std::exception &e)
    {
        impl->valid = false;
        Logger::fetch_logger()->error(e.what());
    }
}
int Engine::update_timing(double time)
{
    if (time - impl->previousSample >= 1)
    {
        impl->fps = int(impl->frameCount / (time - impl->previousSample));
        impl->frameCount = 0;
        impl->previousSample = time;
    }
    return impl->fps;
}
bool Engine::set_present_mode(RendererPresentMode mode)
{
    if (mode != RendererPresentMode::eAuto && mode != RendererPresentMode::eFifo &&
        mode != RendererPresentMode::eImmediate)
        return false;
    impl->preference = mode;
    impl->layer->setDisplaySyncEnabled(mode != RendererPresentMode::eImmediate);
    return true;
}
RendererPresentMode Engine::present_mode_preference() const
{
    return impl->preference;
}
RendererPresentMode Engine::active_present_mode() const
{
    return impl->preference == RendererPresentMode::eImmediate ? RendererPresentMode::eImmediate
                                                               : RendererPresentMode::eFifo;
}
std::vector<RendererPresentMode> Engine::available_present_modes() const
{
    return {RendererPresentMode::eImmediate, RendererPresentMode::eFifo};
}
void Engine::set_target_frame_rate(uint32_t value)
{
    impl->frameRate = value;
    impl->nextFrame = Clock::now();
}
uint32_t Engine::target_frame_rate() const
{
    return impl->frameRate;
}
uint32_t Engine::recommended_ui_update_rate() const
{
    if (frameBatch.active)
        return frameBatch.rate;
    return impl->frameRate ? impl->frameRate : 60;
}
void Engine::resize(uint32_t width, uint32_t height)
{
    impl->resize(width, height);
}
Model3DHandle Engine::load_model_3d(const std::filesystem::path &path)
{
    const auto key =
        std::filesystem::absolute(path).lexically_normal().string() + "|" + resource_file_cache_version(path);
    if (auto it = impl->modelCache.find(key); it != impl->modelCache.end())
        return {it->second, 0, uint32_t(impl->models[it->second].vertices.size() / 3)};
    auto model = load_metal_model(path, impl->upload);
    if (model.vertices.empty())
        return {};
    const uint32_t id = impl->nextModel++, count = model.vertices.size() / 3;
    impl->models.emplace(id, std::move(model));
    impl->modelCache[key] = id;
    return {id, 0, count};
}
Media2DHandle Engine::load_media_2d(const std::filesystem::path &path, const Media2DLoadOptions &options)
{
    std::ostringstream key;
    key << std::filesystem::absolute(path).lexically_normal().string() << '|'
        << resource_file_cache_version(path) << '|' << options.rasterWidth << '|' << options.rasterHeight
        << '|' << options.srgb << '|' << options.generateMipmaps << '|' << options.premultiplyAlpha << '|'
        << options.maxAnimationFrames << '|' << options.svgAnimationFrames << '|'
        << options.svgAnimationFrameRate << '|' << options.lottieAnimationFrames << '|'
        << options.maxVideoFrames << '|' << options.maxVideoPixels;
    if (auto found = impl->mediaCache.find(key.str()); found != impl->mediaCache.end())
        return impl->media.at(found->second).handle();
    Media2DAsset asset;
    const uint32_t id = impl->nextMedia++;
    auto handle = load_media_2d_asset(path, options, id, impl->upload, asset);
    if (handle.valid())
    {
        impl->media.emplace(id, std::move(asset));
        impl->mediaCache[key.str()] = id;
        impl->scene.mark_dirty();
    }
    return handle;
}
AudioClipHandle Engine::load_audio_clip(const std::filesystem::path &path)
{
    return impl->audio.load_clip(path);
}
AudioEngine &Engine::audio()
{
    return impl->audio;
}
const AudioEngine &Engine::audio() const
{
    return impl->audio;
}
bool Engine::load_renderer2d_font(const std::filesystem::path &path)
{
    return load_renderer2d_font(path, {});
}
bool Engine::load_renderer2d_font(const std::filesystem::path &path,
                                  const Renderer2DFontAtlasLoadOptions &options)
{
    if (!impl->font.load_from_file(path, options, impl->upload))
        return false;
    impl->fontPath = path;
    impl->frameDamage.invalidate();
    impl->fontOptions = options;
    impl->attemptedGlyphs.clear();
    impl->checkedText.clear();
    impl->checkedTextGeneration = UINT64_MAX;
    refresh_localised_texts();
    impl->scene.registry().view<TextComponent>().each([&](entt::entity entity, TextComponent &) {
        apply_font_layout(impl->font, impl->scene.registry(), entity);
    });
    impl->scene.mark_dirty();
    return true;
}
bool Engine::load_localisation_directory(const std::filesystem::path &path)
{
    const bool result = impl->localisation.load_directory(path);
    if (result)
        set_locale(shared_locale());
    return result;
}
bool Engine::load_localisation_directories(const std::vector<std::filesystem::path> &paths)
{
    impl->localisation.clear();
    bool loaded = false;
    for (const auto &path : paths)
        loaded = impl->localisation.load_directory(path, loaded) || loaded;
    if (loaded)
        set_locale(shared_locale());
    return loaded;
}
bool Engine::set_locale(const std::string &locale)
{
    const bool result = impl->localisation.set_locale(locale);
    refresh_localised_texts();
    return result;
}
bool Engine::set_shared_locale(const std::string &locale)
{
    std::lock_guard lock(liveMutex);
    sharedLocale = locale;
    bool changed = false;
    for (auto engine : liveEngines)
        changed = engine->set_locale(locale) || changed;
    return changed;
}
std::string Engine::shared_locale()
{
    std::lock_guard lock(liveMutex);
    return sharedLocale;
}
std::string Engine::locale() const
{
    return impl->localisation.locale();
}
std::string Engine::resolve_text(const Text &text) const
{
    return impl->localisation.resolve(text);
}
RenderBackend Engine::render_backend() const
{
    return RenderBackend::eMetal;
}
PresentationBackend Engine::presentation_backend() const
{
    return PresentationBackend::eNative;
}
bool Engine::system_backdrop_available() const
{
    return false;
}
bool Engine::ready() const
{
    return impl->valid;
}
std::string Engine::vulkan_api_version() const
{
    return {};
}
std::string Engine::graphics_api_version() const
{
    return impl->device.metal4 ? "4" : "3";
}
void Engine::refresh_localised_texts()
{
    impl->scene.registry().view<TextComponent, LocalisedTextComponent>().each(
        [&](entt::entity entity, TextComponent &text, const LocalisedTextComponent &localised) {
            text.text = impl->localisation.resolve(localised.value);
            apply_font_layout(impl->font, impl->scene.registry(), entity);
            impl->scene.mark_dirty(entity);
        });
}
Localisation &Engine::localisation()
{
    return impl->localisation;
}
const Localisation &Engine::localisation() const
{
    return impl->localisation;
}
bool Engine::set_external_backdrop_rgba(uint32_t width, uint32_t height, const unsigned char *rgba,
                                        size_t count)
{
    if (!rgba)
        return false;
    auto image = impl->upload(width, height, std::span(rgba, count), false, false);
    if (!image)
        return false;
    impl->backdrop = std::move(image);
    impl->frameDamage.invalidate();
    impl->scene.mark_dirty();
    return true;
}
void Engine::clear_external_backdrop()
{
    impl->backdrop.reset();
    impl->frameDamage.invalidate();
    impl->scene.mark_dirty();
}
uint32_t Engine::render_width() const
{
    return impl->extent.width;
}
uint32_t Engine::render_height() const
{
    return impl->extent.height;
}
Renderer2DScene &Engine::renderer2d_scene()
{
    return impl->scene;
}
const Renderer2DScene &Engine::renderer2d_scene() const
{
    return impl->scene;
}
Renderer2DFontAtlas &Engine::renderer2d_font_atlas()
{
    return impl->font;
}
const Renderer2DFontAtlas &Engine::renderer2d_font_atlas() const
{
    return impl->font;
}

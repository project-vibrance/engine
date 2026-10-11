#include "device.h"
#include "mesh.h"
#include "frame_damage.h"
#include "foreground_blur.h"
#include "native_glass.h"
#include "../common/scene_helpers.h"
#include "../common/render_op_order.h"
#include <vibranceUI/renderer/renderer.h>
#include <vibranceUI/core/logger.h>
#include <vibranceUI/ui/text.h>
#include <vibranceUI/ui/liquid_controls.h>
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

std::string font_source_key(const std::filesystem::path& path,
    const Renderer2DFontAtlasLoadOptions& options)
{
    std::ostringstream key;
    key << options.includeBasicLatin << '|' << options.fontWeight;
    const auto addPath = [&](const std::filesystem::path& candidate) {
        const auto absolute = std::filesystem::absolute(candidate).lexically_normal();
        const auto name = absolute.string();
        key << '|' << name.size() << ':' << name << '@' << resource_file_cache_version(absolute);
    };
    addPath(path);
    key << "|emoji:" << options.emojiFontPaths.size();
    if (options.emojiFontPaths.empty()) addPath(path.parent_path() / "AppleColorEmoji.ttf");
    else for (const auto& candidate : options.emojiFontPaths) addPath(candidate);
    key << "|fallbacks:" << options.fallbackFontPaths.size();
    for (const auto& candidate : options.fallbackFontPaths) addPath(candidate);
    key << "|ranges:" << options.ranges.size();
    for (const auto& range : options.ranges) key << '|' << range.first << ':' << range.last;
    return key.str();
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
    Renderer2DRenderPlan staticPlan;
    uint64_t preparedCacheGeneration = UINT64_MAX;
    std::vector<uint32_t> preparedDynamicEntities;
    std::vector<Renderer2DRenderOp> ops;
    FrameDamage frameDamage;
    std::vector<FrameDamage::Item> paintItems;
    DispatchBounds damageBounds;
    uint64_t fontTextureId = 0;
    std::vector<Renderer2DPushConstants> textConstants;
    std::vector<DispatchBounds> textBounds;
    vibrance::renderer::TextBatchPlan textBatchPlan;
    Renderer2DFontAtlas font;
    AudioEngine audio;
    Localisation localisation;
    MetalTextureUpload upload;
    std::filesystem::path fontPath;
    std::string fontSourceKey;
    bool strictFontRequirements = false;
    Renderer2DFontAtlasLoadOptions fontOptions;
    std::unordered_set<uint32_t> attemptedGlyphs;
    std::unordered_map<entt::entity, std::string> checkedText;
    uint64_t checkedTextGeneration = UINT64_MAX;
    std::unordered_map<uint32_t, Media2DAsset> media;
    std::unordered_map<uint32_t, MetalModel> models;
    std::unordered_map<uint32_t, Owned<MTL::Buffer>> modelBuffers;
    std::unordered_map<std::string, uint32_t> modelCache, mediaCache;
    uint32_t nextMedia = 1, nextModel = 1;
    Owned<MTL::Texture> colour, temporary, blur, foregroundBlurSource, depth, hosted, hostedDepth, hostedMSAA;
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
    std::unique_ptr<NativeGlassSurface> nativeGlass;
    struct NativeContent
    {
        Owned<MTL::Texture> colour, temporary, blur;
        FrameDamage damage;
        uint64_t atlas = 0;
    };
    std::unordered_map<uint64_t, NativeContent> nativeContents;
    uint64_t presentedGeneration = UINT64_MAX;
    bool presentationPending = false;
    unsigned consecutiveSubmissionFailures = 0;
    uint64_t successfulCompletionsAtFailure = 0;
    struct CpuTiming
    {
        double total = 0, maximum = 0;
        uint64_t samples = 0;
        void add(Clock::duration duration)
        {
            const double milliseconds = std::chrono::duration<double, std::milli>(duration).count();
            total += milliseconds;
            maximum = std::max(maximum, milliseconds);
            ++samples;
        }
    };
    std::array<CpuTiming, 3> cpuTimings;
    uint64_t staticPlanBuilds = 0, mainDamagePixels = 0;
    Clock::time_point diagnosticsStart = Clock::now();

    void log_diagnostics(Clock::time_point now)
    {
        if (!device.diagnosticsEnabled || now - diagnosticsStart < std::chrono::seconds(1)) return;
        std::ostringstream message;
        message.setf(std::ios::fixed);
        message.precision(3);
        message << "Metal renderer diagnostics: " << device.diagnostics.submissions << " submissions, "
                << device.diagnostics.dispatches << " dispatches, "
                << device.diagnostics.textGlyphs << " glyphs in " << device.diagnostics.textDispatches
                << " text dispatches, " << device.diagnostics.dispatchPixels << " dispatched pixels, "
                << mainDamagePixels << " damaged base pixels, " << staticPlanBuilds
                << " static plan builds. CPU wall timings (mean/max ms, samples; frame pacing excluded): ";
        constexpr std::array names {"preparation", "recording", "presentation"};
        for (size_t index = 0; index < cpuTimings.size(); ++index)
        {
            const auto& timing = cpuTimings[index];
            if (index) message << "; ";
            message << names[index] << ' ' << (timing.samples ? timing.total / timing.samples : 0)
                    << '/' << timing.maximum << ", " << timing.samples;
        }
        const auto& gpu = device.diagnostics;
        if (gpu.gpuSamples)
            message << ". Sampled GPU mean/max ms: " << gpu.gpuMilliseconds / gpu.gpuSamples
                    << '/' << gpu.gpuMaximumMilliseconds << ", " << gpu.gpuSamples << " samples";
        else
            message << ". GPU timings " << (!device.gpuDiagnosticsEnabled ? "disabled" :
                "awaiting completed samples");
        Logger::fetch_logger()->info(message.str());
        cpuTimings = {};
        staticPlanBuilds = mainDamagePixels = 0;
        device.diagnostics = {};
        diagnosticsStart = now;
    }

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
        nativeGlass = std::make_unique<NativeGlassSurface>(window, device.gpu.get(), layer.get());
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
        try
        {
            device.flush();
        }
        catch (const std::exception& error)
        {
            // Teardown must still detach AppKit surfaces after a failed GPU
            // frame. Device destruction drains the remaining submitted work.
            Logger::fetch_logger()->error(error.what());
        }
        nativeGlass.reset();
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
            foregroundBlurSource.reset();
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
            return make_blur_replay_dispatch_bounds(*this, batch);
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
        if (std::string_view(name) == "blur_2d_comp")
            constants.data.x |= batch.backdropCapColour << 8u;
        if (std::string_view(name) == "text_msdf_comp")
        {
            // Bound dependency planning for long runs. The shared planner keeps
            // overlapping glyphs ordered and combines independent glyphs.
            if (textConstants.size() == vibrance::renderer::kTextBatchPlanningGlyphLimit)
                flush_text();
            constants.data.x = pack_dispatch_extent(bounds.width, bounds.height);
            textConstants.push_back(constants);
            textBounds.push_back(bounds);
            return;
        }
        flush_text();
        device.dispatch(name, &constants, sizeof(constants), bounds.width, bounds.height, textures);
    }
    void flush_text()
    {
        if (textConstants.empty())
            return;
        vibrance::renderer::build_text_batch_plan(
            std::span<const DispatchBounds>(textBounds), textBatchPlan);
        std::array<Renderer2DPushConstants, vibrance::renderer::kTextBatchGlyphLimit> constants;
        for (const auto& group : textBatchPlan.groups)
        {
            for (uint32_t index = 0; index < group.count; ++index)
                constants[index] = textConstants[textBatchPlan.glyphIndices[group.first + index]];
            device.dispatch("text_msdf_batch_comp", constants.data(),
                            group.count * sizeof(Renderer2DPushConstants), group.width, group.height,
                            {texture(font.image() ? font.image() : fallbackFont.get()), colour.get()},
                            group.count);
        }
        textConstants.clear();
        textBounds.clear();
    }
    void order_simple_ops(std::vector<Renderer2DRenderOp>& renderOps)
    {
        const auto simple = [](const Renderer2DRenderOp& op) {
            return op.batch && (op.type == Renderer2DRenderOpType::eShape ||
                op.type == Renderer2DRenderOpType::eShadow ||
                op.type == Renderer2DRenderOpType::eText ||
                (op.type == Renderer2DRenderOpType::eMedia && op.batch->effect0.z < 0.5f));
        };
        std::vector<Renderer2DRenderOp> ordered;
        std::vector<DispatchBounds> bounds;
        std::vector<uint8_t> foregroundText;
        vibrance::renderer::SimpleRenderOrder order;
        ordered.reserve(renderOps.size());
        for (size_t first = 0; first < renderOps.size();)
        {
            size_t end = first;
            uint32_t textCount = 0;
            while (end < renderOps.size() &&
                   end - first < vibrance::renderer::kTextBatchPlanningGlyphLimit && simple(renderOps[end]))
            {
                textCount += renderOps[end].type == Renderer2DRenderOpType::eText;
                ++end;
            }
            if (end == first)
            {
                ordered.push_back(renderOps[first++]);
                continue;
            }
            if (textCount < 2 || textCount == end - first)
                ordered.insert(ordered.end(), renderOps.begin() + first, renderOps.begin() + end);
            else
            {
                bounds.clear();
                foregroundText.clear();
                for (size_t index = first; index < end; ++index)
                {
                    bounds.push_back(operation_bounds(renderOps[index]));
                    foregroundText.push_back(renderOps[index].type == Renderer2DRenderOpType::eText);
                }
                vibrance::renderer::build_simple_render_order(
                    std::span<const DispatchBounds>(bounds), std::span<const uint8_t>(foregroundText), order);
                for (const auto index : order.indices)
                    ordered.push_back(renderOps[first + index]);
            }
            first = end;
        }
        renderOps.swap(ordered);
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
        if (!presentationPending && presentedGeneration == scene.frame_generation() &&
            !scene.requires_continuous_redraw(now))
            return;
        // A failed drawable acquisition must be retried even when an animation
        // settles without advancing the scene generation again.
        presentationPending = true;
        const auto preparationStart = device.diagnosticsEnabled ? Clock::now() : Clock::time_point{};
        auto pool = own(NS::AutoreleasePool::alloc()->init());
        scene.build_visible_render_plan(plan, now, preparedCacheGeneration);
        if (!plan.rebuildCachedLayer && plan.dynamicEntities != preparedDynamicEntities)
            scene.build_visible_render_plan(plan, now, 0);
        preparedDynamicEntities.assign(plan.dynamicEntities.begin(), plan.dynamicEntities.end());
        if (plan.rebuildCachedLayer)
        {
            if (device.diagnosticsEnabled) ++staticPlanBuilds;
            // Retain the prepared static batches as well as their pixels.
            // Pointer springs should not rebuild every unchanged text glyph
            // and hierarchy effect in the panel on each animation frame.
            staticPlan.cachedPanelBlurs.swap(plan.cachedPanelBlurs);
            staticPlan.cachedShadows.swap(plan.cachedShadows);
            staticPlan.cachedBlurs.swap(plan.cachedBlurs);
            staticPlan.cachedShapes.swap(plan.cachedShapes);
            staticPlan.cachedMedia.swap(plan.cachedMedia);
            staticPlan.cachedTexts.swap(plan.cachedTexts);
            preparedCacheGeneration = scene.cache_generation();
        }
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
        add(staticPlan.cachedPanelBlurs, staticPlan.cachedShadows, staticPlan.cachedBlurs,
            staticPlan.cachedShapes, staticPlan.cachedMedia, staticPlan.cachedTexts);
        add_model_ops(ops, plan.models);
        // Empty underlays must not split otherwise independent shape/text runs.
        std::erase_if(ops, [](const Renderer2DRenderOp& op) {
            return op.type == Renderer2DRenderOpType::eTextUnderlay &&
                !(op.batch->flags & (eRenderer2DStyleShadow | eRenderer2DStyleGlow));
        });
        sort_render_ops(ops);
        const auto native_control = [&](const Renderer2DRenderOp& op) {
            if (op.type != Renderer2DRenderOpType::eShape || !op.batch ||
                !scene.registry().all_of<UiLiquidControlPaint>(op.batch->entity)) return false;
            const auto* glass = scene.registry().try_get<LiquidGlassComponent>(op.batch->entity);
            return glass && glass->backend == LiquidGlassBackend::eMacOSNative &&
                glass->material == GlassMaterial::eLiquid && !operation_bounds(op).empty();
        };
        std::vector<NativeGlassRegion> nativeRegions;
        std::unordered_map<uint32_t, size_t> nativeOwners;
        std::vector<size_t> nativePositions;
        std::vector<bool> nativeTopmost;
        const auto reserve = [](float needed, uint32_t retained) {
            // Preserve the drawable pool through spring-driven size changes.
            return float(needed <= retained ? retained :
                uint32_t(std::ceil(needed * 1.32f / 16.0f)) * 16u);
        };
        for (size_t position = 0; position < ops.size(); ++position)
        {
            const auto& op = ops[position];
            if (native_control(op))
            {
                const auto& batch = *op.batch;
                const auto* backdrop = scene.registry().try_get<SystemBackdropComponent>(batch.entity);
                const float radius = batch.primitive == Renderer2DPrimitive::eEllipse ?
                    std::min(batch.rect.z, batch.rect.w) * 0.5f : batch.effect0.x;
                const float x = std::floor(batch.rect.x), y = std::floor(batch.rect.y);
                glm::vec4 crop {x, y, std::ceil(batch.rect.x + batch.rect.z) - x,
                    std::ceil(batch.rect.y + batch.rect.w) - y};
                const auto cached = nativeContents.find(entity_key(batch.entity));
                crop.z = reserve(crop.z, cached != nativeContents.end() && cached->second.colour ?
                    uint32_t(cached->second.colour->width()) : 0u);
                crop.w = reserve(crop.w, cached != nativeContents.end() && cached->second.colour ?
                    uint32_t(cached->second.colour->height()) : 0u);
                nativeOwners.emplace(entity_key(batch.entity), nativeRegions.size());
                nativePositions.push_back(position);
                nativeTopmost.push_back(batch.alwaysOnTop);
                nativeRegions.push_back({entity_key(batch.entity), batch.rect, batch.clipRect, crop, false,
                    radius, batch.effect0.w, backdrop ? backdrop->region.liquidTint : SystemBackdropColor{}});
            }
        }
        std::vector<std::vector<Renderer2DBatch>> nativeBatches(nativeRegions.size());
        std::vector<std::vector<Renderer2DRenderOpType>> nativeTypes(nativeRegions.size());
        std::vector<std::vector<Renderer2DBatch>> overlayBatches(nativeRegions.size());
        std::vector<std::vector<Renderer2DRenderOpType>> overlayTypes(nativeRegions.size());
        std::vector<DispatchBounds> overlayBounds(nativeRegions.size());
        const auto localise_batch = [](Renderer2DBatch batch, glm::vec4 crop) {
            const glm::vec2 offset(crop);
            batch.rect.x -= offset.x; batch.rect.y -= offset.y;
            batch.clipRect.x -= offset.x; batch.clipRect.y -= offset.y;
            if (batch.textForegroundRect.z > 0 && batch.textForegroundRect.w > 0)
            {
                batch.textForegroundRect.x -= offset.x;
                batch.textForegroundRect.y -= offset.y;
            }
            if (batch.flags & (eRenderer2DStyleShapeMask | eRenderer2DStyleBlurInheritedShapeMask))
            {
                batch.effect1.x -= offset.x; batch.effect1.y -= offset.y;
            }
            return batch;
        };
        size_t position = 0;
        // Native descendants stay with their lens. Independent higher paint
        // needs a transparent foreground above it, otherwise a popup in the
        // base framebuffer is hidden behind AppKit's glass regardless of z.
        std::erase_if(ops, [&](const Renderer2DRenderOp& op) {
            const auto opPosition = position++;
            if (native_control(op)) return true;
            if (!op.batch) return false;
            auto ancestor = op.batch->entity;
            for (unsigned depth = 0; depth < 64 && scene.registry().valid(ancestor); ++depth)
            {
                if (const auto found = nativeOwners.find(entity_key(ancestor)); found != nativeOwners.end())
                {
                    const auto index = found->second;
                    const auto& region = nativeRegions[index];
                    // A popup can be parented to its source control while
                    // deliberately painting above that control's optical clip.
                    if ((op.batch->alwaysOnTop && !nativeTopmost[index]) ||
                        rect_empty(intersect_rect(op.batch->rect, region.rect))) break;
                    auto batch = *op.batch;
                    batch.clipRect = intersect_rect(batch.clipRect, region.rect);
                    nativeBatches[index].push_back(localise_batch(batch, region.contentRect));
                    nativeTypes[index].push_back(op.type);
                    nativeRegions[index].hasContent = true;
                    return true;
                }
                const auto* parent = scene.registry().try_get<Parent2DComponent>(ancestor);
                if (!parent || parent->parent == ancestor) break;
                ancestor = parent->parent;
            }
            const auto bounds = operation_bounds(op);
            size_t lastEarlier = nativeRegions.size();
            bool foreground = false;
            for (size_t index = 0; index < nativeRegions.size() && nativePositions[index] < opPosition; ++index)
            {
                lastEarlier = index;
                const auto visibleLens = make_dispatch_bounds(*this,
                    intersect_rect(nativeRegions[index].rect, nativeRegions[index].clip), 0, false);
                foreground = foreground || dispatch_bounds_overlap(bounds, visibleLens) ||
                    dispatch_bounds_overlap(bounds, overlayBounds[index]);
            }
            if (foreground && lastEarlier < nativeRegions.size() && !bounds.empty())
            {
                overlayBatches[lastEarlier].push_back(*op.batch);
                overlayTypes[lastEarlier].push_back(op.type);
                overlayBounds[lastEarlier] = union_dispatch_bounds(overlayBounds[lastEarlier], bounds);
                return true;
            }
            return false;
        });
        std::vector<NativeGlassRegion> orderedRegions;
        std::vector<std::vector<Renderer2DBatch>> orderedBatches;
        std::vector<std::vector<Renderer2DRenderOpType>> orderedTypes;
        orderedRegions.reserve(nativeRegions.size() * 2);
        orderedBatches.reserve(nativeRegions.size() * 2);
        orderedTypes.reserve(nativeRegions.size() * 2);
        std::unordered_set<uint64_t> retainedNative;
        for (size_t index = 0; index < nativeRegions.size(); ++index)
        {
            retainedNative.insert(nativeRegions[index].identity);
            orderedRegions.push_back(nativeRegions[index]);
            orderedBatches.push_back(std::move(nativeBatches[index]));
            orderedTypes.push_back(std::move(nativeTypes[index]));
            if (overlayBounds[index].empty()) continue;
            const auto bounds = overlayBounds[index];
            const uint64_t identity = (uint64_t(1) << 32) | nativeRegions[index].identity;
            const auto cached = nativeContents.find(identity);
            glm::vec4 crop {bounds.x, bounds.y, bounds.width, bounds.height};
            crop.z = reserve(crop.z, cached != nativeContents.end() && cached->second.colour ?
                uint32_t(cached->second.colour->width()) : 0u);
            crop.w = reserve(crop.w, cached != nativeContents.end() && cached->second.colour ?
                uint32_t(cached->second.colour->height()) : 0u);
            for (auto& batch : overlayBatches[index]) batch = localise_batch(batch, crop);
            retainedNative.insert(identity);
            orderedRegions.push_back({identity, crop, {0, 0, extent.width, extent.height}, crop,
                true, 0, 1, {}, false});
            orderedBatches.push_back(std::move(overlayBatches[index]));
            orderedTypes.push_back(std::move(overlayTypes[index]));
        }
        nativeRegions.swap(orderedRegions);
        nativeBatches.swap(orderedBatches);
        nativeTypes.swap(orderedTypes);
        std::erase_if(nativeContents, [&](const auto& item) {
            return !retainedNative.contains(item.first);
        });
        nativeGlass->begin_frame(nativeRegions, extent.width, extent.height);
        struct NativeFrameEnd {
            NativeGlassSurface* surface;
            ~NativeFrameEnd() { surface->end_frame(); }
        } nativeFrameEnd {nativeGlass.get()};
        std::vector<Owned<CA::MetalDrawable>> retainedDrawables;
        std::vector<CA::MetalDrawable*> drawables;
        bool presentationComplete = true;
        const auto encode_presentation = [&](CA::MetalLayer* output, bool externalBackdrop) {
            const auto acquisitionStart = device.diagnosticsEnabled ? Clock::now() : Clock::time_point{};
            auto drawable = output->nextDrawable();
            if (device.diagnosticsEnabled && Clock::now() - acquisitionStart > std::chrono::milliseconds(30))
                Logger::fetch_logger()->warning("Slow Metal drawable acquisition: " + nativeGlass->describe(output));
            if (!drawable) { presentationComplete = false; return; }
            retainedDrawables.push_back(NS::RetainPtr(drawable));
            drawables.push_back(drawable);
            if (device.metal4) device.queue4->wait(drawable);
            glm::uvec4 composite(2u | 8u | (externalBackdrop && backdrop ? 1u : 0u), 0, 0, 0);
            device.dispatch("composite_2d_comp", &composite, sizeof(composite), extent.width, extent.height,
                {colour.get(), colour.get(), backdrop ? texture(backdrop.get()) : colour.get(),
                 temporary ? temporary.get() : colour.get(), drawable->texture()});
        };
        entt::entity foregroundBlurRoot = entt::null;
        float foregroundBlurRadius = 0.0f;
        const auto foregroundBlurGroups =
            scene.registry().view<const ForegroundBlurGroup2DComponent>();
        for (const entt::entity entity : foregroundBlurGroups)
        {
            const float radius =
                foregroundBlurGroups.get<const ForegroundBlurGroup2DComponent>(entity).radius;
            if (radius > 0.01f)
            {
                foregroundBlurRoot = entity;
                foregroundBlurRadius = radius;
                break;
            }
        }
        DispatchBounds foregroundBlurContentBounds {};
        for (const Renderer2DRenderOp& op : ops)
        {
            if (foregroundBlurRoot == entt::null || !op.batch ||
                op.batch->foregroundBlurGroup != foregroundBlurRoot)
            {
                continue;
            }
            const DispatchBounds bounds = operation_bounds(op);
            if (bounds.empty())
            {
                continue;
            }
            if (foregroundBlurContentBounds.empty())
            {
                foregroundBlurContentBounds = bounds;
                continue;
            }
            const uint32_t left = std::min(foregroundBlurContentBounds.x, bounds.x);
            const uint32_t top = std::min(foregroundBlurContentBounds.y, bounds.y);
            const uint32_t right = std::max(
                foregroundBlurContentBounds.x + foregroundBlurContentBounds.width,
                bounds.x + bounds.width);
            const uint32_t bottom = std::max(
                foregroundBlurContentBounds.y + foregroundBlurContentBounds.height,
                bounds.y + bounds.height);
            foregroundBlurContentBounds = {left, top, right - left, bottom - top};
        }
        Renderer2DBatch foregroundBlurBatch {};
        DispatchBounds foregroundBlurBounds {};
        if (!foregroundBlurContentBounds.empty())
        {
            foregroundBlurBatch.entity = foregroundBlurRoot;
            foregroundBlurBatch.rect = {
                float(foregroundBlurContentBounds.x), float(foregroundBlurContentBounds.y),
                float(foregroundBlurContentBounds.width), float(foregroundBlurContentBounds.height)};
            foregroundBlurBatch.clipRect = {
                0.0f, 0.0f, float(extent.width), float(extent.height)};
            foregroundBlurBatch.primitive = Renderer2DPrimitive::eRectangle;
            foregroundBlurBatch.effect0 = {foregroundBlurRadius, 1.0f, 0.0f, 1.0f};
            foregroundBlurBounds = make_blur_dispatch_bounds(*this, foregroundBlurBatch);
        }
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
        if (!foregroundBlurBounds.empty())
        {
            // A changed member must repaint the complete filtered group and
            // its source halo, including unchanged members and old extents.
            paintItems.push_back({
                foregroundBlurBatch,
                {foregroundBlurBounds.x, foregroundBlurBounds.y,
                 foregroundBlurBounds.width, foregroundBlurBounds.height},
                uint32_t(Renderer2DRenderOpType::eBlur) + 100u,
                true});
            hasBlur = true;
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
        if (device.diagnosticsEnabled)
            mainDamagePixels += uint64_t(damageBounds.width) * damageBounds.height;
        if (damageBounds.empty() && nativeRegions.empty())
        {
            presentedGeneration = scene.frame_generation();
            presentationPending = false;
            return;
        }
        std::vector<Renderer2DRenderOp> foregroundBlurOps;
        if (!foregroundBlurBounds.empty())
        {
            std::erase_if(ops, [&](const Renderer2DRenderOp& op) {
                if (op.batch && op.batch->foregroundBlurGroup == foregroundBlurRoot)
                {
                    foregroundBlurOps.push_back(op);
                    return true;
                }
                return false;
            });
        }
        if (!foregroundBlurOps.empty() && !foregroundBlurSource)
            foregroundBlurSource = device.target(extent.width, extent.height);
        const auto recordingStart = device.diagnosticsEnabled ? Clock::now() : Clock::time_point{};
        if (device.diagnosticsEnabled) cpuTimings[0].add(recordingStart - preparationStart);
        clear(colour.get(), damageBounds);
        if (!foregroundBlurOps.empty())
        {
            const DispatchBounds sourceClearBounds = make_dispatch_bounds(
                *this,
                expand_and_clip_rect(
                    foregroundBlurBatch.rect,
                    foregroundBlurRadius * 2.0f + 2.0f,
                    foregroundBlurBatch.clipRect),
                0.0f);
            clear(foregroundBlurSource.get(), sourceClearBounds);
        }
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
        // Filtered foreground groups keep their authored order, as on Vulkan.
        if (foregroundBlurRoot == entt::null)
            order_simple_ops(ops);
        auto render_ops = [&](const std::vector<Renderer2DRenderOp>& renderOps) {
        for (const Renderer2DRenderOp& op : renderOps)
        {
            const auto b = operation_bounds(op);
            if (clipped_to_damage(b).empty())
                continue;
            if (op.model)
            {
                flush_text();
                draw_model(*op.model);
                continue;
            }
            auto batch = *op.batch;
            switch (op.type)
            {
            case Renderer2DRenderOpType::ePanelBlur:
            case Renderer2DRenderOpType::eBlur: {
                auto bounds = make_blur_replay_dispatch_bounds(*this, batch);
                const bool liquid = (batch.flags & eRenderer2DStyleLiquidGlass) != 0u;
                const auto outputBounds = liquid ? make_liquid_output_dispatch_bounds(*this, batch) : bounds;
                // The Metal path never populates a separate static layer.
                auto bindings = {colour.get(), colour.get(), backdrop ? texture(backdrop.get()) : colour.get(),
                                 temporary.get(), blur.get()};
                Renderer2DBatch clearBatch;
                clearBatch.rect = {outputBounds.x, outputBounds.y, outputBounds.width, outputBounds.height};
                clearBatch.primitive = Renderer2DPrimitive::eClear;
                clearBatch.flags = eRenderer2DStyleClear;
                dispatch("blur_2d_comp", clearBatch, outputBounds, 0, bindings);
                auto passBatch = batch;
                // Liquid optics store corner radius in effect0.y. Preserve
                // their opacity and packed optics in one separable pass.
                const uint32_t passes = liquid ? 1u :
                    std::max(1u, uint32_t(std::round(std::max(batch.effect0.y, 1.0f))));
                if (!liquid) passBatch.effect0.w =
                    1.0f - std::pow(1.0f - std::clamp(batch.effect0.w, 0.0f, 1.0f), 1.0f / passes);
                for (uint32_t i = 0; i < passes; ++i)
                {
                    passBatch.flags = batch.flags | eRenderer2DStyleBlurHorizontal;
                    dispatch("blur_2d_comp", passBatch, bounds, i | (backdrop ? kBlurUseExternalBackdrop : 0),
                             bindings);
                    passBatch.flags = batch.flags | eRenderer2DStyleBlurVertical;
                    dispatch("blur_2d_comp", passBatch, outputBounds, i | (backdrop ? kBlurUseExternalBackdrop : 0),
                             bindings);
                }
                batch.flags |= eRenderer2DStyleBlurComposite;
                dispatch("blur_2d_comp", batch, outputBounds, 0, bindings);
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
        };

        if (!foregroundBlurOps.empty())
        {
            std::swap(colour, foregroundBlurSource);
            render_ops(foregroundBlurOps);
            std::swap(colour, foregroundBlurSource);
        }
        render_ops(ops);

        if (!foregroundBlurBounds.empty())
        {
            const auto bounds = clipped_to_damage(foregroundBlurBounds);
            auto constants = make_push_constants(foregroundBlurBatch, 0, bounds.x, bounds.y);
            render_foreground_blur(device, constants, bounds.width, bounds.height,
                foregroundBlurSource.get(), colour.get(), temporary.get(), blur.get());
        }

        const auto mainExtent = extent;
        const auto mainDamage = damageBounds;
        struct RestoreRenderState
        {
            Impl& renderer;
            RendererExtent extent;
            DispatchBounds damage;
            ~RestoreRenderState()
            {
                renderer.extent = extent;
                renderer.damageBounds = damage;
            }
        } restoreRenderState {*this, mainExtent, mainDamage};
        for (size_t index = 0; index < nativeRegions.size(); ++index)
        {
            const auto& region = nativeRegions[index];
            if (!region.hasContent) continue; // A thumb needs no Metal foreground drawable.
            auto& content = nativeContents[region.identity];
            if (!nativeGlass->visible(index))
            {
                // Hidden or fully clipped layers have no drawable to present.
                // Keep their repaint pending until AppKit makes them visible.
                content.damage.invalidate();
                continue;
            }
            const uint32_t width = uint32_t(region.contentRect.z), height = uint32_t(region.contentRect.w);
            if (!content.colour || content.colour->width() != width || content.colour->height() != height)
            {
                content.colour = device.target(width, height);
                content.temporary.reset();
                content.blur.reset();
                content.damage.invalidate();
            }
            if (content.atlas != atlasId) { content.atlas = atlasId; content.damage.invalidate(); }
            extent = {width, height};
            std::vector<Renderer2DRenderOp> contentOps;
            std::vector<FrameDamage::Item> contentItems;
            for (size_t item = 0; item < nativeBatches[index].size(); ++item)
            {
                Renderer2DRenderOp op;
                op.batch = &nativeBatches[index][item]; op.type = nativeTypes[index][item];
                const auto bounds = operation_bounds(op);
                contentItems.push_back({*op.batch, {bounds.x, bounds.y, bounds.width, bounds.height},
                    uint32_t(op.type), op.type == Renderer2DRenderOpType::eBlur ||
                    op.type == Renderer2DRenderOpType::ePanelBlur});
                contentOps.push_back(op);
            }
            const auto damage = content.damage.update(contentItems, width, height, false);
            damageBounds = {damage.x, damage.y, damage.z, damage.w};
            if (!damageBounds.empty())
            {
                struct RestoreTargets
                {
                    Impl& renderer;
                    NativeContent& content;
                    RestoreTargets(Impl& renderer, NativeContent& content)
                        : renderer(renderer), content(content)
                    {
                        std::swap(renderer.colour, content.colour);
                        std::swap(renderer.temporary, content.temporary);
                        std::swap(renderer.blur, content.blur);
                    }
                    ~RestoreTargets()
                    {
                        std::swap(renderer.colour, content.colour);
                        std::swap(renderer.temporary, content.temporary);
                        std::swap(renderer.blur, content.blur);
                    }
                } restoreTargets {*this, content};
                clear(colour.get(), damageBounds);
                if (std::any_of(contentItems.begin(), contentItems.end(), [](const auto& item) {
                    return item.backdropEffect;
                }))
                {
                    if (!temporary) temporary = device.target(width, height);
                    if (!blur) blur = device.target(width, height);
                    clear(temporary.get(), {0, 0, width, height});
                    clear(blur.get(), {0, 0, width, height});
                }
                order_simple_ops(contentOps);
                render_ops(contentOps);
                encode_presentation(nativeGlass->layer(index), false);
                if (!presentationComplete) content.damage.invalidate();
            }
        }
        extent = mainExtent;
        damageBounds = mainDamage;
        const auto presentationStart = device.diagnosticsEnabled ? Clock::now() : Clock::time_point{};
        if (device.diagnosticsEnabled) cpuTimings[1].add(presentationStart - recordingStart);
        if (!damageBounds.empty()) encode_presentation(layer.get(), true);
        // All native content and the base drawable share one submission. Metal
        // 4 no longer waits for a complete GPU frame between individual lenses.
        device.submit(drawables, nativeGlass->active() || surface_presents_with_transaction(layer.get()));
        if (!presentationComplete) { frameDamage.invalidate(); return; }
        ++frameCount;
        presentedGeneration = scene.frame_generation();
        presentationPending = false;
        lastPresentation = Clock::now();
        if (device.diagnosticsEnabled)
        {
            cpuTimings[2].add(lastPresentation - presentationStart);
            log_diagnostics(lastPresentation);
        }
    }
};

Engine::Engine(const EngineCreateInfo &info) : impl(std::make_unique<Impl>(info))
{
    Logger::fetch_logger()->log(ready() ? LogLevel::eInfo : LogLevel::eError,
        ready() ? "Engine renderer ready: metal." : "Engine renderer initialisation failed: metal.");
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
    Logger::fetch_logger()->info("Engine instance shutting down: metal.");
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
            state.presentationPending ||
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
    catch (const SubmissionError& error)
    {
        // A failed GPU frame invalidates every retained surface. Native glass
        // can continue compositing independently, so retry its foreground and
        // the base UI together instead of leaving a glass-only window.
        impl->frameDamage.invalidate();
        for (auto& [identity, content] : impl->nativeContents)
            content.damage.invalidate();
        impl->presentationPending = true;
        if (impl->device.successfulCompletions != impl->successfulCompletionsAtFailure)
            impl->consecutiveSubmissionFailures = 0;
        impl->successfulCompletionsAtFailure = impl->device.successfulCompletions;
        if (++impl->consecutiveSubmissionFailures >= 3)
            impl->valid = false;
        Logger::fetch_logger()->error(std::string(error.what()) +
            (impl->valid ? "; retrying the complete UI frame." :
                           "; rendering stopped after three consecutive failures."));
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
    const LogOperation diagnosticOperation("metal: Engine::set_present_mode");
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
    const LogOperation diagnosticOperation("metal: Engine::load_model_3d");
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
    const LogOperation diagnosticOperation("metal: Engine::load_media_2d");
    std::ostringstream key;
    key << std::filesystem::absolute(path).lexically_normal().string() << '|'
        << resource_file_cache_version(path) << '|' << options.rasterWidth << '|' << options.rasterHeight
        << '|' << options.srgb << '|' << options.generateMipmaps << '|' << options.premultiplyAlpha << '|'
        << options.maxAnimationFrames << '|' << options.svgAnimationFrames << '|'
        << options.svgAnimationFrameRate << '|' << options.lottieAnimationFrames << '|'
        << options.maxVideoFrames << '|' << options.maxVideoPixels << '|' << options.trimTransparentBorders;
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
    const LogOperation diagnosticOperation("metal: Engine::load_renderer2d_font");
    return load_renderer2d_font(path, {});
}
bool Engine::load_renderer2d_font(const std::filesystem::path &path,
                                  const Renderer2DFontAtlasLoadOptions &options)
{
    const LogOperation diagnosticOperation("metal: Engine::load_renderer2d_font");
    const auto sourceKey = font_source_key(path, options);
    if (impl->font.loaded() && impl->fontSourceKey == sourceKey &&
        (!options.requireRequestedGlyphs || impl->strictFontRequirements) &&
        ((!options.requireRequestedGlyphs && impl->fontOptions.preloadText == options.preloadText) ||
         impl->font.missing_codepoints(options.preloadText).empty()))
    {
        // Template refreshes often request the same font and locale. Keep an
        // atlas expanded by live text rather than baking it again or shrinking
        // it back to the template's preload. Resource stamps preserve reloads.
        return true;
    }
    if (!impl->font.load_from_file(path, options, impl->upload))
        return false;
    impl->fontPath = path;
    // Live glyph expansion relaxes only its new text. Fixed basic/range
    // requirements already verified with these unchanged fonts stay verified.
    impl->strictFontRequirements = options.requireRequestedGlyphs ||
        (impl->fontSourceKey == sourceKey && impl->strictFontRequirements);
    impl->fontSourceKey = sourceKey;
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
    const LogOperation diagnosticOperation("metal: Engine::load_localisation_directory");
    const bool result = impl->localisation.load_directory(path);
    if (result)
        set_locale(shared_locale());
    return result;
}
bool Engine::load_localisation_directories(const std::vector<std::filesystem::path> &paths)
{
    const LogOperation diagnosticOperation("metal: Engine::load_localisation_directories");
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
    const LogOperation diagnosticOperation("metal: Engine::set_locale");
    const bool result = impl->localisation.set_locale(locale);
    refresh_localised_texts();
    return result;
}
bool Engine::set_shared_locale(const std::string &locale)
{
    const LogOperation diagnosticOperation("metal: Engine::set_shared_locale");
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

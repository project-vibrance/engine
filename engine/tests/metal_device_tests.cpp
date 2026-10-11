#include "../src/renderer/metal/device.h"
#include "../src/renderer/metal/frame_damage.h"
#include "../src/renderer/metal/foreground_blur.h"
#include "../src/renderer/common/text_batch.h"
#include <vibranceUI/renderer/render_types.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <array>
#include <iostream>
#include <stdexcept>
#include <cstring>
#include <future>
#include <chrono>
#include <objc/runtime.h>
#include <objc/message.h>
using namespace vibrance::metal;

void check_retained_frames(Device &device)
{
    constexpr uint32_t width = 48, height = 32, rowBytes = 256;
    auto retained = device.target(width, height), reference = device.target(width, height);
    auto depth = device.target(width, height, MTL::PixelFormatR32Uint);
    auto pixels = own(device.gpu->newBuffer(rowBytes * height * 2, MTL::ResourceStorageModeShared));
    FrameDamage cache;
    std::vector<FrameDamage::Item> items;
    auto item = [](glm::vec4 rect, glm::vec4 colour) {
        FrameDamage::Item result;
        result.batch.rect = rect;
        result.batch.color0 = colour;
        result.batch.effect0.w = 1;
        result.bounds = glm::uvec4(rect);
        return result;
    };
    const glm::uvec4 full(0, 0, width, height);
    auto draw = [&](MTL::Texture *surface, glm::uvec4 damage) {
        glm::uvec4 clear(0, damage.x, damage.y, 0);
        device.dispatch("clear_screen_comp", &clear, sizeof(clear), damage.z, damage.w,
                        {surface, depth.get()});
        for (const auto &entry : items)
        {
            const auto start = glm::max(glm::uvec2(entry.bounds), glm::uvec2(damage));
            const auto end = glm::min(glm::uvec2(entry.bounds) + glm::uvec2(entry.bounds.z, entry.bounds.w),
                                      glm::uvec2(damage) + glm::uvec2(damage.z, damage.w));
            if (end.x <= start.x || end.y <= start.y)
                continue;
            Renderer2DPushConstants shape;
            shape.rect = entry.batch.rect;
            shape.color0 = entry.batch.color0;
            shape.effect0.w = 1;
            shape.data = {uint32_t(Renderer2DPrimitive::eRectangle), 0, 0, start.x | (start.y << 16)};
            device.dispatch("shape_2d_comp", &shape, sizeof(shape), end.x - start.x, end.y - start.y,
                            {surface});
        }
    };
    for (unsigned frame = 0; frame < 18; ++frame)
    {
        items = {item({0, 0, width, height}, {0.1f, 0.2f, 0.3f, 0.6f}),
                 item({float(3 + frame), 7, 9, float(8 + frame % 3)}, {1, 0, 0, 0.5f}),
                 item({15, 11, 23, 15}, {0, 1, 0, 0.4f})};
        if (frame == 5 || frame == 6)
            items.erase(items.begin() + 1); // Removal must erase the old pixels.
        if (frame == 10)
            std::swap(items[1], items[2]); // Reordering must preserve alpha blending.
        if (frame == 12)
            cache.invalidate(); // Resize, atlas or external backdrop replacement.
        const bool fullEffects = frame == 14;
        const auto damage = cache.update(items, width, height, fullEffects);
        if ((frame == 12 || frame == 14 || frame == 15) && damage != full)
            throw std::runtime_error("Full-frame invalidation was lost");
        if (frame == 2 && damage.z * damage.w >= width * height)
            throw std::runtime_error("Small animation still damaged the whole surface");
        draw(retained.get(), damage);
        draw(reference.get(), full);
        device.flush();
        auto command = device.queue->commandBuffer();
        auto blit = command->blitCommandEncoder();
        for (unsigned i = 0; i < 2; ++i)
            blit->copyFromTexture(i ? reference.get() : retained.get(), 0, 0, MTL::Origin(0, 0, 0),
                                  MTL::Size(width, height, 1), pixels.get(), i * rowBytes * height, rowBytes,
                                  rowBytes * height);
        blit->endEncoding();
        command->commit();
        command->waitUntilCompleted();
        auto bytes = static_cast<unsigned char *>(pixels->contents());
        for (unsigned y = 0; y < height; ++y)
            if (std::memcmp(bytes + y * rowBytes, bytes + rowBytes * height + y * rowBytes, width * 4))
                throw std::runtime_error("Retained animation differs from a full redraw");
        const auto unchanged = cache.update(items, width, height, fullEffects);
        if (unchanged.z || unchanged.w)
            throw std::runtime_error("Unchanged paint operations caused another redraw");
    }
}

void check_batched_text(Device &device)
{
    constexpr uint32_t count = 40, width = count * 16, height = 16, rowBytes = width * 4;
    auto target = device.target(width, height), reference = device.target(width, height);
    auto depth = device.target(width, height, MTL::PixelFormatR32Uint);
    auto readback = own(device.gpu->newBuffer(rowBytes * height * 2, MTL::ResourceStorageModeShared));
    std::array<unsigned char, 4> white{255, 255, 255, 255};
    auto atlas = device.upload(1, 1, white, false, false);
    glm::uvec4 zero(0);
    device.dispatch("clear_screen_comp", &zero, sizeof(zero), width, height, {target.get(), depth.get()});
    device.dispatch("clear_screen_comp", &zero, sizeof(zero), width, height, {reference.get(), depth.get()});
    std::array<Renderer2DPushConstants, count> glyphs;
    for (unsigned i = 0; i < count; ++i)
    {
        auto &glyph = glyphs[i];
        glyph.rect = {i * 16 + 2, 2, 10, 10};
        glyph.uvRect = {0, 0, 1, 1};
        glyph.color0 = {float(i % 3) / 2, 0.5f, 1, 0.5f};
        glyph.effect0 = {4, 0, 0, 1};
        glyph.data = {16 | (16 << 16), eRenderer2DStyleAtlasText, 2, i * 16};
        device.dispatch("text_msdf_comp", &glyph, sizeof(glyph), 16, 16,
                        {texture(atlas.get()), reference.get()});
    }
    // More than 4 KiB also exercises the Metal 3 constant-buffer path.
    device.dispatch("text_msdf_batch_comp", glyphs.data(), sizeof(glyphs), 16, 16,
                    {texture(atlas.get()), target.get()}, count);
    device.flush();
    auto command = device.queue->commandBuffer();
    auto blit = command->blitCommandEncoder();
    for (unsigned i = 0; i < 2; ++i)
        blit->copyFromTexture(i ? reference.get() : target.get(), 0, 0, MTL::Origin(0, 0, 0),
                              MTL::Size(width, height, 1), readback.get(), i * rowBytes * height, rowBytes,
                              rowBytes * height);
    blit->endEncoding();
    command->commit();
    command->waitUntilCompleted();
    const auto pixels = static_cast<const unsigned char *>(readback->contents());
    if (std::memcmp(pixels, pixels + rowBytes * height, rowBytes * height) ||
        pixels[8 * rowBytes + 8 * 4 + 3] == 0)
        throw std::runtime_error("Batched text differs from ordered glyph rendering");
}

void check_text_dependency_waves(Device &device)
{
    struct DispatchBounds { uint32_t x, y, width, height; };
    constexpr uint32_t count = 320, width = 96, height = 32, rowBytes = 512;
    auto target = device.target(width, height), reference = device.target(width, height);
    auto depth = device.target(width, height, MTL::PixelFormatR32Uint);
    auto readback = own(device.gpu->newBuffer(rowBytes * height * 2, MTL::ResourceStorageModeShared));
    const std::array<unsigned char, 4> rgba{210, 160, 240, 192};
    auto atlas = device.upload(1, 1, rgba, false, false);
    for (const bool colourGlyph : {false, true})
    {
        glm::uvec4 zero(0);
        device.dispatch("clear_screen_comp", &zero, sizeof(zero), width, height, {target.get(), depth.get()});
        device.dispatch("clear_screen_comp", &zero, sizeof(zero), width, height, {reference.get(), depth.get()});
        std::vector<Renderer2DPushConstants> glyphs(count);
        std::vector<DispatchBounds> bounds(count);
        for (uint32_t index = 0; index < count; ++index)
        {
            // Alternating distant and overlapping fractional glyphs produces
            // dependency waves, including dependencies across the chunk limit.
            const uint32_t x = (index * 17) % 72;
            const uint32_t y = (index * 7) % 12;
            auto& glyph = glyphs[index];
            glyph.rect = {x + 0.25f, y + 0.5f, 12, 12};
            glyph.uvRect = {0, 0, 1, 1};
            glyph.color0 = {float(index % 3) / 2, 0.5f, 1, 0.35f};
            glyph.effect0 = {4, 0, 0, 1};
            glyph.data = {16 | (16 << 16), eRenderer2DStyleAtlasText |
                (colourGlyph ? uint32_t(eRenderer2DStyleColourGlyph) : 0u),
                2u | (2048u << 2), x | (y << 16)};
            bounds[index] = {x, y, 16, 16};
            device.dispatch("text_msdf_comp", &glyph, sizeof(glyph), 16, 16,
                            {texture(atlas.get()), reference.get()});
        }
        vibrance::renderer::TextBatchPlan plan;
        std::array<Renderer2DPushConstants, vibrance::renderer::kTextBatchGlyphLimit> constants;
        for (uint32_t first = 0; first < count; first += vibrance::renderer::kTextBatchPlanningGlyphLimit)
        {
            const uint32_t chunk = std::min(count - first, vibrance::renderer::kTextBatchPlanningGlyphLimit);
            vibrance::renderer::build_text_batch_plan(std::span<const DispatchBounds>(bounds).subspan(first, chunk), plan);
            for (const auto& group : plan.groups)
            {
                for (uint32_t offset = 0; offset < group.count; ++offset)
                    constants[offset] = glyphs[first + plan.glyphIndices[group.first + offset]];
                device.dispatch("text_msdf_batch_comp", constants.data(), group.count * sizeof(constants[0]),
                    group.width, group.height, {texture(atlas.get()), target.get()}, group.count);
            }
        }
        device.flush();
        auto command = device.queue->commandBuffer();
        auto blit = command->blitCommandEncoder();
        for (unsigned index = 0; index < 2; ++index)
            blit->copyFromTexture(index ? reference.get() : target.get(), 0, 0, MTL::Origin(0, 0, 0),
                MTL::Size(width, height, 1), readback.get(), index * rowBytes * height, rowBytes, rowBytes * height);
        blit->endEncoding();
        command->commit();
        command->waitUntilCompleted();
        if (command->status() == MTL::CommandBufferStatusError)
            throw std::runtime_error("Text dependency readback failed");
        const auto pixels = static_cast<const unsigned char*>(readback->contents());
        for (uint32_t y = 0; y < height; ++y)
            if (std::memcmp(pixels + y * rowBytes, pixels + (y + height) * rowBytes, width * 4))
                throw std::runtime_error("Text dependency waves changed overlapping glyph pixels");
        if (pixels[8 * rowBytes + 8 * 4 + 3] == 0)
            throw std::runtime_error("Text dependency fixture drew no pixels");
    }
}

void check_foreground_blur_transitions(Device &device)
{
    constexpr uint32_t width = 128, height = 96, rowBytes = 512;
    auto source = device.target(width, height), destination = device.target(width, height);
    auto temporary = device.target(width, height), blurred = device.target(width, height);
    auto depth = device.target(width, height, MTL::PixelFormatR32Uint);
    auto readback = own(device.gpu->newBuffer(rowBytes * height, MTL::ResourceStorageModeShared));
    for (uint32_t frame = 0; frame < 16; ++frame)
    {
        glm::uvec4 zero(0);
        for (auto image : {source.get(), destination.get(), temporary.get(), blurred.get()})
            device.dispatch("clear_screen_comp", &zero, sizeof(zero), width, height, {image, depth.get()});
        Renderer2DPushConstants background;
        background.rect = {0, 0, width, height};
        background.color0 = {0, 0, 1, 1};
        background.effect0.w = 1;
        background.data.x = uint32_t(Renderer2DPrimitive::eRectangle);
        device.dispatch("shape_2d_comp", &background, sizeof(background), width, height, {destination.get()});
        // Expand, then shrink while reusing the source and scratch textures.
        // A blue main scene must never replace the isolated green foreground.
        const uint32_t step = frame < 8 ? frame : 15 - frame;
        const uint32_t x = 20 + step, y = 18 + step;
        const uint32_t sizeX = 24 + step * 8, sizeY = 16 + step * 4;
        auto foreground = background;
        foreground.rect = {x, y, sizeX, sizeY};
        foreground.color0 = {0, 1, 0, 0.75f};
        device.dispatch("shape_2d_comp", &foreground, sizeof(foreground), width, height, {source.get()});
        Renderer2DPushConstants blur;
        blur.rect = foreground.rect;
        blur.effect0 = {2.0f + step, 1, 0, 1};
        blur.data.x = uint32_t(Renderer2DPrimitive::eRectangle);
        render_foreground_blur(device, blur, width, height, source.get(), destination.get(),
            temporary.get(), blurred.get());
        device.flush();
        auto command = device.queue->commandBuffer();
        auto blit = command->blitCommandEncoder();
        blit->copyFromTexture(destination.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(width, height, 1),
            readback.get(), 0, rowBytes, rowBytes * height);
        blit->endEncoding();
        command->commit();
        command->waitUntilCompleted();
        if (command->status() == MTL::CommandBufferStatusError)
            throw std::runtime_error("Foreground transition readback failed");
        const auto pixels = static_cast<const unsigned char*>(readback->contents());
        const auto centre = pixels + (y + sizeY / 2) * rowBytes + (x + sizeX / 2) * 4;
        if (centre[0] != 0 || centre[1] < 180 || centre[2] > 75 || centre[3] != 255)
            throw std::runtime_error("Island foreground blur sampled the main scene during a size transition");
        if (pixels[0] != 0 || pixels[1] != 0 || pixels[2] != 255 || pixels[3] != 255)
            throw std::runtime_error("Foreground blur changed pixels outside its sampling halo");
    }
}

void check_async_constants(Device &device)
{
    std::array<Owned<MTL::Texture>, 4> frames;
    auto depth = device.target(8, 8, MTL::PixelFormatR32Uint);
    for (unsigned frame = 0; frame < frames.size(); ++frame)
    {
        frames[frame] = device.target(8, 8);
        glm::uvec4 clear(0);
        device.dispatch("clear_screen_comp", &clear, sizeof(clear), 8, 8, {frames[frame].get(), depth.get()});
        Renderer2DPushConstants shape;
        shape.rect = {0, 0, 8, 8};
        shape.effect0.w = 1;
        shape.data.x = uint32_t(Renderer2DPrimitive::eRectangle);
        // Large constants use shared Metal buffers instead of setBytes. Cross
        // a 64 KiB page in each queued frame, then verify that recycling the
        // first slot does not overwrite a submission still using its page.
        std::array<unsigned char, 8192> constants{};
        for (unsigned i = 0; i < 12; ++i)
        {
            shape.color0 = {(frame + 1) / 4.0f, (i % 2) / 2.0f, 0, 1};
            std::memcpy(constants.data(), &shape, sizeof(shape));
            device.dispatch("shape_2d_comp", constants.data(), constants.size(), 8, 8,
                            {frames[frame].get()});
        }
        device.submit();
    }
    device.flush();
    auto readback = own(device.gpu->newBuffer(256 * 8 * frames.size(), MTL::ResourceStorageModeShared));
    auto command = device.queue->commandBuffer();
    auto blit = command->blitCommandEncoder();
    for (unsigned frame = 0; frame < frames.size(); ++frame)
        blit->copyFromTexture(frames[frame].get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(8, 8, 1),
                              readback.get(), frame * 256 * 8, 256, 256 * 8);
    blit->endEncoding();
    command->commit();
    command->waitUntilCompleted();
    const auto bytes = static_cast<const unsigned char *>(readback->contents());
    for (unsigned frame = 0; frame < frames.size(); ++frame)
    {
        const auto pixel = bytes + frame * 256 * 8;
        if (std::abs(int(pixel[0]) - int(std::lround((frame + 1) * 255.0 / 4))) > 1 || pixel[1] < 127 ||
            pixel[1] > 128 || pixel[3] != 255)
            throw std::runtime_error("Deferred submission reused constants before GPU completion");
    }
}

void check_two_frames_in_flight(Device &device)
{
    auto gate = own(device.gpu->newSharedEvent());
    if (device.metal4)
        device.queue4->wait(gate.get(), 1);
    else
    {
        auto command = device.queue->commandBuffer();
        command->encodeWait(gate.get(), 1);
        command->commit();
    }
    // Release on successful encoding, or after a timeout so a regression in
    // CPU/GPU overlap reports a failure instead of hanging the test process.
    std::promise<void> encoded;
    auto release = std::async(std::launch::async,
        [gate, ready = encoded.get_future()]() mutable {
            ready.wait_for(std::chrono::seconds(2));
            gate->setSignaledValue(1);
        });
    auto surface = device.target(8, 8);
    auto depth = device.target(1, 1, MTL::PixelFormatR32Uint);
    for (unsigned frame = 0; frame < 2; ++frame)
    {
        glm::uvec4 clear(0);
        device.dispatch("clear_screen_comp", &clear, sizeof(clear), 8, 8,
                        {surface.get(), depth.get()});
        Renderer2DPushConstants shape;
        shape.rect = {0, 0, 8, 8};
        shape.color0 = {0, 1, 0, 1};
        shape.effect0.w = 1;
        shape.data.x = uint32_t(Renderer2DPrimitive::eRectangle);
        std::array<unsigned char, 8192> constants{};
        std::memcpy(constants.data(), &shape, sizeof(shape));
        device.dispatch("shape_2d_comp", constants.data(), constants.size(), 8, 8, {surface.get()});
        device.submit();
    }
    const bool overlapped = gate->signaledValue() == 0;
    encoded.set_value();
    release.get();
    device.flush();
    if (!overlapped)
        throw std::runtime_error("Encoding the second frame waited for the first GPU submission");
}

void check_submission_resource_lifetime(Device &device)
{
    constexpr uint32_t width = 16, height = 16, rowBytes = 256;
    auto retained = device.target(width, height);
    auto depth = device.target(1, 1, MTL::PixelFormatR32Uint);
    auto readback = own(device.gpu->newBuffer(rowBytes * height, MTL::ResourceStorageModeShared));
    glm::uvec4 zero(0);
    device.dispatch("clear_screen_comp", &zero, sizeof(zero), width, height, {retained.get(), depth.get()});
    for (unsigned frame = 0; frame < 48; ++frame)
    {
        // Scratch owners disappear immediately after submit while their
        // commands may still be queued. Alternating retained and transient
        // images exercises residency retirement during texture replacement.
        auto scratch = device.target(width, height);
        device.dispatch("clear_screen_comp", &zero, sizeof(zero), width, height, {scratch.get(), depth.get()});
        Renderer2DPushConstants shape;
        shape.rect = {0, 0, width, height};
        shape.color0 = {frame % 2 ? 0.0f : 1.0f, frame % 2 ? 1.0f : 0.0f, 0, 0.5f};
        shape.effect0.w = 1;
        shape.data.x = uint32_t(Renderer2DPrimitive::eRectangle);
        device.dispatch("shape_2d_comp", &shape, sizeof(shape), width, height, {scratch.get()});
        device.dispatch("shape_2d_comp", &shape, sizeof(shape), width, height, {retained.get()});
        device.submit();
    }
    device.flush();
    auto command = device.queue->commandBuffer();
    auto blit = command->blitCommandEncoder();
    blit->copyFromTexture(retained.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(width, height, 1),
                          readback.get(), 0, rowBytes, rowBytes * height);
    blit->endEncoding();
    command->commit();
    command->waitUntilCompleted();
    const auto pixels = static_cast<const unsigned char *>(readback->contents());
    // Repeated red/green half-opacity blends converge to one-third red,
    // two-thirds green. Reading only the final frame would remain translucent.
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
        {
            const auto pixel = pixels + y * rowBytes + x * 4;
            if (std::abs(int(pixel[0]) - 85) > 1 || std::abs(int(pixel[1]) - 170) > 1 ||
                pixel[2] != 0 || pixel[3] < 254)
                throw std::runtime_error("Queued submissions lost resources or reordered shared-target blending");
        }
}

void check_failed_submission_retirement(Device &device)
{
    if (device.metal4)
    {
        auto &frame = device.metal4Frames[device.metal4Frame];
        const auto completedBeforeFailure = device.successfulCompletions;
        frame.feedback = std::make_shared<Device::Metal4Feedback>();
        frame.completionValue = device.serial + 1; // This event deliberately never signals.
        frame.constantPage = 3;
        device.inFlight = true;
        auto feedback = frame.feedback;
        {
            auto pool = own(NS::AutoreleasePool::alloc()->init());
            feedback->finish(NS::Error::error(string("VibranceTestSubmission"),
                                              MTL4::CommandQueueErrorInternal, nullptr));
        } // Error transport must own the autoreleased NSError independently.
        auto target = device.target(8, 8);
        auto depth = device.target(1, 1, MTL::PixelFormatR32Uint);
        glm::uvec4 clear(0);
        bool reported = false;
        try
        {
            device.dispatch("clear_screen_comp", &clear, sizeof(clear), 8, 8, {target.get(), depth.get()});
        }
        catch (const SubmissionError &error)
        {
            reported = std::string_view(error.what()).find("VibranceTestSubmission") != std::string_view::npos;
        }
        if (!reported || frame.feedback || frame.completionValue || frame.constantPage ||
            device.recording || device.inFlight || device.successfulCompletions != completedBeforeFailure)
            throw std::runtime_error("Metal 4 failed feedback was not retained, reported and retired");
        device.dispatch("clear_screen_comp", &clear, sizeof(clear), 8, 8, {target.get(), depth.get()});
        device.flush();
        if (device.successfulCompletions != completedBeforeFailure + 1)
            throw std::runtime_error("Metal 4 feedback recovery did not complete the later frame");
        return;
    }
    // A small Objective-C stand-in covers the terminal error path without
    // deliberately causing a GPU fault or relying on driver timeout behaviour.
    const auto type = objc_allocateClassPair(objc_getClass("NSObject"), "VibranceFailedCommandBuffer", 0);
    if (type)
    {
        class_addMethod(type, sel_registerName("status"),
            reinterpret_cast<IMP>(+[](id, SEL) -> NS::UInteger { return MTL::CommandBufferStatusError; }), "Q@:");
        class_addMethod(type, sel_registerName("error"),
            reinterpret_cast<IMP>(+[](id, SEL) -> id { return nullptr; }), "@@:");
        class_addMethod(type, sel_registerName("waitUntilCompleted"),
            reinterpret_cast<IMP>(+[](id, SEL) {}), "v@:");
        objc_registerClassPair(type);
    }
    auto &frame = device.metal3Frames[device.metal3Frame];
    const auto send = reinterpret_cast<id (*)(id, SEL)>(objc_msgSend);
    auto instance = send(reinterpret_cast<id>(objc_getClass("VibranceFailedCommandBuffer")),
                         sel_registerName("alloc"));
    instance = send(instance, sel_registerName("init"));
    frame.pending = own(reinterpret_cast<MTL::CommandBuffer *>(instance));
    frame.constantPage = 3;
    const auto completedBeforeFailure = device.successfulCompletions;
    auto target = device.target(8, 8);
    auto depth = device.target(1, 1, MTL::PixelFormatR32Uint);
    glm::uvec4 clear(0);
    bool reported = false;
    try
    {
        device.dispatch("clear_screen_comp", &clear, sizeof(clear), 8, 8, {target.get(), depth.get()});
    }
    catch (const SubmissionError &)
    {
        reported = true;
    }
    if (!reported || frame.pending || frame.constantPage != 0 || device.recording ||
        device.successfulCompletions != completedBeforeFailure)
        throw std::runtime_error("A failed command buffer was not retired before reporting its error");
    device.dispatch("clear_screen_comp", &clear, sizeof(clear), 8, 8, {target.get(), depth.get()});
    device.flush();
    if (device.successfulCompletions != completedBeforeFailure + 1)
        throw std::runtime_error("Submission recovery did not record the later successful completion");
}
int main()
{
    auto pool = own(NS::AutoreleasePool::alloc()->init());
    auto gpu = own(MTL::CreateSystemDefaultDevice());
    if (!gpu || !gpu->supportsFamily(MTL::GPUFamilyMetal3))
        return 77;
    try
    {
        for (bool prefer4 : {false, true})
        {
            Device device(prefer4);
            for (const char *name :
                 {"shape_2d_comp", "shadow_2d_comp", "blur_2d_comp", "media_2d_comp", "text_msdf_comp",
                  "clear_screen_comp", "composite_2d_comp", "hosted_3d_composite_comp"})
                device.pipeline(name);
            device.function("model_3d_vert");
            device.function("model_3d_frag");
            auto target = device.target(16, 16);
            auto depth = device.target(16, 16, MTL::PixelFormatR32Uint);
            glm::uvec4 zero(0);
            device.dispatch("clear_screen_comp", &zero, sizeof(zero), 16, 16, {target.get(), depth.get()});
            Renderer2DPushConstants shape;
            shape.rect = {0, 0, 16, 16};
            shape.color0 = {1, 0, 0, 1};
            shape.effect0.w = 1;
            shape.data.x = uint32_t(Renderer2DPrimitive::eRectangle);
            device.dispatch("shape_2d_comp", &shape, sizeof(shape), 16, 16, {target.get()});
            device.flush();
            auto readback = own(device.gpu->newBuffer(16 * 256, MTL::ResourceStorageModeShared));
            auto command = device.queue->commandBuffer();
            auto blit = command->blitCommandEncoder();
            blit->copyFromTexture(target.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(16, 16, 1),
                                  readback.get(), 0, 256, 16 * 256);
            blit->endEncoding();
            command->commit();
            command->waitUntilCompleted();
            if (command->status() == MTL::CommandBufferStatusError)
                throw std::runtime_error("Readback failed");
            const auto pixels = static_cast<const unsigned char *>(readback->contents());
            for (size_t y = 0; y < 16; ++y)
                for (size_t x = 0; x < 16; ++x)
                {
                    const auto p = pixels + y * 256 + x * 4;
                    if (p[0] != 255 || p[1] != 0 || p[2] != 0 || p[3] != 255)
                        throw std::runtime_error("Incorrect shape pixels or command ordering");
                }
            // Core Animation requires premultiplied alpha at the native surface.
            auto blank = device.target(16, 16), output = device.target(16, 16),
                 scratch = device.target(16, 16);
            device.dispatch("clear_screen_comp", &zero, sizeof(zero), 16, 16, {blank.get(), depth.get()});
            device.dispatch("clear_screen_comp", &zero, sizeof(zero), 16, 16, {target.get(), depth.get()});
            shape.color0.w = 0.5f;
            device.dispatch("shape_2d_comp", &shape, sizeof(shape), 16, 16, {target.get()});
            glm::uvec4 composite(2, 0, 0, 0);
            device.dispatch("composite_2d_comp", &composite, sizeof(composite), 16, 16,
                            {target.get(), blank.get(), blank.get(), scratch.get(), output.get()});
            device.flush();
            command = device.queue->commandBuffer();
            blit = command->blitCommandEncoder();
            blit->copyFromTexture(output.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(16, 16, 1),
                                  readback.get(), 0, 256, 16 * 256);
            blit->endEncoding();
            command->commit();
            command->waitUntilCompleted();
            if (pixels[0] < 127 || pixels[0] > 128 || pixels[3] < 127 || pixels[3] > 128)
                throw std::runtime_error("Native composition alpha is not premultiplied");
            // Hosted 3D must honour the complete dispatch rectangle, including a non-zero origin.
            glm::uvec4 modelRect(4, 4, 4, 4);
            device.dispatch("hosted_3d_composite_comp", &modelRect, sizeof(modelRect), 4, 4,
                            {blank.get(), output.get()});
            device.flush();
            command = device.queue->commandBuffer();
            blit = command->blitCommandEncoder();
            blit->copyFromTexture(blank.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(16, 16, 1),
                                  readback.get(), 0, 256, 16 * 256);
            blit->endEncoding();
            command->commit();
            command->waitUntilCompleted();
            if (pixels[0] != 0 || pixels[4 * 256 + 4 * 4] < 254 || pixels[4 * 256 + 4 * 4 + 3] < 127)
                throw std::runtime_error("Hosted 3D composition bounds are incorrect");
            const std::array<unsigned char, 4> rgba{10, 20, 30, 255};
            if (!device.upload(1, 1, rgba, false, true) || device.upload(2, 2, rgba, false, false))
                throw std::runtime_error("Texture upload validation failed");
            check_retained_frames(device);
            check_batched_text(device);
            check_text_dependency_waves(device);
            check_foreground_blur_transitions(device);
            check_async_constants(device);
            check_two_frames_in_flight(device);
            check_submission_resource_lifetime(device);
            check_failed_submission_retirement(device);
            // Consecutive submissions reuse storage only after the GPU completes.
            // Alternate constants to catch stale bindings and premature reuse.
            for (unsigned frame = 0; frame < 12; ++frame)
            {
                device.dispatch("clear_screen_comp", &zero, sizeof(zero), 16, 16,
                                {target.get(), depth.get()});
                shape.color0 = frame % 2 ? glm::vec4(0, 1, 0, 1) : glm::vec4(1, 0, 0, 1);
                device.dispatch("shape_2d_comp", &shape, sizeof(shape), 16, 16, {target.get()});
                device.flush();
                command = device.queue->commandBuffer();
                blit = command->blitCommandEncoder();
                blit->copyFromTexture(target.get(), 0, 0, MTL::Origin(0, 0, 0), MTL::Size(16, 16, 1),
                                      readback.get(), 0, 256, 16 * 256);
                blit->endEncoding();
                command->commit();
                command->waitUntilCompleted();
                if (pixels[0] != (frame % 2 ? 0 : 255) || pixels[1] != (frame % 2 ? 255 : 0) ||
                    pixels[2] != 0 || pixels[3] != 255)
                    throw std::runtime_error("Reused dispatch storage produced stale pixels");
            }
            std::cout << "Metal " << (device.metal4 ? 4 : 3) << " shader and pixel checks passed\n";
        }
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

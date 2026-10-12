#include <vibranceUI/glfw/window.h>
#include <vibranceUI/renderer/metal/capabilities.h>
#include <vibranceUI/ui/liquid_controls.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#import <QuartzCore/CATransaction.h>
#import <Metal/Metal.h>
#import <objc/runtime.h>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <string_view>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <cstring>

struct CapturedDrawable
{
    __strong id<CAMetalDrawable> drawable;
    std::shared_ptr<std::atomic<bool>> presented;
};
static std::unordered_map<uintptr_t, CapturedDrawable> capturedDrawables;
struct CaptureClassHook { IMP previous = nullptr; size_t users = 0; };
static std::unordered_map<Class, CaptureClassHook> captureHooks;
static std::unordered_set<uintptr_t> captureTargets;
static CAMetalLayer* interruptedLayer = nil;
static unsigned drawableAttempts = 0;
static id<CAMetalDrawable> capture_next_drawable(id self, SEL selector)
{
    if (self == interruptedLayer && ++drawableAttempts == 1) return nil;
    const auto original = captureHooks.at(object_getClass(self)).previous;
    auto drawable = reinterpret_cast<id<CAMetalDrawable> (*)(id, SEL)>(original)(self, selector);
    if (drawable && captureTargets.contains(uintptr_t((__bridge void*)self)))
    {
        auto presented = std::make_shared<std::atomic<bool>>(false);
        [drawable addPresentedHandler:^(id<MTLDrawable>) { presented->store(true, std::memory_order_release); }];
        capturedDrawables[uintptr_t((__bridge void*)self)] = {drawable, presented};
    }
    return drawable;
}

struct CaptureLayer
{
    __strong CAMetalLayer* layer;
    Class type;
    explicit CaptureLayer(CAMetalLayer* value) : layer(value), type(object_getClass(value))
    {
        auto& hook = captureHooks[type];
        if (hook.users++ == 0)
        {
            hook.previous = class_getMethodImplementation(type, @selector(nextDrawable));
            class_replaceMethod(type, @selector(nextDrawable), reinterpret_cast<IMP>(capture_next_drawable), "@@:");
        }
        captureTargets.insert(uintptr_t((__bridge void*)value));
    }
    ~CaptureLayer()
    {
        captureTargets.erase(uintptr_t((__bridge void*)layer));
        auto& hook = captureHooks.at(type);
        if (--hook.users == 0)
            class_replaceMethod(type, @selector(nextDrawable), hook.previous, "@@:");
    }
};

static std::vector<unsigned char> presented_pixels(CAMetalLayer* layer)
{
    const auto found = capturedDrawables.find(uintptr_t((__bridge void*)layer));
    if (found == capturedDrawables.end())
        throw std::runtime_error("A changed native UI surface acquired no drawable");
    const auto& capture = found->second;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    [CATransaction flush];
    while (!capture.presented->load(std::memory_order_acquire))
    {
        if (std::chrono::steady_clock::now() >= deadline)
            throw std::runtime_error("Native drawable presentation did not complete");
        [[NSRunLoop currentRunLoop] runMode:NSDefaultRunLoopMode beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.001]];
    }
    const auto texture = capture.drawable.texture;
    const NSUInteger rowBytes = ((texture.width * 4 + 255) / 256) * 256;
    const auto buffer = [texture.device newBufferWithLength:rowBytes * texture.height options:MTLResourceStorageModeShared];
    const auto queue = [texture.device newCommandQueue];
    const auto command = queue.commandBuffer;
    const auto blit = command.blitCommandEncoder;
    [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
        sourceSize:MTLSizeMake(texture.width, texture.height, 1) toBuffer:buffer destinationOffset:0
        destinationBytesPerRow:rowBytes destinationBytesPerImage:rowBytes * texture.height];
    [blit endEncoding];
    [command commit];
    [command waitUntilCompleted];
    if (command.error) throw std::runtime_error("Native presented pixel readback failed");
    std::vector<unsigned char> pixels(texture.width * texture.height * 4);
    for (NSUInteger y = 0; y < texture.height; ++y)
        std::memcpy(pixels.data() + y * texture.width * 4,
            static_cast<const unsigned char*>(buffer.contents) + y * rowBytes, texture.width * 4);
    return pixels;
}

static void check_drawable_retry(Engine& engine, Renderer2DScene& scene,
    entt::entity background, CAMetalLayer* layer)
{
    DisplayTransition2DComponent transition;
    transition.durationSeconds = 1.0f;
    transition.fromOpacity = 0.2f;
    transition.toOpacity = 0.8f;
    scene.registry().emplace<DisplayTransition2DComponent>(background, transition);
    scene.mark_dirty(background);
    engine.draw(); // Start the animation and present its first frame.
    const auto generation = scene.frame_generation();
    CaptureLayer capture(layer);
    interruptedLayer = layer;
    drawableAttempts = 0;
    struct RestoreInterruption
    {
        ~RestoreInterruption() { interruptedLayer = nil; }
    } restore;
    engine.draw(); // Drop one time-driven frame without changing generation.
    if (drawableAttempts != 1 || scene.frame_generation() != generation)
        throw std::runtime_error("Drawable retry fixture did not interrupt a retained animation frame");
    // Simulate settling before the next tick, with no new authored scene edit.
    scene.registry().get<DisplayTransition2DComponent>(background).enabled = false;
    engine.draw();
    if (drawableAttempts != 2 || !engine.ready())
        throw std::runtime_error("A lost drawable left settled panel content unpresented");
    capturedDrawables.clear();
}

static NSMutableArray<NSView*>* materials(NSView* root)
{
    NSMutableArray<NSView*>* result = [NSMutableArray array];
    NSMutableArray<NSView*>* pending = [NSMutableArray arrayWithObject:root];
    while (pending.count)
    {
        NSView* view = pending.lastObject;
        [pending removeLastObject];
        if (@available(macOS 26.0, *))
        {
            if ([view isKindOfClass:[NSGlassEffectView class]]) [result addObject:view];
        }
        else if ([view isKindOfClass:[NSVisualEffectView class]]) [result addObject:view];
        [pending addObjectsFromArray:view.subviews];
    }
    return result;
}

static void check_native_foreground_pixels(Engine& engine, Renderer2DScene& scene,
    UiBuilder& ui, entt::entity lens, entt::entity label,
    CAMetalLayer* foreground, CAMetalLayer* base)
{
    scene.enable_mask(lens, false);
    const auto icon = scene.create_shape({}, {14, 14}, make_solid_style("#00FF00FF"), Renderer2DPrimitive::eEllipse);
    ui.attach_aligned(icon, lens, UiAlignment::eTopLeft, {5, 13}, {14, 14});
    ui.set_layer(icon, 3, 100);
    const auto overlay = scene.create_shape({2, 2}, {12, 12}, make_solid_style("#FF00FFFF"));
    ui.set_layer(overlay, 9, 100);
    CaptureLayer captureForeground(foreground), captureBase(base);
    for (unsigned frame = 0; frame < 12; ++frame)
    {
        auto& transform = scene.registry().get<Transform2DComponent>(lens);
        transform.position.x = 20 + (frame % 4) * 17;
        auto& visual = scene.registry().get<InteractiveVisual2DComponent>(lens);
        visual.scaleOrigin = glm::vec2(ui_entity_base_framebuffer_rect(scene.registry(), lens));
        visual.scale = {1.0f + (frame % 3) * 0.06f, 1.0f};
        visual.opacity = frame % 3 == 1 ? 0.5f : 1.0f;
        scene.registry().get<ShapeStyleComponent>(icon).set_color(frame % 2 ? "#FF0000FF" : "#00FF00FF");
        scene.mark_dirty(lens);
        scene.mark_dirty(icon);
        scene.mark_dirty(overlay);
        capturedDrawables.clear();
        engine.draw();
        const auto pixels = presented_pixels(foreground);
        const auto lensRect = ui_entity_visual_framebuffer_rect(scene.registry(), lens);
        const auto iconRect = ui_entity_visual_framebuffer_rect(scene.registry(), icon);
        const auto x = uint32_t(iconRect.x + iconRect.z * 0.5f - std::floor(lensRect.x));
        const auto y = uint32_t(iconRect.y + iconRect.w * 0.5f - std::floor(lensRect.y));
        const auto width = uint32_t(foreground.drawableSize.width);
        const auto pixel = pixels.data() + (y * width + x) * 4;
        // Native surfaces use BGRA and premultiplied alpha. Their foreground
        // must retain both a changing masked icon and unrelated white text.
        const int expectedAlpha = visual.opacity < 1 ? 128 : 255;
        if (pixel[0] > 5 || std::abs(int(pixel[frame % 2 ? 2 : 1]) - expectedAlpha) > 3 ||
            std::abs(int(pixel[3]) - expectedAlpha) > 3 || foreground.superlayer.opacity != 1)
            throw std::runtime_error("A moving native control lost its masked icon pixels at frame " +
                std::to_string(frame) + " sample=" + std::to_string(x) + "," + std::to_string(y) +
                " BGRA=" + std::to_string(pixel[0]) + "," + std::to_string(pixel[1]) + "," +
                std::to_string(pixel[2]) + "," + std::to_string(pixel[3]) +
                " expected alpha=" + std::to_string(expectedAlpha) +
                " parent opacity=" + std::to_string(foreground.superlayer.opacity));
        unsigned whitePixels = 0;
        for (size_t offset = 0; offset < pixels.size(); offset += 4)
            whitePixels += pixels[offset] > 90 && pixels[offset + 1] > 90 && pixels[offset + 2] > 90 && pixels[offset + 3] > 90;
        if (whitePixels < 8)
            throw std::runtime_error("A moving native control lost its retained foreground label pixels");
        if (const auto found = capturedDrawables.find(uintptr_t((__bridge void*)base)); found != capturedDrawables.end())
        {
            const auto main = presented_pixels(base);
            const auto mainPixel = main.data() + (7 * uint32_t(base.drawableSize.width) + 7) * 4;
            if (mainPixel[0] < 245 || mainPixel[1] > 5 || mainPixel[2] < 245 || mainPixel[3] < 245)
                throw std::runtime_error("Native foreground splitting lost independent overlay pixels");
        }
    }
    capturedDrawables.clear();
    scene.destroy_entity(icon);
    scene.destroy_entity(overlay);
}

static void check_overlapping_overlay_pixels(Engine& engine, Renderer2DScene& scene,
    UiBuilder& ui, entt::entity lens, NSWindow* window)
{
    const auto lensRect = ui_entity_visual_framebuffer_rect(scene.registry(), lens);
    const auto overlay = scene.create_shape({lensRect.x + 80, lensRect.y + 10}, {8, 8},
        make_solid_style("#FF00FFFF"));
    ui.set_layer(overlay, 9, 200);
    engine.draw();
    NSView* plane = nil;
    NSMutableArray<NSView*>* pending = [NSMutableArray arrayWithObject:window.contentView];
    while (pending.count)
    {
        NSView* view = pending.lastObject;
        [pending removeLastObject];
        if ([view.identifier hasPrefix:@"vibrance-foreground-"]) plane = view;
        [pending addObjectsFromArray:view.subviews];
    }
    if (!plane || plane.superview.superview.subviews.lastObject != plane.superview)
        throw std::runtime_error("An independent high-order overlay remained below native glass");
    NSView* paintView = plane.superview.subviews.lastObject.subviews.firstObject;
    CAMetalLayer* paintLayer = (CAMetalLayer*)paintView.layer;
    if (![paintLayer isKindOfClass:CAMetalLayer.class])
        throw std::runtime_error("An overlapping overlay has no transparent foreground surface");
    CaptureLayer capture(paintLayer);
    for (unsigned frame = 0; frame < 4; ++frame)
    {
        scene.registry().get<ShapeStyleComponent>(overlay).set_color(frame % 2 ? "#FF00FFFF" : "#00FFFFFF");
        scene.mark_dirty(overlay);
        capturedDrawables.clear();
        engine.draw();
        const auto pixels = presented_pixels(paintLayer);
        const auto width = uint32_t(paintLayer.drawableSize.width);
        const NSRect viewFrame = [paintView convertRect:paintView.bounds toView:window.contentView];
        const double sx = engine.render_width() / NSWidth(window.contentView.bounds);
        const double sy = engine.render_height() / NSHeight(window.contentView.bounds);
        const auto shape = ui_entity_visual_framebuffer_rect(scene.registry(), overlay);
        const auto x = uint32_t(std::lround(shape.x + shape.z * 0.5f - NSMinX(viewFrame) * sx));
        const auto y = uint32_t(std::lround(shape.y + shape.w * 0.5f -
            (NSHeight(window.contentView.bounds) - NSMaxY(viewFrame)) * sy));
        if (x >= width || y >= uint32_t(paintLayer.drawableSize.height))
            throw std::runtime_error("Popup pixel coordinates escaped their compact foreground surface");
        const auto pixel = pixels.data() + (y * width + x) * 4;
        if (pixel[0] < 245 || pixel[frame % 2 ? 2 : 1] < 245 ||
            pixel[frame % 2 ? 1 : 2] > 5 || pixel[3] < 245)
            throw std::runtime_error("An overlapping popup lost its foreground pixels above native glass");
    }
    capturedDrawables.clear();
    scene.destroy_entity(overlay);
}

static void check_ordinary_control_pixels(Engine& engine, Renderer2DScene& scene,
    UiBuilder& ui, entt::entity lens, CAMetalLayer* foreground, CAMetalLayer* base)
{
    // Keep a native lens completely unchanged while a conventional control
    // changes. Its base-only submissions must present without another glass
    // animation or click waking the native foreground layer.
    const auto ordinary = scene.create_shape({8, 80}, {24, 14}, make_solid_style("#FF0000FF"));
    ui.set_layer(ordinary, 5, 50);
    const auto retainedIcon = scene.create_shape({}, {10, 10}, make_solid_style("#00FF00FF"));
    ui.attach_aligned(retainedIcon, lens, UiAlignment::eTopLeft, {3, 3}, {10, 10});
    ui.set_layer(retainedIcon, 3, 50);
    CaptureLayer captureForeground(foreground), captureBase(base);
    capturedDrawables.clear();
    engine.draw();
    const uintptr_t foregroundKey = uintptr_t((__bridge void*)foreground);
    const auto retained = capturedDrawables.at(foregroundKey);
    const auto retainedPixels = presented_pixels(foreground);
    const CGSize retainedSize = foreground.drawableSize;
    for (unsigned frame = 0; frame < 12; ++frame)
    {
        scene.registry().get<ShapeStyleComponent>(ordinary).set_color(frame % 2 ? "#FF0000FF" : "#0000FFFF");
        scene.mark_dirty(ordinary);
        capturedDrawables.clear();
        engine.draw();
        if (capturedDrawables.contains(foregroundKey) || !CGSizeEqualToSize(retainedSize, foreground.drawableSize))
            throw std::runtime_error("A base-only control change unnecessarily repainted native foreground content");
        const auto pixels = presented_pixels(base);
        const auto rectangle = ui_entity_visual_framebuffer_rect(scene.registry(), ordinary);
        const auto x = uint32_t(rectangle.x + rectangle.z * 0.5f);
        const auto y = uint32_t(rectangle.y + rectangle.w * 0.5f);
        const auto pixel = pixels.data() + (y * uint32_t(base.drawableSize.width) + x) * 4;
        if (pixel[frame % 2 ? 2 : 0] < 245 || pixel[1] > 5 || pixel[frame % 2 ? 0 : 2] > 5 || pixel[3] < 245)
            throw std::runtime_error("An ordinary control change froze or presented stale base pixels at frame " +
                std::to_string(frame));
        capturedDrawables[foregroundKey] = retained;
        if (presented_pixels(foreground) != retainedPixels)
            throw std::runtime_error("A base-only control change lost retained native foreground content");
    }
    capturedDrawables.clear();
    scene.destroy_entity(ordinary);
    scene.destroy_entity(retainedIcon);
}

int main(int argc, char** argv)
{
    if (vibrance_metal_version() < 3) return 77;
    try
    {
        const bool benchmark = argc > 1 && std::string_view(argv[1]) == "--benchmark";
        {
            GlfwWindowHostOptions options;
            options.title = "Native liquid control test";
            options.size = benchmark ? glm::ivec2(1000, 850) : glm::ivec2(240, 160);
            options.transparentFramebuffer = true;
            options.enableAudio = false;
            options.focusOnShow = false;
            options.configureEngine = [benchmark](EngineCreateInfo& info) {
                if (benchmark) info.presentMode = RendererPresentMode::eImmediate;
                info.defaultRenderer2DFontPath = "/System/Library/Fonts/Helvetica.ttc";
            };
            GlfwWindowHost host(options);
            if (!host.open()) throw std::runtime_error("Cannot open native glass test window");
            auto& engine = *host.engine();
            auto& scene = engine.renderer2d_scene();
            UiBuilder ui(scene);
            const auto background = scene.create_shape({0, 0}, {float(engine.render_width()), float(engine.render_height())},
                make_solid_style("#304080FF"));
            const auto first = scene.create_shape({20, 20}, {100, 40}, make_solid_style("#FFFFFFFF"));
            const auto second = scene.create_shape({140, 20}, {100, 40}, make_solid_style("#FFFFFFFF"));
            for (const auto lens : {first, second})
            {
                scene.registry().get<ShapeComponent>(lens).cornerRadius = 16;
                scene.registry().emplace<UiLiquidControlSurface>(lens);
                ui.set_layer(lens, 1, lens == first ? 1u : 2u);
            }
            const auto label = ui.create_aligned_text("Sharp", engine.renderer2d_font_atlas(),
                first, UiAlignment::eCentre, 12, TextStyleComponent{}, 2, 0);
            ui_enable_stretch_dynamics(scene, first);
            auto optics = ui_liquid_glass_options();
            optics.liquidTint = {0.2f, 0.4f, 0.8f, 0.35f};
            ui_update_liquid_controls(scene, true, optics);
            engine.draw();
            if (benchmark)
            {
                for (unsigned item = 0; item < 3; ++item)
                {
                    const auto lens = scene.create_shape({300.0f + item * 24, 20}, {16, 16},
                        make_solid_style("#FFFFFFFF"), Renderer2DPrimitive::eEllipse);
                    scene.registry().emplace<UiLiquidControlSurface>(lens);
                    ui.set_layer(lens, 1, 3 + item);
                }
                for (unsigned item = 0; item < 20; ++item)
                {
                    const auto text = scene.create_text("Settings panel content", {30, 100.0f + item * 24}, 14);
                    apply_font_layout(engine.renderer2d_font_atlas(), scene.registry(), text);
                    ui.set_layer(text, 2, item + 1);
                }
                ui_update_liquid_controls(scene, true, optics);
                const auto animate = [&](unsigned frame) {
                    auto& visual = scene.registry().get<InteractiveVisual2DComponent>(first);
                    visual.scaleOrigin = {20, 20};
                    visual.scale = {1.0f + 0.12f * std::sin(frame * 0.15f), 1.0f};
                    scene.mark_dirty(first);
                    Engine::begin_externally_paced_frame(120);
                    engine.draw();
                    Engine::end_externally_paced_frame();
                };
                for (unsigned frame = 0; frame < 20; ++frame) animate(frame);
                const auto start = std::chrono::steady_clock::now();
                for (unsigned frame = 0; frame < 180; ++frame) animate(frame);
                const double elapsed = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - start).count();
                std::cout << "Metal 3 native glass animation: "
                    << elapsed / 180 << " ms/frame\n";
                host.close();
                return 0;
            }
            NSWindow* window = (__bridge NSWindow*)glfw_native_window_handle(host.window());
            CAMetalLayer* baseLayer = (CAMetalLayer*)window.contentView.layer;
            auto views = materials(window.contentView);
            if (views.count != 2) throw std::runtime_error("Controls did not create two native glass views");
            const auto findGlass = [&](entt::entity entity) -> NSView* {
                const NSString* identity = [NSString stringWithFormat:@"vibrance-liquid-%llu", uint64_t(entt::to_integral(entity))];
                for (NSView* view in materials(window.contentView))
                    if ([view.identifier isEqual:identity]) return view;
                return nil;
            };
            NSView* glass = findGlass(first);
            NSView* container = glass.superview.superview;
            if ([container hitTest:NSMakePoint(20, 20)] != nil || container.subviews.count != 2)
                throw std::runtime_error("Native glass intercepted input or lost foreground paint layers");
            NSView* content = glass.superview.subviews.lastObject;
            NSView* foreground = content.subviews.firstObject;
            CAMetalLayer* foregroundLayer = (CAMetalLayer*)foreground.layer;
            if (!foreground || ![foregroundLayer isKindOfClass:[CAMetalLayer class]] ||
                foregroundLayer.drawableSize.width > 144 || foregroundLayer.drawableSize.height > 64)
                throw std::runtime_error("Glass did not own a compact foreground surface");
            if (@available(macOS 26.0, *))
            {
                auto native = (NSGlassEffectView*)glass;
                if (std::abs(native.tintColor.alphaComponent - 0.35) > 0.001 || native.cornerRadius <= 0)
                    throw std::runtime_error("Native tint or corner geometry was not applied");
            }
            const NSRect oldFrame = glass.frame;
            const CGSize retainedSize = foregroundLayer.drawableSize;
            scene.registry().get<Transform2DComponent>(first).position.x += 25;
            scene.mark_dirty(first);
            optics.liquidTint.alpha = 0.6f;
            ui_update_liquid_controls(scene, true, optics);
            engine.draw();
            if (NSEqualRects(oldFrame, glass.frame))
                throw std::runtime_error("Native lens did not follow moving control geometry");
            auto& visual = scene.registry().get<InteractiveVisual2DComponent>(first);
            visual.scaleOrigin = glm::vec2(ui_entity_base_framebuffer_rect(scene.registry(), first));
            visual.scale = {1.15f, 1.10f};
            scene.mark_dirty(first);
            engine.draw();
            const auto stretched = ui_entity_visual_framebuffer_rect(scene.registry(), first);
            if (!CGSizeEqualToSize(retainedSize, foregroundLayer.drawableSize) ||
                foregroundLayer.drawableSize.width < stretched.z ||
                NSWidth(foreground.frame) < NSWidth(glass.bounds) - 1)
                throw std::runtime_error("Foreground content did not stretch with its native lens");
            const NSRect nativeLabelFrame = [foreground convertRect:foreground.bounds toView:window.contentView];
            const NSRect lensFrame = [glass convertRect:glass.bounds toView:window.contentView];
            if (std::abs(NSMinX(nativeLabelFrame) - NSMinX(lensFrame)) > 1 ||
                std::abs(NSMaxY(nativeLabelFrame) - NSMaxY(lensFrame)) > 1)
                throw std::runtime_error("Glass and foreground content used different coordinate origins");
            UiSliderOptions sliderOptions;
            sliderOptions.maxValue = 100;
            sliderOptions.thumbSize = {24, 18};
            const auto slider = ui_create_slider(ui, ui.root(), UiAlignment::eTopLeft,
                {20, 100}, {180, 7}, 3, 0, sliderOptions);
            auto& input = scene.registry().get<SliderInputComponent>(slider.root);
            input.dragging = true;
            input.liquidThumbScale = 1.25f;
            ui_update_liquid_controls(scene, true, optics);
            input.value = input.visualValue = 100;
            ui_update_slider_visual(scene, engine.renderer2d_font_atlas(), slider.root);
            engine.draw();
            if (findGlass(first) != glass || content.subviews.firstObject != foreground)
                throw std::runtime_error("Adding a grabbed thumb reassigned an existing glass content view");
            NSView* thumbGlass = findGlass(slider.knob);
            const auto thumb = ui_entity_visual_framebuffer_rect(scene.registry(), slider.knob);
            const NSRect thumbFrame = [thumbGlass convertRect:thumbGlass.bounds toView:window.contentView];
            const CGFloat sx = NSWidth(window.contentView.bounds) / engine.render_width();
            const CGFloat sy = NSHeight(window.contentView.bounds) / engine.render_height();
            if (!thumbGlass || std::abs(NSMinX(thumbFrame) - thumb.x * sx) > 0.01 ||
                std::abs(NSMaxY(thumbFrame) - (NSHeight(window.contentView.bounds) - thumb.y * sy)) > 0.01)
                throw std::runtime_error("Native slider thumb drifted from the resolved slider geometry");
            check_ordinary_control_pixels(engine, scene, ui, first, foregroundLayer, baseLayer);
            check_drawable_retry(engine, scene, background, baseLayer);
            check_native_foreground_pixels(engine, scene, ui, first, label, foregroundLayer, baseLayer);
            check_overlapping_overlay_pixels(engine, scene, ui, first, window);
            ui_update_liquid_controls(scene, false, optics);
            engine.draw();
            if (materials(window.contentView).count || window.contentView.subviews.count)
                throw std::runtime_error("Leaving liquid material retained native control views");
            host.close();
        }
        terminate_glfw();
        std::cout << "Native glass material, paint order, tint, geometry, input and teardown passed\n";
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

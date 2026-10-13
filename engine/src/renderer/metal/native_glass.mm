#include "native_glass.h"
#import <AppKit/AppKit.h>
#import <Metal/Metal.h>
#import <QuartzCore/QuartzCore.h>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>

@interface VibranceNativeGlassPassThrough : NSView
@end
@implementation VibranceNativeGlassPassThrough
- (NSView*)hitTest:(NSPoint)point { return nil; }
@end

bool vibrance_macos_native_liquid_glass_available()
{
    if (@available(macOS 26.0, *)) return true;
    return false;
}
namespace vibrance::metal
{
struct NativeGlassSurface::Impl
{
    __weak NSWindow* window;
    __strong NSView* renderView;
    __strong VibranceNativeGlassPassThrough* container;
    __strong id<MTLDevice> device;
    __strong CAMetalLayer* base;
    struct Entry
    {
        __strong NSView* clip;
        __strong NSView* glass;
        __strong NSView* content;
        __strong NSView* foregroundClip;
        __strong NSView* overlay;
        __strong CAMetalLayer* layer;
        SystemBackdropColor tint;
        bool hasTint = false;
        bool material = true;
    };
    std::unordered_map<uint64_t, Entry> entries;
    std::vector<uint64_t> order;
    bool transaction = false, originalTransaction = false;
};
NativeGlassSurface::NativeGlassSurface(void* handle, MTL::Device* device, CA::MetalLayer* base)
    : impl(std::make_unique<Impl>())
{
    impl->window = (__bridge NSWindow*)handle;
    impl->device = (__bridge id<MTLDevice>)device;
    impl->base = (__bridge CAMetalLayer*)base;
}
NativeGlassSurface::~NativeGlassSurface()
{
    end_frame();
    if (impl->container) impl->base.presentsWithTransaction = impl->originalTransaction;
    [impl->container removeFromSuperview];
}
bool NativeGlassSurface::active() const { return !impl->entries.empty(); }
CA::MetalLayer* NativeGlassSurface::layer(size_t index) const
{
    return (__bridge CA::MetalLayer*)impl->entries.at(impl->order.at(index)).layer;
}
bool NativeGlassSurface::visible(size_t index) const
{
    const auto& entry = impl->entries.at(impl->order.at(index));
    return entry.layer && ![entry.overlay isHiddenOrHasHiddenAncestor] &&
        !NSIsEmptyRect(entry.overlay.visibleRect);
}
std::string NativeGlassSurface::describe(CA::MetalLayer* layer) const
{
    for (const auto& [identity, entry] : impl->entries)
        if ((__bridge CA::MetalLayer*)entry.layer == layer)
        {
            NSString* value = [NSString stringWithFormat:
                @"native surface %llu material=%d hidden=%d clip=%@ foreground=%@ visible=%@ opacity=%.3f pixels=%.0fx%.0f",
                identity, entry.material, [entry.overlay isHiddenOrHasHiddenAncestor],
                NSStringFromRect(entry.clip.frame), NSStringFromRect(entry.overlay.frame),
                NSStringFromRect(entry.overlay.visibleRect), entry.glass.alphaValue,
                entry.layer.drawableSize.width, entry.layer.drawableSize.height];
            return value.UTF8String;
        }
    return "base UI surface";
}
void NativeGlassSurface::begin_frame(std::span<const NativeGlassRegion> regions, uint32_t width, uint32_t height)
{
    if (regions.empty() && !impl->container) return;
    [CATransaction begin];
    [CATransaction setDisableActions:YES];
    impl->transaction = true;
    if (regions.empty())
    {
        impl->entries.clear();
        impl->order.clear();
        [impl->container removeFromSuperview];
        impl->container = nil;
        impl->renderView = nil;
        impl->base.presentsWithTransaction = impl->originalTransaction;
        return;
    }
    if (!impl->container)
    {
        NSMutableArray<NSView*>* pending = [NSMutableArray arrayWithObject:impl->window.contentView];
        while (pending.count)
        {
            NSView* view = pending.lastObject;
            [pending removeLastObject];
            if (view.layer == impl->base) { impl->renderView = view; break; }
            [pending addObjectsFromArray:view.subviews];
        }
        impl->container = [[VibranceNativeGlassPassThrough alloc] initWithFrame:impl->renderView.bounds];
        impl->container.wantsLayer = YES;
        impl->container.layer.backgroundColor = NSColor.clearColor.CGColor;
        impl->container.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
        [impl->renderView addSubview:impl->container];
        impl->originalTransaction = impl->base.presentsWithTransaction;
        impl->base.presentsWithTransaction = YES;
    }
    std::unordered_set<uint64_t> retained;
    std::vector<uint64_t> order;
    for (const auto& region : regions) { retained.insert(region.identity); order.push_back(region.identity); }
    std::erase_if(impl->entries, [&](auto& item) {
        if (retained.contains(item.first)) return false;
        [item.second.clip removeFromSuperview];
        return true;
    });
    const bool reorder = impl->order != order;
    impl->order = std::move(order);
    const NSRect bounds = impl->renderView.bounds;
    if (!NSEqualRects(impl->container.frame, bounds)) impl->container.frame = bounds;
    const CGFloat sx = NSWidth(bounds) / std::max(width, 1u);
    const CGFloat sy = NSHeight(bounds) / std::max(height, 1u);
    NSView* previous = nil;
    for (const auto& region : regions)
    {
        auto [item, added] = impl->entries.try_emplace(region.identity);
        auto& entry = item->second;
        if (!added && entry.material != region.material)
        {
            [entry.clip removeFromSuperview];
            entry = {};
            added = true;
        }
        if (added)
        {
            entry.material = region.material;
            entry.clip = [[NSView alloc] initWithFrame:bounds];
            entry.clip.wantsLayer = YES;
            entry.clip.layer.masksToBounds = YES;
            entry.content = [[NSView alloc] initWithFrame:NSZeroRect];
            entry.content.wantsLayer = YES;
            entry.content.layer.masksToBounds = YES;
            if (!region.material)
                entry.glass = [[VibranceNativeGlassPassThrough alloc] initWithFrame:NSZeroRect];
            else if (@available(macOS 26.0, *))
            {
                NSGlassEffectView* glass = [[NSGlassEffectView alloc] initWithFrame:NSZeroRect];
                glass.style = NSGlassEffectViewStyleRegular;
                glass.contentView = entry.content;
                entry.glass = glass;
            }
            else
            {
                NSVisualEffectView* glass = [[NSVisualEffectView alloc] initWithFrame:NSZeroRect];
                glass.material = NSVisualEffectMaterialPopover;
                glass.blendingMode = NSVisualEffectBlendingModeWithinWindow;
                glass.state = NSVisualEffectStateActive;
                [glass addSubview:entry.content];
                entry.glass = glass;
            }
            entry.glass.identifier = [NSString stringWithFormat:region.material ?
                @"vibrance-liquid-%llu" : @"vibrance-foreground-%llu", region.identity];
            entry.glass.wantsLayer = YES;
            [entry.clip addSubview:entry.glass];
        }
        if (reorder || added)
            [impl->container addSubview:entry.clip positioned:NSWindowAbove relativeTo:previous];
        previous = entry.clip;
        const auto points = [&](glm::vec4 rect) {
            return NSMakeRect(rect.x * sx, NSHeight(bounds) - (rect.y + rect.w) * sy,
                std::max(0.0f, rect.z) * sx, std::max(0.0f, rect.w) * sy);
        };
        const NSRect frame = points(region.rect);
        const NSRect clip = NSIntersectionRect(points(region.clip), bounds);
        if (!NSEqualRects(entry.clip.frame, clip)) entry.clip.frame = clip;
        const bool hidden = NSIsEmptyRect(NSIntersectionRect(frame, clip)) || region.opacity <= 0.001f;
        if (entry.clip.hidden != hidden) entry.clip.hidden = hidden;
        const NSRect local = NSOffsetRect(frame, -NSMinX(clip), -NSMinY(clip));
        if (!NSEqualRects(entry.glass.frame, local)) entry.glass.frame = local;
        if (entry.glass.alphaValue != region.opacity) entry.glass.alphaValue = std::clamp(region.opacity, 0.0f, 1.0f);
        const CGFloat radius = std::clamp(region.cornerRadius * std::min(sx, sy),
            0.0, std::min(NSWidth(frame), NSHeight(frame)) * 0.5);
        if (!region.material)
        {
            entry.glass.layer.backgroundColor = NSColor.clearColor.CGColor;
        }
        else if (@available(macOS 26.0, *))
        {
            NSGlassEffectView* glass = (NSGlassEffectView*)entry.glass;
            if (glass.cornerRadius != radius) glass.cornerRadius = radius;
            const auto& tint = region.tint;
            if (!entry.hasTint || entry.tint.red != tint.red || entry.tint.green != tint.green ||
                entry.tint.blue != tint.blue || entry.tint.alpha != tint.alpha)
            {
                glass.tintColor = tint.alpha > 0.001f ? [NSColor colorWithSRGBRed:tint.red
                    green:tint.green blue:tint.blue alpha:std::clamp(tint.alpha, 0.0f, 1.0f)] : nil;
                entry.tint = tint;
                entry.hasTint = true;
            }
        }
        else
        {
            entry.glass.layer.cornerRadius = radius;
            entry.glass.layer.masksToBounds = YES;
        }
        if (!NSEqualRects(entry.content.frame, entry.glass.bounds)) entry.content.frame = entry.glass.bounds;
        if (region.hasContent && !entry.layer)
        {
            // Foreground constants already include inherited opacity. Keeping
            // them beside the material prevents AppKit from multiplying the
            // ancestor's glass opacity into their pixels a second time.
            entry.foregroundClip = [[VibranceNativeGlassPassThrough alloc] initWithFrame:NSZeroRect];
            entry.foregroundClip.wantsLayer = YES;
            entry.foregroundClip.layer.masksToBounds = YES;
            entry.foregroundClip.identifier = [NSString stringWithFormat:@"vibrance-liquid-content-%llu", region.identity];
            [entry.clip addSubview:entry.foregroundClip positioned:NSWindowAbove relativeTo:entry.glass];
            entry.overlay = [[NSView alloc] initWithFrame:NSZeroRect];
            entry.overlay.wantsLayer = YES;
            entry.layer = [CAMetalLayer layer];
            entry.layer.device = impl->device;
            entry.layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            entry.layer.framebufferOnly = NO;
            entry.layer.opaque = NO;
            entry.layer.presentsWithTransaction = YES;
            entry.overlay.layer = entry.layer;
            [entry.foregroundClip addSubview:entry.overlay];
        }
        if (entry.foregroundClip && !NSEqualRects(entry.foregroundClip.frame, local))
            entry.foregroundClip.frame = local;
        if (entry.foregroundClip.layer.cornerRadius != radius)
            entry.foregroundClip.layer.cornerRadius = radius;
        if (entry.overlay.hidden == region.hasContent) entry.overlay.hidden = !region.hasContent;
        if (region.hasContent)
        {
            const NSRect crop = points(region.contentRect);
            const NSRect contentFrame = NSOffsetRect(crop, -NSMinX(frame), -NSMinY(frame));
            if (!NSEqualRects(entry.overlay.frame, contentFrame)) entry.overlay.frame = contentFrame;
            entry.layer.contentsScale = impl->window.backingScaleFactor;
            const CGSize pixels = CGSizeMake(region.contentRect.z, region.contentRect.w);
            if (!CGSizeEqualToSize(entry.layer.drawableSize, pixels)) entry.layer.drawableSize = pixels;
            entry.layer.displaySyncEnabled = impl->base.displaySyncEnabled;
        }
    }
}
void NativeGlassSurface::end_frame()
{
    if (!impl->transaction) return;
    impl->transaction = false;
    [CATransaction commit];
}
}

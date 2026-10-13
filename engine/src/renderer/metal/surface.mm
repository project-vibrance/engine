#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "device.h"
namespace vibrance::metal
{
CA::MetalLayer *attach_surface(void *handle, MTL::Device *device, bool transparent)
{
    NSWindow *window = static_cast<NSWindow *>(handle);
    if (!window || ![NSThread isMainThread])
        return nullptr;
    CAMetalLayer *layer = [[CAMetalLayer alloc] init];
    layer.device = (__bridge id<MTLDevice>)device;
    layer.pixelFormat = MTLPixelFormatBGRA8Unorm;
    layer.framebufferOnly = NO;
    layer.opaque = !transparent;
    layer.contentsScale = window.backingScaleFactor;
    layer.frame = window.contentView.bounds;
    layer.autoresizingMask = kCALayerWidthSizable | kCALayerHeightSizable;
    window.contentView.wantsLayer = YES;
    window.contentView.layer = layer;
    return reinterpret_cast<CA::MetalLayer *>(layer);
}
bool surface_presents_with_transaction(CA::MetalLayer *nativeLayer)
{
    CAMetalLayer *layer = reinterpret_cast<CAMetalLayer *>(nativeLayer);
    return layer && layer.presentsWithTransaction;
}
void detach_surface(void *handle, CA::MetalLayer *nativeLayer)
{
    NSWindow *window = static_cast<NSWindow *>(handle);
    CAMetalLayer *layer = reinterpret_cast<CAMetalLayer *>(nativeLayer);
    if (!window || !layer || !window.contentView)
        return;
    // A native material may wrap the GLFW render view while the window is
    // alive. Find the view that still owns the Metal layer before tearing it
    // down, instead of assuming it remains the window's direct content view.
    NSMutableArray<NSView *> *pending = [NSMutableArray arrayWithObject:window.contentView];
    while (pending.count)
    {
        NSView *view = pending.lastObject;
        [pending removeLastObject];
        if (view.layer == layer)
        {
            view.layer = nil;
            return;
        }
        [pending addObjectsFromArray:view.subviews];
    }
}
} // namespace vibrance::metal

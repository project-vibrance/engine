#if defined(__APPLE__)

#include <vibranceUI/platform/native_tray.h>

#import <AppKit/AppKit.h>

#include <algorithm>
#include <cmath>
#include <deque>
#include <utility>

namespace
{
    NSString* make_string(const std::string& value)
    {
        NSString* result = [NSString stringWithUTF8String:value.c_str()];
        return result ? result : @"";
    }

    struct MacNativeTrayState
    {
        std::deque<NativeTrayEvent> actions {};
        NativeTrayAnchor anchor {};
    };
}

@interface VibranceNativeTrayTarget : NSObject
{
    MacNativeTrayState* state;
}
- (instancetype)initWithState:(MacNativeTrayState*)value;
- (void)toggleCustomMenu:(id)sender;
- (void)selectCommand:(id)sender;
@end

@implementation VibranceNativeTrayTarget
- (instancetype)initWithState:(MacNativeTrayState*)value
{
    self = [super init];
    if (self)
    {
        state = value;
    }
    return self;
}

- (void)toggleCustomMenu:(id)sender
{
    NSStatusBarButton* button =
        [sender isKindOfClass:[NSStatusBarButton class]] ? sender : nil;
    NSWindow* window = [button window];
    NSArray<NSScreen*>* screens = [NSScreen screens];
    NSScreen* primary = [screens count] > 0 ? [screens objectAtIndex:0] : nil;
    if (button && window && primary)
    {
        const NSRect windowRect = [button convertRect:[button bounds] toView:nil];
        const NSRect screenRect = [window convertRectToScreen:windowRect];
        const CGFloat glfwOriginY = NSMaxY([primary frame]);
        state->anchor.x = static_cast<int>(std::lround(NSMinX(screenRect)));
        state->anchor.y = static_cast<int>(std::lround(
            glfwOriginY - NSMaxY(screenRect)));
        state->anchor.width = std::max(
            static_cast<int>(std::lround(NSWidth(screenRect))),
            1);
        state->anchor.height = std::max(
            static_cast<int>(std::lround(NSHeight(screenRect))),
            1);
        state->anchor.valid = true;
    }
    else
    {
        state->anchor = {};
    }
    state->actions.push_back(NativeTrayEvent { NativeTrayEventKind::eToggleCustomMenu });
}

- (void)selectCommand:(id)sender
{
    NSMenuItem* item = [sender isKindOfClass:[NSMenuItem class]] ? sender : nil;
    if (item && [item isEnabled])
        state->actions.push_back({ NativeTrayEventKind::eCommand,
            static_cast<std::uint32_t>([item tag]) });
}
@end

struct NativeTray::Impl
{
    explicit Impl(NativeTrayOptions options) :
        options(std::move(options))
    {
    }

    bool initialise()
    {
        if (statusItem)
        {
            return true;
        }

        if (!options.registerWithShell) return false;
        @autoreleasepool
        {
            statusItem = [[NSStatusBar systemStatusBar]
                statusItemWithLength:NSSquareStatusItemLength];
            if (!statusItem)
            {
                return false;
            }

            target = [[VibranceNativeTrayTarget alloc]
                initWithState:&state];
            NSStatusBarButton* button = [statusItem button];
            NSImage* image = options.iconPath.empty() ? nil :
                [[NSImage alloc] initWithContentsOfFile:make_string(options.iconPath.string())];
            if (image)
            {
                [image setSize:NSMakeSize(18, 18)];
                [button setImage:image];
                [image release];
            }
            else [button setTitle:make_string(options.fallbackText)];
            [button setToolTip:make_string(options.tooltip)];

            nativeMenu = [[NSMenu alloc] initWithTitle:make_string(options.tooltip)];
            [nativeMenu setAutoenablesItems:NO];
            for (const auto& entry : options.menu)
            {
                if (entry.separator)
                {
                    [nativeMenu addItem:[NSMenuItem separatorItem]];
                    continue;
                }
                NSMenuItem* item = [[NSMenuItem alloc]
                    initWithTitle:make_string(entry.label)
                    action:@selector(selectCommand:)
                    keyEquivalent:make_string(entry.shortcut)];
                [item setTag:static_cast<NSInteger>(entry.command)];
                [item setEnabled:entry.enabled];
                [item setTarget:target];
                [nativeMenu addItem:item];
                [item release];
            }

            apply_menu_mode();
            return true;
        }
    }

    void apply_menu_mode()
    {
        if (!statusItem)
        {
            return;
        }
        NSStatusBarButton* button = [statusItem button];
        if (options.preferCustomMenu)
        {
            [statusItem setMenu:nil];
            [button setTarget:target];
            [button setAction:@selector(toggleCustomMenu:)];
        }
        else
        {
            [button setTarget:nil];
            [button setAction:nil];
            [statusItem setMenu:nativeMenu];
        }
    }

    void shutdown()
    {
        @autoreleasepool
        {
            if (statusItem)
            {
                [statusItem setMenu:nil];
                [[NSStatusBar systemStatusBar]
                    removeStatusItem:statusItem];
                statusItem = nil;
            }
            if (nativeMenu)
            {
                [nativeMenu release];
                nativeMenu = nil;
            }
            if (target)
            {
                [target release];
                target = nil;
            }
            state.actions.clear();
            state.anchor = {};
        }
    }

    NativeTrayOptions options {};
    MacNativeTrayState state {};
    NSStatusItem* statusItem = nil;
    NSMenu* nativeMenu = nil;
    VibranceNativeTrayTarget* target = nil;
};

NativeTray::NativeTray(NativeTrayOptions options) :
    impl(std::make_unique<Impl>(std::move(options)))
{
}

NativeTray::~NativeTray()
{
    shutdown();
}

bool NativeTray::initialise()
{
    return impl && impl->initialise();
}

void NativeTray::shutdown()
{
    if (impl)
    {
        impl->shutdown();
    }
}

bool NativeTray::available() const
{
    return impl && impl->statusItem != nil;
}

NativeTrayEvent NativeTray::take_event()
{
    if (!impl || impl->state.actions.empty())
    {
        return NativeTrayEvent { NativeTrayEventKind::eNone };
    }
    const NativeTrayEvent action = impl->state.actions.front();
    impl->state.actions.pop_front();
    return action;
}

NativeTrayAnchor NativeTray::menu_anchor() const
{
    return impl ? impl->state.anchor : NativeTrayAnchor {};
}

void NativeTray::set_custom_menu_enabled(bool enabled)
{
    if (!impl)
    {
        return;
    }
    impl->options.preferCustomMenu = enabled;
    impl->apply_menu_mode();
}

bool NativeTray::custom_menu_enabled() const
{
    return impl && impl->options.preferCustomMenu;
}

void NativeTray::show_native_menu()
{
    if (impl && impl->statusItem && impl->nativeMenu)
    {
        [impl->statusItem popUpStatusItemMenu:impl->nativeMenu];
    }
}

#endif

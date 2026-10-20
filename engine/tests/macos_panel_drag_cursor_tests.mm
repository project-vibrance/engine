#include <vibranceUI/glfw/window.h>
#include "../src/glfw/cursor_macos.h"
#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include <iostream>
#include <stdexcept>

namespace
{
GLFWwindow* draggedWindow = nullptr;
GLFWwindow* otherWindow = nullptr;
GlfwCursorSet cursors {};
bool cancelDrag = false;
unsigned trackedDrags = 0;

void require(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

void send_cursor_update(NSWindow* window)
{
    NSEvent* event = [NSEvent otherEventWithType:NSEventTypeApplicationDefined
        location:NSZeroPoint modifierFlags:0 timestamp:0 windowNumber:window.windowNumber
        context:nil subtype:0 data1:0 data2:0];
    [window.contentView cursorUpdate:event];
}

void track_drag(id window, SEL, NSEvent*)
{
    ++trackedDrags;
    require(![(NSWindow*)window areCursorRectsEnabled], "AppKit cursor rectangles must be suspended during dragging");
    require(NSCursor.currentCursor == NSCursor.closedHandCursor, "Native dragging must start with the closed-hand cursor");
    // Exercise competing hover updates inside AppKit's nested tracking loop.
    // These previously changed the cursor back to an arrow or pointing hand.
    for (unsigned sample = 0; sample < 12; ++sample)
    {
        clear_glfw_cursor(draggedWindow);
        require(NSCursor.currentCursor == NSCursor.closedHandCursor, "Clearing hover replaced the drag cursor");
        set_glfw_cursor_for_kind(draggedWindow, cursors, UiCursorKind::ePointer);
        require(NSCursor.currentCursor == NSCursor.closedHandCursor, "Pointer hover replaced the drag cursor");
        set_glfw_cursor_for_kind(draggedWindow, cursors, UiCursorKind::eText);
        require(NSCursor.currentCursor == NSCursor.closedHandCursor, "Text hover replaced the drag cursor");
        clear_glfw_cursor(otherWindow);
        set_glfw_cursor_for_kind(otherWindow, cursors, UiCursorKind::ePointer);
        require(NSCursor.currentCursor == NSCursor.closedHandCursor, "A background window replaced the drag cursor");
    }
    if (cancelDrag) @throw [NSException exceptionWithName:@"VibranceTestDragCancelled" reason:nil userInfo:nil];
}

struct DragHook
{
    Class type;
    IMP original;
    explicit DragHook(NSWindow* window) : type(object_getClass(window)),
        original(class_getMethodImplementation(type, @selector(performWindowDragWithEvent:)))
    {
        class_replaceMethod(type, @selector(performWindowDragWithEvent:), reinterpret_cast<IMP>(track_drag), "v@:@");
    }
    ~DragHook() { class_replaceMethod(type, @selector(performWindowDragWithEvent:), original, "v@:@"); }
};

NSInteger hoveredWindowNumber = 0;
NSPoint screenMouseLocation;
NSPoint test_mouse_location(id, SEL) { return NSMakePoint(160.0, 80.0); }
NSPoint test_screen_mouse_location(id, SEL) { return screenMouseLocation; }
NSInteger test_window_at_point(id, SEL, NSPoint, NSInteger) { return hoveredWindowNumber; }

struct OverlappingWindows
{
    Class windowType;
    Class metaType = object_getClass(NSWindow.class);
    Class eventMetaType = object_getClass(NSEvent.class);
    IMP mouseLocation;
    IMP windowAtPoint;
    IMP screenMouse;
    explicit OverlappingWindows(NSWindow* window) : windowType(object_getClass(window)),
        mouseLocation(class_getMethodImplementation(windowType, @selector(mouseLocationOutsideOfEventStream))),
        windowAtPoint(class_getMethodImplementation(metaType, @selector(windowNumberAtPoint:belowWindowWithWindowNumber:))),
        screenMouse(class_getMethodImplementation(eventMetaType, @selector(mouseLocation)))
    {
        // Simulate overlapping windows without moving the user's pointer or
        // depending on which app happens to be beneath it during the test.
        Method mouseMethod = class_getInstanceMethod(windowType, @selector(mouseLocationOutsideOfEventStream));
        Method hitMethod = class_getClassMethod(NSWindow.class, @selector(windowNumberAtPoint:belowWindowWithWindowNumber:));
        screenMouseLocation = [window convertPointToScreen:test_mouse_location(nil, nullptr)];
        class_replaceMethod(windowType, @selector(mouseLocationOutsideOfEventStream),
            reinterpret_cast<IMP>(test_mouse_location), method_getTypeEncoding(mouseMethod));
        class_replaceMethod(metaType, @selector(windowNumberAtPoint:belowWindowWithWindowNumber:),
            reinterpret_cast<IMP>(test_window_at_point), method_getTypeEncoding(hitMethod));
        class_replaceMethod(eventMetaType, @selector(mouseLocation),
            reinterpret_cast<IMP>(test_screen_mouse_location),
            method_getTypeEncoding(class_getClassMethod(NSEvent.class, @selector(mouseLocation))));
    }
    ~OverlappingWindows()
    {
        class_replaceMethod(windowType, @selector(mouseLocationOutsideOfEventStream), mouseLocation,
            method_getTypeEncoding(class_getInstanceMethod(windowType, @selector(mouseLocationOutsideOfEventStream))));
        class_replaceMethod(metaType, @selector(windowNumberAtPoint:belowWindowWithWindowNumber:), windowAtPoint,
            method_getTypeEncoding(class_getClassMethod(NSWindow.class, @selector(windowNumberAtPoint:belowWindowWithWindowNumber:))));
        class_replaceMethod(eventMetaType, @selector(mouseLocation), screenMouse,
            method_getTypeEncoding(class_getClassMethod(NSEvent.class, @selector(mouseLocation))));
    }
};

void test_overlapping_control_cursors(NSWindow* native)
{
    NSWindow* other = (__bridge NSWindow*)glfw_native_window_handle(otherWindow);
    hoveredWindowNumber = native.windowNumber;
    // Confirm the fixture exercises Cocoa's global-cursor bug in raw GLFW.
    glfwSetCursor(draggedWindow, cursors.text);
    require(NSCursor.currentCursor == NSCursor.IBeamCursor, "The cursor fixture must cover the active window");
    glfwSetCursor(otherWindow, nullptr);
    require(NSCursor.currentCursor == NSCursor.arrowCursor, "The fixture must reproduce the competing background cursor");

    for (const UiCursorKind kind : {UiCursorKind::eText, UiCursorKind::ePointer,
             UiCursorKind::eResizeEw, UiCursorKind::eResizeNs, UiCursorKind::eDefault})
    {
        set_glfw_cursor_for_kind(draggedWindow, cursors, kind);
        NSCursor* expected = NSCursor.currentCursor;
        for (unsigned sample = 0; sample < 12; ++sample)
        {
            clear_glfw_cursor(otherWindow);
            require(NSCursor.currentCursor == expected, "Background clearing replaced the active control cursor");
            set_glfw_cursor_for_kind(otherWindow, cursors, UiCursorKind::ePointer);
            require(NSCursor.currentCursor == expected, "Background hover replaced the active control cursor");
            send_cursor_update(native);
            require(NSCursor.currentCursor == expected, "AppKit lost the active control cursor");
        }
    }
    hoveredWindowNumber = other.windowNumber;
    set_glfw_cursor_for_kind(otherWindow, cursors, UiCursorKind::eText);
    require(NSCursor.currentCursor == NSCursor.IBeamCursor, "Cursor ownership did not follow the front window");
    clear_glfw_cursor(draggedWindow);
    require(NSCursor.currentCursor == NSCursor.IBeamCursor, "Previous owner overwrote the new front window");
    clear_glfw_cursor(otherWindow);
    require(NSCursor.currentCursor == NSCursor.arrowCursor, "The active window could not restore the arrow");

    entt::registry registry;
    UiInputState state;
    const auto text = registry.create();
    registry.emplace<TextInputComponent>(text);
    const auto slider = registry.create();
    registry.emplace<SliderInputComponent>(slider);
    const auto scrollbar = registry.create();
    registry.emplace<ScrollBarInputComponent>(scrollbar);
    const auto button = registry.create();
    registry.emplace<ButtonInputComponent>(button);
    for (const auto owner : {text, slider, scrollbar, button})
    {
        state.pointerInputCapture = owner;
        const auto kind = macos_captured_cursor_kind(registry, state);
        require(kind == (owner == text ? UiCursorKind::eText : UiCursorKind::ePointer),
            "A captured control must retain its cursor outside its hit region");
    }
    state.clear_capture();
    require(!macos_captured_cursor_kind(registry, state), "Release must restore ordinary hover cursor selection");
    state.pointerInputCapture = text;
    registry.destroy(text);
    require(!macos_captured_cursor_kind(registry, state), "Removed controls must not keep the cursor captured");
    state.clear_capture();
    state.resizingPanel = registry.create();
    state.activeResizeEdges = ePanelResizeLeft | ePanelResizeTop;
    require(macos_captured_cursor_kind(registry, state) == UiCursorKind::eResizeNwse,
        "Panel resizing must retain the captured edge cursor");
    state.clear_capture();
    state.draggedPanel = registry.create();
    require(macos_captured_cursor_kind(registry, state) == UiCursorKind::ePointer,
        "Scene panel dragging must retain its drag cursor");
}
}

int main()
{
    @autoreleasepool
    {
        try
        {
            glfwInitHint(GLFW_COCOA_MENUBAR, GLFW_FALSE);
            if (!glfwInit()) return 77;
            struct RestoreCursor
            {
                NSCursor* cursor;
                ~RestoreCursor() { [cursor set]; }
            } restoreCursor {NSCursor.currentCursor};
            GlfwWindowCreateInfo options;
            options.name = "macOS panel drag cursor test";
            options.width = 320;
            options.height = 160;
            options.decorated = false;
            options.focusOnShow = false;
            draggedWindow = build_glfw_window(options);
            require(draggedWindow != nullptr, "Could not create the drag fixture window");
            struct Cleanup
            {
                ~Cleanup()
                {
                    destroy_glfw_standard_cursors(cursors);
                    destroy_glfw_window(otherWindow);
                    destroy_glfw_window(draggedWindow);
                    terminate_glfw();
                }
            } cleanup;
            cursors = create_glfw_standard_cursors();
            otherWindow = build_glfw_window(options);
            require(otherWindow != nullptr, "Could not create the competing hover fixture window");
            NSWindow* native = (__bridge NSWindow*)glfw_native_window_handle(draggedWindow);
            NSWindow* other = (__bridge NSWindow*)glfw_native_window_handle(otherWindow);
            [other setFrameOrigin:native.frame.origin];
            OverlappingWindows overlap(native);
            hoveredWindowNumber = other.windowNumber;
            set_glfw_cursor_for_kind(otherWindow, cursors, UiCursorKind::eText);
            hoveredWindowNumber = native.windowNumber;
            DragHook hook(native);
            for (BOOL enabled : {YES, NO})
            {
                if (enabled) [native enableCursorRects];
                else [native disableCursorRects];
                for (bool cancelled : {false, true, false})
                {
                    cancelDrag = cancelled;
                    [NSCursor.pointingHandCursor set];
                    bool cancellationCaught = false;
                    @try { require(begin_glfw_native_window_drag(draggedWindow), "Native drag did not start"); }
                    @catch (NSException* exception)
                    {
                        if (![exception.name isEqualToString:@"VibranceTestDragCancelled"])
                            throw std::runtime_error(std::string(exception.name.UTF8String) + ": " +
                                (exception.reason.UTF8String ?: ""));
                        cancellationCaught = true;
                    }
                    require(cancellationCaught == cancelled, "The cancellation fixture did not run");
                    require(native.areCursorRectsEnabled == enabled, "Dragging changed the previous cursor rectangle state");
                    require(NSCursor.currentCursor == NSCursor.pointingHandCursor, "Dragging did not restore the previous hover cursor");
                    send_cursor_update(other);
                    require(NSCursor.currentCursor == NSCursor.IBeamCursor, "Background hover changed its stored cursor during dragging");
                    set_glfw_cursor_for_kind(draggedWindow, cursors, UiCursorKind::eText);
                    // Deliver GLFW's usual cursor-update callback explicitly;
                    // this does not depend on where the real pointer happens
                    // to be, and never warps the user's mouse.
                    send_cursor_update(native);
                    require(NSCursor.currentCursor == NSCursor.IBeamCursor, "Text hover remained locked after dragging");
                    clear_glfw_cursor(draggedWindow);
                    send_cursor_update(native);
                    require(NSCursor.currentCursor == NSCursor.arrowCursor, "Default hover remained locked after dragging");
                }
            }
            require(trackedDrags == 6, "Not all drag and cancellation paths ran");
            [native enableCursorRects];
            test_overlapping_control_cursors(native);
            std::cout << "macOS drag cursor ownership, hover competition and restoration checks passed\n";
        }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
}

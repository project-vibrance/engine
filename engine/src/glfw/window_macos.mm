#include <vibranceUI/glfw/window.h>

#if defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>

namespace
{
    NSEvent* current_left_mouse_down(NSWindow* window)
    {
        NSEvent* event = [NSApp currentEvent];
        if (event && [event type] == NSEventTypeLeftMouseDown && [event window] == window)
        {
            return event;
        }

        const NSPoint mouseLocation = [window mouseLocationOutsideOfEventStream];
        return [NSEvent mouseEventWithType:NSEventTypeLeftMouseDown
                                  location:mouseLocation
                             modifierFlags:event ? [event modifierFlags] : 0
                                 timestamp:[[NSProcessInfo processInfo] systemUptime]
                              windowNumber:[window windowNumber]
                                   context:nil
                               eventNumber:event ? [event eventNumber] : 0
                                clickCount:1
                                  pressure:1.0];
    }
}

bool begin_glfw_native_window_drag(GLFWwindow* window)
{
    if (!window)
    {
        return false;
    }

    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    if (!nativeWindow)
    {
        return false;
    }

    [nativeWindow setMovable:YES];
    NSEvent* event = current_left_mouse_down(nativeWindow);
    if (!event)
    {
        return false;
    }

    [nativeWindow performWindowDragWithEvent:event];
    return true;
}
#endif

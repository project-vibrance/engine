#include <vibranceUI/glfw/window.h>

#if defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#include <GLFW/glfw3native.h>
#import <Cocoa/Cocoa.h>

#include <array>
#include <string>

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

std::string glfw_monitor_identifier(GLFWmonitor* monitor)
{
    if (!monitor)
    {
        return {};
    }
    const CGDirectDisplayID displayId = glfwGetCocoaMonitor(monitor);
    CFUUIDRef uuid = CGDisplayCreateUUIDFromDisplayID(displayId);
    if (!uuid)
    {
        return "macos:" + std::to_string(displayId);
    }
    CFStringRef value = CFUUIDCreateString(kCFAllocatorDefault, uuid);
    CFRelease(uuid);
    if (!value)
    {
        return "macos:" + std::to_string(displayId);
    }
    std::array<char, 128> buffer {};
    const bool converted = CFStringGetCString(
        value,
        buffer.data(),
        static_cast<CFIndex>(buffer.size()),
        kCFStringEncodingUTF8);
    CFRelease(value);
    return converted ?
        "macos:" + std::string(buffer.data()) :
        "macos:" + std::to_string(displayId);
}

bool apply_glfw_window_placement(
    GLFWwindow* window,
    const GlfwWindowPlacement& placement)
{
    if (!window || placement.size.x <= 0 || placement.size.y <= 0)
    {
        return false;
    }
    glfwSetWindowAttrib(
        window,
        GLFW_DECORATED,
        placement.decorated ? GLFW_TRUE : GLFW_FALSE);
    glfwSetWindowPos(window, placement.position.x, placement.position.y);
    glfwSetWindowSize(window, placement.size.x, placement.size.y);
    glfwSetWindowAttrib(
        window,
        GLFW_FLOATING,
        placement.alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);

    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    if (!nativeWindow)
    {
        return false;
    }
    [nativeWindow setLevel:(placement.alwaysOnTop ?
        NSStatusWindowLevel : NSNormalWindowLevel)];
    [nativeWindow setCollectionBehavior:
        NSWindowCollectionBehaviorCanJoinAllSpaces |
        NSWindowCollectionBehaviorFullScreenAuxiliary];
    return true;
}

void set_glfw_window_application_presence(
    GLFWwindow* window,
    bool showInTaskbar,
    bool showInAltTab)
{
    (void)showInTaskbar;
    if (!window)
    {
        return;
    }

    NSWindow* nativeWindow = glfwGetCocoaWindow(window);
    if (!nativeWindow)
    {
        return;
    }

    NSWindowCollectionBehavior behavior =
        [nativeWindow collectionBehavior];
    behavior &= ~(
        NSWindowCollectionBehaviorParticipatesInCycle |
        NSWindowCollectionBehaviorIgnoresCycle);
    behavior |= showInAltTab ?
        NSWindowCollectionBehaviorParticipatesInCycle :
        NSWindowCollectionBehaviorIgnoresCycle;
    [nativeWindow setCollectionBehavior:behavior];
    [nativeWindow setExcludedFromWindowsMenu:!showInAltTab];
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

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "window_metadata.h"

#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
#define GLFW_EXPOSE_NATIVE_X11
#include <X11/Xatom.h>
#endif
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_METADATA
#define GLFW_EXPOSE_NATIVE_WAYLAND
#include "plasma-shell.h"
#endif
#include <GLFW/glfw3native.h>

#include <algorithm>
#include <array>
#include <string_view>
#include <unordered_map>
#include <vector>

#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_ACTIONS
extern "C" int glfwVibranceWaylandWindowAction(GLFWwindow* window, int horizontal, int vertical);
extern "C" int glfwVibranceWaylandOverlay(GLFWwindow* window, int x, int y, int above);
extern "C" int glfwVibranceWaylandOverlayPosition(GLFWwindow* window, int* x, int* y, int set);
extern "C" int glfwVibranceWaylandOverlayAbove(GLFWwindow* window, int above);
#endif

namespace
{
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_METADATA
    struct WaylandWindow
    {
        wl_surface* surface = nullptr;
        org_kde_plasma_surface* metadata = nullptr;
        std::optional<glm::ivec2> position;
        bool fixedPlacement = false;
        bool skipTaskbar = false;
        bool skipSwitcher = false;
    };

    org_kde_plasma_shell* plasmaShell = nullptr;
    bool plasmaShellProbed = false;
    std::unordered_map<GLFWwindow*, WaylandWindow> waylandWindows;

    org_kde_plasma_shell* find_plasma_shell(wl_display* display)
    {
        if (plasmaShellProbed) return plasmaShell;
        plasmaShellProbed = true;
        auto* queue = wl_display_create_queue(display);
        if (!queue) return nullptr;
        auto* wrapper = wl_proxy_create_wrapper(display);
        if (wrapper)
        {
            auto* proxy = static_cast<wl_proxy*>(wrapper);
            wl_proxy_set_queue(proxy, queue);
            auto* registry = wl_display_get_registry(static_cast<wl_display*>(wrapper));
            wl_proxy_wrapper_destroy(wrapper);
            if (registry)
            {
                static constexpr wl_registry_listener listener {
                    [](void*, wl_registry* registry, std::uint32_t name,
                        const char* interface, std::uint32_t version) {
                        if (!plasmaShell && std::string_view(interface) == org_kde_plasma_shell_interface.name)
                            plasmaShell = static_cast<org_kde_plasma_shell*>(wl_registry_bind(registry,
                                name, &org_kde_plasma_shell_interface, std::min(version, 5u)));
                    },
                    [](void*, wl_registry*, std::uint32_t) {}
                };
                wl_registry_add_listener(registry, &listener, nullptr);
                wl_display_roundtrip_queue(display, queue);
                wl_registry_destroy(registry);
            }
        }
        if (plasmaShell) wl_proxy_set_queue(reinterpret_cast<wl_proxy*>(plasmaShell), nullptr);
        wl_event_queue_destroy(queue);
        return plasmaShell;
    }

    WaylandWindow* wayland_window(GLFWwindow* window)
    {
        if (!window) return nullptr;
#ifdef GLFW_PLATFORM_WAYLAND
        if (glfwGetPlatform() != GLFW_PLATFORM_WAYLAND) return nullptr;
#endif
        const auto existing = waylandWindows.find(window);
        if (existing != waylandWindows.end()) return &existing->second;
        auto* display = glfwGetWaylandDisplay();
        auto* surface = glfwGetWaylandWindow(window);
        if (!display || !surface || !find_plasma_shell(display)) return nullptr;
        auto* metadata = org_kde_plasma_shell_get_surface(plasmaShell, surface);
        if (!metadata) return nullptr;
        return &waylandWindows.emplace(window, WaylandWindow { surface, metadata }).first->second;
    }
#endif

#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
    void set_x11_presence(GLFWwindow* window, bool showInTaskbar, bool showInAltTab)
    {
        Display* display = glfwGetX11Display();
        const Window handle = glfwGetX11Window(window);
        if (!display || !handle) return;
        const Atom property = XInternAtom(display, "_NET_WM_STATE", False);
        const std::array<Atom, 2> flags {
            XInternAtom(display, "_NET_WM_STATE_SKIP_TASKBAR", False),
            XInternAtom(display, "_KDE_NET_WM_STATE_SKIP_SWITCHER", False)
        };
        const std::array<bool, 2> skip { !showInTaskbar, !showInAltTab };
        if (glfwGetWindowAttrib(window, GLFW_VISIBLE))
        {
            for (std::size_t index = 0; index < flags.size(); ++index)
            {
                XEvent event {};
                event.xclient.type = ClientMessage;
                event.xclient.window = handle;
                event.xclient.message_type = property;
                event.xclient.format = 32;
                event.xclient.data.l[0] = skip[index] ? 1 : 0;
                event.xclient.data.l[1] = static_cast<long>(flags[index]);
                event.xclient.data.l[3] = 1;
                XSendEvent(display, DefaultRootWindow(display), False,
                    SubstructureRedirectMask | SubstructureNotifyMask, &event);
            }
        }
        else
        {
            Atom type = None;
            int format = 0;
            unsigned long count = 0;
            unsigned long remaining = 0;
            unsigned char* data = nullptr;
            const auto result = XGetWindowProperty(display, handle, property, 0, 256,
                False, XA_ATOM, &type, &format, &count, &remaining, &data);
            std::vector<Atom> states;
            if (result == Success && data && type == XA_ATOM && format == 32)
            {
                const auto* atoms = reinterpret_cast<const Atom*>(data);
                states.assign(atoms, atoms + count);
            }
            if (data) XFree(data);
            if (result != Success || remaining != 0) return;
            for (std::size_t index = 0; index < flags.size(); ++index)
            {
                std::erase(states, flags[index]);
                if (skip[index]) states.push_back(flags[index]);
            }
            XChangeProperty(display, handle, property, XA_ATOM, 32, PropModeReplace,
                reinterpret_cast<const unsigned char*>(states.data()), static_cast<int>(states.size()));
        }
        XFlush(display);
    }
#endif
}

void vibrance::unix_window::configure([[maybe_unused]] GLFWwindow* window,
    [[maybe_unused]] const GlfwWindowCreateInfo& options, [[maybe_unused]] bool useDesktopLayer)
{
    if (!options.position || options.focusOnShow || options.showInTaskbar || options.showInAltTab)
        return;
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_ACTIONS
    if (useDesktopLayer && !options.decorated && glfwVibranceWaylandOverlay(
            window, options.position->x, options.position->y, options.alwaysOnTop))
        return;
#endif
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_METADATA
    if (auto* native = wayland_window(window)) native->fixedPlacement = true;
#endif
}

void vibrance::unix_window::set_presence([[maybe_unused]] GLFWwindow* window,
    [[maybe_unused]] bool showInTaskbar, [[maybe_unused]] bool showInAltTab)
{
#ifdef VIBRANCE_HAS_X11_WINDOW_SHAPE
#ifdef GLFW_PLATFORM_X11
    if (glfwGetPlatform() == GLFW_PLATFORM_X11)
#endif
        set_x11_presence(window, showInTaskbar, showInAltTab);
#endif
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_ACTIONS
    int x, y;
    if (glfwVibranceWaylandOverlayPosition(window, &x, &y, false)) return;
#endif
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_METADATA
    if (showInTaskbar && showInAltTab && !waylandWindows.contains(window)) return;
    auto* native = wayland_window(window);
    if (!native) return;
    const auto version = org_kde_plasma_surface_get_version(native->metadata);
    if (native->skipTaskbar != !showInTaskbar && version >= ORG_KDE_PLASMA_SURFACE_SET_SKIP_TASKBAR_SINCE_VERSION)
    {
        native->skipTaskbar = !showInTaskbar;
        org_kde_plasma_surface_set_skip_taskbar(native->metadata, native->skipTaskbar);
    }
    if (native->skipSwitcher != !showInAltTab && version >= ORG_KDE_PLASMA_SURFACE_SET_SKIP_SWITCHER_SINCE_VERSION)
    {
        native->skipSwitcher = !showInAltTab;
        org_kde_plasma_surface_set_skip_switcher(native->metadata, native->skipSwitcher);
    }
#endif
}

void vibrance::unix_window::set_position([[maybe_unused]] GLFWwindow* window,
    [[maybe_unused]] glm::ivec2 position)
{
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_ACTIONS
    if (glfwVibranceWaylandOverlayPosition(window, &position.x, &position.y, true)) return;
#endif
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_METADATA
    auto* native = wayland_window(window);
    if (!native || native->position == position) return;
    native->position = position;
    org_kde_plasma_surface_set_position(native->metadata, position.x, position.y);
    if (glfwGetWindowAttrib(window, GLFW_VISIBLE))
    {
        wl_surface_commit(native->surface);
        wl_display_flush(glfwGetWaylandDisplay());
    }
#endif
}

bool vibrance::unix_window::fixed_position([[maybe_unused]] GLFWwindow* window,
    [[maybe_unused]] int& x, [[maybe_unused]] int& y)
{
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_ACTIONS
    if (glfwVibranceWaylandOverlayPosition(window, &x, &y, false)) return true;
#endif
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_METADATA
    const auto found = waylandWindows.find(window);
    if (found != waylandWindows.end() && found->second.fixedPlacement && found->second.position)
    {
        x = found->second.position->x;
        y = found->second.position->y;
        return true;
    }
#endif
    return false;
}

void vibrance::unix_window::set_above([[maybe_unused]] GLFWwindow* window, [[maybe_unused]] bool above)
{
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_ACTIONS
    glfwVibranceWaylandOverlayAbove(window, above);
#endif
}

bool vibrance::unix_window::begin_move([[maybe_unused]] GLFWwindow* window)
{
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_ACTIONS
    return glfwVibranceWaylandWindowAction(window, 0, 0) == GLFW_TRUE;
#else
    return false;
#endif
}

bool vibrance::unix_window::begin_resize([[maybe_unused]] GLFWwindow* window,
    [[maybe_unused]] int horizontal, [[maybe_unused]] int vertical)
{
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_ACTIONS
    return glfwVibranceWaylandWindowAction(window, horizontal, vertical) == GLFW_TRUE;
#else
    return false;
#endif
}

void vibrance::unix_window::forget([[maybe_unused]] GLFWwindow* window)
{
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_METADATA
    const auto found = waylandWindows.find(window);
    if (found != waylandWindows.end())
    {
        org_kde_plasma_surface_destroy(found->second.metadata);
        waylandWindows.erase(found);
    }
#endif
}

void vibrance::unix_window::shutdown()
{
#ifdef VIBRANCE_HAS_WAYLAND_WINDOW_METADATA
    for (const auto& [window, native] : waylandWindows)
        org_kde_plasma_surface_destroy(native.metadata);
    waylandWindows.clear();
    if (plasmaShell) org_kde_plasma_shell_destroy(plasmaShell);
    plasmaShell = nullptr;
    plasmaShellProbed = false;
#endif
}

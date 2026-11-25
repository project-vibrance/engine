// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "vibrance-layer-shell-client.h"
#include "vibrance-layer-shell-code.h"

typedef struct VibranceOverlay
{
    _GLFWwindow* window;
    struct VibranceOverlay* next;
    struct zwlr_layer_shell_v1* shell;
    struct zwlr_layer_surface_v1* layer;
    _GLFWmonitor* monitor;
    int x, y;
    int width, height;
    GLFWbool above;
} VibranceOverlay;

static VibranceOverlay* vibranceOverlays;

static VibranceOverlay* vibranceFindOverlay(_GLFWwindow* window)
{
    for (VibranceOverlay* overlay = vibranceOverlays; overlay; overlay = overlay->next)
    {
        if (overlay->window == window)
            return overlay;
    }
    return NULL;
}

static void vibranceFindLayerShell(void* data, struct wl_registry* registry,
                                   uint32_t name, const char* interface, uint32_t version)
{
    if (version >= 2 && strcmp(interface, zwlr_layer_shell_v1_interface.name) == 0)
        *(struct zwlr_layer_shell_v1**) data =
            wl_registry_bind(registry, name, &zwlr_layer_shell_v1_interface, _glfw_min(version, 3));
}

static void vibranceDestroyLayerShell(struct zwlr_layer_shell_v1* shell)
{
    if (zwlr_layer_shell_v1_get_version(shell) >= ZWLR_LAYER_SHELL_V1_DESTROY_SINCE_VERSION)
        zwlr_layer_shell_v1_destroy(shell);
    else
        wl_proxy_destroy((struct wl_proxy*) shell);
}

static void vibranceIgnoreGlobalRemoval(void* data, struct wl_registry* registry, uint32_t name)
{
}

static void vibranceOverlayConfigured(void* data, struct zwlr_layer_surface_v1* layer,
                                      uint32_t serial, uint32_t width, uint32_t height)
{
    VibranceOverlay* overlay = data;
    _GLFWwindow* window = overlay->window;
    zwlr_layer_surface_v1_ack_configure(layer, serial);
    window->wl.visible = GLFW_TRUE;
    if (resizeWindow(window, width ? (int) width : overlay->width,
                             height ? (int) height : overlay->height))
        _glfwInputWindowSize(window, window->wl.width, window->wl.height);
    _glfwInputWindowDamage(window);
}

static void vibranceOverlayClosed(void* data, struct zwlr_layer_surface_v1* layer)
{
    VibranceOverlay* overlay = data;
    zwlr_layer_surface_v1_destroy(layer);
    overlay->layer = NULL;
    overlay->window->wl.visible = GLFW_FALSE;
    _glfwInputWindowCloseRequest(overlay->window);
}

static uint32_t vibranceOverlayLayer(const VibranceOverlay* overlay)
{
    return overlay->above ? ZWLR_LAYER_SHELL_V1_LAYER_TOP : ZWLR_LAYER_SHELL_V1_LAYER_BOTTOM;
}

static _GLFWmonitor* vibranceOverlayMonitor(int x, int y)
{
    for (int index = 0; index < _glfw.monitorCount; index++)
    {
        _GLFWmonitor* monitor = _glfw.monitors[index];
        const GLFWvidmode* mode = &monitor->modes[monitor->wl.currentMode];
        if (x >= monitor->wl.x && x - monitor->wl.x < mode->width &&
            y >= monitor->wl.y && y - monitor->wl.y < mode->height)
            return monitor;
    }
    return _glfw.monitorCount ? _glfw.monitors[0] : NULL;
}

static void vibranceCommitOverlay(VibranceOverlay* overlay)
{
    _GLFWmonitor* monitor = overlay->monitor;
    zwlr_layer_surface_v1_set_size(overlay->layer, overlay->width, overlay->height);
    zwlr_layer_surface_v1_set_margin(overlay->layer,
        overlay->y - monitor->wl.y, 0, 0, overlay->x - monitor->wl.x);
    wl_surface_commit(overlay->window->wl.surface);
    wl_display_flush(_glfw.wl.display);
}

static GLFWbool vibranceShowOverlay(VibranceOverlay* overlay)
{
    if (overlay->layer)
        return GLFW_TRUE;
    overlay->monitor = vibranceOverlayMonitor(overlay->x, overlay->y);
    if (!overlay->monitor)
        return GLFW_FALSE;
    _GLFWwindow* window = overlay->window;
    overlay->layer = zwlr_layer_shell_v1_get_layer_surface(overlay->shell, window->wl.surface,
        overlay->monitor->wl.output, vibranceOverlayLayer(overlay), window->title);
    static const struct zwlr_layer_surface_v1_listener listener = {
        vibranceOverlayConfigured, vibranceOverlayClosed
    };
    zwlr_layer_surface_v1_add_listener(overlay->layer, &listener, overlay);
    zwlr_layer_surface_v1_set_anchor(overlay->layer,
        ZWLR_LAYER_SURFACE_V1_ANCHOR_TOP | ZWLR_LAYER_SURFACE_V1_ANCHOR_LEFT);
    zwlr_layer_surface_v1_set_exclusive_zone(overlay->layer, -1);
    zwlr_layer_surface_v1_set_keyboard_interactivity(overlay->layer,
        ZWLR_LAYER_SURFACE_V1_KEYBOARD_INTERACTIVITY_NONE);
    vibranceCommitOverlay(overlay);
    return wl_display_roundtrip(_glfw.wl.display) >= 0;
}

static void vibranceHideOverlay(_GLFWwindow* window)
{
    VibranceOverlay* overlay = vibranceFindOverlay(window);
    if (overlay && overlay->layer)
    {
        zwlr_layer_surface_v1_destroy(overlay->layer);
        overlay->layer = NULL;
    }
}

static void vibranceForgetOverlay(_GLFWwindow* window)
{
    VibranceOverlay** link = &vibranceOverlays;
    while (*link && (*link)->window != window)
        link = &(*link)->next;
    if (*link)
    {
        VibranceOverlay* overlay = *link;
        *link = overlay->next;
        vibranceDestroyLayerShell(overlay->shell);
        _glfw_free(overlay);
    }
}

GLFWAPI int glfwVibranceWaylandOverlay(GLFWwindow* handle, int x, int y, int above)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);
    if (_glfw.platform.platformID != GLFW_PLATFORM_WAYLAND || !handle)
        return GLFW_FALSE;
    _GLFWwindow* window = (_GLFWwindow*) handle;
    if (window->wl.visible || window->wl.xdg.toplevel || window->wl.libdecor.frame)
        return GLFW_FALSE;

    struct zwlr_layer_shell_v1* shell = NULL;
    struct wl_registry* registry = wl_display_get_registry(_glfw.wl.display);
    static const struct wl_registry_listener listener = {
        vibranceFindLayerShell, vibranceIgnoreGlobalRemoval
    };
    wl_registry_add_listener(registry, &listener, &shell);
    const int result = wl_display_roundtrip(_glfw.wl.display);
    wl_registry_destroy(registry);
    if (result < 0 || !shell)
    {
        if (shell)
            vibranceDestroyLayerShell(shell);
        return GLFW_FALSE;
    }

    VibranceOverlay* overlay = _glfw_calloc(1, sizeof(VibranceOverlay));
    if (!overlay)
    {
        vibranceDestroyLayerShell(shell);
        return GLFW_FALSE;
    }
    overlay->window = window;
    overlay->shell = shell;
    overlay->x = x;
    overlay->y = y;
    overlay->width = window->wl.width;
    overlay->height = window->wl.height;
    overlay->above = above;
    overlay->next = vibranceOverlays;
    vibranceOverlays = overlay;
    return GLFW_TRUE;
}

GLFWAPI int glfwVibranceWaylandOverlayPosition(GLFWwindow* handle, int* x, int* y, int set)
{
    VibranceOverlay* overlay = vibranceFindOverlay((_GLFWwindow*) handle);
    if (!overlay)
        return GLFW_FALSE;
    if (!set)
    {
        *x = overlay->x;
        *y = overlay->y;
    }
    else if (overlay->x != *x || overlay->y != *y)
    {
        overlay->x = *x;
        overlay->y = *y;
        if (overlay->layer)
        {
            if (vibranceOverlayMonitor(*x, *y) != overlay->monitor)
            {
                _glfwHideWindowWayland(overlay->window);
                _glfwShowWindowWayland(overlay->window);
            }
            else
                vibranceCommitOverlay(overlay);
        }
    }
    return GLFW_TRUE;
}

GLFWAPI int glfwVibranceWaylandOverlayAbove(GLFWwindow* handle, int above)
{
    VibranceOverlay* overlay = vibranceFindOverlay((_GLFWwindow*) handle);
    if (!overlay)
        return GLFW_FALSE;
    if (overlay->above != above)
    {
        overlay->above = above;
        if (overlay->layer)
        {
            zwlr_layer_surface_v1_set_layer(overlay->layer, vibranceOverlayLayer(overlay));
            wl_surface_commit(overlay->window->wl.surface);
            wl_display_flush(_glfw.wl.display);
        }
    }
    return GLFW_TRUE;
}

static GLFWbool vibranceResizeOverlay(_GLFWwindow* window, int width, int height)
{
    VibranceOverlay* overlay = vibranceFindOverlay(window);
    if (!overlay)
        return GLFW_FALSE;
    if (overlay->width != width || overlay->height != height)
    {
        overlay->width = width;
        overlay->height = height;
        if (overlay->layer)
            vibranceCommitOverlay(overlay);
    }
    return GLFW_TRUE;
}

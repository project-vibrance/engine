# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

function(vibrance_enable_glfw_wayland_window_actions source)
    set(original "${source}/src/wl_window.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" contents)
    set(button_handler "static void pointerHandleButton(")
    set(input_call [=[
        _glfw.wl.serial = serial;

        _glfwInputMouseClick(window,
]=])
    string(FIND "${contents}" "${input_call}" input_position)
    if(input_position LESS 0)
        return()
    endif()
    set(grab_state [=[
static _GLFWwindow* vibrancePointerWindow;
static uint32_t vibrancePointerSerial;

]=])
    set(capture [=[
        if (button == BTN_LEFT)
        {
            vibrancePointerWindow = state == WL_POINTER_BUTTON_STATE_PRESSED ? window : NULL;
            vibrancePointerSerial = serial;
        }

]=])
    string(REPLACE "${button_handler}" "${grab_state}${button_handler}" contents "${contents}")
    string(REPLACE "${input_call}" "${capture}${input_call}" contents "${contents}")
    set(cursor_update [=[
    struct itimerspec timer = {0};
    struct wl_cursor* wlCursor = cursorWayland->cursor;
]=])
    string(REPLACE "${cursor_update}" [=[
    if (!_glfw.wl.pointer || !_glfw.wl.cursorSurface)
        return;

    struct itimerspec timer = {0};
    struct wl_cursor* wlCursor = cursorWayland->cursor;
]=] contents "${contents}")
    string(APPEND contents [=[

GLFWAPI int glfwVibranceWaylandWindowAction(GLFWwindow* handle, int horizontal, int vertical)
{
    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);
    _GLFWwindow* window = (_GLFWwindow*) handle;
    if (_glfw.platform.platformID != GLFW_PLATFORM_WAYLAND || !window ||
        window != vibrancePointerWindow || window != _glfw.wl.pointerFocus ||
        !window->wl.xdg.toplevel || !_glfw.wl.seat)
        return GLFW_FALSE;

    const uint32_t resizeEdge =
        (horizontal < 0 ? XDG_TOPLEVEL_RESIZE_EDGE_LEFT :
         horizontal > 0 ? XDG_TOPLEVEL_RESIZE_EDGE_RIGHT : XDG_TOPLEVEL_RESIZE_EDGE_NONE) |
        (vertical < 0 ? XDG_TOPLEVEL_RESIZE_EDGE_TOP :
         vertical > 0 ? XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM : XDG_TOPLEVEL_RESIZE_EDGE_NONE);
    if (resizeEdge)
        xdg_toplevel_resize(window->wl.xdg.toplevel, _glfw.wl.seat,
                            vibrancePointerSerial, resizeEdge);
    else
        xdg_toplevel_move(window->wl.xdg.toplevel, _glfw.wl.seat, vibrancePointerSerial);

    vibrancePointerWindow = NULL;
    wl_display_flush(_glfw.wl.display);
    return GLFW_TRUE;
}

static void vibranceActivationDone(void* data, struct xdg_activation_token_v1* token, const char* value)
{
    (void) token;
    size_t length = strlen(value);
    if (length < 1024)
        memcpy(data, value, length + 1);
}

GLFWAPI int glfwVibranceWaylandActivationToken(const char* appId, char output[1024])
{
    _GLFW_REQUIRE_INIT_OR_RETURN(GLFW_FALSE);
    if (_glfw.platform.platformID != GLFW_PLATFORM_WAYLAND || !_glfw.wl.activationManager || !_glfw.wl.pointer ||
        !_glfw.wl.seat || !_glfw.wl.pointerFocus || !vibrancePointerSerial)
        return GLFW_FALSE;

    struct wl_event_queue* queue = wl_display_create_queue(_glfw.wl.display);
    if (!queue)
        return GLFW_FALSE;
    struct xdg_activation_token_v1* token =
        xdg_activation_v1_get_activation_token(_glfw.wl.activationManager);
    if (!token)
    {
        wl_event_queue_destroy(queue);
        return GLFW_FALSE;
    }
    wl_proxy_set_queue((struct wl_proxy*) token, queue);
    static const struct xdg_activation_token_v1_listener listener = { vibranceActivationDone };
    output[0] = '\0';
    xdg_activation_token_v1_add_listener(token, &listener, output);
    xdg_activation_token_v1_set_serial(token, vibrancePointerSerial, _glfw.wl.seat);
    xdg_activation_token_v1_set_surface(token, _glfw.wl.pointerFocus->wl.surface);
    if (appId && *appId)
        xdg_activation_token_v1_set_app_id(token, appId);
    xdg_activation_token_v1_commit(token);
    const int result = wl_display_roundtrip_queue(_glfw.wl.display, queue);
    xdg_activation_token_v1_destroy(token);
    wl_event_queue_destroy(queue);
    return result >= 0 && output[0] != '\0';
}
]=])
    find_program(VIBRANCE_GLFW_WAYLAND_SCANNER wayland-scanner REQUIRED)
    set(layer_xml "${PROJECT_SOURCE_DIR}/engine/protocols/wlr-layer-shell-unstable-v1.xml")
    set(layer_directory "${CMAKE_BINARY_DIR}/external/glfw")
    set(layer_client "${layer_directory}/vibrance-layer-shell-client.h")
    set(layer_code "${layer_directory}/vibrance-layer-shell-code.h")
    add_custom_command(OUTPUT "${layer_client}" "${layer_code}"
        COMMAND "${VIBRANCE_GLFW_WAYLAND_SCANNER}" client-header "${layer_xml}" "${layer_client}"
        COMMAND "${VIBRANCE_GLFW_WAYLAND_SCANNER}" private-code "${layer_xml}" "${layer_code}"
        DEPENDS "${layer_xml}" VERBATIM)
    add_custom_target(vibrance_glfw_layer_protocol DEPENDS "${layer_client}" "${layer_code}")
    add_dependencies(glfw vibrance_glfw_layer_protocol)
    target_include_directories(glfw PRIVATE "${CMAKE_CURRENT_FUNCTION_LIST_DIR}" "${layer_directory}")
    set(create_shell "static GLFWbool createShellObjects(_GLFWwindow* window)\n{")
    string(REPLACE "${create_shell}"
        "#include \"glfw_wayland_overlay.h\"\n\n${create_shell}\n    VibranceOverlay* overlay = vibranceFindOverlay(window);\n    if (overlay)\n        return vibranceShowOverlay(overlay);"
        contents "${contents}")
    set(destroy_shell "static void destroyShellObjects(_GLFWwindow* window)\n{")
    string(REPLACE "${destroy_shell}" "${destroy_shell}\n    vibranceHideOverlay(window);" contents "${contents}")
    set(destroy_window "    destroyShellObjects(window);\n\n    if (window->wl.fallback.buffer)")
    string(REPLACE "${destroy_window}"
        "    destroyShellObjects(window);\n    vibranceForgetOverlay(window);\n\n    if (window->wl.fallback.buffer)"
        contents "${contents}")
    set(resize_window "void _glfwSetWindowSizeWayland(_GLFWwindow* window, int width, int height)\n{")
    string(REPLACE "${resize_window}"
        "${resize_window}\n    if (vibranceResizeOverlay(window, width, height))\n        return;"
        contents "${contents}")
    set(patched "${CMAKE_BINARY_DIR}/external/glfw/wl_window.c")
    file(CONFIGURE OUTPUT "${patched}" CONTENT "${contents}" @ONLY)
    get_target_property(sources glfw SOURCES)
    list(REMOVE_ITEM sources wl_window.c "${original}")
    set_property(TARGET glfw PROPERTY SOURCES "${sources}")
    target_sources(glfw PRIVATE "${patched}")
    set_property(TARGET glfw PROPERTY VIBRANCE_GLFW_WAYLAND_WINDOW_ACTIONS TRUE)
endfunction()

function(vibrance_build_glfw source)
    set(GLFW_LIBRARY_TYPE SHARED)
    set(GLFW_BUILD_DOCS OFF)
    set(GLFW_BUILD_EXAMPLES OFF)
    set(GLFW_BUILD_TESTS OFF)
    set(GLFW_INSTALL OFF)
    add_subdirectory("${source}" "${CMAKE_BINARY_DIR}/external/glfw" EXCLUDE_FROM_ALL)
    if(GLFW_BUILD_WAYLAND)
        vibrance_enable_glfw_wayland_window_actions("${source}")
    endif()
    if(NOT GLFW_BUILD_X11)
        return()
    endif()

    set(original "${source}/src/x11_window.c")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${original}")
    file(READ "${original}" contents)
    set(default_visual [=[
    if (!visual)
    {
        visual = DefaultVisual(_glfw.x11.display, _glfw.x11.screen);
]=])
    string(FIND "${contents}" "${default_visual}" default_visual_position)
    if(default_visual_position LESS 0)
        message(FATAL_ERROR "Use GLFW 3.4 source for the X11 transparency build.")
    endif()
    set(transparent_visual [=[
    if (!visual && ctxconfig->client == GLFW_NO_API && fbconfig->transparent)
    {
        XVisualInfo candidate = {0};
        candidate.screen = _glfw.x11.screen;
        candidate.depth = 32;
        candidate.class = TrueColor;
        int count = 0;
        XVisualInfo* visuals = XGetVisualInfo(_glfw.x11.display,
            VisualScreenMask | VisualDepthMask | VisualClassMask, &candidate, &count);
        for (int i = 0; i < count; ++i)
        {
            if (_glfwIsVisualTransparentX11(visuals[i].visual))
            {
                visual = visuals[i].visual;
                depth = visuals[i].depth;
                break;
            }
        }
        if (visuals)
            XFree(visuals);
    }

]=])
    string(REPLACE "${default_visual}" "${transparent_visual}${default_visual}" contents "${contents}")
    set(patched "${CMAKE_BINARY_DIR}/external/glfw/x11_window.c")
    file(CONFIGURE OUTPUT "${patched}" CONTENT "${contents}" @ONLY)
    get_target_property(sources glfw SOURCES)
    list(REMOVE_ITEM sources x11_window.c "${original}")
    set_property(TARGET glfw PROPERTY SOURCES "${sources}")
    target_sources(glfw PRIVATE "${patched}")
endfunction()

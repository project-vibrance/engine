# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

function(vibrance_build_glfw source)
    set(GLFW_LIBRARY_TYPE SHARED)
    set(GLFW_BUILD_DOCS OFF)
    set(GLFW_BUILD_EXAMPLES OFF)
    set(GLFW_BUILD_TESTS OFF)
    set(GLFW_INSTALL OFF)
    add_subdirectory("${source}" "${CMAKE_BINARY_DIR}/external/glfw" EXCLUDE_FROM_ALL)
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

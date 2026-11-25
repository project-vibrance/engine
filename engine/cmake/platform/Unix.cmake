# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

if(UNIX AND NOT APPLE)
    find_package(X11 QUIET)
    if(VIBRANCE_BUNDLED_GLFW)
        set(VIBRANCE_GLFW_HAS_X11 ${GLFW_BUILD_X11})
        set(VIBRANCE_GLFW_HAS_WAYLAND ${GLFW_BUILD_WAYLAND})
    elseif(TARGET X11::Xext)
        include(CheckCXXSourceCompiles)
        include(CMakePushCheckState)
        cmake_push_check_state(RESET)
        set(CMAKE_REQUIRED_INCLUDES ${GLFW_INCLUDE_DIRS})
        set(CMAKE_REQUIRED_LIBRARIES ${GLFW_LIBRARIES} X11::Xext)
        check_cxx_source_compiles([=[
            #define GLFW_INCLUDE_NONE
            #define GLFW_EXPOSE_NATIVE_X11
            #include <GLFW/glfw3.h>
            #include <GLFW/glfw3native.h>
            int main() { return glfwGetX11Display() != nullptr; }
        ]=] VIBRANCE_GLFW_HAS_X11)
        cmake_pop_check_state()
    endif()
    if(TARGET X11::Xext AND VIBRANCE_GLFW_HAS_X11)
        target_sources(vibrance_engine PRIVATE src/renderer/vulkan/x11_window_shape.cpp)
        target_compile_definitions(vibrance_engine PRIVATE VIBRANCE_HAS_X11_WINDOW_SHAPE=1)
        target_link_libraries(vibrance_engine PRIVATE X11::X11 X11::Xext)
    endif()

    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(VIBRANCE_GIO QUIET IMPORTED_TARGET gio-2.0)
        if(TARGET PkgConfig::VIBRANCE_GIO)
            find_package(CURL QUIET)
            if(NOT FFMPEG_FOUND)
                pkg_check_modules(VIBRANCE_MEDIA_PROBE QUIET IMPORTED_TARGET libavformat libavcodec libavutil)
            endif()
        endif()
        pkg_check_modules(VIBRANCE_PULSE QUIET IMPORTED_TARGET libpulse)
        pkg_check_modules(VIBRANCE_SECRET QUIET IMPORTED_TARGET libsecret-1)
        pkg_check_modules(VIBRANCE_XCB QUIET IMPORTED_TARGET xcb)
        pkg_check_modules(VIBRANCE_WAYLAND_CLIENT QUIET IMPORTED_TARGET wayland-client)
    endif()
    target_sources(vibrance_engine PRIVATE src/platform/unix/window_metadata.cpp)
    if(VIBRANCE_BUNDLED_GLFW)
        get_target_property(_vibrance_wayland_actions glfw VIBRANCE_GLFW_WAYLAND_WINDOW_ACTIONS)
        if(_vibrance_wayland_actions)
            set_property(SOURCE src/platform/unix/window_metadata.cpp APPEND PROPERTY
                COMPILE_DEFINITIONS VIBRANCE_HAS_WAYLAND_WINDOW_ACTIONS=1)
        endif()
    endif()
    if(TARGET PkgConfig::VIBRANCE_WAYLAND_CLIENT)
        if(NOT VIBRANCE_BUNDLED_GLFW)
            include(CheckCXXSourceCompiles)
            include(CMakePushCheckState)
            cmake_push_check_state(RESET)
            set(CMAKE_REQUIRED_INCLUDES ${GLFW_INCLUDE_DIRS})
            set(CMAKE_REQUIRED_LIBRARIES ${GLFW_LIBRARIES} PkgConfig::VIBRANCE_WAYLAND_CLIENT)
            check_cxx_source_compiles([=[
                #define GLFW_INCLUDE_NONE
                #define GLFW_EXPOSE_NATIVE_WAYLAND
                #include <GLFW/glfw3.h>
                #include <GLFW/glfw3native.h>
                int main() { return glfwGetWaylandDisplay() != nullptr; }
            ]=] VIBRANCE_GLFW_HAS_WAYLAND)
            cmake_pop_check_state()
        endif()
        find_program(VIBRANCE_BACKDROP_WAYLAND_SCANNER wayland-scanner)
        if(VIBRANCE_GLFW_HAS_WAYLAND AND VIBRANCE_BACKDROP_WAYLAND_SCANNER)
            enable_language(C)
            set(_vibrance_plasma_shell_protocol "${CMAKE_CURRENT_BINARY_DIR}/generated/plasma-shell")
            set(_vibrance_plasma_shell_xml "${CMAKE_CURRENT_SOURCE_DIR}/protocols/plasma-shell.xml")
            add_custom_command(
                OUTPUT "${_vibrance_plasma_shell_protocol}.h" "${_vibrance_plasma_shell_protocol}.c"
                COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
                COMMAND ${VIBRANCE_BACKDROP_WAYLAND_SCANNER} client-header
                    "${_vibrance_plasma_shell_xml}" "${_vibrance_plasma_shell_protocol}.h"
                COMMAND ${VIBRANCE_BACKDROP_WAYLAND_SCANNER} private-code
                    "${_vibrance_plasma_shell_xml}" "${_vibrance_plasma_shell_protocol}.c"
                DEPENDS "${_vibrance_plasma_shell_xml}" VERBATIM)
            target_sources(vibrance_engine PRIVATE
                "${_vibrance_plasma_shell_protocol}.h" "${_vibrance_plasma_shell_protocol}.c")
            target_include_directories(vibrance_engine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
            target_link_libraries(vibrance_engine PRIVATE PkgConfig::VIBRANCE_WAYLAND_CLIENT)
            set_property(SOURCE src/platform/unix/window_metadata.cpp APPEND PROPERTY
                COMPILE_DEFINITIONS VIBRANCE_HAS_WAYLAND_WINDOW_METADATA=1)
        endif()
        pkg_get_variable(_vibrance_wayland_protocols wayland-protocols pkgdatadir)
        set(_vibrance_background_effect_xml
            "${_vibrance_wayland_protocols}/staging/ext-background-effect/ext-background-effect-v1.xml")
        if(VIBRANCE_BACKDROP_WAYLAND_SCANNER AND EXISTS "${_vibrance_background_effect_xml}")
            if(VIBRANCE_GLFW_HAS_WAYLAND)
                enable_language(C)
                set(_vibrance_backdrop_protocol "${CMAKE_CURRENT_BINARY_DIR}/generated/background-effect")
                set(_vibrance_liquid_protocol "${CMAKE_CURRENT_BINARY_DIR}/generated/vibrance-liquid-glass-v1")
                set(_vibrance_liquid_xml "${CMAKE_CURRENT_SOURCE_DIR}/protocols/vibrance-liquid-glass-v1.xml")
                add_custom_command(
                    OUTPUT "${_vibrance_backdrop_protocol}.h" "${_vibrance_backdrop_protocol}.c"
                        "${_vibrance_liquid_protocol}.h" "${_vibrance_liquid_protocol}.c"
                    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_CURRENT_BINARY_DIR}/generated"
                    COMMAND ${VIBRANCE_BACKDROP_WAYLAND_SCANNER} client-header
                        "${_vibrance_background_effect_xml}" "${_vibrance_backdrop_protocol}.h"
                    COMMAND ${VIBRANCE_BACKDROP_WAYLAND_SCANNER} private-code
                        "${_vibrance_background_effect_xml}" "${_vibrance_backdrop_protocol}.c"
                    COMMAND ${VIBRANCE_BACKDROP_WAYLAND_SCANNER} client-header
                        "${_vibrance_liquid_xml}" "${_vibrance_liquid_protocol}.h"
                    COMMAND ${VIBRANCE_BACKDROP_WAYLAND_SCANNER} private-code
                        "${_vibrance_liquid_xml}" "${_vibrance_liquid_protocol}.c"
                    DEPENDS "${_vibrance_background_effect_xml}" "${_vibrance_liquid_xml}" VERBATIM)
                target_sources(vibrance_engine PRIVATE "${_vibrance_backdrop_protocol}.c"
                    "${_vibrance_backdrop_protocol}.h" "${_vibrance_liquid_protocol}.h"
                    "${_vibrance_liquid_protocol}.c")
                target_include_directories(vibrance_engine PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated")
                target_link_libraries(vibrance_engine PRIVATE PkgConfig::VIBRANCE_WAYLAND_CLIENT)
                set_property(SOURCE src/platform/unix/desktop_backdrop.cpp APPEND PROPERTY
                    COMPILE_DEFINITIONS VIBRANCE_HAS_WAYLAND_BACKDROP=1)
                set(_vibrance_has_wayland_backdrop TRUE)
            endif()
        endif()
    endif()
    if((TARGET X11::Xext AND VIBRANCE_GLFW_HAS_X11) OR _vibrance_has_wayland_backdrop)
        target_sources(vibrance_engine PRIVATE src/platform/unix/desktop_backdrop.cpp)
        set_property(SOURCE src/renderer/vulkan/renderer.cpp src/renderer/common/scene.cpp
            APPEND PROPERTY COMPILE_DEFINITIONS VIBRANCE_HAS_DESKTOP_BACKDROP=1)
    endif()
    if(TARGET PkgConfig::VIBRANCE_GIO)
        target_sources(vibrance_engine PRIVATE src/media/session_unix.cpp src/platform/unix/native_tray.cpp)
        target_compile_definitions(vibrance_engine PRIVATE VIBRANCE_HAS_GIO=1)
        target_link_libraries(vibrance_engine PRIVATE PkgConfig::VIBRANCE_GIO)
        if(TARGET CURL::libcurl)
            target_sources(vibrance_engine PRIVATE src/media/session_unix_artwork.cpp)
            set_property(SOURCE src/media/session_unix.cpp APPEND PROPERTY COMPILE_DEFINITIONS VIBRANCE_HAS_ARTWORK_CURL=1)
            target_link_libraries(vibrance_engine PRIVATE CURL::libcurl)
        endif()
        if(FFMPEG_FOUND OR TARGET PkgConfig::VIBRANCE_MEDIA_PROBE)
            set_property(SOURCE src/media/session_unix.cpp APPEND PROPERTY COMPILE_DEFINITIONS VIBRANCE_HAS_MPRIS_CODEC=1)
        endif()
        if(TARGET PkgConfig::VIBRANCE_MEDIA_PROBE)
            target_link_libraries(vibrance_engine PRIVATE PkgConfig::VIBRANCE_MEDIA_PROBE)
        endif()
    endif()
    if(TARGET PkgConfig::VIBRANCE_PULSE)
        target_sources(vibrance_engine PRIVATE src/audio/loopback_capture_unix.cpp)
        target_compile_definitions(vibrance_engine PRIVATE VIBRANCE_HAS_PULSE=1)
        target_link_libraries(vibrance_engine PRIVATE PkgConfig::VIBRANCE_PULSE)
    endif()
    if(TARGET PkgConfig::VIBRANCE_SECRET)
        target_compile_definitions(vibrance_engine PRIVATE VIBRANCE_HAS_SECRET_SERVICE=1)
        target_link_libraries(vibrance_engine PRIVATE PkgConfig::VIBRANCE_SECRET)
    endif()
    if(TARGET PkgConfig::VIBRANCE_XCB)
        set_property(SOURCE src/display/foreground.cpp APPEND PROPERTY COMPILE_DEFINITIONS VIBRANCE_HAS_XCB=1)
        target_link_libraries(vibrance_engine PRIVATE PkgConfig::VIBRANCE_XCB)
    endif()
endif()

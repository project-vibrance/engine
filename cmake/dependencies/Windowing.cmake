# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

if(DEFINED GLFW_PATH AND GLFW_PATH)
    if(NOT EXISTS "${GLFW_PATH}")
        message(STATUS "GLFW_PATH does not exist at '${GLFW_PATH}'; falling back to the system-installed GLFW.")
        unset(GLFW_PATH)
    endif()
endif()

if(DEFINED GLFW_PATH)
    set(GLFW_INCLUDE_DIRS "${GLFW_PATH}/include")
    if((APPLE OR (WIN32 AND VIBRANCE_WINDOWS_ARCHITECTURE STREQUAL "arm64")) AND
       EXISTS "${GLFW_PATH}/CMakeLists.txt")
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory(
            "${GLFW_PATH}"
            "${CMAKE_BINARY_DIR}/external/glfw"
            EXCLUDE_FROM_ALL)
        set(GLFW_LIBRARIES glfw)
    elseif(MSVC)
        set(_GLFW_LIBRARY_HINTS "${GLFW_PATH}/lib-vc2022" "${GLFW_PATH}/build/src")
        find_library(GLFW_LIBRARIES NAMES glfw3 glfw HINTS ${_GLFW_LIBRARY_HINTS} REQUIRED)
    else()
        set(_GLFW_LIBRARY_HINTS "${GLFW_PATH}/lib" "${GLFW_PATH}/build/src")
        find_library(GLFW_LIBRARIES NAMES glfw3 glfw HINTS ${_GLFW_LIBRARY_HINTS} REQUIRED)
    endif()
else()
    find_package(glfw3 3.3 REQUIRED)
    if(TARGET glfw)
        set(GLFW_LIBRARIES glfw)
        get_target_property(GLFW_INCLUDE_DIRS glfw INTERFACE_INCLUDE_DIRECTORIES)
    elseif(TARGET glfw3::glfw)
        set(GLFW_LIBRARIES glfw3::glfw)
        get_target_property(GLFW_INCLUDE_DIRS glfw3::glfw INTERFACE_INCLUDE_DIRECTORIES)
    endif()
endif()

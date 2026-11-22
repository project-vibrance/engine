# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

if(APPLE)
    set(VIBRANCE_RENDER_BACKEND Metal)
else()
    set(VIBRANCE_RENDER_BACKEND Vulkan)
endif()
if(UNIX AND NOT APPLE)
    find_package(Threads REQUIRED)
endif()
include(cmake/build/Sources.cmake)
include(cmake/build/Manifest.cmake)

if(APPLE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/metal/shaders.cmake")
else()
    include("${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/vulkan/shaders.cmake")
endif()


add_library(vibrance_engine SHARED ${ENGINE_SOURCES})
add_library(vibrance::engine ALIAS vibrance_engine)

include(cmake/platform/WindowsComposition.cmake)
include(cmake/platform/WindowsInterop.cmake)
include(cmake/build/Target.cmake)
include(cmake/build/AudioBackend.cmake)
include(cmake/build/Link.cmake)
include(cmake/build/Install.cmake)

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

set_target_properties(vibrance_engine PROPERTIES
    EXPORT_NAME engine
    OUTPUT_NAME vibrance_engine
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    POSITION_INDEPENDENT_CODE ON)

option(
    VIBRANCE_ENABLE_VULKAN_VALIDATION
    "Enable Vulkan validation layers (use for diagnostics; adds runtime overhead)"
    OFF)
option(
    VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING
    "Enable verbose Vulkan renderer logging"
    OFF)

target_compile_features(vibrance_engine PUBLIC cxx_std_20)
if(APPLE)
    target_include_directories(vibrance_engine PRIVATE "${METAL_CPP_INCLUDE_DIR}")
    target_compile_options(vibrance_engine PRIVATE -fblocks)
endif()
if (CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")



    target_compile_options(vibrance_engine PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
target_compile_definitions(vibrance_engine PRIVATE
    VIBRANCE_ENGINE_EXPORTS
    GLM_ENABLE_EXPERIMENTAL
    $<$<CONFIG:Debug>:DEBUG>
    VIBRANCE_ENABLE_VULKAN_VALIDATION=$<BOOL:${VIBRANCE_ENABLE_VULKAN_VALIDATION}>
    VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING=$<BOOL:${VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING}>)

target_include_directories(vibrance_engine
    PUBLIC
        "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
        "$<BUILD_INTERFACE:${GLM_INCLUDE_DIR}>"
        "$<BUILD_INTERFACE:${VMA_INCLUDE_DIR}>"
        "$<BUILD_INTERFACE:${ENTT_INCLUDE_DIR}>"
        "$<BUILD_INTERFACE:${GLFW_INCLUDE_DIRS}>"
        "$<BUILD_INTERFACE:${Vulkan_INCLUDE_DIRS}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
    PRIVATE
        "${CMAKE_CURRENT_SOURCE_DIR}/src"
        "${ENGINE_GENERATED_DIR}"
        "${STB_INCLUDE_DIR}"
        ${FREETYPE_INCLUDE_DIRS})

if(DEFINED NANOSVG_INCLUDE_DIR)
    target_include_directories(vibrance_engine PRIVATE "${NANOSVG_INCLUDE_DIR}")
    target_compile_definitions(vibrance_engine PRIVATE VIBRANCE_HAS_NANOSVG=1)
endif()

if(FFMPEG_FOUND)
    target_include_directories(vibrance_engine PRIVATE "${FFMPEG_INCLUDE_DIR}")
    target_compile_definitions(vibrance_engine PRIVATE VIBRANCE_HAS_FFMPEG=1)
endif()

# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0




set(EMBEDDED_SHADER_DIR "${ENGINE_GENERATED_DIR}")
set(EMBEDDED_SHADER_HEADER "${EMBEDDED_SHADER_DIR}/embedded_shaders.h")
set(EMBEDDED_SHADER_CPP "${EMBEDDED_SHADER_DIR}/embedded_shaders.cpp")
set(EMBEDDED_SHADER_MANIFEST "${EMBEDDED_SHADER_DIR}/embedded_shaders.manifest")
file(MAKE_DIRECTORY "${EMBEDDED_SHADER_DIR}/spirv")
file(CONFIGURE
    OUTPUT "${EMBEDDED_SHADER_HEADER}"
    CONTENT [[// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once
#include <cstddef>
#include <span>
#include <string_view>
std::span<const std::byte> vibrance_embedded_shader_spirv(std::string_view fileName);
]])

file(GLOB ENGINE_SHADER_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.comp"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.vert"
    "${CMAKE_CURRENT_SOURCE_DIR}/shaders/*.frag")
set(EMBEDDED_SHADER_BINARIES)
set(EMBEDDED_SHADER_MANIFEST_CONTENT "")
set(LIQUID_REFRACTION_PROFILE
    "${CMAKE_CURRENT_SOURCE_DIR}/include/vibranceUI/graphics/liquid_glass_refraction.h")
foreach(SHADER_SOURCE IN LISTS ENGINE_SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
    set(SHADER_INCLUDES)
    if(SHADER_NAME STREQUAL "blur_2d.comp")
        list(APPEND SHADER_INCLUDES "${LIQUID_REFRACTION_PROFILE}")
    endif()
    set(SHADER_BINARY "${EMBEDDED_SHADER_DIR}/spirv/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${SHADER_BINARY}"
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -O
            -DVIBRANCE_VULKAN_SAMPLE_LOOPS=1
            "${SHADER_SOURCE}" -o "${SHADER_BINARY}"
        DEPENDS "${SHADER_SOURCE}" "${CMAKE_CURRENT_LIST_FILE}" ${SHADER_INCLUDES}
        COMMENT "Compiling ${SHADER_NAME} to SPIR-V"
        VERBATIM)
    list(APPEND EMBEDDED_SHADER_BINARIES "${SHADER_BINARY}")
    string(APPEND EMBEDDED_SHADER_MANIFEST_CONTENT "${SHADER_NAME}|${SHADER_BINARY}\n")
endforeach()


set(TEXT_BATCH_SHADER_BINARY "${EMBEDDED_SHADER_DIR}/spirv/text_msdf_batch.comp.spv")
add_custom_command(
    OUTPUT "${TEXT_BATCH_SHADER_BINARY}"
    COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -O
        -DVIBRANCE_VULKAN_SAMPLE_LOOPS=1
        -DVIBRANCE_TEXT_BATCH=1 "${CMAKE_CURRENT_SOURCE_DIR}/shaders/text_msdf.comp"
        -o "${TEXT_BATCH_SHADER_BINARY}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/text_msdf.comp" "${CMAKE_CURRENT_LIST_FILE}"
    COMMENT "Compiling batched text to SPIR-V"
    VERBATIM)
list(APPEND EMBEDDED_SHADER_BINARIES "${TEXT_BATCH_SHADER_BINARY}")
string(APPEND EMBEDDED_SHADER_MANIFEST_CONTENT "text_msdf_batch.comp|${TEXT_BATCH_SHADER_BINARY}\n")


set(NVIDIA_MEDIA_SHADER_BINARY "${EMBEDDED_SHADER_DIR}/spirv/media_2d_nvidia.comp.spv")
add_custom_command(
    OUTPUT "${NVIDIA_MEDIA_SHADER_BINARY}"
    COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -O
        -DVIBRANCE_VULKAN_SAMPLE_LOOPS=1
        -DVIBRANCE_NVIDIA_MEDIA_SAMPLE_LOOPS=1
        "${CMAKE_CURRENT_SOURCE_DIR}/shaders/media_2d.comp"
        -o "${NVIDIA_MEDIA_SHADER_BINARY}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/shaders/media_2d.comp" "${CMAKE_CURRENT_LIST_FILE}"
    COMMENT "Compiling NVIDIA media to SPIR-V"
    VERBATIM)
list(APPEND EMBEDDED_SHADER_BINARIES "${NVIDIA_MEDIA_SHADER_BINARY}")
string(APPEND EMBEDDED_SHADER_MANIFEST_CONTENT "media_2d_nvidia.comp|${NVIDIA_MEDIA_SHADER_BINARY}\n")

function(vibrance_compile_vulkan12_shader SHADER_SOURCE SHADER_NAME)
    set(SHADER_INCLUDES)
    if(SHADER_NAME STREQUAL "blur_2d.comp")
        list(APPEND SHADER_INCLUDES "${LIQUID_REFRACTION_PROFILE}")
    endif()
    get_filename_component(SHADER_STEM "${SHADER_NAME}" NAME_WE)
    get_filename_component(SHADER_EXTENSION "${SHADER_NAME}" EXT)
    set(COMPAT_NAME "${SHADER_STEM}_vulkan12${SHADER_EXTENSION}")
    set(COMPAT_BINARY "${EMBEDDED_SHADER_DIR}/spirv/${COMPAT_NAME}.spv")
    add_custom_command(
        OUTPUT "${COMPAT_BINARY}"
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.2 -O
            -DVIBRANCE_VULKAN_SAMPLE_LOOPS=1 ${ARGN}
            "${SHADER_SOURCE}" -o "${COMPAT_BINARY}"
        DEPENDS "${SHADER_SOURCE}" "${CMAKE_CURRENT_LIST_FILE}" ${SHADER_INCLUDES}
        COMMENT "Compiling Vulkan 1.2 compatibility shader ${COMPAT_NAME}"
        VERBATIM)
    list(APPEND EMBEDDED_SHADER_BINARIES "${COMPAT_BINARY}")
    set(EMBEDDED_SHADER_BINARIES "${EMBEDDED_SHADER_BINARIES}" PARENT_SCOPE)
    set(EMBEDDED_SHADER_MANIFEST_CONTENT
        "${EMBEDDED_SHADER_MANIFEST_CONTENT}${COMPAT_NAME}|${COMPAT_BINARY}\n" PARENT_SCOPE)
endfunction()
foreach(SHADER_SOURCE IN LISTS ENGINE_SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
    vibrance_compile_vulkan12_shader("${SHADER_SOURCE}" "${SHADER_NAME}")
endforeach()
vibrance_compile_vulkan12_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/text_msdf.comp"
    "text_msdf_batch.comp" -DVIBRANCE_TEXT_BATCH=1)
vibrance_compile_vulkan12_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/media_2d.comp"
    "media_2d_nvidia.comp" -DVIBRANCE_NVIDIA_MEDIA_SAMPLE_LOOPS=1)

file(CONFIGURE
    OUTPUT "${EMBEDDED_SHADER_MANIFEST}"
    CONTENT "${EMBEDDED_SHADER_MANIFEST_CONTENT}")

add_custom_command(
    OUTPUT "${EMBEDDED_SHADER_CPP}"
    COMMAND "${CMAKE_COMMAND}"
        "-DMANIFEST=${EMBEDDED_SHADER_MANIFEST}"
        "-DOUTPUT=${EMBEDDED_SHADER_CPP}"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    DEPENDS
        ${EMBEDDED_SHADER_BINARIES}
        "${EMBEDDED_SHADER_MANIFEST}"
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/EmbedShaders.cmake"
    COMMENT "Embedding engine SPIR-V in the DLL"
    VERBATIM)
list(APPEND ENGINE_SOURCES "${EMBEDDED_SHADER_CPP}")

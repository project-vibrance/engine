# Compile shader sources once, then turn the SPIR-V binaries into a generated
# translation unit. Runtime applications therefore need neither shader files
# nor shaderc beside the engine DLL.
set(EMBEDDED_SHADER_DIR "${ENGINE_GENERATED_DIR}")
set(EMBEDDED_SHADER_HEADER "${EMBEDDED_SHADER_DIR}/embedded_shaders.h")
set(EMBEDDED_SHADER_CPP "${EMBEDDED_SHADER_DIR}/embedded_shaders.cpp")
set(EMBEDDED_SHADER_MANIFEST "${EMBEDDED_SHADER_DIR}/embedded_shaders.manifest")
file(MAKE_DIRECTORY "${EMBEDDED_SHADER_DIR}/spirv")
file(CONFIGURE
    OUTPUT "${EMBEDDED_SHADER_HEADER}"
    CONTENT [[#pragma once
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
foreach(SHADER_SOURCE IN LISTS ENGINE_SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
    set(SHADER_BINARY "${EMBEDDED_SHADER_DIR}/spirv/${SHADER_NAME}.spv")
    add_custom_command(
        OUTPUT "${SHADER_BINARY}"
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.3 -O
            -DVIBRANCE_VULKAN_SAMPLE_LOOPS=1
            "${SHADER_SOURCE}" -o "${SHADER_BINARY}"
        DEPENDS "${SHADER_SOURCE}" "${CMAKE_CURRENT_LIST_FILE}"
        COMMENT "Compiling ${SHADER_NAME} to SPIR-V"
        VERBATIM)
    list(APPEND EMBEDDED_SHADER_BINARIES "${SHADER_BINARY}")
    string(APPEND EMBEDDED_SHADER_MANIFEST_CONTENT "${SHADER_NAME}|${SHADER_BINARY}\n")
endforeach()
# Reuse the complete text shader, including colour glyphs, for disjoint glyph
# batches. Only the source of its 128-byte constants changes.
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
# The measured NVIDIA path shares repeated media samples; other vendors use
# the original shader, which remains faster on the local integrated GPU.
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

# Drivers exposing Vulkan 1.1/1.2 cannot consume SPIR-V 1.6, even when a newer
# system loader is installed. Keep modern binaries unchanged and embed a
# separately validated SPIR-V 1.3 profile for those physical devices.
function(vibrance_compile_vulkan11_shader SHADER_SOURCE SHADER_NAME)
    get_filename_component(SHADER_STEM "${SHADER_NAME}" NAME_WE)
    get_filename_component(SHADER_EXTENSION "${SHADER_NAME}" EXT)
    set(COMPAT_NAME "${SHADER_STEM}_vulkan11${SHADER_EXTENSION}")
    set(COMPAT_BINARY "${EMBEDDED_SHADER_DIR}/spirv/${COMPAT_NAME}.spv")
    add_custom_command(
        OUTPUT "${COMPAT_BINARY}"
        COMMAND "${GLSLC_EXECUTABLE}" --target-env=vulkan1.1 -O
            -DVIBRANCE_VULKAN_SAMPLE_LOOPS=1 ${ARGN}
            "${SHADER_SOURCE}" -o "${COMPAT_BINARY}"
        DEPENDS "${SHADER_SOURCE}" "${CMAKE_CURRENT_LIST_FILE}"
        COMMENT "Compiling Vulkan 1.1 compatibility shader ${COMPAT_NAME}"
        VERBATIM)
    list(APPEND EMBEDDED_SHADER_BINARIES "${COMPAT_BINARY}")
    set(EMBEDDED_SHADER_BINARIES "${EMBEDDED_SHADER_BINARIES}" PARENT_SCOPE)
    set(EMBEDDED_SHADER_MANIFEST_CONTENT
        "${EMBEDDED_SHADER_MANIFEST_CONTENT}${COMPAT_NAME}|${COMPAT_BINARY}\n" PARENT_SCOPE)
endfunction()
foreach(SHADER_SOURCE IN LISTS ENGINE_SHADER_SOURCES)
    get_filename_component(SHADER_NAME "${SHADER_SOURCE}" NAME)
    vibrance_compile_vulkan11_shader("${SHADER_SOURCE}" "${SHADER_NAME}")
endforeach()
vibrance_compile_vulkan11_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/text_msdf.comp"
    "text_msdf_batch.comp" -DVIBRANCE_TEXT_BATCH=1)
vibrance_compile_vulkan11_shader("${CMAKE_CURRENT_SOURCE_DIR}/shaders/media_2d.comp"
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

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
            "${SHADER_SOURCE}" -o "${SHADER_BINARY}"
        DEPENDS "${SHADER_SOURCE}"
        COMMENT "Compiling ${SHADER_NAME} to SPIR-V"
        VERBATIM)
    list(APPEND EMBEDDED_SHADER_BINARIES "${SHADER_BINARY}")
    string(APPEND EMBEDDED_SHADER_MANIFEST_CONTENT "${SHADER_NAME}|${SHADER_BINARY}\n")
endforeach()
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

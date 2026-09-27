# Embed native Metal source; no external shader compiler or Vulkan SDK is needed.
file(GLOB METAL_SHADERS CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/renderer/metal/shaders/*.metal")
set(METAL_SHADER_CPP "${ENGINE_GENERATED_DIR}/metal_shaders.cpp")
set(_metal_embedded "#include <string_view>\n#include <stdexcept>\nstd::string_view vibrance_metal_shader(std::string_view name) {\n")
foreach(_shader IN LISTS METAL_SHADERS)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_shader}")
    get_filename_component(_name "${_shader}" NAME_WE)
    file(READ "${_shader}" _source)
    string(APPEND _metal_embedded "if (name == \"${_name}\") return R\"MSL(${_source})MSL\";\n")
    if(_name STREQUAL "text_msdf_comp")
        # Each z slice draws one disjoint glyph rectangle with its own constants.
        # Derive the batched entry from the same checked-in shader maths.
        string(REPLACE "kernel void text_msdf_comp(constant Renderer2DConstants& pc"
                       "kernel void text_msdf_batch_comp(constant Renderer2DConstants* glyphs"
                       _batch_source "${_source}")
        string(REPLACE "    int2 localPixel = int2(gl_GlobalInvocationID.xy);"
                       "    constant Renderer2DConstants& pc = glyphs[gl_GlobalInvocationID.z];\n    int2 localPixel = int2(gl_GlobalInvocationID.xy);"
                       _batch_source "${_batch_source}")
        string(APPEND _metal_embedded "if (name == \"text_msdf_batch_comp\") return R\"MSL(${_batch_source})MSL\";\n")
    endif()
endforeach()
string(APPEND _metal_embedded "throw std::runtime_error(\"Unknown Metal shader\");\n}\n")
file(CONFIGURE OUTPUT "${METAL_SHADER_CPP}" CONTENT "${_metal_embedded}" @ONLY)
list(APPEND ENGINE_SOURCES "${METAL_SHADER_CPP}")

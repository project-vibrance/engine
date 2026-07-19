if(NOT DEFINED MANIFEST OR NOT EXISTS "${MANIFEST}")
    message(FATAL_ERROR "EmbedShaders.cmake requires -DMANIFEST=<existing manifest>")
endif()
if(NOT DEFINED OUTPUT)
    message(FATAL_ERROR "EmbedShaders.cmake requires -DOUTPUT=<generated cpp>")
endif()

file(STRINGS "${MANIFEST}" SHADER_ENTRIES)
file(WRITE "${OUTPUT}"
    "#include \"embedded_shaders.h\"\n\n"
    "#include <cstddef>\n"
    "#include <span>\n"
    "#include <string_view>\n\n"
    "namespace\n{\n")

set(SHADER_CASES "")
set(SHADER_INDEX 0)
foreach(SHADER_ENTRY IN LISTS SHADER_ENTRIES)
    string(FIND "${SHADER_ENTRY}" "|" SEPARATOR_INDEX)
    if(SEPARATOR_INDEX LESS 1)
        message(FATAL_ERROR "Invalid shader manifest entry: ${SHADER_ENTRY}")
    endif()

    string(SUBSTRING "${SHADER_ENTRY}" 0 ${SEPARATOR_INDEX} SHADER_NAME)
    math(EXPR PATH_INDEX "${SEPARATOR_INDEX} + 1")
    string(SUBSTRING "${SHADER_ENTRY}" ${PATH_INDEX} -1 SHADER_PATH)
    if(NOT EXISTS "${SHADER_PATH}")
        message(FATAL_ERROR "Compiled shader does not exist: ${SHADER_PATH}")
    endif()

    file(READ "${SHADER_PATH}" SHADER_HEX HEX)
    string(REGEX REPLACE "(..)" "0x\\1," SHADER_BYTES "${SHADER_HEX}")
    file(APPEND "${OUTPUT}"
        "    alignas(4) constexpr unsigned char shader_${SHADER_INDEX}[] = {${SHADER_BYTES}};\n")
    string(APPEND SHADER_CASES
        "    if (fileName == \"${SHADER_NAME}\")\n"
        "    {\n"
        "        return {reinterpret_cast<const std::byte*>(shader_${SHADER_INDEX}), sizeof(shader_${SHADER_INDEX})};\n"
        "    }\n")
    math(EXPR SHADER_INDEX "${SHADER_INDEX} + 1")
endforeach()

file(APPEND "${OUTPUT}"
    "}\n\n"
    "std::span<const std::byte> vibrance_embedded_shader_spirv(std::string_view fileName)\n"
    "{\n"
    "${SHADER_CASES}"
    "    return {};\n"
    "}\n")

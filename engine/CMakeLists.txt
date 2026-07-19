file(GLOB_RECURSE ENGINE_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(ENGINE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "${ENGINE_GENERATED_DIR}")
set(ENGINE_MANIFEST_CPP "${ENGINE_GENERATED_DIR}/engine_manifest.cpp")
set(ENGINE_MANIFEST_JSON "${ENGINE_GENERATED_DIR}/vibrance_engine_manifest.json")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/engine_manifest.cpp.in"
    "${ENGINE_MANIFEST_CPP}"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vibrance_engine_manifest.json.in"
    "${ENGINE_MANIFEST_JSON}"
    @ONLY)
list(APPEND ENGINE_SOURCES "${ENGINE_MANIFEST_CPP}")

if(APPLE)
    file(GLOB_RECURSE ENGINE_OBJCXX_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/src/*.mm")
    list(APPEND ENGINE_SOURCES ${ENGINE_OBJCXX_SOURCES})
endif()

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

add_library(vibrance_engine SHARED ${ENGINE_SOURCES})
add_library(vibrance::engine ALIAS vibrance_engine)

set_target_properties(vibrance_engine PROPERTIES
    EXPORT_NAME engine
    OUTPUT_NAME vibrance_engine
    VERSION "${PROJECT_VERSION}"
    SOVERSION "${PROJECT_VERSION_MAJOR}"
    POSITION_INDEPENDENT_CODE ON)

target_compile_features(vibrance_engine PUBLIC cxx_std_20)
target_compile_definitions(vibrance_engine PRIVATE
    VIBRANCE_ENGINE_EXPORTS
    GLM_ENABLE_EXPERIMENTAL
    $<$<CONFIG:Debug>:DEBUG>
    $<$<CONFIG:Debug>:VIBRANCE_ENABLE_VULKAN_VALIDATION=1>
    $<$<NOT:$<CONFIG:Debug>>:VIBRANCE_ENABLE_VULKAN_VALIDATION=0>
    $<$<CONFIG:Debug>:VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING=1>
    $<$<NOT:$<CONFIG:Debug>>:VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING=0>)

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
        "${EMBEDDED_SHADER_DIR}"
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

if(SOLOUD_FOUND AND NOT TARGET soloud)
    file(GLOB SOLOUD_CORE_SOURCES CONFIGURE_DEPENDS "${SOLOUD_SOURCE_DIR}/core/*.cpp")
    add_library(soloud STATIC
        ${SOLOUD_CORE_SOURCES}
        "${SOLOUD_SOURCE_DIR}/audiosource/wav/dr_impl.cpp"
        "${SOLOUD_SOURCE_DIR}/audiosource/wav/soloud_wav.cpp"
        "${SOLOUD_SOURCE_DIR}/audiosource/wav/soloud_wavstream.cpp"
        "${SOLOUD_SOURCE_DIR}/audiosource/wav/stb_vorbis.c"
        "${SOLOUD_SOURCE_DIR}/backend/miniaudio/soloud_miniaudio.cpp")
    target_compile_definitions(soloud PUBLIC WITH_MINIAUDIO)
    target_include_directories(soloud
        PUBLIC "${SOLOUD_INCLUDE_DIR}"
        PRIVATE
            "${SOLOUD_SOURCE_DIR}/audiosource/wav"
            "${SOLOUD_SOURCE_DIR}/backend/miniaudio"
            "${SOLOUD_SOURCE_DIR}/backend/coreaudio")
    set_target_properties(soloud PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(WIN32)
        target_link_libraries(soloud PRIVATE ole32 winmm)
    elseif(APPLE)
        target_sources(soloud PRIVATE "${SOLOUD_SOURCE_DIR}/backend/coreaudio/soloud_coreaudio.cpp")
        target_compile_definitions(soloud PUBLIC WITH_COREAUDIO)
        target_link_libraries(soloud PRIVATE
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework CoreServices")
    else()
        target_link_libraries(soloud PRIVATE pthread dl m)
    endif()
endif()

target_link_libraries(vibrance_engine PRIVATE
    Vulkan::Vulkan
    ${FREETYPE_LIBRARIES}
    ${GLFW_LIBRARIES})

if(SOLOUD_FOUND)
    target_compile_definitions(vibrance_engine PRIVATE VIBRANCE_HAS_SOLOUD=1)
    target_link_libraries(vibrance_engine PRIVATE soloud)
endif()

if(FFMPEG_FOUND)
    target_link_libraries(vibrance_engine PRIVATE ${FFMPEG_LIBRARIES})
endif()

if(TARGET fastgltf::fastgltf)
    target_link_libraries(vibrance_engine PRIVATE fastgltf::fastgltf)
endif()

if(WIN32)
    target_link_libraries(vibrance_engine PRIVATE
        ole32 winmm user32 gdi32 shell32)
elseif(APPLE)
    target_link_libraries(vibrance_engine PRIVATE "-framework Cocoa")
else()
    target_link_libraries(vibrance_engine PRIVATE pthread dl m)
endif()

if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND VIBRANCE_STATIC_MINGW_RUNTIME)
    target_link_options(vibrance_engine PRIVATE
        -static
        -static-libgcc
        -static-libstdc++)
endif()

include(CMakePackageConfigHelpers)
set(CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/vibrance_engine")

install(TARGETS vibrance_engine
    EXPORT vibrance_engineTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# Public headers have one canonical source and install namespace.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/vibranceUI/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vibranceUI")

install(FILES "${ENGINE_MANIFEST_JSON}"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine")

# Copy every dependency header tree referenced by the public C++ API. All
# compiled dependency code remains private inside vibrance_engine.
install(DIRECTORY "${GLM_INCLUDE_DIR}/glm"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${ENTT_INCLUDE_DIR}/entt"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${VMA_HEADER_TO_INSTALL}"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/vma"
    RENAME vk_mem_alloc.h)
install(DIRECTORY "${GLFW_INCLUDE_DIRS}/GLFW"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(DIRECTORY "${Vulkan_INCLUDE_DIRS}/vulkan"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(EXISTS "${Vulkan_INCLUDE_DIRS}/vk_video")
    install(DIRECTORY "${Vulkan_INCLUDE_DIRS}/vk_video"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()

install(EXPORT vibrance_engineTargets
    FILE vibrance_engineTargets.cmake
    NAMESPACE vibrance::
    DESTINATION "${CONFIG_INSTALL_DIR}")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/vibrance_engineConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/vibrance_engineConfig.cmake"
    INSTALL_DESTINATION "${CONFIG_INSTALL_DIR}"
    PATH_VARS CMAKE_INSTALL_DATADIR)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/vibrance_engineConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/vibrance_engineConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/vibrance_engineConfigVersion.cmake"
    DESTINATION "${CONFIG_INSTALL_DIR}")

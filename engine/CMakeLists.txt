set(ENGINE_SOURCES
    src/audio/audio.cpp
    src/audio/loopback_capture.cpp
    src/audio/spectrum.cpp
    src/core/camera.cpp
    src/core/file.cpp
    src/core/logger.cpp
    src/core/secure_credential.cpp
    src/core/single_instance.cpp
    src/core/vma_usage.cpp
    src/display/placement.cpp
    src/display/preferences.cpp
    src/factories/mesh_factory.cpp
    src/glfw/panel_window.cpp
    src/glfw/ui_surface.cpp
    src/glfw/ui_window.cpp
    src/glfw/window.cpp
    src/localisation/localisation.cpp
    src/media/artwork.cpp
    src/media/artwork_view.cpp
    src/media/presentation.cpp
    src/media/session.cpp
    src/notifications/card.cpp
    src/notifications/provider.cpp
    src/notifications/stack_layout.cpp
    src/platform/native_dialog.cpp
    src/time/countdown_timer.cpp
    src/ui/animated_text_view.cpp
    src/ui/context_menu.cpp
    src/ui/dialog_controls.cpp
    src/ui/resource_loading.cpp
    src/ui/theme.cpp
    src/vibranceUI/animation/lottie.cpp
    src/vibranceUI/backdrop.cpp
    src/vibranceUI/directx/composition_presenter.cpp
    src/vibranceUI/vulkan/buffer.cpp
    src/vibranceUI/vulkan/command.cpp
    src/vibranceUI/vulkan/descriptors.cpp
    src/vibranceUI/vulkan/device.cpp
    src/vibranceUI/vulkan/font_atlas.cpp
    src/vibranceUI/vulkan/frame.cpp
    src/vibranceUI/vulkan/image.cpp
    src/vibranceUI/vulkan/instance.cpp
    src/vibranceUI/vulkan/media2d.cpp
    src/vibranceUI/vulkan/renderer.cpp
    src/vibranceUI/vulkan/renderer2d.cpp
    src/vibranceUI/vulkan/renderer3d.cpp
    src/vibranceUI/vulkan/shader.cpp
    src/vibranceUI/vulkan/swapchain.cpp
    src/vibranceUI/vulkan/synchronisation.cpp
    src/vibranceUI/vulkan/text_direction.cpp
)
# C++/WinRT stays in its MSVC companion DLL and must never enter the MinGW
# engine target. The MinGW side talks to it through a versioned POD C ABI.

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
    list(APPEND ENGINE_SOURCES src/glfw/window_macos.mm)
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

set(_vibrance_msvc_requirements
    Microsoft.VisualStudio.Component.VC.Tools.x86.x64)
if(WIN32 AND VIBRANCE_WINDOWS_ARCHITECTURE STREQUAL "arm64")
    list(APPEND _vibrance_msvc_requirements
        Microsoft.VisualStudio.Component.VC.Tools.ARM64)
endif()

set(VIBRANCE_WIN32_COMPOSITION_AVAILABLE OFF)
if(WIN32 AND VIBRANCE_ENABLE_WIN32_COMPOSITION)
    find_program(
        VIBRANCE_VSWHERE_EXECUTABLE
        NAMES vswhere.exe
        PATHS
            "C:/Program Files (x86)/Microsoft Visual Studio/Installer"
            "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
        NO_DEFAULT_PATH)
    if(VIBRANCE_VSWHERE_EXECUTABLE)
        execute_process(
            COMMAND
                "${VIBRANCE_VSWHERE_EXECUTABLE}"
                -latest
                -products *
                -requires ${_vibrance_msvc_requirements}
                -property installationPath
            OUTPUT_VARIABLE VIBRANCE_VISUAL_STUDIO_ROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()
    set(VIBRANCE_VCVARSALL
        "${VIBRANCE_VISUAL_STUDIO_ROOT}/VC/Auxiliary/Build/vcvarsall.bat")
    if(VIBRANCE_WINDOWS_ARCHITECTURE STREQUAL "arm64")
        set(VIBRANCE_COMPOSITION_VCVARS_ARCH amd64_arm64)
        set(VIBRANCE_COMPOSITION_SDK_ARCH arm64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(VIBRANCE_COMPOSITION_VCVARS_ARCH x64)
        set(VIBRANCE_COMPOSITION_SDK_ARCH x64)
    else()
        set(VIBRANCE_COMPOSITION_VCVARS_ARCH x86)
        set(VIBRANCE_COMPOSITION_SDK_ARCH x86)
    endif()

    # The private interop ABI was verified against the 19041 SDK generation.
    # Prefer an explicitly supplied SDK, then a normal machine-wide kit, and
    # finally the official Microsoft.Windows.SDK.CPP NuGet package in the
    # ignored development build cache. No absolute developer path is baked
    # into the installed engine SDK.
    set(_vibrance_sdk_root "${VIBRANCE_COMPOSITION_WINDOWS_SDK_ROOT}")
    set(_vibrance_sdk_arch_root "${VIBRANCE_COMPOSITION_WINDOWS_SDK_ARCH_ROOT}")
    if(NOT _vibrance_sdk_root)
        get_filename_component(_vibrance_windows_kits_root
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]"
            ABSOLUTE)
        if(EXISTS "${_vibrance_windows_kits_root}/Include/${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION}")
            set(_vibrance_sdk_root "${_vibrance_windows_kits_root}")
            set(_vibrance_sdk_arch_root
                "${_vibrance_windows_kits_root}/Lib/${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION}")
        endif()
    endif()
    if(NOT _vibrance_sdk_root)
        file(GLOB _vibrance_sdk_nuget_roots LIST_DIRECTORIES TRUE
            "${CMAKE_SOURCE_DIR}/build/sdk-19041/packages/microsoft.windows.sdk.cpp/*/c")
        file(GLOB _vibrance_sdk_nuget_arch_roots LIST_DIRECTORIES TRUE
            "${CMAKE_SOURCE_DIR}/build/sdk-19041/packages/microsoft.windows.sdk.cpp.${VIBRANCE_COMPOSITION_SDK_ARCH}/*/c")
        if(_vibrance_sdk_nuget_roots AND _vibrance_sdk_nuget_arch_roots)
            list(SORT _vibrance_sdk_nuget_roots COMPARE NATURAL ORDER DESCENDING)
            list(SORT _vibrance_sdk_nuget_arch_roots COMPARE NATURAL ORDER DESCENDING)
            list(GET _vibrance_sdk_nuget_roots 0 _vibrance_sdk_root)
            list(GET _vibrance_sdk_nuget_arch_roots 0 _vibrance_sdk_arch_root)
        endif()
    endif()

    set(VIBRANCE_COMPOSITION_SDK_INCLUDE_ROOT "")
    set(VIBRANCE_COMPOSITION_SDK_UM_LIBRARY_DIR "")
    set(VIBRANCE_COMPOSITION_SDK_UCRT_LIBRARY_DIR "")
    if(EXISTS "${_vibrance_sdk_root}/Include/${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION}")
        set(VIBRANCE_COMPOSITION_SDK_INCLUDE_ROOT
            "${_vibrance_sdk_root}/Include/${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION}")
        if(EXISTS "${_vibrance_sdk_arch_root}/um/${VIBRANCE_COMPOSITION_SDK_ARCH}")
            set(VIBRANCE_COMPOSITION_SDK_UM_LIBRARY_DIR
                "${_vibrance_sdk_arch_root}/um/${VIBRANCE_COMPOSITION_SDK_ARCH}")
            set(VIBRANCE_COMPOSITION_SDK_UCRT_LIBRARY_DIR
                "${_vibrance_sdk_arch_root}/ucrt/${VIBRANCE_COMPOSITION_SDK_ARCH}")
        endif()
    endif()

    if(EXISTS "${VIBRANCE_VCVARSALL}")
        set(VIBRANCE_COMPOSITION_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}/win32-composition")
        set(VIBRANCE_COMPOSITION_DLL
            "${VIBRANCE_COMPOSITION_DIRECTORY}/vibrance_win32_composition.dll")
        set(VIBRANCE_COMPOSITION_OBJECT_DIRECTORY
            "${VIBRANCE_COMPOSITION_DIRECTORY}/obj")
        set(VIBRANCE_COMPOSITION_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/vibranceUI/directx/composition_bridge.cpp")
        set(VIBRANCE_COMPOSITION_ABI_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/src/vibranceUI/directx/composition_bridge_abi.h")
        set(VIBRANCE_COMPOSITION_PRIVATE_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/src/vibranceUI/directx/private_composition_19041.h")
        set(VIBRANCE_COMPOSITION_BUILD_SCRIPT
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/build_composition_bridge.bat")
        add_custom_command(
            OUTPUT "${VIBRANCE_COMPOSITION_DLL}"
            COMMAND
                "${VIBRANCE_COMPOSITION_BUILD_SCRIPT}"
                "${VIBRANCE_VCVARSALL}"
                "${VIBRANCE_COMPOSITION_VCVARS_ARCH}"
                "${VIBRANCE_COMPOSITION_SOURCE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/vibranceUI/directx"
                "${VIBRANCE_COMPOSITION_DLL}"
                "${VIBRANCE_COMPOSITION_OBJECT_DIRECTORY}"
                "${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION}"
                "${VIBRANCE_COMPOSITION_SDK_INCLUDE_ROOT}"
                "${VIBRANCE_COMPOSITION_SDK_UM_LIBRARY_DIR}"
                "${VIBRANCE_COMPOSITION_SDK_UCRT_LIBRARY_DIR}"
            DEPENDS
                "${VIBRANCE_COMPOSITION_SOURCE}"
                "${VIBRANCE_COMPOSITION_ABI_HEADER}"
                "${VIBRANCE_COMPOSITION_PRIVATE_HEADER}"
                "${VIBRANCE_COMPOSITION_BUILD_SCRIPT}"
            BYPRODUCTS
                "${VIBRANCE_COMPOSITION_DIRECTORY}/vibrance_win32_composition.exp"
                "${VIBRANCE_COMPOSITION_DIRECTORY}/vibrance_win32_composition.lib"
            COMMENT "Building Windows Composition/D3D11 presenter bridge with MSVC"
            VERBATIM)
        add_custom_target(vibrance_win32_composition ALL
            DEPENDS "${VIBRANCE_COMPOSITION_DLL}")
        add_dependencies(vibrance_engine vibrance_win32_composition)
        set(VIBRANCE_WIN32_COMPOSITION_AVAILABLE ON)
        if(VIBRANCE_COMPOSITION_SDK_INCLUDE_ROOT AND
           VIBRANCE_COMPOSITION_SDK_UM_LIBRARY_DIR AND
           VIBRANCE_COMPOSITION_SDK_UCRT_LIBRARY_DIR)
            message(STATUS
                "Windows Composition presenter: enabled (${VIBRANCE_COMPOSITION_SDK_ARCH}, MSVC, D3D11, SDK ${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION})")
        else()
            message(WARNING
                "Windows SDK ${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION} was not found. "
                "The Composition bridge will use the SDK selected by vcvarsall; "
                "set VIBRANCE_COMPOSITION_WINDOWS_SDK_ROOT and "
                "VIBRANCE_COMPOSITION_WINDOWS_SDK_ARCH_ROOT for a reproducible private-ABI build.")
        endif()
    else()
        message(WARNING
            "VIBRANCE_ENABLE_WIN32_COMPOSITION is ON, but MSVC was not found. "
            "Windows Composition materials will resolve to off.")
    endif()
endif()

set(VIBRANCE_WIN32_INTEROP_AVAILABLE OFF)
if(WIN32 AND VIBRANCE_ENABLE_WIN32_INTEROP)
    if(NOT VIBRANCE_VSWHERE_EXECUTABLE)
        find_program(
            VIBRANCE_VSWHERE_EXECUTABLE
            NAMES vswhere.exe
            PATHS
                "C:/Program Files (x86)/Microsoft Visual Studio/Installer"
                "$ENV{ProgramFiles\(x86\)}/Microsoft Visual Studio/Installer"
            NO_DEFAULT_PATH)
    endif()
    if(VIBRANCE_VSWHERE_EXECUTABLE AND NOT VIBRANCE_VISUAL_STUDIO_ROOT)
        execute_process(
            COMMAND
                "${VIBRANCE_VSWHERE_EXECUTABLE}"
                -latest
                -products *
                -requires ${_vibrance_msvc_requirements}
                -property installationPath
            OUTPUT_VARIABLE VIBRANCE_VISUAL_STUDIO_ROOT
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
    endif()

    set(VIBRANCE_VCVARSALL
        "${VIBRANCE_VISUAL_STUDIO_ROOT}/VC/Auxiliary/Build/vcvarsall.bat")
    if(VIBRANCE_WINDOWS_ARCHITECTURE STREQUAL "arm64")
        set(VIBRANCE_WIN32_INTEROP_ARCH amd64_arm64)
    elseif(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(VIBRANCE_WIN32_INTEROP_ARCH x64)
    else()
        set(VIBRANCE_WIN32_INTEROP_ARCH x86)
    endif()

    if(EXISTS "${VIBRANCE_VCVARSALL}")
        set(VIBRANCE_WIN32_INTEROP_DIRECTORY
            "${CMAKE_CURRENT_BINARY_DIR}/win32-interop")
        set(VIBRANCE_WIN32_INTEROP_DLL
            "${VIBRANCE_WIN32_INTEROP_DIRECTORY}/vibrance_win32_interop.dll")
        set(VIBRANCE_WIN32_INTEROP_OBJECT_DIRECTORY
            "${VIBRANCE_WIN32_INTEROP_DIRECTORY}/obj")
        set(VIBRANCE_WIN32_INTEROP_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/interop_bridge.cpp")
        set(VIBRANCE_WIN32_INTEROP_ABI_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/include/vibranceUI/platform/win32_interop_abi.h")
        set(VIBRANCE_WIN32_INTEROP_BUILD_SCRIPT
            "${CMAKE_CURRENT_SOURCE_DIR}/platform/win32/build_interop_bridge.bat")

        add_custom_command(
            OUTPUT "${VIBRANCE_WIN32_INTEROP_DLL}"
            COMMAND
                "${VIBRANCE_WIN32_INTEROP_BUILD_SCRIPT}"
                "${VIBRANCE_VCVARSALL}"
                "${VIBRANCE_WIN32_INTEROP_ARCH}"
                "${VIBRANCE_WIN32_INTEROP_SOURCE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/include"
                "${VIBRANCE_WIN32_INTEROP_DLL}"
                "${VIBRANCE_WIN32_INTEROP_OBJECT_DIRECTORY}"
            DEPENDS
                "${VIBRANCE_WIN32_INTEROP_SOURCE}"
                "${VIBRANCE_WIN32_INTEROP_ABI_HEADER}"
                "${VIBRANCE_WIN32_INTEROP_BUILD_SCRIPT}"
            BYPRODUCTS
                "${VIBRANCE_WIN32_INTEROP_DIRECTORY}/vibrance_win32_interop.exp"
                "${VIBRANCE_WIN32_INTEROP_DIRECTORY}/vibrance_win32_interop.lib"
            COMMENT "Building media/notification Win32 interop companion with MSVC"
            VERBATIM)
        add_custom_target(vibrance_win32_interop ALL
            DEPENDS "${VIBRANCE_WIN32_INTEROP_DLL}")
        add_dependencies(vibrance_engine vibrance_win32_interop)
        set(VIBRANCE_WIN32_INTEROP_AVAILABLE ON)
        message(STATUS
            "Win32 media/notification interop: enabled (${VIBRANCE_WIN32_INTEROP_ARCH}, MSVC)")
    else()
        message(WARNING
            "VIBRANCE_ENABLE_WIN32_INTEROP is ON, but MSVC was not found. "
            "Media-session and notification providers will remain unavailable.")
    endif()
endif()

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
if (CMAKE_CXX_COMPILER_ID MATCHES "^(GNU|Clang)$")
    # Debug retains symbols/assertions, but renderer hot paths must not run at
    # GCC's default -O0. -O2 is required here because this target contains the
    # per-pixel batching and frame-submission code used by live UI profiling.
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

if(TARGET simdjson::simdjson)
    # The native Lottie reader uses simdjson privately; it remains folded into
    # the engine DLL and is not exposed as an application dependency.
    target_link_libraries(vibrance_engine PRIVATE simdjson::simdjson)
endif()

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
    target_compile_definitions(vibrance_engine PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    target_link_libraries(vibrance_engine PRIVATE
        advapi32 dwmapi mmdevapi ole32 uuid winmm user32 gdi32 shell32)
elseif(APPLE)
    target_link_libraries(vibrance_engine PRIVATE
        "-framework Cocoa"
        "-framework CoreFoundation"
        "-framework Security")
else()
    target_link_libraries(vibrance_engine PRIVATE pthread dl m)
endif()

if(WIN32 AND MINGW AND VIBRANCE_STATIC_MINGW_RUNTIME)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(vibrance_engine PRIVATE
            -static
            -static-libgcc
            -static-libstdc++)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(vibrance_engine PRIVATE -static)
    endif()
endif()

include(CMakePackageConfigHelpers)
set(CONFIG_INSTALL_DIR "${CMAKE_INSTALL_LIBDIR}/cmake/vibrance_engine")

install(TARGETS vibrance_engine
    EXPORT vibrance_engineTargets
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
if(VIBRANCE_WIN32_COMPOSITION_AVAILABLE)
    install(FILES "${VIBRANCE_COMPOSITION_DLL}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
if(VIBRANCE_WIN32_INTEROP_AVAILABLE)
    install(FILES "${VIBRANCE_WIN32_INTEROP_DLL}"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()

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

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

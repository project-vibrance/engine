cmake_minimum_required(VERSION 3.28)

include(./.env.cmake OPTIONAL RESULT_VARIABLE LOCAL_ENV)
message(STATUS "Local .env.cmake: ${LOCAL_ENV}")

# The compiler must be selected before project() initializes the toolchain.
if(CMAKE_GENERATOR STREQUAL "MinGW Makefiles" AND MINGW_PATH)
    set(CMAKE_C_COMPILER "${MINGW_PATH}/bin/gcc.exe" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${MINGW_PATH}/bin/g++.exe" CACHE FILEPATH "" FORCE)
endif()

set(VIBRANCE_MAJOR 0)
set(VIBRANCE_MINOR 5)
set(VIBRANCE_PATCH 0)

project(vibrance_engine VERSION ${VIBRANCE_MAJOR}.${VIBRANCE_MINOR}.${VIBRANCE_PATCH} LANGUAGES C CXX)

if(APPLE)
    enable_language(OBJCXX)
endif()

include(GNUInstallDirs)

# Release metadata is the single source for both the DLL query API and the
# machine-readable manifest installed with the SDK.
set(VIBRANCE_ENGINE_NAME "vibranceUI")
set(VIBRANCE_SUFFIX "20260809.0-a")
set(VIBRANCE_FULL_VERSION "${PROJECT_VERSION}-${VIBRANCE_SUFFIX}")
set(VIBRANCE_ENGINE_LAST_UPDATED "2026-08-02")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party dependencies statically" FORCE)

option(VIBRANCE_ENABLE_FFMPEG "Enable FFmpeg-backed video decoding when static FFmpeg archives are available" ON)
option(VIBRANCE_ENABLE_WIN32_COMPOSITION "Build the MSVC Windows Composition/D3D11 presenter bridge" ON)
option(VIBRANCE_STATIC_MINGW_RUNTIME "Link the MinGW C/C++ runtime into the engine DLL" ON)
set(FFMPEG_EXTRA_LIBRARIES "" CACHE STRING "Extra libraries required by a custom static FFmpeg build")
set(VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION "10.0.19041.0" CACHE STRING
    "Windows SDK used to compile the private Win32 Composition bridge")
set(VIBRANCE_COMPOSITION_WINDOWS_SDK_ROOT "" CACHE PATH
    "Optional Windows SDK root containing Include/<version> and Lib/<version>")
set(VIBRANCE_COMPOSITION_WINDOWS_SDK_ARCH_ROOT "" CACHE PATH
    "Optional NuGet architecture package root containing um/<arch> and ucrt/<arch>")

# Public engine headers expose types from these header-only SDKs. They are copied
# into the installed engine SDK so consumers need only one include directory.
if(DEFINED GLM_PATH)
    if(EXISTS "${GLM_PATH}/glm.hpp" AND NOT EXISTS "${GLM_PATH}/glm/glm.hpp")
        get_filename_component(GLM_INCLUDE_DIR "${GLM_PATH}" DIRECTORY)
    else()
        set(GLM_INCLUDE_DIR "${GLM_PATH}")
    endif()
else()
    find_path(GLM_INCLUDE_DIR NAMES glm/glm.hpp REQUIRED)
endif()

if(DEFINED VMA_PATH)
    if(EXISTS "${VMA_PATH}/include/vma/vk_mem_alloc.h")
        set(VMA_INCLUDE_DIR "${VMA_PATH}/include")
        set(VMA_HEADER_TO_INSTALL "${VMA_PATH}/include/vma/vk_mem_alloc.h")
    elseif(EXISTS "${VMA_PATH}/include/vk_mem_alloc.h")
        set(VMA_INCLUDE_DIR "${CMAKE_BINARY_DIR}/external/vma")
        set(VMA_HEADER_TO_INSTALL "${VMA_PATH}/include/vk_mem_alloc.h")
        file(MAKE_DIRECTORY "${VMA_INCLUDE_DIR}/vma")
        file(CONFIGURE
            OUTPUT "${VMA_INCLUDE_DIR}/vma/vk_mem_alloc.h"
            CONTENT "#pragma once\n#include \"${VMA_PATH}/include/vk_mem_alloc.h\"\n")
    else()
        message(FATAL_ERROR "VMA_PATH must point to a VMA checkout containing include/vk_mem_alloc.h or include/vma/vk_mem_alloc.h.")
    endif()
else()
    find_path(VMA_INCLUDE_DIR NAMES vma/vk_mem_alloc.h REQUIRED)
    set(VMA_HEADER_TO_INSTALL "${VMA_INCLUDE_DIR}/vma/vk_mem_alloc.h")
endif()

if(DEFINED ENTT_PATH)
    if(EXISTS "${ENTT_PATH}/entt/entt.hpp")
        set(ENTT_INCLUDE_DIR "${ENTT_PATH}")
    elseif(EXISTS "${ENTT_PATH}/single_include/entt/entt.hpp")
        set(ENTT_INCLUDE_DIR "${ENTT_PATH}/single_include")
    elseif(EXISTS "${ENTT_PATH}/src/entt/entt.hpp")
        set(ENTT_INCLUDE_DIR "${ENTT_PATH}/src")
    elseif(EXISTS "${ENTT_PATH}/include/entt/entt.hpp")
        set(ENTT_INCLUDE_DIR "${ENTT_PATH}/include")
    else()
        message(FATAL_ERROR "ENTT_PATH must point to a directory containing entt/entt.hpp.")
    endif()
else()
    find_path(ENTT_INCLUDE_DIR NAMES entt/entt.hpp REQUIRED)
endif()

if(DEFINED STB_PATH)
    if(EXISTS "${STB_PATH}/stb_image.h")
        set(STB_INCLUDE_DIR "${STB_PATH}")
    elseif(EXISTS "${STB_PATH}/include/stb_image.h")
        set(STB_INCLUDE_DIR "${STB_PATH}/include")
    else()
        message(FATAL_ERROR "STB_PATH must point to a directory containing stb_image.h.")
    endif()
else()
    find_path(STB_INCLUDE_DIR NAMES stb_image.h REQUIRED)
endif()

# FreeType is private to the DLL. Prefer a package, then accept a local static
# build/source tree for the development setup used by this repository.
if(DEFINED FREETYPE_PATH)
    set(_FREETYPE_SEARCH_ROOTS "${FREETYPE_PATH}")
    get_filename_component(_FREETYPE_PARENT_PATH "${FREETYPE_PATH}" DIRECTORY)
    if(EXISTS "${_FREETYPE_PARENT_PATH}/include/ft2build.h" OR EXISTS "${_FREETYPE_PARENT_PATH}/CMakeLists.txt")
        list(APPEND _FREETYPE_SEARCH_ROOTS "${_FREETYPE_PARENT_PATH}")
    endif()

    set(_FREETYPE_PACKAGE_PATHS)
    foreach(_freetype_root IN LISTS _FREETYPE_SEARCH_ROOTS)
        list(APPEND _FREETYPE_PACKAGE_PATHS
            "${_freetype_root}"
            "${_freetype_root}/build"
            "${_freetype_root}/install")
    endforeach()

    find_package(Freetype QUIET PATHS ${_FREETYPE_PACKAGE_PATHS} NO_DEFAULT_PATH)
    if(TARGET Freetype::Freetype)
        set(FREETYPE_TARGET Freetype::Freetype)
    else()
        find_path(FREETYPE_INCLUDE_DIR
            NAMES ft2build.h
            HINTS
                "${FREETYPE_PATH}/include"
                "${FREETYPE_PATH}/include/freetype2"
                "${FREETYPE_PATH}"
                "${_FREETYPE_PARENT_PATH}/include"
                "${_FREETYPE_PARENT_PATH}/include/freetype2"
            NO_DEFAULT_PATH)
        find_library(FREETYPE_LIBRARY
            NAMES freetype libfreetype
            HINTS
                "${FREETYPE_PATH}/lib"
                "${FREETYPE_PATH}/lib64"
                "${FREETYPE_PATH}/build"
                "${_FREETYPE_PARENT_PATH}/lib"
                "${_FREETYPE_PARENT_PATH}/lib64"
                "${_FREETYPE_PARENT_PATH}/build"
            NO_DEFAULT_PATH)

        if(NOT FREETYPE_LIBRARY AND EXISTS "${FREETYPE_PATH}/CMakeLists.txt")
            set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
            add_subdirectory("${FREETYPE_PATH}" "${CMAKE_BINARY_DIR}/external/freetype" EXCLUDE_FROM_ALL)
            set(FREETYPE_TARGET freetype)
        elseif(NOT FREETYPE_LIBRARY AND EXISTS "${_FREETYPE_PARENT_PATH}/CMakeLists.txt")
            set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
            set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
            add_subdirectory("${_FREETYPE_PARENT_PATH}" "${CMAKE_BINARY_DIR}/external/freetype" EXCLUDE_FROM_ALL)
            set(FREETYPE_TARGET freetype)
        endif()
    endif()
else()
    find_package(Freetype QUIET)
    if(TARGET Freetype::Freetype)
        set(FREETYPE_TARGET Freetype::Freetype)
    else()
        find_path(FREETYPE_INCLUDE_DIR NAMES ft2build.h)
        find_library(FREETYPE_LIBRARY NAMES freetype libfreetype)
    endif()
endif()

if(FREETYPE_TARGET)
    set(FREETYPE_LIBRARIES "${FREETYPE_TARGET}")
else()
    if(NOT FREETYPE_INCLUDE_DIR OR NOT FREETYPE_LIBRARY)
        message(FATAL_ERROR "FreeType is required. Set FREETYPE_PATH to a FreeType install, source, or build directory.")
    endif()
    set(FREETYPE_INCLUDE_DIRS "${FREETYPE_INCLUDE_DIR}" "${FREETYPE_INCLUDE_DIR}/freetype2")
    set(FREETYPE_LIBRARIES "${FREETYPE_LIBRARY}")
endif()

if(DEFINED NANOSVG_PATH)
    if(EXISTS "${NANOSVG_PATH}/nanosvg.h")
        set(NANOSVG_INCLUDE_DIR "${NANOSVG_PATH}")
    elseif(EXISTS "${NANOSVG_PATH}/src/nanosvg.h")
        set(NANOSVG_INCLUDE_DIR "${NANOSVG_PATH}/src")
    elseif(EXISTS "${NANOSVG_PATH}/include/nanosvg.h")
        set(NANOSVG_INCLUDE_DIR "${NANOSVG_PATH}/include")
    else()
        message(FATAL_ERROR "NANOSVG_PATH must point to a directory containing nanosvg.h and nanosvgrast.h.")
    endif()
    if(NOT EXISTS "${NANOSVG_INCLUDE_DIR}/nanosvgrast.h")
        message(FATAL_ERROR "NANOSVG_PATH must also contain nanosvgrast.h.")
    endif()
endif()

# A DLL import library cannot be folded into another DLL. Video support is
# therefore enabled only for an FFmpeg SDK that provides real static archives.
set(FFMPEG_FOUND OFF)
if(VIBRANCE_ENABLE_FFMPEG AND DEFINED FFMPEG_PATH)
    find_program(FFMPEG_EXECUTABLE
        NAMES ffmpeg ffmpeg.exe
        HINTS "${FFMPEG_PATH}/bin"
        NO_DEFAULT_PATH)
    find_path(FFMPEG_INCLUDE_DIR
        NAMES libavformat/avformat.h
        HINTS "${FFMPEG_PATH}/include" "${FFMPEG_PATH}"
        NO_DEFAULT_PATH)

    set(_FFMPEG_ALL_STATIC TRUE)
    foreach(_ffmpeg_lib avformat avcodec avutil swscale swresample)
        string(TOUPPER "${_ffmpeg_lib}" _ffmpeg_var)
        unset(FFMPEG_${_ffmpeg_var}_LIBRARY CACHE)
        find_file(FFMPEG_${_ffmpeg_var}_LIBRARY
            NAMES "lib${_ffmpeg_lib}.a" "${_ffmpeg_lib}.a"
            HINTS "${FFMPEG_PATH}/lib" "${FFMPEG_PATH}/lib64"
            NO_DEFAULT_PATH)
        if(NOT FFMPEG_${_ffmpeg_var}_LIBRARY OR FFMPEG_${_ffmpeg_var}_LIBRARY MATCHES "\\.dll\\.a$")
            set(_FFMPEG_ALL_STATIC FALSE)
        endif()
    endforeach()

    if(FFMPEG_INCLUDE_DIR AND _FFMPEG_ALL_STATIC)
        set(FFMPEG_FOUND ON)
        # GNU ld resolves static archives from left to right. swscale uses
        # libavutil, so libavutil must follow it for FFmpeg 8.1 and newer.
        set(FFMPEG_LIBRARIES
            "${FFMPEG_AVFORMAT_LIBRARY}"
            "${FFMPEG_AVCODEC_LIBRARY}"
            "${FFMPEG_SWRESAMPLE_LIBRARY}"
            "${FFMPEG_SWSCALE_LIBRARY}"
            "${FFMPEG_AVUTIL_LIBRARY}")
        list(APPEND FFMPEG_LIBRARIES ${FFMPEG_EXTRA_LIBRARIES})
        if(WIN32)
            # av_random_bytes() uses CNG in static Windows builds.
            list(APPEND FFMPEG_LIBRARIES bcrypt)
        endif()
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            list(APPEND FFMPEG_LIBRARIES m atomic)
        endif()
        message(STATUS "Static FFmpeg SDK found: ${FFMPEG_PATH}")
    elseif(FFMPEG_EXECUTABLE AND NOT FFMPEG_INCLUDE_DIR)
        message(WARNING
            "FFmpeg at '${FFMPEG_PATH}' contains static command-line tools, but not a linkable SDK. "
            "A self-contained vibrance_engine DLL requires include/libavformat/avformat.h and the "
            "static archives libavformat.a, libavcodec.a, libavutil.a, libswscale.a, and libswresample.a. "
            "Video decoding is disabled until those SDK files are installed into this prefix.")
    else()
        message(WARNING
            "FFmpeg at '${FFMPEG_PATH}' is shared-only or an incomplete SDK. Video decoding is disabled so "
            "vibrance_engine remains a self-contained DLL. Point FFMPEG_PATH at a static FFmpeg build "
            "(libavformat.a, libavcodec.a, libavutil.a, libswscale.a, libswresample.a) to enable it.")
    endif()
endif()

set(SOLOUD_FOUND OFF)
if(DEFINED SOLOUD_PATH)
    find_path(SOLOUD_INCLUDE_DIR NAMES soloud.h HINTS "${SOLOUD_PATH}/include" "${SOLOUD_PATH}" NO_DEFAULT_PATH)
    find_path(SOLOUD_SOURCE_DIR
        NAMES core/soloud.cpp audiosource/wav/soloud_wav.cpp backend/miniaudio/soloud_miniaudio.cpp
        HINTS "${SOLOUD_PATH}/src"
        NO_DEFAULT_PATH)
else()
    find_path(SOLOUD_INCLUDE_DIR NAMES soloud.h)
    find_path(SOLOUD_SOURCE_DIR NAMES core/soloud.cpp audiosource/wav/soloud_wav.cpp backend/miniaudio/soloud_miniaudio.cpp)
endif()

if(SOLOUD_INCLUDE_DIR AND SOLOUD_SOURCE_DIR)
    set(SOLOUD_FOUND ON)
    message(STATUS "SoLoud found: ${SOLOUD_INCLUDE_DIR}")
elseif(DEFINED SOLOUD_PATH)
    message(FATAL_ERROR "SOLOUD_PATH must point to a SoLoud checkout containing include/soloud.h and src/core/soloud.cpp.")
else()
    message(WARNING "SoLoud was not found. Engine audio will compile as a no-op.")
endif()

# Vulkan is the one intentional runtime dependency: vulkan-1.dll is the
# operating-system/driver loader and cannot be embedded in an application DLL.
if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    find_path(Vulkan_INCLUDE_DIRS
        NAMES vulkan/vulkan.h vulkan/vulkan.hpp
        HINTS "${VULKAN_SDK_PATH}/Include" "$ENV{VULKAN_SDK}/Include"
        REQUIRED)
    find_file(VULKAN_DLL
        NAMES vulkan-1.dll
        HINTS "$ENV{WINDIR}/System32" "C:/Windows/System32"
        REQUIRED)
    add_library(Vulkan::Vulkan INTERFACE IMPORTED)
    set_target_properties(Vulkan::Vulkan PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}"
        INTERFACE_LINK_LIBRARIES "${VULKAN_DLL}")
else()
    find_package(Vulkan REQUIRED)
endif()

# Shaders are compiled during the engine build and embedded in the DLL. glslc
# is a build tool only; downstream applications do not need shaderc.
find_program(GLSLC_EXECUTABLE
    NAMES glslc glslc.exe
    HINTS "${VULKAN_SDK_PATH}/Bin" "$ENV{VULKAN_SDK}/Bin"
    REQUIRED)

if(DEFINED GLFW_PATH)
    set(GLFW_INCLUDE_DIRS "${GLFW_PATH}/include")
    if(MSVC)
        set(_GLFW_LIBRARY_HINTS "${GLFW_PATH}/lib-vc2022" "${GLFW_PATH}/build/src")
    else()
        set(_GLFW_LIBRARY_HINTS "${GLFW_PATH}/build/src")
    endif()
    find_library(GLFW_LIBRARIES NAMES glfw3 glfw HINTS ${_GLFW_LIBRARY_HINTS} REQUIRED)
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

if(DEFINED SIMDJSON_PATH AND EXISTS "${SIMDJSON_PATH}/simdjson.cpp")
    add_library(simdjson STATIC "${SIMDJSON_PATH}/simdjson.cpp")
    add_library(simdjson::simdjson ALIAS simdjson)
    target_include_directories(simdjson PUBLIC "${SIMDJSON_PATH}")
    target_compile_features(simdjson PUBLIC cxx_std_17)
    if(WIN32 AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        # simdjson's MinGW AVX2/AVX-512 kernels can fault while executing from
        # the engine DLL even when CPUID advertises the matching feature set.
        # Prefer correctness for asset loading; Lottie JSON is small enough
        # that the portable implementation has no measurable frame-time cost.
        target_compile_definitions(simdjson PRIVATE
            SIMDJSON_IMPLEMENTATION_ICELAKE=0
            SIMDJSON_IMPLEMENTATION_HASWELL=0
            SIMDJSON_IMPLEMENTATION_WESTMERE=0
            SIMDJSON_IMPLEMENTATION_FALLBACK=1)
    endif()
    set_target_properties(simdjson PROPERTIES POSITION_INDEPENDENT_CODE ON)
elseif(DEFINED SIMDJSON_PATH)
    message(FATAL_ERROR "SIMDJSON_PATH must point to a directory containing simdjson.cpp and simdjson.h.")
endif()

if(DEFINED GLTF_PATH)
    if(NOT TARGET simdjson::simdjson)
        message(FATAL_ERROR "GLTF_PATH requires SIMDJSON_PATH to point to simdjson.cpp and simdjson.h.")
    endif()
    foreach(_fastgltf_source src/fastgltf.cpp src/base64.cpp src/io.cpp)
        if(NOT EXISTS "${GLTF_PATH}/${_fastgltf_source}")
            message(FATAL_ERROR "GLTF_PATH is missing ${_fastgltf_source}.")
        endif()
    endforeach()
    add_library(vibrance_fastgltf STATIC
        "${GLTF_PATH}/src/fastgltf.cpp"
        "${GLTF_PATH}/src/base64.cpp"
        "${GLTF_PATH}/src/io.cpp")
    add_library(fastgltf::fastgltf ALIAS vibrance_fastgltf)
    target_include_directories(vibrance_fastgltf PUBLIC "${GLTF_PATH}/include")
    target_compile_features(vibrance_fastgltf PUBLIC cxx_std_17)
    target_compile_definitions(vibrance_fastgltf PUBLIC
        FASTGLTF_USE_CUSTOM_SMALLVECTOR=0
        FASTGLTF_ENABLE_DEPRECATED_EXT=0
        FASTGLTF_DISABLE_CUSTOM_MEMORY_POOL=1
        FASTGLTF_USE_64BIT_FLOAT=0
        FASTGLTF_ENABLE_KHR_IMPLICIT_SHAPES=0
        FASTGLTF_ENABLE_KHR_PHYSICS_RIGID_BODIES=0)
    target_link_libraries(vibrance_fastgltf PRIVATE simdjson::simdjson)
    set_target_properties(vibrance_fastgltf PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

add_subdirectory(engine)

# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build third-party dependencies statically" FORCE)

option(VIBRANCE_ENABLE_FFMPEG "Enable FFmpeg-backed video decoding when static FFmpeg archives are available" ON)
option(VIBRANCE_ENABLE_WIN32_COMPOSITION "Build the MSVC Windows Composition/D3D11 presenter bridge" ON)
option(VIBRANCE_ENABLE_WIN32_INTEROP "Build the MSVC media/notification C++/WinRT companion" ON)
option(VIBRANCE_STATIC_MINGW_RUNTIME "Link the MinGW C/C++ runtime into the engine DLL" ON)
set(FFMPEG_EXTRA_LIBRARIES "" CACHE STRING "Extra libraries required by a custom static FFmpeg build")
set(VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION "10.0.19041.0" CACHE STRING
    "Windows SDK used to compile the private Win32 Composition bridge")
set(VIBRANCE_COMPOSITION_WINDOWS_SDK_ROOT "" CACHE PATH
    "Optional Windows SDK root containing Include/<version> and Lib/<version>")
set(VIBRANCE_COMPOSITION_WINDOWS_SDK_ARCH_ROOT "" CACHE PATH
    "Optional NuGet architecture package root containing um/<arch> and ucrt/<arch>")
set(GLFW_ARM64_PATH "" CACHE PATH
    "Optional GLFW source tree or ARM64 install used by Windows ARM64 builds")
set(FREETYPE_ARM64_PATH "" CACHE PATH
    "Optional FreeType source tree or ARM64 install used by Windows ARM64 builds")
set(FFMPEG_ARM64_PATH "" CACHE PATH
    "Optional static FFmpeg ARM64 SDK prefix")

if(WIN32 AND VIBRANCE_WINDOWS_ARCHITECTURE STREQUAL "arm64")
    if(GLFW_ARM64_PATH)
        set(GLFW_PATH "${GLFW_ARM64_PATH}")
    endif()
    if(FREETYPE_ARM64_PATH)
        set(FREETYPE_PATH "${FREETYPE_ARM64_PATH}")
    endif()
    if(FFMPEG_ARM64_PATH)
        set(FFMPEG_PATH "${FFMPEG_ARM64_PATH}")
    elseif(VIBRANCE_ENABLE_FFMPEG)
        set(VIBRANCE_ENABLE_FFMPEG OFF)
        message(STATUS
            "Static FFmpeg video support is disabled for ARM64. Set "
            "FFMPEG_ARM64_PATH to an ARM64 static SDK to enable it.")
    endif()
endif()

# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

if(APPLE)
    if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 13.0)
        message(FATAL_ERROR "The Metal renderer requires macOS 13 or later.")
    endif()
    find_path(METAL_CPP_INCLUDE_DIR Metal/Metal.hpp
        HINTS "${METAL_CPP_PATH}" "$ENV{HOME}/dev/metal-cpp" REQUIRED)
    if(NOT EXISTS "${METAL_CPP_INCLUDE_DIR}/Metal/MTLCommandQueue.hpp")
        message(FATAL_ERROR "Metal-cpp with Metal 3 headers is required.")
    endif()
    set(_vibrance_macos_arch "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT _vibrance_macos_arch)
        set(_vibrance_macos_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(NOT _vibrance_macos_arch MATCHES "^(arm64|x86_64)$")
        message(FATAL_ERROR "Build one macOS architecture at a time: arm64 or x86_64. Use the unixBuild.sh platform targets 'silicon', 'intel', or 'all'.")
    endif()
    if(_vibrance_macos_arch STREQUAL "arm64")
        set(_vibrance_macos_suffix SILICON)
        set(VIBRANCE_TARGET_PLATFORM "macos-silicon")
    else()
        set(_vibrance_macos_suffix INTEL)
        set(VIBRANCE_TARGET_PLATFORM "macos-intel")
    endif()
    foreach(_dependency GLFW FREETYPE FFMPEG)
        if(${_dependency}_${_vibrance_macos_suffix}_PATH)
            set(${_dependency}_PATH "${${_dependency}_${_vibrance_macos_suffix}_PATH}")
        endif()
    endforeach()
endif()

set(VIBRANCE_WINDOWS_ARCHITECTURE "auto" CACHE STRING
    "Windows target architecture: auto, x64, or arm64")
set_property(CACHE VIBRANCE_WINDOWS_ARCHITECTURE PROPERTY STRINGS auto x64 arm64)

if(WIN32)
    string(TOLOWER "${VIBRANCE_WINDOWS_ARCHITECTURE}" _vibrance_requested_arch)
    if(_vibrance_requested_arch STREQUAL "auto")
        if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(ARM64|arm64|aarch64)$" OR
           CMAKE_SYSTEM_PROCESSOR MATCHES "^(ARM64|arm64|aarch64)$")
            set(_vibrance_requested_arch arm64)
        else()
            set(_vibrance_requested_arch x64)
        endif()
    endif()
    if(NOT _vibrance_requested_arch MATCHES "^(x64|arm64)$")
        message(FATAL_ERROR
            "VIBRANCE_WINDOWS_ARCHITECTURE must be auto, x64, or arm64.")
    endif()

    if(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(ARM64|arm64|aarch64)$")
        set(_vibrance_compiler_arch arm64)
    elseif(CMAKE_CXX_COMPILER_ARCHITECTURE_ID MATCHES "^(x64|X64|AMD64|amd64|x86_64)$")
        set(_vibrance_compiler_arch x64)
    else()
        set(_vibrance_compiler_arch "")
    endif()
    if(_vibrance_compiler_arch AND
       NOT "${_vibrance_compiler_arch}" STREQUAL "${_vibrance_requested_arch}")
        message(FATAL_ERROR
            "The selected compiler targets ${_vibrance_compiler_arch}, but "
            "VIBRANCE_WINDOWS_ARCHITECTURE is ${_vibrance_requested_arch}.")
    endif()
    if(_vibrance_requested_arch STREQUAL "arm64" AND
       (NOT MINGW OR NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang"))
        message(FATAL_ERROR
            "Windows ARM64 requires the LLVM-MinGW toolchain. Configure with "
            "cmake/toolchains/windows-arm64-llvm-mingw.cmake.")
    endif()

    set(VIBRANCE_WINDOWS_ARCHITECTURE "${_vibrance_requested_arch}" CACHE STRING
        "Windows target architecture: auto, x64, or arm64" FORCE)
    set(VIBRANCE_TARGET_PLATFORM "windows-${VIBRANCE_WINDOWS_ARCHITECTURE}")
elseif(APPLE)
    if(_vibrance_macos_arch STREQUAL "arm64")
        set(VIBRANCE_TARGET_PLATFORM "macos-silicon")
    else()
        set(VIBRANCE_TARGET_PLATFORM "macos-intel")
    endif()
else()
    string(TOLOWER "${CMAKE_SYSTEM_NAME}" _vibrance_system_name)
    set(VIBRANCE_TARGET_PLATFORM "${_vibrance_system_name}-${CMAKE_SYSTEM_PROCESSOR}")
endif()
message(STATUS "Vibrance target platform: ${VIBRANCE_TARGET_PLATFORM}")

if(WIN32 AND CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_CURRENT_SOURCE_DIR}/install/${VIBRANCE_TARGET_PLATFORM}"
        CACHE PATH "Engine SDK install prefix" FORCE)
endif()

if(APPLE)

    enable_language(OBJC OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

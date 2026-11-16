# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

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
            "${PROJECT_SOURCE_DIR}/scripts/win32/build_interop_bridge.bat")

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
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/playback_quality_probe.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/notification_activation.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/notification_payload_store.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/notification_record_cache.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/media_process_routing.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/media_seek_fallback.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/media/playback_seek_policy.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/media/playback_quality_detection.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/media/timeline_range.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/vibranceUI/media/playback_quality.h"
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

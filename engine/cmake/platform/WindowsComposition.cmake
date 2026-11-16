# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

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
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/composition_bridge.cpp")
        set(VIBRANCE_COMPOSITION_ABI_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/composition_bridge_abi.h")
        set(VIBRANCE_COMPOSITION_PRIVATE_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/private_composition_19041.h")
        set(VIBRANCE_LIQUID_SHADER_ABI_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_shader_abi.h")
        set(VIBRANCE_LIQUID_D3D_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_d3d11.cpp")
        set(VIBRANCE_LIQUID_D3D_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_d3d11.h")
        set(VIBRANCE_LIQUID_DWM_PROTOCOL_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_protocol.h")
        set(VIBRANCE_LIQUID_DWM_SECURITY_HEADER
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_security.h")
        set(VIBRANCE_COMPOSITION_BUILD_SCRIPT
            "${PROJECT_SOURCE_DIR}/scripts/win32/build_composition_bridge.bat")
        add_custom_command(
            OUTPUT "${VIBRANCE_COMPOSITION_DLL}"
            COMMAND
                "${VIBRANCE_COMPOSITION_BUILD_SCRIPT}"
                "${VIBRANCE_VCVARSALL}"
                "${VIBRANCE_COMPOSITION_VCVARS_ARCH}"
                "${VIBRANCE_COMPOSITION_SOURCE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32"
                "${VIBRANCE_COMPOSITION_DLL}"
                "${VIBRANCE_COMPOSITION_OBJECT_DIRECTORY}"
                "${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION}"
                "${VIBRANCE_COMPOSITION_SDK_INCLUDE_ROOT}"
                "${VIBRANCE_COMPOSITION_SDK_UM_LIBRARY_DIR}"
                "${VIBRANCE_COMPOSITION_SDK_UCRT_LIBRARY_DIR}"
            DEPENDS
                "${VIBRANCE_COMPOSITION_SOURCE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/include/vibranceUI/graphics/liquid_glass_profile.h"
                "${VIBRANCE_COMPOSITION_ABI_HEADER}"
                "${VIBRANCE_COMPOSITION_PRIVATE_HEADER}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/system_backdrop_policy.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/composition_region_effects.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/composition_raw_backdrop_clip.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_cap_policy.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_output_bounds.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/composition_present_retry.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/composition_upload.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/composition_damage.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/composition_copy_diagnostics.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_windows10_layout.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_windows10_session.h"
                "${VIBRANCE_LIQUID_SHADER_ABI_HEADER}"
                "${VIBRANCE_LIQUID_D3D_SOURCE}"
                "${VIBRANCE_LIQUID_D3D_HEADER}"
                "${VIBRANCE_LIQUID_DWM_PROTOCOL_HEADER}"
                "${VIBRANCE_LIQUID_DWM_SECURITY_HEADER}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_startup.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_runtime_identity.h"
                "${VIBRANCE_COMPOSITION_BUILD_SCRIPT}"
            BYPRODUCTS
                "${VIBRANCE_COMPOSITION_DIRECTORY}/vibrance_win32_composition.exp"
                "${VIBRANCE_COMPOSITION_DIRECTORY}/vibrance_win32_composition.lib"
            COMMENT "Building Windows Composition/D3D11 presenter bridge with MSVC"
            VERBATIM)
        add_custom_target(vibrance_win32_composition ALL
            DEPENDS "${VIBRANCE_COMPOSITION_DLL}")
        add_dependencies(vibrance_engine vibrance_win32_composition)
        set(VIBRANCE_LIQUID_DWM_EXTENSION
            "${VIBRANCE_COMPOSITION_DIRECTORY}/vibrance_liquid_dwm_extension.dll")
        set(VIBRANCE_LIQUID_DWM_BROKER
            "${VIBRANCE_COMPOSITION_DIRECTORY}/liquid_glass_dwm_broker.exe")
        set(VIBRANCE_LIQUID_DWM_OBSERVER
            "${VIBRANCE_COMPOSITION_DIRECTORY}/liquid_glass_dwm_observer.exe")


        get_filename_component(_vibrance_debugging_kits_root
            "[HKEY_LOCAL_MACHINE\\SOFTWARE\\Microsoft\\Windows Kits\\Installed Roots;KitsRoot10]"
            ABSOLUTE)
        find_file(VIBRANCE_LIQUID_DBGHELP_RUNTIME NAMES dbghelp.dll
            HINTS "${_vibrance_sdk_root}/Debuggers/${VIBRANCE_COMPOSITION_SDK_ARCH}"
                "${_vibrance_debugging_kits_root}/Debuggers/${VIBRANCE_COMPOSITION_SDK_ARCH}"
            NO_DEFAULT_PATH)
        find_file(VIBRANCE_LIQUID_SYMSRV_RUNTIME NAMES symsrv.dll
            HINTS "${_vibrance_sdk_root}/Debuggers/${VIBRANCE_COMPOSITION_SDK_ARCH}"
                "${_vibrance_debugging_kits_root}/Debuggers/${VIBRANCE_COMPOSITION_SDK_ARCH}"
            NO_DEFAULT_PATH)
        if(NOT VIBRANCE_LIQUID_DBGHELP_RUNTIME OR NOT VIBRANCE_LIQUID_SYMSRV_RUNTIME)
            message(FATAL_ERROR
                "Windows Liquid Glass requires matching dbghelp.dll and symsrv.dll. "
                "Install the Windows SDK Debugging Tools for the target architecture.")
        endif()
        set(VIBRANCE_LIQUID_DWM_EXTENSION_SCRIPT
            "${PROJECT_SOURCE_DIR}/scripts/win32/build_liquid_dwm_extension.bat")
        set(VIBRANCE_LIQUID_DWM_BROKER_SCRIPT
            "${PROJECT_SOURCE_DIR}/scripts/win32/build_liquid_dwm_broker.bat")
        set(VIBRANCE_LIQUID_DWM_EXTENSION_SOURCE
            "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_present.cpp")
        set(VIBRANCE_LIQUID_DWM_BROKER_SOURCE
            "${PROJECT_SOURCE_DIR}/scripts/win32/liquid_glass_dwm_broker.cpp")
        add_custom_command(
            OUTPUT "${VIBRANCE_LIQUID_DWM_EXTENSION}"
            COMMAND "${VIBRANCE_LIQUID_DWM_EXTENSION_SCRIPT}"
                "${VIBRANCE_VCVARSALL}"
                "${VIBRANCE_COMPOSITION_VCVARS_ARCH}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32"
                "${VIBRANCE_LIQUID_DWM_EXTENSION}"
                "${VIBRANCE_COMPOSITION_DIRECTORY}/dwm-extension-obj"
                "${VIBRANCE_COMPOSITION_SDK_INCLUDE_ROOT}"
                "${VIBRANCE_COMPOSITION_SDK_UM_LIBRARY_DIR}"
                "${VIBRANCE_COMPOSITION_SDK_UCRT_LIBRARY_DIR}"
            DEPENDS "${VIBRANCE_LIQUID_DWM_EXTENSION_SOURCE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_batch.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_windows10.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_windows10_lifecycle.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_windows10_lifecycle_policy.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_windows10_session.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_windows10_layout.h"
                "${VIBRANCE_LIQUID_DWM_PROTOCOL_HEADER}"
                "${VIBRANCE_LIQUID_DWM_SECURITY_HEADER}"
                "${VIBRANCE_LIQUID_D3D_SOURCE}"
                "${VIBRANCE_LIQUID_D3D_HEADER}"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_cap_policy.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_output_bounds.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/detours/detours.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/detours/modules.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/detours/disasm.cpp"
                "${CMAKE_CURRENT_SOURCE_DIR}/third_party/detours/detours.h"
                "${VIBRANCE_LIQUID_DWM_EXTENSION_SCRIPT}"
            COMMENT "Building Windows Liquid Glass DWM extension with MSVC"
            VERBATIM)
        add_custom_target(vibrance_liquid_dwm_extension ALL
            DEPENDS "${VIBRANCE_LIQUID_DWM_EXTENSION}")
        add_custom_command(
            OUTPUT "${VIBRANCE_LIQUID_DWM_BROKER}" "${VIBRANCE_LIQUID_DWM_OBSERVER}"
            COMMAND "${VIBRANCE_LIQUID_DWM_BROKER_SCRIPT}"
                "${VIBRANCE_VCVARSALL}"
                "${VIBRANCE_COMPOSITION_VCVARS_ARCH}"
                "${VIBRANCE_LIQUID_DWM_BROKER}"
                "${VIBRANCE_COMPOSITION_DIRECTORY}/dwm-broker-obj"
                "${VIBRANCE_COMPOSITION_SDK_INCLUDE_ROOT}"
                "${VIBRANCE_COMPOSITION_SDK_UM_LIBRARY_DIR}"
                "${VIBRANCE_COMPOSITION_SDK_UCRT_LIBRARY_DIR}"
            DEPENDS "${VIBRANCE_LIQUID_DWM_BROKER_SOURCE}"
                "${PROJECT_SOURCE_DIR}/scripts/win32/liquid_glass_runtime_cache.h"
                "${PROJECT_SOURCE_DIR}/scripts/win32/liquid_glass_broker_startup.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_runtime_identity.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_startup.h"
                "${CMAKE_CURRENT_SOURCE_DIR}/src/platform/win32/liquid_glass_dwm_windows10_layout.h"
                "${VIBRANCE_LIQUID_DWM_PROTOCOL_HEADER}"
                "${VIBRANCE_LIQUID_DWM_SECURITY_HEADER}"
                "${VIBRANCE_LIQUID_DWM_BROKER_SCRIPT}"
            COMMENT "Building Windows Liquid Glass DWM broker with MSVC"
            VERBATIM)
        add_custom_target(vibrance_liquid_dwm_broker ALL
            DEPENDS "${VIBRANCE_LIQUID_DWM_BROKER}" "${VIBRANCE_LIQUID_DWM_OBSERVER}")
        add_dependencies(vibrance_engine
            vibrance_liquid_dwm_extension vibrance_liquid_dwm_broker)
        find_program(VIBRANCE_LIQUID_FXC_EXECUTABLE
            NAMES fxc.exe
            HINTS
                "${_vibrance_sdk_root}/bin/${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION}/x64"
                "${_vibrance_sdk_root}/bin/${VIBRANCE_COMPOSITION_WINDOWS_SDK_VERSION}/${VIBRANCE_COMPOSITION_SDK_ARCH}"
            NO_DEFAULT_PATH)
        if(VIBRANCE_LIQUID_FXC_EXECUTABLE)
            set(VIBRANCE_LIQUID_SHADER_SOURCE
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/liquid_glass_win32.hlsl")
            set(VIBRANCE_LIQUID_SHADER_OBJECT
                "${VIBRANCE_COMPOSITION_DIRECTORY}/liquid_glass_win32.cso")
            add_custom_command(
                OUTPUT "${VIBRANCE_LIQUID_SHADER_OBJECT}"
                COMMAND "${VIBRANCE_LIQUID_FXC_EXECUTABLE}"
                    /nologo /T ps_4_0 /E main
                    /Fo "${VIBRANCE_LIQUID_SHADER_OBJECT}"
                    "${VIBRANCE_LIQUID_SHADER_SOURCE}"
                DEPENDS "${VIBRANCE_LIQUID_SHADER_SOURCE}"
                COMMENT "Compiling the Windows Liquid Glass optical pixel shader"
                VERBATIM)
            add_custom_target(vibrance_liquid_glass_shader ALL
                DEPENDS "${VIBRANCE_LIQUID_SHADER_OBJECT}")
            set(VIBRANCE_LIQUID_COMPARE_SOURCE
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/liquid_glass_compare_win32.hlsl")
            set(VIBRANCE_LIQUID_COMPARE_OBJECT
                "${VIBRANCE_COMPOSITION_DIRECTORY}/liquid_glass_compare.cso")
            add_custom_command(
                OUTPUT "${VIBRANCE_LIQUID_COMPARE_OBJECT}"
                COMMAND "${VIBRANCE_LIQUID_FXC_EXECUTABLE}"
                    /nologo /T ps_4_0 /E main
                    /Fo "${VIBRANCE_LIQUID_COMPARE_OBJECT}"
                    "${VIBRANCE_LIQUID_COMPARE_SOURCE}"
                DEPENDS "${VIBRANCE_LIQUID_COMPARE_SOURCE}"
                COMMENT "Compiling the Windows Liquid Glass backdrop comparison shader"
                VERBATIM)
            add_custom_target(vibrance_liquid_glass_compare_shader ALL
                DEPENDS "${VIBRANCE_LIQUID_COMPARE_OBJECT}")
            set(VIBRANCE_LIQUID_UI_SHADER_SOURCE
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/liquid_glass_ui_win32.hlsl")
            set(VIBRANCE_LIQUID_UI_SHADER_OBJECT
                "${VIBRANCE_COMPOSITION_DIRECTORY}/liquid_glass_ui_win32.cso")
            add_custom_command(
                OUTPUT "${VIBRANCE_LIQUID_UI_SHADER_OBJECT}"
                COMMAND "${VIBRANCE_LIQUID_FXC_EXECUTABLE}"
                    /nologo /T ps_4_0_level_9_3 /E main
                    /Fo "${VIBRANCE_LIQUID_UI_SHADER_OBJECT}"
                    "${VIBRANCE_LIQUID_UI_SHADER_SOURCE}"
                DEPENDS "${VIBRANCE_LIQUID_UI_SHADER_SOURCE}"
                COMMENT "Compiling the Windows Liquid Glass UI composite pixel shader"
                VERBATIM)
            add_custom_target(vibrance_liquid_glass_ui_shader ALL
                DEPENDS "${VIBRANCE_LIQUID_UI_SHADER_OBJECT}")
            set(VIBRANCE_LIQUID_FULLSCREEN_SOURCE
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/liquid_glass_fullscreen_win32.hlsl")
            set(VIBRANCE_LIQUID_FULLSCREEN_OBJECT
                "${VIBRANCE_COMPOSITION_DIRECTORY}/liquid_glass_fullscreen.cso")
            add_custom_command(
                OUTPUT "${VIBRANCE_LIQUID_FULLSCREEN_OBJECT}"
                COMMAND "${VIBRANCE_LIQUID_FXC_EXECUTABLE}"
                    /nologo /T vs_4_0 /E main
                    /Fo "${VIBRANCE_LIQUID_FULLSCREEN_OBJECT}"
                    "${VIBRANCE_LIQUID_FULLSCREEN_SOURCE}"
                DEPENDS "${VIBRANCE_LIQUID_FULLSCREEN_SOURCE}"
                COMMENT "Compiling Windows Liquid Glass fullscreen vertex shader"
                VERBATIM)
            add_custom_target(vibrance_liquid_glass_fullscreen_shader ALL
                DEPENDS "${VIBRANCE_LIQUID_FULLSCREEN_OBJECT}")
            set(VIBRANCE_LIQUID_BLUR_SOURCE
                "${CMAKE_CURRENT_SOURCE_DIR}/shaders/liquid_glass_blur_win32.hlsl")
            set(VIBRANCE_LIQUID_BLUR_OBJECTS "")
            foreach(_vibrance_blur_axis IN ITEMS horizontal vertical)
                set(_vibrance_blur_object
                    "${VIBRANCE_COMPOSITION_DIRECTORY}/liquid_glass_blur_${_vibrance_blur_axis}.cso")
                add_custom_command(
                    OUTPUT "${_vibrance_blur_object}"
                    COMMAND "${VIBRANCE_LIQUID_FXC_EXECUTABLE}"
                        /nologo /T ps_4_0 /E "blur_${_vibrance_blur_axis}"
                        /Fo "${_vibrance_blur_object}"
                        "${VIBRANCE_LIQUID_BLUR_SOURCE}"
                    DEPENDS "${VIBRANCE_LIQUID_BLUR_SOURCE}"
                    COMMENT "Compiling Windows Liquid Glass ${_vibrance_blur_axis} blur pass"
                    VERBATIM)
                list(APPEND VIBRANCE_LIQUID_BLUR_OBJECTS "${_vibrance_blur_object}")
            endforeach()
            add_custom_target(vibrance_liquid_glass_blur_shaders ALL
                DEPENDS ${VIBRANCE_LIQUID_BLUR_OBJECTS})
        endif()
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

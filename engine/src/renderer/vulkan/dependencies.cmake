# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0


if(DEFINED VMA_PATH)
    if(EXISTS "${VMA_PATH}/include/vma/vk_mem_alloc.h")
        set(VMA_HEADER_TO_INSTALL "${VMA_PATH}/include/vma/vk_mem_alloc.h")
    elseif(EXISTS "${VMA_PATH}/include/vk_mem_alloc.h")
        set(VMA_HEADER_TO_INSTALL "${VMA_PATH}/include/vk_mem_alloc.h")
    else()
        message(FATAL_ERROR "VMA_PATH must point to a VMA checkout containing include/vk_mem_alloc.h or include/vma/vk_mem_alloc.h.")
    endif()
elseif(UNIX)
    find_file(VMA_HEADER_TO_INSTALL NAMES vk_mem_alloc.h
        HINTS "${VULKAN_SDK_PATH}/include" "$ENV{VULKAN_SDK}/include"
        PATH_SUFFIXES vma REQUIRED)
else()
    find_path(VMA_INCLUDE_DIR NAMES vma/vk_mem_alloc.h REQUIRED)
    set(VMA_HEADER_TO_INSTALL "${VMA_INCLUDE_DIR}/vma/vk_mem_alloc.h")
endif()

get_filename_component(VMA_HEADER_TO_INSTALL "${VMA_HEADER_TO_INSTALL}" ABSOLUTE)
get_filename_component(_vibrance_vma_header_dir "${VMA_HEADER_TO_INSTALL}" DIRECTORY)
get_filename_component(_vibrance_vma_header_dir_name "${_vibrance_vma_header_dir}" NAME)
if(_vibrance_vma_header_dir_name STREQUAL "vma")
    get_filename_component(VMA_INCLUDE_DIR "${_vibrance_vma_header_dir}" DIRECTORY)
else()
    set(VMA_INCLUDE_DIR "${CMAKE_BINARY_DIR}/external/vma")
    file(CONFIGURE
        OUTPUT "${VMA_INCLUDE_DIR}/vma/vk_mem_alloc.h"
        CONTENT "#pragma once\n#include \"${VMA_HEADER_TO_INSTALL}\"\n")
endif()


if(WIN32 AND MINGW)
    find_path(Vulkan_INCLUDE_DIRS
        NAMES vulkan/vulkan.h vulkan/vulkan.hpp
        HINTS "${VULKAN_SDK_PATH}/Include" "$ENV{VULKAN_SDK}/Include"
        REQUIRED)
    add_library(Vulkan::Vulkan INTERFACE IMPORTED)
    if(VIBRANCE_WINDOWS_ARCHITECTURE STREQUAL "arm64")
        find_program(VIBRANCE_LLVM_DLLTOOL
            NAMES llvm-dlltool.exe llvm-dlltool
            HINTS "${LLVM_MINGW_PATH}/bin"
            REQUIRED)
        file(GLOB _vibrance_vulkan_headers
            "${Vulkan_INCLUDE_DIRS}/vulkan/*.h")
        set(_vibrance_vulkan_exports)
        foreach(_vibrance_vulkan_header IN LISTS _vibrance_vulkan_headers)
            file(STRINGS "${_vibrance_vulkan_header}"
                _vibrance_vulkan_prototypes
                REGEX "VKAPI_ATTR[ \t]+.*VKAPI_CALL[ \t]+vk[A-Za-z0-9_]+\\(")
            foreach(_vibrance_vulkan_prototype IN LISTS _vibrance_vulkan_prototypes)
                if(_vibrance_vulkan_prototype MATCHES
                   "VKAPI_CALL[ \t]+(vk[A-Za-z0-9_]+)\\(")
                    list(APPEND _vibrance_vulkan_exports "${CMAKE_MATCH_1}")
                endif()
            endforeach()
        endforeach()
        list(REMOVE_DUPLICATES _vibrance_vulkan_exports)
        list(SORT _vibrance_vulkan_exports)
        if(NOT _vibrance_vulkan_exports)
            message(FATAL_ERROR
                "No Vulkan loader exports could be read from ${Vulkan_INCLUDE_DIRS}.")
        endif()
        string(JOIN "\n" _vibrance_vulkan_export_lines
            ${_vibrance_vulkan_exports})
        set(_vibrance_vulkan_import_dir
            "${CMAKE_BINARY_DIR}/generated/vulkan-arm64")
        file(MAKE_DIRECTORY "${_vibrance_vulkan_import_dir}")
        set(_vibrance_vulkan_def
            "${_vibrance_vulkan_import_dir}/vulkan-1.def")
        set(_vibrance_vulkan_import_library
            "${_vibrance_vulkan_import_dir}/libvulkan-1.a")
        file(WRITE "${_vibrance_vulkan_def}"
            "LIBRARY vulkan-1.dll\nEXPORTS\n${_vibrance_vulkan_export_lines}\n")
        execute_process(
            COMMAND "${VIBRANCE_LLVM_DLLTOOL}"
                -m arm64
                -d "${_vibrance_vulkan_def}"
                -l "${_vibrance_vulkan_import_library}"
            RESULT_VARIABLE _vibrance_vulkan_dlltool_result
            ERROR_VARIABLE _vibrance_vulkan_dlltool_error)
        if(NOT _vibrance_vulkan_dlltool_result EQUAL 0 OR
           NOT EXISTS "${_vibrance_vulkan_import_library}")
            message(FATAL_ERROR
                "Could not generate the ARM64 Vulkan loader import library: "
                "${_vibrance_vulkan_dlltool_error}")
        endif()
        set_target_properties(Vulkan::Vulkan PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${_vibrance_vulkan_import_library}")
    else()
        find_file(VULKAN_DLL
            NAMES vulkan-1.dll
            HINTS "$ENV{WINDIR}/System32" "C:/Windows/System32"
            REQUIRED)
        set_target_properties(Vulkan::Vulkan PROPERTIES
            INTERFACE_INCLUDE_DIRECTORIES "${Vulkan_INCLUDE_DIRS}"
            INTERFACE_LINK_LIBRARIES "${VULKAN_DLL}")
    endif()
else()
    if(UNIX AND VULKAN_SDK_PATH)
        set(Vulkan_ROOT "${VULKAN_SDK_PATH}")
    endif()
    find_package(Vulkan REQUIRED)
endif()



find_program(GLSLC_EXECUTABLE
    NAMES glslc glslc.exe
    HINTS "${VULKAN_SDK_PATH}/Bin" "$ENV{VULKAN_SDK}/Bin"
          "${VULKAN_SDK_PATH}/bin" "$ENV{VULKAN_SDK}/bin"
    REQUIRED)

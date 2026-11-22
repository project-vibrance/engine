# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

if(HARFBUZZ_PATH)
    cmake_path(ABSOLUTE_PATH HARFBUZZ_PATH BASE_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        NORMALIZE OUTPUT_VARIABLE _vibrance_harfbuzz_source_dir)
    foreach(_harfbuzz_required_file src/harfbuzz.cc src/hb.h COPYING)
        if(NOT EXISTS "${_vibrance_harfbuzz_source_dir}/${_harfbuzz_required_file}")
            message(FATAL_ERROR
                "HARFBUZZ_PATH must point to a HarfBuzz source tree containing "
                "${_harfbuzz_required_file}: ${_vibrance_harfbuzz_source_dir}")
        endif()
    endforeach()
    message(STATUS "HarfBuzz local source: ${_vibrance_harfbuzz_source_dir}")
    add_library(vibrance_harfbuzz STATIC "${_vibrance_harfbuzz_source_dir}/src/harfbuzz.cc")
    target_include_directories(vibrance_harfbuzz SYSTEM PUBLIC "${_vibrance_harfbuzz_source_dir}/src")
    target_compile_definitions(vibrance_harfbuzz PRIVATE HB_NO_SUBSET HB_NO_DRAW HB_NO_PAINT)
    target_compile_options(vibrance_harfbuzz PRIVATE $<$<CXX_COMPILER_ID:Clang,GNU,AppleClang>:-w>)
    set_target_properties(vibrance_harfbuzz PROPERTIES POSITION_INDEPENDENT_CODE ON)
    install(FILES "${_vibrance_harfbuzz_source_dir}/COPYING"
        DESTINATION "share/vibrance_engine/licences" RENAME HarfBuzz.txt)
    set(HARFBUZZ_TARGET vibrance_harfbuzz)
elseif(UNIX AND NOT APPLE)
    find_package(harfbuzz CONFIG QUIET)
    if(TARGET harfbuzz::harfbuzz)
        set(HARFBUZZ_TARGET harfbuzz::harfbuzz)
    else()
        find_package(PkgConfig REQUIRED)
        pkg_check_modules(HarfBuzz REQUIRED IMPORTED_TARGET harfbuzz)
        set(HARFBUZZ_TARGET PkgConfig::HarfBuzz)
    endif()
else()
    message(FATAL_ERROR "Set HARFBUZZ_PATH in .env.cmake to the local HarfBuzz source tree.")
endif()



if((APPLE OR (WIN32 AND VIBRANCE_WINDOWS_ARCHITECTURE STREQUAL "arm64")) AND
   DEFINED FREETYPE_PATH AND EXISTS "${FREETYPE_PATH}/CMakeLists.txt")


    set(FT_DISABLE_ZLIB TRUE CACHE BOOL "" FORCE)
    set(FT_DISABLE_BZIP2 TRUE CACHE BOOL "" FORCE)
    set(FT_DISABLE_PNG TRUE CACHE BOOL "" FORCE)
    set(FT_DISABLE_HARFBUZZ TRUE CACHE BOOL "" FORCE)
    set(FT_DISABLE_BROTLI TRUE CACHE BOOL "" FORCE)
    add_subdirectory("${FREETYPE_PATH}"
        "${CMAKE_BINARY_DIR}/external/freetype" EXCLUDE_FROM_ALL)
    set(FREETYPE_TARGET freetype)
elseif(DEFINED FREETYPE_PATH)
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

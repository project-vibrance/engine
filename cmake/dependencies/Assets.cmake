# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

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
elseif(UNIX AND NOT APPLE)
    find_path(NANOSVG_INCLUDE_DIR NAMES nanosvgrast.h PATH_SUFFIXES nanosvg)
    if(NOT EXISTS "${NANOSVG_INCLUDE_DIR}/nanosvg.h")
        unset(NANOSVG_INCLUDE_DIR CACHE)
    endif()
endif()

if(DEFINED SIMDJSON_PATH)
    if(NOT EXISTS "${SIMDJSON_PATH}/simdjson.cpp" OR NOT EXISTS "${SIMDJSON_PATH}/simdjson.h")
        message(FATAL_ERROR "SIMDJSON_PATH must point to a directory containing simdjson.cpp and simdjson.h.")
    endif()
    add_library(simdjson STATIC "${SIMDJSON_PATH}/simdjson.cpp")
    add_library(simdjson::simdjson ALIAS simdjson)
    target_include_directories(simdjson PUBLIC "${SIMDJSON_PATH}")
    target_compile_features(simdjson PUBLIC cxx_std_17)
    if(WIN32 AND MINGW)




        target_compile_definitions(simdjson PRIVATE
            SIMDJSON_IMPLEMENTATION_ICELAKE=0
            SIMDJSON_IMPLEMENTATION_HASWELL=0
            SIMDJSON_IMPLEMENTATION_WESTMERE=0
            SIMDJSON_IMPLEMENTATION_FALLBACK=1)
    endif()
    set_target_properties(simdjson PROPERTIES POSITION_INDEPENDENT_CODE ON)
elseif(UNIX AND NOT APPLE)
    find_package(simdjson CONFIG REQUIRED)
else()
    message(FATAL_ERROR "Set SIMDJSON_PATH in .env.cmake to the directory containing simdjson.cpp and simdjson.h.")
endif()

if(DEFINED GLTF_PATH)
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
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        target_compile_options(vibrance_fastgltf PRIVATE -flax-vector-conversions)
    endif()
    target_link_libraries(vibrance_fastgltf PRIVATE simdjson::simdjson)
    set_target_properties(vibrance_fastgltf PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif()

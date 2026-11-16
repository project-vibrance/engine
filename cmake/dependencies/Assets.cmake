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
endif()

if(DEFINED SIMDJSON_PATH AND EXISTS "${SIMDJSON_PATH}/simdjson.cpp")
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

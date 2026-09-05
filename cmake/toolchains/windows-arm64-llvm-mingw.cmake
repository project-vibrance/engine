set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_VERSION 10.0)
set(CMAKE_SYSTEM_PROCESSOR ARM64)

set(VIBRANCE_WINDOWS_ARCHITECTURE arm64 CACHE STRING
    "Vibrance Windows target architecture" FORCE)

if(NOT LLVM_MINGW_PATH AND DEFINED ENV{LLVM_MINGW_PATH})
    file(TO_CMAKE_PATH "$ENV{LLVM_MINGW_PATH}" LLVM_MINGW_PATH)
endif()

# A repository-local .env.cmake may provide LLVM_MINGW_PATH. This is evaluated
# only while establishing the initial toolchain; compiler paths are cached for
# CMake's later try-compile projects.
if(NOT LLVM_MINGW_PATH AND EXISTS "${CMAKE_SOURCE_DIR}/.env.cmake")
    include("${CMAKE_SOURCE_DIR}/.env.cmake")
endif()

if(NOT LLVM_MINGW_PATH)
    message(FATAL_ERROR
        "Windows ARM64 builds require LLVM_MINGW_PATH to name an unpacked "
        "LLVM-MinGW toolchain. Set it in the environment, .env.cmake, or with "
        "-DLLVM_MINGW_PATH=C:/path/to/llvm-mingw.")
endif()

get_filename_component(LLVM_MINGW_PATH "${LLVM_MINGW_PATH}" ABSOLUTE)
set(LLVM_MINGW_PATH "${LLVM_MINGW_PATH}" CACHE PATH
    "Unpacked UCRT LLVM-MinGW toolchain root" FORCE)
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES LLVM_MINGW_PATH)
set(_VIBRANCE_LLVM_MINGW_BIN "${LLVM_MINGW_PATH}/bin")
set(_VIBRANCE_ARM64_PREFIX "aarch64-w64-mingw32")

set(CMAKE_C_COMPILER
    "${_VIBRANCE_LLVM_MINGW_BIN}/${_VIBRANCE_ARM64_PREFIX}-clang.exe"
    CACHE FILEPATH "" FORCE)
set(CMAKE_CXX_COMPILER
    "${_VIBRANCE_LLVM_MINGW_BIN}/${_VIBRANCE_ARM64_PREFIX}-clang++.exe"
    CACHE FILEPATH "" FORCE)
set(CMAKE_RC_COMPILER
    "${_VIBRANCE_LLVM_MINGW_BIN}/${_VIBRANCE_ARM64_PREFIX}-windres.exe"
    CACHE FILEPATH "" FORCE)
set(CMAKE_AR "${_VIBRANCE_LLVM_MINGW_BIN}/llvm-ar.exe"
    CACHE FILEPATH "" FORCE)
set(CMAKE_RANLIB "${_VIBRANCE_LLVM_MINGW_BIN}/llvm-ranlib.exe"
    CACHE FILEPATH "" FORCE)
set(CMAKE_DLLTOOL "${_VIBRANCE_LLVM_MINGW_BIN}/llvm-dlltool.exe"
    CACHE FILEPATH "" FORCE)

foreach(_vibrance_tool IN ITEMS
    CMAKE_C_COMPILER CMAKE_CXX_COMPILER CMAKE_RC_COMPILER CMAKE_AR
    CMAKE_RANLIB CMAKE_DLLTOOL)
    if(NOT EXISTS "${${_vibrance_tool}}")
        message(FATAL_ERROR
            "LLVM-MinGW is incomplete: ${_vibrance_tool} was not found at "
            "'${${_vibrance_tool}}'.")
    endif()
endforeach()

set(CMAKE_FIND_ROOT_PATH
    "${LLVM_MINGW_PATH}/${_VIBRANCE_ARM64_PREFIX}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)

unset(_vibrance_tool)
unset(_VIBRANCE_ARM64_PREFIX)
unset(_VIBRANCE_LLVM_MINGW_BIN)

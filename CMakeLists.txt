# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.28)

include(./.env.cmake OPTIONAL RESULT_VARIABLE LOCAL_ENV)
message(STATUS "Local .env.cmake: ${LOCAL_ENV}")

if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()

if(CMAKE_GENERATOR STREQUAL "MinGW Makefiles" AND MINGW_PATH AND
   NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_C_COMPILER "${MINGW_PATH}/bin/gcc.exe" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${MINGW_PATH}/bin/g++.exe" CACHE FILEPATH "" FORCE)
endif()

set(VIBRANCE_MAJOR 0)
set(VIBRANCE_MINOR 8)
set(VIBRANCE_PATCH 0)

project(vibrance_engine VERSION ${VIBRANCE_MAJOR}.${VIBRANCE_MINOR}.${VIBRANCE_PATCH} LANGUAGES C CXX)

include(cmake/build/Platform.cmake)
include(CTest)
if(UNIX AND NOT APPLE)
    set(CMAKE_INSTALL_LIBDIR "lib" CACHE PATH "SDK library directory")
endif()
include(GNUInstallDirs)
include(cmake/build/Metadata.cmake)
include(cmake/build/Options.cmake)
include(cmake/dependencies/Headers.cmake)
include(cmake/dependencies/Text.cmake)
include(cmake/dependencies/Media.cmake)
include(cmake/dependencies/Windowing.cmake)
include(cmake/dependencies/Assets.cmake)

add_subdirectory(engine)

install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchains/windows-arm64-llvm-mingw.cmake"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/toolchains")

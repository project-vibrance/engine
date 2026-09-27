cmake_minimum_required(VERSION 3.28)

include(./.env.cmake OPTIONAL RESULT_VARIABLE LOCAL_ENV)
message(STATUS "Local .env.cmake: ${LOCAL_ENV}")

if(CMAKE_HOST_APPLE AND NOT CMAKE_OSX_DEPLOYMENT_TARGET)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()
# Select the compiler before project() initialises the toolchain.
if(CMAKE_GENERATOR STREQUAL "MinGW Makefiles" AND MINGW_PATH AND
   NOT CMAKE_TOOLCHAIN_FILE)
    set(CMAKE_C_COMPILER "${MINGW_PATH}/bin/gcc.exe" CACHE FILEPATH "" FORCE)
    set(CMAKE_CXX_COMPILER "${MINGW_PATH}/bin/g++.exe" CACHE FILEPATH "" FORCE)
endif()

set(VIBRANCE_MAJOR 0)
set(VIBRANCE_MINOR 7)
set(VIBRANCE_PATCH 0)

project(vibrance_engine VERSION ${VIBRANCE_MAJOR}.${VIBRANCE_MINOR}.${VIBRANCE_PATCH} LANGUAGES C CXX)

if(APPLE)
    if(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS 13.0)
        message(FATAL_ERROR "The Metal renderer requires macOS 13 or later.")
    endif()
    find_path(METAL_CPP_INCLUDE_DIR Metal/Metal.hpp
        HINTS "${METAL_CPP_PATH}" "$ENV{HOME}/dev/metal-cpp" REQUIRED)
    if(NOT EXISTS "${METAL_CPP_INCLUDE_DIR}/Metal/MTL4CommandQueue.hpp")
        message(FATAL_ERROR "Metal-cpp with Metal 4 headers is required (macOS 26 release or newer).")
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

include(CTest)

if(APPLE)
    # GLFW's .m files require Objective-C, while engine adapters use Objective-C++.
    enable_language(OBJC OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
    set(CMAKE_OBJCXX_EXTENSIONS OFF)
endif()

include(GNUInstallDirs)

# Release metadata is the single source for both the DLL query API and the
# machine-readable manifest installed with the SDK.
set(VIBRANCE_ENGINE_NAME "vibranceUI")
set(VIBRANCE_ENGINE_RELEASE_STAGE "alpha" CACHE STRING
    "Human-facing engine build stage: alpha, beta, release-candidate, or release" FORCE)
set_property(CACHE VIBRANCE_ENGINE_RELEASE_STAGE PROPERTY STRINGS
    alpha beta release-candidate release)
set(VIBRANCE_ENGINE_BUILD_SEQUENCE 0 CACHE STRING
    "Human-facing engine build sequence for the current UTC build date" FORCE)
if(NOT VIBRANCE_ENGINE_BUILD_SEQUENCE MATCHES "^[0-9]+$")
    message(FATAL_ERROR
        "VIBRANCE_ENGINE_BUILD_SEQUENCE must be a non-negative integer.")
endif()
if(VIBRANCE_ENGINE_RELEASE_STAGE STREQUAL "alpha")
    set(VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX "a")
elseif(VIBRANCE_ENGINE_RELEASE_STAGE STREQUAL "beta")
    set(VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX "b")
elseif(VIBRANCE_ENGINE_RELEASE_STAGE STREQUAL "release-candidate")
    set(VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX "rc")
elseif(VIBRANCE_ENGINE_RELEASE_STAGE STREQUAL "release")
    set(VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX "r")
else()
    message(FATAL_ERROR
        "VIBRANCE_ENGINE_RELEASE_STAGE must be alpha, beta, release-candidate, or release.")
endif()
string(TIMESTAMP VIBRANCE_ENGINE_BUILD_DATE "%Y%m%d" UTC)
string(TIMESTAMP VIBRANCE_ENGINE_LAST_UPDATED "%Y-%m-%d" UTC)
set(VIBRANCE_SUFFIX
    "${VIBRANCE_ENGINE_BUILD_DATE}.${VIBRANCE_ENGINE_BUILD_SEQUENCE}-${VIBRANCE_ENGINE_RELEASE_STAGE_SUFFIX}")
set(VIBRANCE_FULL_VERSION "${PROJECT_VERSION}-${VIBRANCE_SUFFIX}")
message(STATUS "vibranceUI display build: ${VIBRANCE_FULL_VERSION}")

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

# Public engine headers expose types from these header-only SDKs. They are copied
# into the installed engine SDK so consumers need only one include directory.
if(DEFINED GLM_PATH)
    if(EXISTS "${GLM_PATH}/glm.hpp" AND NOT EXISTS "${GLM_PATH}/glm/glm.hpp")
        get_filename_component(GLM_INCLUDE_DIR "${GLM_PATH}" DIRECTORY)
    else()
        set(GLM_INCLUDE_DIR "${GLM_PATH}")
    endif()
else()
    find_path(GLM_INCLUDE_DIR NAMES glm/glm.hpp REQUIRED)
endif()

if(NOT APPLE)
    include("${CMAKE_CURRENT_SOURCE_DIR}/engine/src/renderer/vulkan/dependencies.cmake")
endif()

if(DEFINED ENTT_PATH)
    if(EXISTS "${ENTT_PATH}/entt/entt.hpp")
        set(ENTT_INCLUDE_DIR "${ENTT_PATH}")
    elseif(EXISTS "${ENTT_PATH}/single_include/entt/entt.hpp")
        set(ENTT_INCLUDE_DIR "${ENTT_PATH}/single_include")
    elseif(EXISTS "${ENTT_PATH}/src/entt/entt.hpp")
        set(ENTT_INCLUDE_DIR "${ENTT_PATH}/src")
    elseif(EXISTS "${ENTT_PATH}/include/entt/entt.hpp")
        set(ENTT_INCLUDE_DIR "${ENTT_PATH}/include")
    else()
        message(FATAL_ERROR "ENTT_PATH must point to a directory containing entt/entt.hpp.")
    endif()
else()
    find_path(ENTT_INCLUDE_DIR NAMES entt/entt.hpp REQUIRED)
endif()

if(DEFINED STB_PATH)
    if(EXISTS "${STB_PATH}/stb_image.h")
        set(STB_INCLUDE_DIR "${STB_PATH}")
    elseif(EXISTS "${STB_PATH}/include/stb_image.h")
        set(STB_INCLUDE_DIR "${STB_PATH}/include")
    else()
        message(FATAL_ERROR "STB_PATH must point to a directory containing stb_image.h.")
    endif()
else()
    find_path(STB_INCLUDE_DIR NAMES stb_image.h REQUIRED)
endif()

# FreeType is private to the DLL. Prefer a package, then accept a local static
# build/source tree for the development setup used by this repository.
if((APPLE OR (WIN32 AND VIBRANCE_WINDOWS_ARCHITECTURE STREQUAL "arm64")) AND
   DEFINED FREETYPE_PATH AND EXISTS "${FREETYPE_PATH}/CMakeLists.txt")
    # Build in this architecture's own tree rather than reusing a native archive
    # under the shared source checkout.
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

# A DLL import library cannot be folded into another DLL. Video support is
# therefore enabled only for an FFmpeg SDK that provides real static archives.
set(FFMPEG_FOUND OFF)
if(VIBRANCE_ENABLE_FFMPEG AND DEFINED FFMPEG_PATH)
    find_program(FFMPEG_EXECUTABLE
        NAMES ffmpeg ffmpeg.exe
        HINTS "${FFMPEG_PATH}/bin"
        NO_DEFAULT_PATH)
    find_path(FFMPEG_INCLUDE_DIR
        NAMES libavformat/avformat.h
        HINTS "${FFMPEG_PATH}/include" "${FFMPEG_PATH}"
        NO_DEFAULT_PATH)

    set(_FFMPEG_ALL_STATIC TRUE)
    foreach(_ffmpeg_lib avformat avcodec avutil swscale swresample)
        string(TOUPPER "${_ffmpeg_lib}" _ffmpeg_var)
        unset(FFMPEG_${_ffmpeg_var}_LIBRARY CACHE)
        find_file(FFMPEG_${_ffmpeg_var}_LIBRARY
            NAMES "lib${_ffmpeg_lib}.a" "${_ffmpeg_lib}.a"
            HINTS "${FFMPEG_PATH}/lib" "${FFMPEG_PATH}/lib64"
            NO_DEFAULT_PATH)
        if(NOT FFMPEG_${_ffmpeg_var}_LIBRARY OR FFMPEG_${_ffmpeg_var}_LIBRARY MATCHES "\\.dll\\.a$")
            set(_FFMPEG_ALL_STATIC FALSE)
        endif()
    endforeach()

    if(FFMPEG_INCLUDE_DIR AND _FFMPEG_ALL_STATIC)
        set(FFMPEG_FOUND ON)
        # GNU ld resolves static archives from left to right. swscale uses
        # libavutil, so libavutil must follow it for FFmpeg 8.1 and newer.
        set(FFMPEG_LIBRARIES
            "${FFMPEG_AVFORMAT_LIBRARY}"
            "${FFMPEG_AVCODEC_LIBRARY}"
            "${FFMPEG_SWRESAMPLE_LIBRARY}"
            "${FFMPEG_SWSCALE_LIBRARY}"
            "${FFMPEG_AVUTIL_LIBRARY}")
        list(APPEND FFMPEG_LIBRARIES ${FFMPEG_EXTRA_LIBRARIES})
        if(WIN32)
            # av_random_bytes() uses CNG in static Windows builds.
            # libavformat's built-in network protocols use Winsock even when
            # every optional external dependency is disabled.
            list(APPEND FFMPEG_LIBRARIES bcrypt ws2_32)
        elseif(APPLE)
            # The static macOS SDK's libavutil uses CoreVideo/CoreMedia even
            # with external codec autodetection disabled.
            list(APPEND FFMPEG_LIBRARIES
                "-framework CoreFoundation"
                "-framework CoreVideo"
                "-framework CoreMedia")
        endif()
        if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
            list(APPEND FFMPEG_LIBRARIES m atomic)
        endif()
        message(STATUS "Static FFmpeg SDK found: ${FFMPEG_PATH}")
    elseif(FFMPEG_EXECUTABLE AND NOT FFMPEG_INCLUDE_DIR)
        message(WARNING
            "FFmpeg at '${FFMPEG_PATH}' contains static command-line tools, but not a linkable SDK. "
            "A self-contained vibrance_engine DLL requires include/libavformat/avformat.h and the "
            "static archives libavformat.a, libavcodec.a, libavutil.a, libswscale.a, and libswresample.a. "
            "Video decoding is disabled until those SDK files are installed into this prefix.")
    else()
        message(WARNING
            "FFmpeg at '${FFMPEG_PATH}' is shared-only or an incomplete SDK. Video decoding is disabled so "
            "vibrance_engine remains a self-contained DLL. Point FFMPEG_PATH at a static FFmpeg build "
            "(libavformat.a, libavcodec.a, libavutil.a, libswscale.a, libswresample.a) to enable it.")
    endif()
endif()

set(SOLOUD_FOUND OFF)
if(DEFINED SOLOUD_PATH)
    find_path(SOLOUD_INCLUDE_DIR NAMES soloud.h HINTS "${SOLOUD_PATH}/include" "${SOLOUD_PATH}" NO_DEFAULT_PATH)
    find_path(SOLOUD_SOURCE_DIR
        NAMES core/soloud.cpp audiosource/wav/soloud_wav.cpp backend/miniaudio/soloud_miniaudio.cpp
        HINTS "${SOLOUD_PATH}/src"
        NO_DEFAULT_PATH)
else()
    find_path(SOLOUD_INCLUDE_DIR NAMES soloud.h)
    find_path(SOLOUD_SOURCE_DIR NAMES core/soloud.cpp audiosource/wav/soloud_wav.cpp backend/miniaudio/soloud_miniaudio.cpp)
endif()

if(SOLOUD_INCLUDE_DIR AND SOLOUD_SOURCE_DIR)
    set(SOLOUD_FOUND ON)
    message(STATUS "SoLoud found: ${SOLOUD_INCLUDE_DIR}")
elseif(DEFINED SOLOUD_PATH)
    message(FATAL_ERROR "SOLOUD_PATH must point to a SoLoud checkout containing include/soloud.h and src/core/soloud.cpp.")
else()
    message(WARNING "SoLoud was not found. Engine audio will compile as a no-op.")
endif()


if(DEFINED GLFW_PATH AND GLFW_PATH)
    if(NOT EXISTS "${GLFW_PATH}")
        message(STATUS "GLFW_PATH does not exist at '${GLFW_PATH}'; falling back to the system-installed GLFW.")
        unset(GLFW_PATH)
    endif()
endif()

if(DEFINED GLFW_PATH)
    set(GLFW_INCLUDE_DIRS "${GLFW_PATH}/include")
    if((APPLE OR (WIN32 AND VIBRANCE_WINDOWS_ARCHITECTURE STREQUAL "arm64")) AND
       EXISTS "${GLFW_PATH}/CMakeLists.txt")
        set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
        set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory(
            "${GLFW_PATH}"
            "${CMAKE_BINARY_DIR}/external/glfw"
            EXCLUDE_FROM_ALL)
        set(GLFW_LIBRARIES glfw)
    elseif(MSVC)
        set(_GLFW_LIBRARY_HINTS "${GLFW_PATH}/lib-vc2022" "${GLFW_PATH}/build/src")
        find_library(GLFW_LIBRARIES NAMES glfw3 glfw HINTS ${_GLFW_LIBRARY_HINTS} REQUIRED)
    else()
        set(_GLFW_LIBRARY_HINTS "${GLFW_PATH}/lib" "${GLFW_PATH}/build/src")
        find_library(GLFW_LIBRARIES NAMES glfw3 glfw HINTS ${_GLFW_LIBRARY_HINTS} REQUIRED)
    endif()
else()
    find_package(glfw3 3.3 REQUIRED)
    if(TARGET glfw)
        set(GLFW_LIBRARIES glfw)
        get_target_property(GLFW_INCLUDE_DIRS glfw INTERFACE_INCLUDE_DIRECTORIES)
    elseif(TARGET glfw3::glfw)
        set(GLFW_LIBRARIES glfw3::glfw)
        get_target_property(GLFW_INCLUDE_DIRS glfw3::glfw INTERFACE_INCLUDE_DIRECTORIES)
    endif()
endif()

if(DEFINED SIMDJSON_PATH AND EXISTS "${SIMDJSON_PATH}/simdjson.cpp")
    add_library(simdjson STATIC "${SIMDJSON_PATH}/simdjson.cpp")
    add_library(simdjson::simdjson ALIAS simdjson)
    target_include_directories(simdjson PUBLIC "${SIMDJSON_PATH}")
    target_compile_features(simdjson PUBLIC cxx_std_17)
    if(WIN32 AND MINGW)
        # simdjson's MinGW AVX2/AVX-512 kernels can fault while executing from
        # the engine DLL even when CPUID advertises the matching feature set.
        # Prefer correctness for asset loading; Lottie JSON is small enough
        # that the portable implementation has no measurable frame-time cost.
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

add_subdirectory(engine)

install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/toolchains/windows-arm64-llvm-mingw.cmake"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/vibrance_engine/toolchains")

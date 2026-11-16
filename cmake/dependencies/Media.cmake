# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0



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


        set(FFMPEG_LIBRARIES
            "${FFMPEG_AVFORMAT_LIBRARY}"
            "${FFMPEG_AVCODEC_LIBRARY}"
            "${FFMPEG_SWRESAMPLE_LIBRARY}"
            "${FFMPEG_SWSCALE_LIBRARY}"
            "${FFMPEG_AVUTIL_LIBRARY}")
        list(APPEND FFMPEG_LIBRARIES ${FFMPEG_EXTRA_LIBRARIES})
        if(WIN32)



            list(APPEND FFMPEG_LIBRARIES bcrypt ws2_32)
        elseif(APPLE)


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

# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

if(SOLOUD_FOUND AND NOT TARGET soloud)
    file(GLOB SOLOUD_CORE_SOURCES CONFIGURE_DEPENDS "${SOLOUD_SOURCE_DIR}/core/*.cpp")
    add_library(soloud STATIC
        ${SOLOUD_CORE_SOURCES}
        "${SOLOUD_SOURCE_DIR}/audiosource/wav/dr_impl.cpp"
        "${SOLOUD_SOURCE_DIR}/audiosource/wav/soloud_wav.cpp"
        "${SOLOUD_SOURCE_DIR}/audiosource/wav/soloud_wavstream.cpp"
        "${SOLOUD_SOURCE_DIR}/audiosource/wav/stb_vorbis.c"
        "${SOLOUD_SOURCE_DIR}/backend/miniaudio/soloud_miniaudio.cpp")
    target_compile_definitions(soloud PUBLIC WITH_MINIAUDIO)
    target_include_directories(soloud
        PUBLIC "${SOLOUD_INCLUDE_DIR}"
        PRIVATE
            "${SOLOUD_SOURCE_DIR}/audiosource/wav"
            "${SOLOUD_SOURCE_DIR}/backend/miniaudio"
            "${SOLOUD_SOURCE_DIR}/backend/coreaudio")
    set_target_properties(soloud PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(WIN32)
        target_link_libraries(soloud PRIVATE ole32 winmm)
    elseif(APPLE)
        target_sources(soloud PRIVATE "${SOLOUD_SOURCE_DIR}/backend/coreaudio/soloud_coreaudio.cpp")
        target_compile_definitions(soloud PUBLIC WITH_COREAUDIO)
        target_link_libraries(soloud PRIVATE
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-framework AudioUnit"
            "-framework CoreServices")
    else()
        target_link_libraries(soloud PRIVATE pthread dl m)
    endif()
endif()

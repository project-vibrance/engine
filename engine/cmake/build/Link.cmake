# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0

target_link_libraries(vibrance_engine PRIVATE
    ${HARFBUZZ_TARGET}
    $<$<NOT:$<PLATFORM_ID:Darwin>>:Vulkan::Vulkan>
    ${FREETYPE_LIBRARIES}
    ${GLFW_LIBRARIES}
    simdjson::simdjson)

if(SOLOUD_FOUND)
    target_compile_definitions(vibrance_engine PRIVATE VIBRANCE_HAS_SOLOUD=1)
    target_link_libraries(vibrance_engine PRIVATE soloud)
endif()

if(FFMPEG_FOUND)
    target_link_libraries(vibrance_engine PRIVATE ${FFMPEG_LIBRARIES})
endif()

if(TARGET fastgltf::fastgltf)
    target_link_libraries(vibrance_engine PRIVATE fastgltf::fastgltf)
endif()

if(WIN32)
    target_compile_definitions(vibrance_engine PRIVATE VK_USE_PLATFORM_WIN32_KHR)
    if(MINGW)



        target_link_options(vibrance_engine PRIVATE
            "-Wl,--wrap=__imp_CreateWindowExW")
    endif()
    target_link_libraries(vibrance_engine PRIVATE
        windowscodecs comdlg32 dwrite advapi32 bcrypt dwmapi mmdevapi ole32 uuid winmm user32 gdi32 shell32 comctl32 wtsapi32)
elseif(APPLE)

    find_package(ZLIB REQUIRED)
    find_package(BZip2 REQUIRED)
    target_link_libraries(vibrance_engine PRIVATE
        ZLIB::ZLIB
        BZip2::BZip2
        "-framework Metal"
        "-framework Cocoa"
        "-framework ImageIO"
        "-framework CoreText"
        "-framework ApplicationServices"
        "-framework IOKit"
        "-framework CoreFoundation"
        "-framework CoreAudio"
        "-framework CoreMedia"
        "-weak_framework ScreenCaptureKit"
        "-framework CoreVideo"
        "-framework QuartzCore"
        "-framework Security")
else()
    target_link_libraries(vibrance_engine PRIVATE Threads::Threads ${CMAKE_DL_LIBS} m)
endif()

if(WIN32 AND MINGW AND VIBRANCE_STATIC_MINGW_RUNTIME)
    if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        target_link_options(vibrance_engine PRIVATE
            -static
            -static-libgcc
            -static-libstdc++)
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        target_link_options(vibrance_engine PRIVATE -static)
    endif()
endif()

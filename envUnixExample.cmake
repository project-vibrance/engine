# Copyright 2026 59xa
# SPDX-License-Identifier: Apache-2.0







if(CMAKE_HOST_APPLE)
    set(GLFW_PATH "$ENV{HOME}/dev/glfw-3.4")
    set(MEDIAREMOTE_ADAPTER_ROOT "$ENV{HOME}/dev/mediaremote-adapter")
    set(VMA_PATH "$ENV{HOME}/dev/VulkanMemoryAllocator-3.3.0")
    set(ENTT_PATH "$ENV{HOME}/dev/entt-3.16.0")
    set(STB_PATH "$ENV{HOME}/dev/stb-master")
    set(FREETYPE_PATH "$ENV{HOME}/dev/freetype-2.14.3")
    set(HARFBUZZ_PATH "$ENV{HOME}/dev/harfbuzz-11.4.5")
    set(SIMDJSON_PATH "$ENV{HOME}/dev/simdjson/singleheader")
    set(GLTF_PATH "$ENV{HOME}/dev/fastgltf-0.9.0")
else()
    set(ENTT_PATH "$ENV{HOME}/dev/entt")
    if(EXISTS "$ENV{HOME}/dev/fastgltf-0.9.0")
        set(GLTF_PATH "$ENV{HOME}/dev/fastgltf-0.9.0")
    endif()
    if(EXISTS "$ENV{HOME}/dev/soloud")
        set(SOLOUD_PATH "$ENV{HOME}/dev/soloud")
    endif()
endif()

# This is only an example, change the values below as needed.
# Rename into .env.cmake afterwards

set(GLFW_PATH "C:/dev/glfw-3.4")
set(GLM_PATH "C:/dev/glm")
set(VULKAN_SDK_PATH  "C:/dev/VulkanSDK/1.4.341.1")
set(VMA_PATH "C:/dev/VulkanMemoryAllocator-3.3.0")
set(FMT_PATH "C:/dev/fmt-12.1.0")
set(VKB_PATH "C:/dev/vk-bootstrap-1.4.341")
set(ENTT_PATH "C:/dev/entt")
set(STB_PATH "C:/dev/stb")
set(NANOSVG_PATH "C:/dev/nanosvg")
set(SIMDJSON_PATH "C:/dev/simdjson/singleheader")

# The UCRT LLVM-MinGW distribution can build both x64 and ARM64.
# set(MINGW_PATH "C:/llvm-mingw")

# Required for Windows ARM64. Use the unpacked UCRT LLVM-MinGW distribution;
# the engine, app, and installer must all use this same toolchain.
# set(LLVM_MINGW_PATH "C:/llvm-mingw")

# ARM64 builds compile GLFW and FreeType from source when these are source
# trees. Set separate paths if the x64 values above point at prebuilt binaries.
# set(GLFW_ARM64_PATH "C:/dev/glfw-3.4")
# set(FREETYPE_ARM64_PATH "C:/dev/freetype-VER-2-14-3")

# LGPL-only static FFmpeg SDKs produced by scripts/build_ffmpeg_windows.ps1.
# set(FFMPEG_PATH "C:/dev/ffmpeg-8.1.2-lgpl-static-windows-x64")
# set(FFMPEG_ARM64_PATH "C:/dev/ffmpeg-8.1.2-lgpl-static-windows-arm64")

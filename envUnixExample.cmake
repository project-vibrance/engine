# macOS setup: see unix_env_setup.md. Copy this file to .env.cmake and edit
# paths to match the directories you actually downloaded/extracted.
# CMake expands $ENV{HOME}; shell-style $HOME and ~ are not expanded here.

# GLM is discovered from Homebrew. For both architectures, download GLFW
# source and let the engine build it for each target (Homebrew GLFW is native).
set(GLFW_PATH "$ENV{HOME}/dev/glfw-3.4")
# Vulkan is discovered from the
# SDK environment (source its setup-env.sh before running unixBuild.sh).
set(VMA_PATH "$ENV{HOME}/dev/VulkanMemoryAllocator-3.3.0")
set(ENTT_PATH "$ENV{HOME}/dev/entt-3.16.0")
set(STB_PATH "$ENV{HOME}/dev/stb-master")
set(FREETYPE_PATH "$ENV{HOME}/dev/freetype-2.14.3")
set(SIMDJSON_PATH "$ENV{HOME}/dev/simdjson/singleheader")
set(GLTF_PATH "$ENV{HOME}/dev/fastgltf-0.9.0")

# Optional SVG and audio support: uncomment after downloading these sources.
# set(NANOSVG_PATH "$ENV{HOME}/dev/nanosvg")
# set(SOLOUD_PATH "$ENV{HOME}/dev/soloud20200207")

# Optional video decoding: configure SDKs for either or both target architectures.
# Apple Silicon (native arm64 build):
# set(FFMPEG_SILICON_PATH "$ENV{HOME}/dev/ffmpeg-8.1.2-lgpl-static-darwin-arm64")
# Intel (native x86_64 build):
# set(FFMPEG_INTEL_PATH "$ENV{HOME}/dev/ffmpeg-8.1.2-lgpl-static-darwin-x86_64")
# Architecture-specific paths override FFMPEG_PATH for that target.
# GLFW_SILICON_PATH / GLFW_INTEL_PATH and FREETYPE_SILICON_PATH /
# FREETYPE_INTEL_PATH can likewise select separate sources or installations.
# Shared GLFW and FreeType source trees are built separately for each target.
# Custom FFmpeg builds may need additional dependencies, for example:
# set(FFMPEG_EXTRA_LIBRARIES "-framework VideoToolbox;-framework CoreMedia;-framework CoreVideo")

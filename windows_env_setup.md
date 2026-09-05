> [!NOTE]
> Building the app on Windows operating systems is somewhat jumbled at the moment.
> Because of that, I have created this document to ensure that setup process is properly working.

### Setting up MinGW
Download the UCRT Windows-host
archive from the official
[LLVM-MinGW releases](https://github.com/mstorsjo/llvm-mingw/releases), unpack
it to `C:\llvm-mingw`, and add `C:\llvm-mingw\bin` to `PATH`. The Windows-host
distribution is a UCRT cross-toolchain that produces both x64 and ARM64
binaries from an x64 development machine. Set both `MINGW_PATH` and
`LLVM_MINGW_PATH` to the unpacked root. The build scripts use the included GNU
Make and select the correct target compilers automatically.

Install Visual Studio Build Tools with both the normal C++ workload and the
**MSVC ARM64 build tools** component. Those tools compile the optional private
Composition and WinRT companion DLLs; the main engine remains LLVM-MinGW.

### Getting GLFW binaries working
- Ensure you build the binaries from the GitHub repository, and not use the pre-built ones
```bash
mkdir glfw-3.4
cd glfw-3.4
git clone https://github.com/glfw/glfw.git
```
- Make sure `CMake` is installed, and `MinGW` configured.
- Open a terminal on the repo's directory, and do the following:
```bash
mkdir build
cd build
cmake -G .. "MinGW Makefiles"
mingw32-make
``` 

### Setting up GLM
- Download GLM [here](https://github.com/g-truc/glm).
- Extract contents and place somewhere, for example: `C:\glm`.

### Setting up Vulkan
- Get Vulkan [here](https://vulkan.lunarg.com/sdk/home). The version the engine uses is `1.4.341.1`.
- Install at your preferred location. Example: `C:\VulkanSDK\1.4.341.1`

### Setting up VS Code
- Ensure you have `C/C++ and C/C++ Extensions` installed.
- Press `Ctrl+Shift+P` and open `C/C++: Edit Configurations (UI)`.
- Ensure your `include` path looks like this:
```bash
${workspaceFolder}/**
C:\glfw-3.4\include
C:\glm
C:\VulkanSDK\1.4.341.1\Include
```

### Other requirements
- [VulkanMemoryAllocator-3.3.0](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/releases/tag/v3.3.0)
- [fastgltf-0.9.0](https://github.com/spnda/fastgltf/releases/tag/v0.9.0)
- [simdjson Version 4.6.1](https://github.com/simdjson/simdjson/releases/download/v4.6.1/singleheader.zip) (dependency for `fastgltf`)
- [stb](https://github.com/nothings/stb/archive/refs/heads/master.zip)
- [entt v3.16.0](https://github.com/skypjack/entt/releases/tag/v3.16.0)
- [freetype-VER-2-14-3](https://gitlab.freedesktop.org/freetype/freetype/-/archive/VER-2-14-3/freetype-VER-2-14-3.zip?ref_type=tags)
- [FFmpeg 8.1.2 source](https://ffmpeg.org/releases/ffmpeg-8.1.2.tar.xz),
  built using [the LGPL Windows instructions](docs/building-lgpl-ffmpeg.md)
- [soloud_20200207](https://solhsa.com/soloud/soloud_20200207.zip)

> [!NOTE]
> Make sure you also add these dependencies to your include path in [Setting up VS Code](#setting-up-vs-code).

### `.env.cmake`
- Ensure your `.env.cmake` for Windows builds look similar to this:

```cmake
set(MINGW_PATH "C:/llvm-mingw")
set(GLFW_PATH "C:/dev/glfw-3.4")
set(GLM_PATH "C:/dev/glm")
set(VMA_PATH "C:/dev/VulkanMemoryAllocator-3.3.0")
set(GLTF_PATH "C:/dev/fastgltf-0.9.0")
set(SIMDJSON_PATH "C:/dev/simdjson (or C:/dev/singleheader)")
set(ENTT_PATH "C:/dev/entt")
set(FREETYPE_PATH "C:/dev/freetype-VER-2-14-3")
set(STB_PATH "C:/dev/stb")

# Required for arm64. Prefer source checkouts for GLFW and FreeType.
set(LLVM_MINGW_PATH "C:/llvm-mingw")
set(GLFW_ARM64_PATH "C:/dev/glfw-3.4")
set(FREETYPE_ARM64_PATH "C:/dev/freetype-VER-2-14-3")

set(FFMPEG_PATH "C:/dev/ffmpeg-8.1.2-lgpl-static-windows-x64")
set(FFMPEG_ARM64_PATH "C:/dev/ffmpeg-8.1.2-lgpl-static-windows-arm64")
```

### Clean-up
- Build x64 with `.\mingwBuild.bat Release x64`, ARM64 with
  `.\mingwBuild.bat Release arm64`, or both with
  `.\mingwBuild.bat Release all`.
- If it does not compile, make sure you kill your terminal first or restart VS Code

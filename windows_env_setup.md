> [!NOTE]
> Building the app on Windows operating systems is somewhat jumbled at the moment.
> Because of that, I have created this document to ensure that setup process is properly working.

### Setting up MinGW
- [Get this version of MinGW-w64 specifically
](https://github.com/brechtsanders/winlibs_mingw/releases/download/15.2.0posix-13.0.0-ucrt-r6/winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-13.0.0-r6.7z)
- Keep this directory somewhere, for example: `C:\dev\winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-13.0.0-r6\mingw64`.
- Add this to `PATH`: `C:\dev\winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-13.0.0-r6\mingw64\bin`

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
- [ffmpeg](https://github.com/BtbN/FFmpeg-Builds/releases/tag/latest)
- [soloud_20200207](https://solhsa.com/soloud/soloud_20200207.zip)

> [!NOTE]
> Make sure you also add these dependencies to your include path in [Setting up VS Code](#setting-up-vs-code).

### `.env.cmake`
- Ensure your `.env.cmake` for Windows builds look similar to this:

```cmake
set(MINGW_PATH "C:/dev/winlibs-x86_64-posix-seh-gcc-15.2.0-mingw-w64ucrt-13.0.0-r6/mingw64")
set(GLFW_PATH "C:/dev/glfw-3.4")
set(GLM_PATH "C:/dev/glm")
set(VMA_PATH "C:/dev/VulkanMemoryAllocator-3.3.0")
set(GLTF_PATH "C:/dev/fastgltf-0.9.0")
set(SIMDJSON_PATH "C:/dev/simdjson (or C:/dev/singleheader)")
set(ENTT_PATH "C:/dev/entt")
set(FREETYPE_PATH "C:/dev/freetype-VER-2-14-3")
set(STB_PATH "C:/dev/stb")
```

### Clean-up
- Verify everything works by running `.\mingwBuild.bat` (`CTRL+Shift+B` if building from VS Code)
- If it does not compile, make sure you kill your terminal first or restart VS Code
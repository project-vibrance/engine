# Building the SDK

Build one SDK per operating system and architecture. Applications use that
installed SDK with a matching C++ toolchain. CMake 3.28 or newer and C++20 are
required. Dependency downloads are supplied by the developer; `.env.cmake` records
local paths and is not part of the distributed SDK.

## Dependencies

Use the appropriate environment example as a starting point, and edit paths to
match your machine. Set source-tree paths for dependencies that must be rebuilt
for multiple architectures.

| Dependency | Configuration | Purpose |
| --- | --- | --- |
| GLFW | `GLFW_PATH`; architecture overrides where needed | Windows, input and monitors |
| GLM | `GLM_PATH` or system discovery | Public maths types |
| EnTT | `ENTT_PATH` or system discovery | Scene registry |
| stb | `STB_PATH` | Image decode, resize and supporting assets; includes `stb_image_resize2.h` |
| HarfBuzz | `HARFBUZZ_PATH` | Text shaping; source must contain `src/harfbuzz.cc`, `src/hb.h` and `COPYING` |
| FreeType | `FREETYPE_PATH` or system discovery | Font rasterisation |
| Vulkan SDK | `VULKAN_SDK_PATH` or SDK environment | Vulkan builds, headers and `glslc` |
| Vulkan Memory Allocator | `VMA_PATH` | Vulkan resource allocation |
| Metal-cpp | `METAL_CPP_PATH` or `~/dev/metal-cpp` | Metal builds, with Metal 3 headers |
| MediaRemote adapter | `MEDIAREMOTE_ADAPTER_ROOT` | macOS media-session companion |
| NanoSVG | `NANOSVG_PATH` | Optional SVG rasterisation |
| simdjson | `SIMDJSON_PATH` | JSON support used by asset loading |
| fastgltf | `GLTF_PATH` | Optional hosted glTF support |
| SoLoud | `SOLOUD_PATH` | Optional audio backend |
| Static FFmpeg SDK | `FFMPEG_PATH` and architecture overrides | Optional video decoding |

The root `cmake/dependencies` modules validate paths and decide which optional
features are available. Configuration output reports the result. Headers exposed
by the public API are copied into the installed SDK; compiled dependencies remain
private to the engine.

## Windows

Use a UCRT LLVM-MinGW installation when building ARM64. Set `LLVM_MINGW_PATH` and,
where required by your local setup, `MINGW_PATH`. The engine, application and
installer must use compatible toolchains and architectures.

The native Windows Composition and WinRT companions are built with MSVC. Install
the x64 C++ tools and the ARM64 tools when targeting ARM64. The Composition module
looks for the configured Windows SDK generation through the installed kits or a
local SDK cache. Paths are local build inputs, not part of the installed SDK.

```bat
mingwBuild.bat Release x64
mingwBuild.bat Release arm64
mingwBuild.bat Release all
```

| Target | Build directory | Default SDK directory |
| --- | --- | --- |
| x64 | `build/windows-x64/Release` | `install/windows-x64` |
| ARM64 | `build/windows-arm64/Release` | `install/windows-arm64` |

The long form is `mingwBuild.bat Release <prefix> <architecture>`. A single target
uses the supplied prefix directly. `all` adds `windows-x64` and `windows-arm64`
under that prefix. `Debug` selects the corresponding configuration directories.

For ARM64, `GLFW_ARM64_PATH` and `FREETYPE_ARM64_PATH` can select matching source
or installed dependencies. Video is disabled for that target unless
`FFMPEG_ARM64_PATH` supplies its matching static SDK.

## macOS

Use Apple's command-line build tools, a Metal toolchain and Metal-cpp with Metal 3
headers. The engine deployment target is macOS 13 or later. Native AppKit Liquid
Glass has its own newer operating-system requirement; see [Platforms](platforms.md).
Vulkan and MoltenVK are not required for a Metal engine build.

```sh
./unixBuild.sh Release '' native
./unixBuild.sh Release '' silicon
./unixBuild.sh Release '' intel
./unixBuild.sh Release '' all
```

`all` builds separate Apple Silicon and Intel SDKs. Use dependency source trees or
architecture-specific installations. `GLFW_SILICON_PATH`, `GLFW_INTEL_PATH`,
`FREETYPE_SILICON_PATH`, `FREETYPE_INTEL_PATH`, `FFMPEG_SILICON_PATH` and
`FFMPEG_INTEL_PATH` override shared paths for the selected target.
CMake path expressions use `$ENV{HOME}`; shell `~` is not expanded in a CMake value.

## Linux

Provide the Vulkan SDK/tooling and the common dependencies, then run:

```sh
./unixBuild.sh Release /path/to/vibrance-sdk
```

A renderer build does not imply that every desktop integration is available.
See the [native feature table](platforms.md#native-services) before depending on
a platform provider.

## Direct CMake and tests

For an already configured build tree:

```sh
cmake --build build/windows-x64/Release --target vibrance_engine -j4
ctest --test-dir build/windows-x64/Release --output-on-failure
cmake --install build/windows-x64/Release
```

Use your platform's build path. Set `BUILD_TESTING=OFF` for a library-only build.
The `vibrance_control_header_checks` target compiles each focused control header
independently. Renderer and native tests may need a desktop session, a suitable
GPU, validation tools or font/media fixtures. Some fixtures are discovered in
sibling application checkouts; an application checkout is not required to build
or consume the SDK itself. Inspect skips as well as failures when qualifying a
platform.

Useful options include `VIBRANCE_ENABLE_FFMPEG`,
`VIBRANCE_ENABLE_WIN32_COMPOSITION`, `VIBRANCE_ENABLE_WIN32_INTEROP`,
`VIBRANCE_STATIC_MINGW_RUNTIME`, `VIBRANCE_ENABLE_VULKAN_VALIDATION` and
`VIBRANCE_ENABLE_VULKAN_RENDERER_LOGGING`. Validation and detailed renderer logging
are diagnostic options; benchmark with them disabled.

## FFmpeg source build

`scripts/build_ffmpeg_windows.ps1` builds separate static SDKs from FFmpeg 8.1.2.
It accepts source, toolchain, NASM, installation and job-count arguments:

```powershell
.\scripts\build_ffmpeg_windows.ps1 -Architecture all `
    -SourceDir C:\dev\ffmpeg-8.1.2 -LlvmMingwRoot C:\llvm-mingw
```

The script checks the source version, archive architecture and configuration
flags, including disabled GPL, nonfree, shared and version-3 modes. It records
build evidence and the upstream licence text under `share/ffmpeg-build`.
The engine uses `avformat`, `avcodec`, `avutil`, `swscale` and `swresample` static
archives. A DLL import library does not provide that static configuration.
Custom archive dependencies can be supplied through `FFMPEG_EXTRA_LIBRARIES`.

## Installed layout

An SDK contains the shared library, public headers, CMake package, runtime
companions, release manifest, notices, application template and these guides.
The documentation entry point is `share/vibrance_engine/README.md`.
Build shaders are compiled and embedded by the engine; an ordinary application
consuming the SDK does not compile the engine's shaders.

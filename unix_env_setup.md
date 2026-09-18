# macOS development setup

This guide covers Apple Silicon (`arm64`) and Intel (`x86_64`) builds, including
building both from one Mac. Use dependencies built for the selected architecture.
The Unix script defaults to the host architecture; `all` builds two separate SDKs,
not a merged universal engine binary.

## Toolchain and packages

Install Xcode or Apple's Command Line Tools (`xcode-select --install`), then
[Homebrew](https://brew.sh). CMake 3.28 or newer and a C++20 compiler are required.
Apple's toolchain provides Clang, Make, and the macOS SDK; MinGW is not required.

```sh
brew install cmake glfw glm
xcode-select -p
xcrun --sdk macosx --show-sdk-path
```

[GLFW](https://formulae.brew.sh/formula/glfw) provides windowing; GLM provides
math headers. For a native build, leave `GLFW_PATH` and `GLM_PATH` unset when
using Homebrew discovery. For both architectures, download the
[GLFW 3.4 source archive](https://github.com/glfw/glfw/archive/refs/tags/3.4.tar.gz),
extract it into `~/dev`, and set `GLFW_PATH` to `~/dev/glfw-3.4` using
`$ENV{HOME}` syntax. The engine builds GLFW separately for each target. Homebrew
binaries typically contain only the architecture of that Homebrew installation.
GLM is header-only and can be shared between targets. The Vulkan loader must
contain the requested architecture (`lipo -info /path/to/libvulkan.dylib`).
For a nonstandard package prefix, add it to `CMAKE_PREFIX_PATH` in `.env.cmake`.

Install the [LunarG macOS Vulkan SDK](https://vulkan.lunarg.com/sdk/home#mac),
including its loader, headers, MoltenVK, and shader tools. Follow the
[SDK environment instructions](https://vulkan.lunarg.com/doc/view/1.4.335.1/mac/getting_started.html).
Before building, source the installed version's environment script, for example:

```sh
source "$HOME/VulkanSDK/1.4.341.0/setup-env.sh"
command -v glslc
```

Replace the version with your installed directory. `glslc` must be on `PATH`;
it compiles the engine's embedded shaders. The SDK environment also configures
Vulkan/MoltenVK for running applications. Launch IDE builds with that environment.

## Source dependencies

Download or clone these projects into `~/dev`, then set their actual paths in
`.env.cmake`. The versions below match the local setup used for this guide;
they are examples, not automatic downloads or a dependency lockfile.

| Dependency | Example folder / required contents | Configuration |
| --- | --- | --- |
| [Vulkan Memory Allocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) | `VulkanMemoryAllocator-3.3.0/include/vk_mem_alloc.h` | `VMA_PATH`: checkout root |
| [EnTT](https://github.com/skypjack/entt) | `entt-3.16.0`, containing `single_include/entt/entt.hpp` or `src/entt/entt.hpp` | `ENTT_PATH`: checkout root |
| [stb](https://github.com/nothings/stb) | `stb-master/stb_image.h` | `STB_PATH`: header directory |
| [FreeType](https://freetype.org/download.html) | `freetype-2.14.3`, containing `CMakeLists.txt` and `include/ft2build.h` | `FREETYPE_PATH`: source root |
| [simdjson](https://github.com/simdjson/simdjson) | `simdjson/singleheader/simdjson.cpp` and `simdjson.h` | `SIMDJSON_PATH`: `singleheader` directory |
| [fastgltf](https://github.com/spnda/fastgltf) | `fastgltf-0.9.0`, containing `src/fastgltf.cpp`, `src/base64.cpp`, `src/io.cpp`, and `include/fastgltf/glm_element_traits.hpp` | `GLTF_PATH`: checkout root |

Configure both fastgltf and simdjson: the current engine directly includes
fastgltf headers, and its CMake target requires simdjson. No separate fmt or
vk-bootstrap installation is used by the current engine CMake build.

On macOS, FreeType source checkouts are always built inside each engine target
build directory, so an existing native archive cannot leak into the other target. To build it separately with a small dependency set:

```sh
cmake -S "$HOME/dev/freetype-2.14.3" -B "$HOME/dev/freetype-2.14.3/build" \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_SHARED_LIBS=OFF \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk macosx --show-sdk-path)" \
  -DFT_DISABLE_PNG=ON -DFT_DISABLE_HARFBUZZ=ON -DFT_DISABLE_BROTLI=ON
cmake --build "$HOME/dev/freetype-2.14.3/build" --parallel
```

**zlib and bzip2 already come with the macOS SDK.** Engine CMake finds and links
`ZLIB::ZLIB` and `BZip2::BZip2` on macOS, including for a prebuilt static FreeType
archive. You do not need to install Homebrew copies. Other FreeType build options
can introduce additional dependencies; use the settings above or the engine's
source-build fallback.

## Optional features

| Feature | Install | Configuration |
| --- | --- | --- |
| SVG rasterization | [NanoSVG](https://github.com/memononen/nanosvg), including `src/nanosvg.h` and `src/nanosvgrast.h` | `NANOSVG_PATH`: checkout root |
| Audio | [SoLoud](https://github.com/jarikomppa/soloud), including `include/soloud.h` and `src/` (example: `soloud20200207`) | `SOLOUD_PATH`: checkout root; engine compiles its sources |
| Video decoding | Static FFmpeg development SDK for the engine architecture | `FFMPEG_PATH`: SDK prefix |

Without SoLoud, audio compiles as a no-op. Without a configured static FFmpeg SDK,
video decoding is disabled. A command-line `ffmpeg` executable alone is insufficient:
the engine needs `include/libavformat/avformat.h` and these archives under `lib/`:
`libavformat.a`, `libavcodec.a`, `libavutil.a`, `libswscale.a`, `libswresample.a`.

The repository's [FFmpeg build script](scripts/build_ffmpeg_macOS_universal.sh)
accepts an extracted source directory, output root, and build root:

```sh
brew install nasm
# Download/extract FFmpeg source matching FFMPEG_VERSION in the script first.
bash scripts/build_ffmpeg_macOS_universal.sh \
  "$HOME/src/ffmpeg-8.1.2" "$HOME/dev" "$HOME/build"
```

Despite its name, the script creates **two separate SDKs**, one for `arm64` and
one for `x86_64`; it does not merge them with `lipo`. It deletes and rebuilds its
versioned output and build directories. It defaults to macOS 12.0 as FFmpeg's
minimum deployment target; override with `MACOS_DEPLOYMENT_TARGET` if needed.

Set `FFMPEG_SILICON_PATH` and `FFMPEG_INTEL_PATH` as shown in the environment
example. CMake selects the path matching `CMAKE_OSX_ARCHITECTURES`; these override
`FFMPEG_PATH` for their respective targets. The same `_SILICON_PATH` and
`_INTEL_PATH` overrides are supported for GLFW and FreeType. Shared GLFW and
FreeType source trees are preferable because CMake rebuilds them for each target.
Custom static builds may require `FFMPEG_EXTRA_LIBRARIES` for enabled frameworks
or external codecs. Check the SDK's `lib/pkgconfig/*.pc` files for dependencies.
The core engine build validation does not validate every optional FFmpeg setup.

## Configure, build, and install

From the engine repository, copy the example only if you do not already have a
local configuration:

```sh
cp -n envUnixExample.cmake .env.cmake
```

Edit `.env.cmake` to match your downloaded folder names. Use `$ENV{HOME}` in CMake
paths, not literal `$HOME` or `~`. Uncomment optional dependencies only after
installing them. Then build:

```sh
./unixBuild.sh Debug
# Or select Release and a writable SDK destination:
./unixBuild.sh Release "$HOME/dev/vibrance-engine-sdk"
```

Select a macOS architecture with the third argument (an empty second argument
uses the default install base):

```sh
./unixBuild.sh Release '' arm64
./unixBuild.sh Release '' x86_64
./unixBuild.sh Release '' all
# Or place both SDKs under a custom base:
./unixBuild.sh Release "$HOME/dev/vibrance-engine-sdk" all
```

Build directories are `build/macos-arm64/Release` and
`build/macos-x86_64/Release` (or `Debug`). Explicit architecture builds with an
empty prefix install into `install/macos-<architecture>`. `all` always appends
`macos-<architecture>` to the install base. A single-architecture build with an
explicit prefix installs exactly there. Existing calls without a third argument
still default to `install/`. Set `CMAKE_BUILD_PARALLEL_LEVEL` to change the build
job count (default: 4). Direct CMake builds should set
`-DCMAKE_OSX_ARCHITECTURES=arm64` or `x86_64` and use separate build directories.
Multiple architectures in one CMake tree are rejected; use `all` instead.

Only run tests for an architecture your Mac can execute. Cross-compiling does
not validate behavior on the target hardware. The script configures,
builds, and installs the SDK. It builds enabled test executables but does not run
them; run `ctest --test-dir build/macos-arm64/Debug --output-on-failure` separately.

## Troubleshooting

- **SDK / linker architecture errors:** check `xcode-select -p` and
  `xcrun --sdk macosx --show-sdk-path`. `unixBuild.sh` explicitly selects that SDK
  unless `SDKROOT` is set. Unset a stale `SDKROOT`; use a compiler and SDK from a
  compatible Apple toolchain.
- **Missing headers:** check the exact folder names and `$ENV{HOME}` spelling in
  `.env.cmake`. `fastgltf/glm_element_traits.hpp` requires `GLTF_PATH`.
- **Undefined `_inflate*` / `_BZ2_*` symbols:** these are FreeType's zlib/bzip2
  dependencies. Ensure you have the macOS CMake linkage for both imported targets;
  installing more copies is not the fix.
- **Wrong architecture or stale dependency paths:** use matching libraries and a
  fresh build directory when switching toolchains or architectures. Native Intel
  and Apple Silicon builds must not reuse incompatible cached libraries.

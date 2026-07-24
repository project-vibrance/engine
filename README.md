# vibranceUI

> [!NOTE]
> **`vibranceUI`**_, developed by **[59xa](https://github.com/59xa)** and **[Florian Butz](https://github.com/FlorianButz)**, is licenced under the **[CC BY-SA 4.0](LICENCE)** copyleft licence._

**`vibranceUI`** is a standalone C++20 SDK. It owns the renderer, UI,
windowing adapter, audio, media, engine shaders, and all engine implementation
code. Applications consume its installed CMake package and never add this
source tree with `add_subdirectory()`.

## Build and install

Configure dependency locations in a local `.env.cmake` using
`envWindowsExample.cmake` or `envUnixExample.cmake` as a starting point.

On Windows with MinGW:

```bat
mingwBuild.bat Release C:\dev\vibranceUI
```

On Unix-like systems:

```sh
./unixBuild.sh Release /opt/vibranceUI
```

The selected prefix is a relocatable SDK:

```text
<prefix>/
  bin/vibrance_engine shared library
  include/vibranceUI/... public API
  lib/... import/static metadata
  lib/cmake/vibrance_engine/vibrance_engineConfig.cmake
  share/vibrance_engine/vibrance_engine_manifest.json
```

A downstream CMake project needs only:

```cmake
find_package(vibrance_engine 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE vibrance::engine)
```

Pass the SDK prefix through `CMAKE_PREFIX_PATH`, or point the app's
`VIBRANCE_ENGINE_ROOT` cache variable at it.

The installed package exposes `vibrance_engine_MANIFEST_FILE` for build tools.
At runtime, include `<vibranceUI/core/engine_manifest.h>` and call
`vibrance_engine_manifest()` to query the same engine name, semantic version,
and ISO last-updated date directly from the loaded DLL.

## Renderer and system-backdrop backends

Renderer-specific implementation is separated by API:

```text
engine/src/vibranceUI/
  vulkan/   Vulkan renderer and external-image interop
  directx/  D3D/Windows Composition presentation and backdrop effects
```

`RenderBackend` identifies the API that draws the UI. `PresentationBackend`
is separate because Windows currently keeps Vulkan as the renderer and uses a
D3D11 swap-chain bridge only to place its pixels in a Windows Composition
tree. This lets the OS compositor supply the pixels behind the window without
desktop capture. D3D11 is intentionally used for this bridge: Composition
consumes a DXGI surface, and D3D11 has the smaller interop and synchronization
surface. A future D3D12 renderer remains independent of this choice.

`SystemBackdropRegion` supports the extensible material set `eOff`, `eBlur`,
`eFrosted`, and `eLiquid`, with rectangle, per-corner rounded rectangle, and
ellipse clipping. The engine provider builds shaped HostBackdrop effect
visuals. On non-Windows systems these regions resolve to off. Native DWM
Acrylic and Mica providers are also exposed on Windows, but the raw Win32 DWM
API is window-scoped; the engine therefore accepts them only for a full-window
rectangle and rejects requests that could leak outside their requested shape.

MinGW builds the reusable engine normally and invokes MSVC only for the small
C++/WinRT companion `vibrance_win32_composition.dll`. Installing the engine
places that companion beside the engine DLL, and the exported CMake package
publishes its path so downstream applications can copy or install it.

## Dependency policy

Compiled third-party code is linked privately into the engine shared library.
GLFW support and precompiled engine shaders are part of that same library.
Public dependency headers used by the current C++ API (GLM, EnTT, VMA, GLFW,
and Vulkan headers) are copied into the SDK and do not leak local source paths
through the exported CMake target.

On MinGW, the GCC, standard C++, and threading runtimes are linked statically.
The Vulkan loader remains a system/driver dependency by design.

FFmpeg is enabled only when `FFMPEG_PATH` supplies genuine static archives.
Shared FFmpeg import libraries cannot be embedded in another DLL, so a
shared-only SDK—or a static command-line package containing only `ffmpeg.exe`—
produces a configure warning and FFmpeg decoding is disabled. The static SDK
must contain headers plus `libavformat.a`, `libavcodec.a`, `libavutil.a`,
`libswscale.a`, and `libswresample.a`. Video frames and audio tracks from
supported containers are then decoded inside the engine DLL. Use
`FFMPEG_EXTRA_LIBRARIES` for any additional libraries required by the chosen
static FFmpeg build.

Because the public interface is C++, build applications with an ABI-compatible
compiler and standard library. A future stable cross-toolchain ABI would
require a separate C interface or PIMPL-style boundary.

## Layered application resources

`<vibranceUI/ui/resources.h>` provides reusable runtime resource discovery.
Applications pass their own name to `ui_discover_resource_directories` to add
an AppData/XDG user layer over packaged defaults. `asset_candidates` and
`config_candidates` expose user-to-packaged validation fallbacks, while the
file/directory layer APIs expose packaged-to-user inputs for merged parsers.
`UiResourceChangeTracker` detects additions, removals, and modifications for
runtime reload.

Applications may redirect the user layer with `VIBRANCE_RESOURCE_ROOT`, or
redirect assets and config independently with `VIBRANCE_ASSETS_DIR` and
`VIBRANCE_CONFIG_DIR`. The engine does not copy packaged resources into the
user directory.

# Contribution & documentation
While implementation is currently somewhat vague, developers and users are welcome to audit the source code's infrastructure. Contribution to document the engine's capabilities and functionalities are always welcome.

Developers can also create pull requests to implement features and fixing bugs or issues found within the source code.

# vibranceUI

vibranceUI is a C++20 engine and installed SDK for retained UI, graphics,
windowing, audio, media and native desktop integration. Windows and Linux use
Vulkan; macOS uses Metal. Applications link to the installed `vibrance::engine`
CMake target.

| Task | Guide |
| --- | --- |
| Understand the source tree and ownership boundaries | [Architecture](docs/architecture.md) |
| Build, test and install an SDK | [Building](docs/building.md) |
| Create and distribute an application | [Applications](docs/applications.md) |
| Use controls, panels, fonts, themes and the colour picker | [User interface](docs/user-interface.md) |
| Understand Liquid Glass rendering, interaction and reference validation | [Liquid Glass](docs/liquid-glass.md) |
| Check rendering and native feature support | [Platforms](docs/platforms.md) |
| Diagnose builds, rendering and deployment | [Diagnostics](docs/diagnostics.md) |

Configure local dependency paths using `envWindowsExample.cmake` or
`envUnixExample.cmake`, then build from this directory:

```bat
mingwBuild.bat Release all
```

```sh
./unixBuild.sh Release '' all
```

The Windows command builds separate x64 and ARM64 SDKs. On macOS, the Unix command
builds separate Apple Silicon and Intel SDKs. See the build guide for prerequisites,
single-architecture builds and Linux usage.

The source has three useful entry points: `CMakeLists.txt` orchestrates dependency
setup, `engine/CMakeLists.txt` assembles the SDK, and
`engine/include/vibranceUI/engine.h` provides the compatibility umbrella API.
Focused public headers are available under `engine/include/vibranceUI/`.

Developed by [59xa](https://github.com/59xa). Original engine code is licensed
under [Apache-2.0](LICENCE), with copyright and SPDX headers in source files,
shaders, tests, build scripts and starter code. The starter template includes
its own copy of the licence so it can be distributed separately.

Third-party code retains its upstream terms; see [NOTICE](NOTICE). Microsoft
Detours keeps its original notices, and the Liquid Glass optical shader identifies
both Apache-2.0 additions and MIT-licensed adapted portions. External dependencies
retain their own licences and attribution.

SDK installations include `LICENCE` and `NOTICE` under `share/vibrance_engine`.
Dependency notices installed by the build are under
`share/vibrance_engine/licences`.

First-party code keeps only licensing comments and executable directives such as
shebangs. Third-party source retains its upstream comments and notices. Compilers
discard source comments; the Metal shader embedding step also omits licence
headers from runtime shader strings while keeping them in the source files.

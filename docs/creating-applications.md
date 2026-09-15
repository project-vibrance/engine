# Create an application

The engine provides generic rendering, UI, window and platform mechanisms.
Application names, resources, preferences and feature behaviour belong in your
application. No Vibrance product checkout is required.

## Start from the installed SDK

Windows, with the compiler matching your SDK on `PATH`:

```bat
C:\dev\vibrance-sdk\share\vibrance_engine\tools\create-vibrance-app.bat C:\dev\hello C:\dev\vibrance-sdk Release
```

macOS/Linux, with an SDK built for that platform:

```sh
bash /opt/vibrance-sdk/share/vibrance_engine/tools/create-vibrance-app.sh \
  "$HOME/hello" /opt/vibrance-sdk Release
```

These commands create a project in an empty directory, build it and install its
runtime under `install/bin`. The starter draws shapes without requiring a font.

## Build configuration

```cmake
cmake_minimum_required(VERSION 3.28)
project(hello LANGUAGES CXX)
find_package(vibrance_engine 0.1 CONFIG REQUIRED)
vibrance_add_application(hello SOURCES src/main.cpp ASSETS assets)
```

`ASSETS` is optional. The helper sets C++20, links the engine, refreshes runtime
libraries and assets, and configures installation and platform library search
paths. Continue using ordinary CMake commands to customise the returned target.
Configure with `-DCMAKE_PREFIX_PATH=/path/to/sdk`.

## Create objects

```cpp
#include <vibranceUI/engine.h>

int main()
{
    UiWindowOptions options;
    options.title = "Hello";
    options.size = { 720, 480 };
    options.showTitle = false;
    options.showWindowControls = false;
    options.transparentFramebuffer = false;
    options.contentMargin = { 24, 24, 24, 24 };
    options.build = [](UiWindowContext& view) {
        view.block("#11141AFF").fill().layer(0);
        const auto card = view.block("#202938FF", 20)
            .at(UiAlignment::eCenter).size(420, 180).layer(1).entity();
        view.block("#7C5CFCFF", 4).inside(card)
            .at(UiAlignment::eCenter).size(180, 8).layer(2);
    };
    return UiWindow(std::move(options)).run();
}
```

`view.block`, `view.text` and `view.media` automatically use the content root as
their parent. Chain `.inside`, `.at`, `.size`, `.offset` and `.layer` to place them.
Set `options.fontPath` before using `view.text("Hello", 20)`; the window loads the
font and provides it to the builder. For advanced controls use `view.ui` and the
existing `ui_create_*` helpers.

Placement handles are temporary authoring handles. Keep `.entity()` if a later
callback needs an object, and refresh stored entity IDs when the build callback
runs again after a resize or rebuild. An entity belongs to its scene.

## Use generic services

```cpp
const bool saved = write_file_atomically("settings/preferences.json", "{}\n");
const auto executable = current_executable_path();

NativeTrayOptions trayOptions;
trayOptions.tooltip = "My application";
trayOptions.menu = { { 42, "Open document" }, { 99, "Quit" } };
NativeTray tray(std::move(trayOptions));
const bool trayStarted = tray.initialise();
```

Use the tray on the UI thread and consume `take_event()` in the normal application
loop. `UiWindow::tick()` polls platform events. With a tray, drive `tick()` yourself
so that you can also consume tray events. Handle `eCommand` using your own command
IDs; on `eOpenNativeMenu`, call `show_native_menu()`. Custom popup hosts consume
`eToggleCustomMenu` and use `menu_anchor()` to position their own contents. Ensure
the tray is destroyed before terminating platform event handling.

Atomic writes accept text or binary bytes and preserve the old destination when
replacement fails. They do not supply a preference schema, cross-process locking,
power-loss durability or secure credential storage. Use the engine credential API
for secrets.

## Implement another platform

Public service interfaces contain standard C++ values. Implement native details
in a platform source, select it in CMake (`WIN32`, `APPLE`, or another supported
platform), and guard its native headers and implementation with the matching
preprocessor condition. Preserve the same lifecycle and failure contract.

Tray backends live under `engine/src/platform/{win32,macos,portable}`. The private
`CompositionPresenter` contract has a Windows backend and an unavailable portable
backend; the renderer can use its normal Vulkan swapchain when no native compositor
is available. Linux tray hosting and non-Windows foreground-window queries currently
report unsupported availability rather than simulating native behaviour.

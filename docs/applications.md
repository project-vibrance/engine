# Applications

An application consumes an installed SDK. Its source tree contains its own UI,
assets, preferences and product behaviour. The installed CMake package supplies
the engine target and application helper.

## Start a project

With the matching compiler available, use the installed template generator:

```bat
C:\dev\vibrance-sdk\share\vibrance_engine\tools\create-vibrance-app.bat C:\dev\hello C:\dev\vibrance-sdk Release
```

```sh
bash /path/to/sdk/share/vibrance_engine/tools/create-vibrance-app.sh \
  "$HOME/hello" /path/to/sdk Release
```

The destination must be empty. The generator creates, builds and installs the
starter. Its first window draws shapes and does not require a bundled font.

For a project created by hand:

```cmake
cmake_minimum_required(VERSION 3.28)
project(hello LANGUAGES CXX)
find_package(vibrance_engine 0.7 CONFIG REQUIRED)
vibrance_add_application(hello SOURCES src/main.cpp ASSETS assets)
```

Configure with `-DCMAKE_PREFIX_PATH=/path/to/sdk`. `ASSETS` is optional.
`vibrance_add_application` sets C++20, links `vibrance::engine`, refreshes runtime
companions and assets, and sets installation/search-path rules. Use ordinary
CMake commands for additional target configuration.

## First window

```cpp
#include <vibranceUI/engine.h>
#include <utility>

int main()
{
    UiWindowOptions options;
    options.title = "Hello";
    options.size = { 720, 480 };
    options.contentMargin = { 24, 24, 24, 24 };
    options.transparentFramebuffer = false;
    options.showTitle = false;
    options.showWindowControls = false;
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

`UiWindow` owns the native window, renderer, input routing, event polling and
resize/rebuild behaviour. Its context provides the builder, font atlas,
localisation and content root. `view.block`, `view.text` and `view.media` create
content under that root; placement chains set parent, alignment, size and layer.

Keep `.entity()` when a callback needs to update an object. An entity ID belongs
to its scene and must be refreshed after the build callback runs again. Media
handles likewise belong to their creating engine. Complete asynchronous work
through the UI loop before changing a scene or uploading media.

## Fonts, assets and localisation

Place application-owned resources in your assets directory. Set `options.fontPath`
before creating text, and use `view.fontAtlas` when constructing stock controls.
`fallbackFontPaths` and `emojiFontPaths` extend coverage. Applications choose
whether a locale-specific font becomes primary through `useLocalePrimaryFont`.
The [UI guide](user-interface.md#fonts-and-localisation) explains font inspection
and locale coverage.

Set `options.localisationDirectory` to your translation directory. Each locale
file is a flat UTF-8 JSON object, for example `en_gb.json`:

```json
{
  "home.greeting": "Hello from vibranceUI"
}
```

Use `Engine::set_shared_locale("en_gb")` to select the shared locale.
`ui_tr("home.greeting", "Hello")` describes a localised value.
`view.localisation.resolve(...)` produces a string; localisation-aware builder
overloads can retain the translation binding for subsequent locale changes.

Themes are supplied as `UiTheme` values. Theme documents, read-only templates,
metadata, save/reset/delete rules and directory layout are application policy.
The engine provides styling and retained recolouring rather than a product's
appearance-library schema.

## Services

Include the service header for the feature you use:

| Feature | API entry point |
| --- | --- |
| Secondary panel | `glfw/panel_window.h`: `GlfwPanelWindowHost` |
| Layout and controls | `ui/builder.h`, `ui/controls/*.h` |
| Media metadata and transport | `media/session.h`: `GlobalMediaSession` |
| Process audio and spectrum | `audio/loopback_capture.h`, `audio/spectrum.h` |
| Notifications | `notifications/provider.h`, `notifications/card.h` |
| Countdown | `time/countdown_timer.h` |
| Native dialogs and folders | `platform/native_dialog.h` |
| Clipboard and eyedropper | `platform/image_services.h` |
| Monitor wallpaper | `platform/desktop_wallpaper.h` |
| Logging and process services | `core/logger.h`, `core/process.h` |

Native features expose support/readiness or return unavailable results. Check
those results before exposing a feature. Provider snapshots and transport
capabilities describe what a source supports; applications decide how to present
state, retry commands or persist changes. See [Platforms](platforms.md).

## Distribution

Install the application using CMake's install step. Ship the engine and runtime
companions selected by the SDK helper, together with your assets and the relevant
notices. Windows x64 and ARM64 builds use their matching SDKs; macOS consumers
select one architecture per build. Keep the SDK's library layout and runtime
search-path rules when assembling a package.

The Windows Liquid Glass runtime has a protected deployment lifecycle separate
from an ordinary application window. Use the SDK's runtime helper integration
and inspect its status; see [Platforms](platforms.md#windows-liquid-glass).

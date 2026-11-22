# Your vibranceUI starter

This is an independent C++20 application using the installed `vibrance::engine`
SDK. It contains working examples of the engine's UI, graphics and desktop
services. Application state and actions live in `src/starter.cpp`; `src/main.cpp`
owns the entry point.

## Try the application

The generator has already built and installed it. Run `install/bin/hello_vibrance`
on macOS/Linux or `install/bin/hello_vibrance.exe` on Windows.

| Page | Working examples |
| --- | --- |
| Controls | Buttons, an animated switch, checkbox, dropdown, editable text with selection/clipboard/undo, a slider and retained circular progress |
| Graphics | Bundled SVG artwork, scene Liquid Glass and solid materials, independent blur/refraction controls, a colour-picker window and bounded image/animation/video loading |
| Services | A start/pause/reset countdown, a bundled sound with playback/stop controls, native folder selection, opening the assets directory and an explicit media-session query |

The appearance and language menus demonstrate light/dark themes and English
(`en_gb`)/German (`de_de`) dictionaries. Edited control values survive scene rebuilds. Resizing rebuilds
placement against the available content size; page navigation hides retained
page roots without reconstructing their controls. The slider also sets audio volume.

Services reports platform support separately from provider readiness and access.
Reading a media session starts only after you choose that action. Audio capture,
notification access, desktop capture and native glass deployment are not started
by the example. Sound plays only when requested.

## Fonts and assets

Asset paths resolve beside the executable, so launching from another working
directory also works. The CMake application helper stages `assets` during builds
and installation.

For consistent distribution, add a font you may redistribute as
`assets/fonts/Starter.ttf`. You can also set `VIBRANCE_STARTER_FONT` to an absolute
font path. Without an app font, the example uses an installed Windows, macOS or
Linux system font; system fonts are used in place and are not copied or bundled.
If none of the supported candidates exists, the application explains how to
provide one.

The original sample SVG, sound and dictionaries are included under the Apache-2.0
licence in `LICENCE`. Replace them with your own assets and retain the notices for
any third-party resources you distribute.

Imported files are limited to 64 MiB, with media capped at 640 by 360 pixels,
48 animation/video frames and 48 MiB of decoded data. This demonstrates bounded synchronous loading for small
examples. For larger files, use `prepare_media_2d` on a cancellable worker and
`Engine::upload_media_2d` on the UI thread. Codec support depends on the SDK build.

The Liquid Glass preview uses the renderer's scene backdrop inside an ordinary
opaque window. Native system glass and Windows DWM glass have separate platform
and deployment requirements described in the SDK's `platforms.md`.

## Build again

```bat
mingwBuild.bat Release C:\path\to\vibrance-sdk
```

```sh
./unixBuild.sh Release /path/to/vibrance-sdk
```

You can also set `VIBRANCE_ENGINE_ROOT` and omit the second argument. Use the
architecture-matched SDK and an ABI-compatible C++20 compiler.

The generated project uses the SDK's `vibrance_add_application` helper for linking,
runtime companions, asset staging and installation. Add your sources to its
`SOURCES` list in `CMakeLists.txt`.

## Verify the starter

```bat
install\bin\hello_vibrance.exe --smoke-test
```

```sh
./install/bin/hello_vibrance --smoke-test
```

This briefly opens a real window, loads the packaged SVG and font, exercises
control callbacks, changes retained pages/materials and checks that edited values
survive theme, locale and resize rebuilds, then opens and closes the colour-picker
window. It closes automatically and does not play sound or start native providers.
It needs an interactive graphical session and a
supported graphics device.

## More of the engine

The starter demonstrates common entry points. The SDK also exposes these reusable
features through public headers under `vibranceUI/`:

| Area | What is available | Start with |
| --- | --- | --- |
| Windows and displays | Window/panel hosting, transitions, placement, monitor targeting, scaling and multi-window lifetimes | `glfw/ui_window.h`, `glfw/panel_window.h`, `display/display.h` |
| Layout and input | Retained ECS scenes, grids/stacks, clipping, scroll viewports, focus, pointer/keyboard/drag routing and UTF-8 text editing | `ui/builder.h`, `ui/stack_layout.h`, `ui/input.h`, `ui/controls/` |
| Navigation and dialogs | Page stacks, navigation history, context menus, confirmation dialogs and modal input blocking | `ui/page_stack.h`, `ui/controls/navigation_history.h`, `ui/context_menu.h`, `ui/modal_lifecycle.h` |
| Appearance | Light/dark palettes, gradients, shadows, glass materials, colour/image palettes, SVG recolouring and retained theme bindings | `ui/styles.h`, `ui/glass.h`, `ui/colour_palette_panel.h`, `ui/theme_bindings.h` |
| Text and localisation | Font inspection, fallback and emoji fonts, wrapped text, direction-aware layout, formatted/plural text and live locale selection | `renderer/font_file.h`, `renderer/font_atlas.h`, `ui/text_block.h`, `localisation/localisation.h` |
| Images and motion | Raster/SVG, SVG animation, Lottie, video, media playback, transitions and cancellable preparation/upload | `renderer/media2d.h`, `ui/animated_text_view.h`, `ui/visuals.h` |
| Hosted 3D | GLTF/GLB model loading and model batches within the 2D interface | `renderer/renderer3d.h`, `renderer/renderer2d_components.h` |
| Audio | Clip caching, voice playback, volume/pan/looping, output/process capture and spectrum processing | `audio/audio.h`, `audio/loopback_capture.h`, `audio/spectrum.h` |
| Media sessions | Metadata/timeline snapshots, transport commands, seeking, artwork sizing and playback presentation | `media/session.h`, `media/artwork.h`, `media/presentation.h` |
| Notifications | Provider/access snapshots, callback-driven cards, text measurement and stack placement | `notifications/provider.h`, `notifications/card.h`, `notifications/stack_layout.h` |
| Native desktop services | File/folder dialogs, trays, clipboard images, screen colour sampling and configured wallpaper | `platform/native_dialog.h`, `platform/native_tray.h`, `platform/image_services.h`, `platform/desktop_wallpaper.h` |
| Application support | Resource precedence, atomic file writes, logs, runtime identity, credentials, process relaunch and single-instance handling | `ui/resources.h`, `core/file.h`, `core/logger.h`, `core/engine_manifest.h`, `core/secure_credential.h`, `core/single_instance.h` |
| Timers | Presentation-independent countdown state and formatting | `time/countdown_timer.h` |

Read the installed guides under `<SDK>/share/vibrance_engine/docs`:
`applications.md`, `user-interface.md`, `platforms.md`, `architecture.md` and
`diagnostics.md`. The Windows/Linux renderer uses Vulkan; macOS uses Metal.
Native services and optional dependencies expose their own availability checks.

Your application owns its assets, preferences, navigation destinations, theme
documents and service policy. Keep scene mutations and uploads on the owning UI
thread, keep media/entity handles within their engine lifetime, and continue
ticking secondary panels until their closing transitions finish.

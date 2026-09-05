# vibranceUI

> [!NOTE]
> **`vibranceUI`**_, developed by **[59xa](https://github.com/59xa)** and **[Florian Butz](https://github.com/FlorianButz)**, is licenced under the **[CC BY-SA 4.0](LICENCE)** copyleft licence._

**`vibranceUI`** is a standalone C++20 SDK. It owns the renderer, UI,
windowing adapter, audio, media, neutral system-notification providers and
callback-driven cards, engine shaders, and all engine implementation code.
Applications consume its installed CMake package and never add this source tree
with `add_subdirectory()`.

## Build and install

Configure dependency locations in a local `.env.cmake` using
`envWindowsExample.cmake` or `envUnixExample.cmake` as a starting point.

On Windows with MinGW:

```bat
mingwBuild.bat Release C:\dev\vibranceUI x64
```

Windows builds accept `x64`, `arm64`, or `all` as the third argument:

```bat
mingwBuild.bat Release x64
mingwBuild.bat Release arm64
mingwBuild.bat Release all
```

`all` runs both architecture builds from one command. The normal no-prefix
layout preserves the existing x64 SDK at `install` and places ARM64 at
`install/windows-arm64`; their build trees are `build/Release` and
`build/windows-arm64/Release`. With an explicit base prefix, `all` installs to
`<prefix>/windows-x64` and `<prefix>/windows-arm64`. A single-architecture
command installs directly to the prefix it is given.

x64 continues to use GCC MinGW. Windows ARM64 uses the UCRT LLVM-MinGW
cross-toolchain and Ninja. Set `LLVM_MINGW_PATH` to its unpacked root in the
environment or `.env.cmake`. Install the Visual Studio C++ ARM64 build tools as
well if the Composition and WinRT companion DLLs should be included. GLFW and
FreeType must be source trees or ARM64 installs; x64 libraries are deliberately
not reused. Static FFmpeg video support is disabled for ARM64 unless
`FFMPEG_ARM64_PATH` identifies an ARM64 SDK.

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
  share/vibrance_engine/toolchains/windows-arm64-llvm-mingw.cmake
```

A downstream CMake project needs only:

```cmake
find_package(vibrance_engine 0.1 CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE vibrance::engine)
vibrance_engine_copy_runtime_companions(my_app)
```

Pass the SDK prefix through `CMAKE_PREFIX_PATH`, or point the app's
`VIBRANCE_ENGINE_ROOT` cache variable at it.

Reusable object models and retained controls are part of this same target; no
per-control library or implementation source needs to be added downstream:

```cpp
#include <vibranceUI/time/countdown_timer.h>
#include <vibranceUI/audio/loopback_capture.h>
#include <vibranceUI/audio/spectrum.h>
#include <vibranceUI/display/display.h>
#include <vibranceUI/media/media.h>
#include <vibranceUI/notifications/notifications.h>
#include <vibranceUI/ui/components.h>
#include <array>

CountdownTimer timer { 5 * 60 };
timer.start(0.0); // Supply the application's monotonic time.

AudioSpectrumProcessor spectrum;
std::array<float, AudioSpectrumProcessor::kFftLength> monoSamples {};
spectrum.push_samples(monoSamples); // Samples can come from any source.
spectrum.update(1.0 / 60.0);

// Or let the engine's platform source feed the processor directly.
AudioLoopbackCapture capture(spectrum);
capture.start();

// UiContextMenuItem, UiAnimatedCharacterTextView, and the dialog builders are
// ready to use with any UiBuilder owned by an engine window or panel.
```

`<vibranceUI/media/media.h>` similarly exposes UTF-8/time/aspect helpers,
artwork presentation and playback animation, `RevisionedMediaSlot`, and
`MediaArtworkView`. It also includes the portable `GlobalMediaSession` provider,
status, commands, and snapshot contract from `<vibranceUI/media/session.h>`.
The optional installed Win32 companion supplies GSMTC; other platforms fail
closed until a provider is implemented. Applications provide refresh cadence,
optimistic UI state, and product routing, while the SDK owns provider and
renderer-local behavior.

`<vibranceUI/notifications/notifications.h>` exposes
`SystemNotificationProvider`, access state, revisioned snapshots, neutral
items, explicit dismissal/source-activation commands, and application-icon
resolution. The provider never persists, claims, groups, or displays records;
applications retain those product decisions. On Windows it privately loads the
installed shared interop companion. Other platforms are safe to compile and
construct and return an explicit unsupported diagnostic.

`<vibranceUI/notifications/card.h>` builds a complete retained notification card
from presentation-only strings, media handles, and callbacks. It also owns text
filtering, message wrapping, height measurement, and compact group-fan setup.
The options object has useful defaults, so an app does not need to reproduce the
card's entity graph:

```cpp
UiNotificationCardOptions card {};
card.parent = notificationRoot;
card.content = {
    .header = "Calendar",
    .title = "Stand-up",
    .message = "The meeting starts in ten minutes.",
    .elapsedText = "now"
};
card.callbacks.activate = [] { open_calendar(); };
card.callbacks.dismiss = [] { dismiss_from_model(); };

UiNotificationCardHandle view = ui_create_notification_card(
    ui, fontAtlas, localisation, card);
```

`<vibranceUI/notifications/stack_layout.h>` independently places measured
cards down a right-edge stack, including compact backing-layer depth. Supply
only sizes and group counts; no application notification model is required.

```cpp
SystemNotificationProvider notifications;
if (notifications.bridge_loaded())
{
    // Call request_access() from a user-initiated permission flow when needed.
    notifications.refresh(); // Read-only: this never claims native records.
    for (const SystemNotificationItem& item : notifications.snapshot().items)
    {
        consume_notification(item);
        // Persist application state before an optional dismiss(item.providerId).
    }
}
```

`<vibranceUI/display/display.h>` provides stable monitor targeting, independent
feature trackers, edge geometry, and optional JSON preference storage. Product
code supplies its preference keys and decides which interface owns each
tracker.

Use `<vibranceUI/engine.h>` when a small application prefers one SDK umbrella
include. Narrow headers remain available for larger translation units.

The installed package exposes `vibrance_engine_MANIFEST_FILE` for build tools.
At runtime, include `<vibranceUI/core/engine_manifest.h>` and call
`vibrance_engine_manifest()` to query the same engine name, semantic version,
full display build, and ISO last-updated date directly from the loaded DLL.
The full display build uses
`<version>-<UTC date>.<sequence>-<stage>` (for example,
`0.6.0-20260903.0-a`). Configure `VIBRANCE_ENGINE_RELEASE_STAGE` as `alpha`,
`beta`, `release-candidate`, or `release`; the manifest abbreviates these as
`a`, `b`, `rc`, and `r`. Increment `VIBRANCE_ENGINE_BUILD_SEQUENCE` when
creating another engine build on the same UTC day.

## Start with one window

`UiWindow` is the default entry point for a normal application window. It owns
GLFW, the surface-bound `Engine`, custom chrome, input callbacks, resizing, and
the frame loop:

```cpp
#include <vibranceUI/engine.h>
#include <utility>

int main()
{
    UiWindowOptions options {};
    options.title = "Hello Vibrance";
    options.size = { 720, 480 };
    options.fontPath = "assets/Inter-Regular.ttf";
    options.build = [](UiWindowContext& view) {
        const entt::entity card = view.ui.block("#20242EFF", 20.0f);
        view.ui.place(card).inside(view.root).fill(24.0f).layer(0);

        const entt::entity title = view.ui.text(
            "Hello, world",
            view.fontAtlas,
            28.0f);
        view.ui.place(title)
            .inside(card)
            .at(UiAlignment::eTopLeft)
            .offset(24.0f, 24.0f)
            .layer(1);
    };

    UiWindow window(std::move(options));
    return window.run();
}
```

Placement values are logical pixels and remain DPI-aware. Use
`GlfwWindowHost` when an application needs several independently controlled
windows or monitor-edge placement. The existing `GlfwPanelWindowHost`,
`Renderer2DScene`, and raw GLFW adapters remain available for specialized
chrome, retained scene mutation, and custom process loops.

### Add an animated scroll viewport

`UiScrollViewportOptions` is the application-facing scroll control. One call
creates its positioned surface, rounded mask, wheel input, moving content,
scrollbar, and a border that fades in while shrinking to the viewport. The
surface, content, and scrollbar remain stationary throughout that entrance.
The content callback runs immediately and exposes the correct parent and render
layer:

```cpp
UiScrollViewportOptions countries {};
countries.contentHeight = 24.0f + names.size() * 32.0f;
countries.buildContent = [&](const UiScrollViewportBuildContext& content) {
    for (std::size_t index = 0; index < names.size(); ++index)
    {
        const entt::entity label = content.ui.text(
            names[index], view.fontAtlas, 14.0f);
        content.ui.place(label)
            .inside(content.content)
            .at(UiAlignment::eTopLeft)
            .offset(12.0f, 12.0f + index * 32.0f)
            .layer(content.layer, content.order + index);
    }
};

UiScrollViewportHandle viewport = ui_create_scroll_viewport(
    view.ui,
    view.root,
    UiAlignment::eCenter,
    { 0.0f, 40.0f },
    { 456.0f, 248.0f },
    std::move(countries));
```

All dimensions are logical pixels. Set `entrance.enabled = false` for an
already-visible viewport; colors, outline, corner radius, scrollbar behavior,
edge fades, and scroll callbacks are regular options. The returned handle also
exposes each retained entity for advanced styling without requiring it.

Window hosts also expose independent initialization policy for focus, taskbar
presence, and Alt-Tab/window-cycle presence. Disabling one of these policies
causes the native window to be configured before its first visible frame.
`GlfwWindowPositionOptions` adds reusable monitor-work-area alignment (including
centering, stable monitor ids, reference-point targeting, margins, and offsets)
across `GlfwWindowHost`, `GlfwPanelWindowHost`, and `UiWindow`.
`GlfwPanelWindowHostOptions::topDragHeight` can independently opt a borderless
panel into a logical-pixel top drag strip after its template factory runs;
title and content geometry remain unchanged, and zero disables the strip.
Use `GlfwPanelWindowHost::refresh_template()` for later rebuilds so this and
other host-owned policies are reapplied.

### Build reusable page stacks

`<vibranceUI/ui/page_stack.h>` owns the common keep-alive page pattern: each
page is an entity subtree, exactly one registered root is visible, and page IDs
connect directly to a history-enabled icon button or navigation cluster. Apps
do not need one visibility boolean per page.

```cpp
enum class Page : std::size_t
{
    eWelcome,
    eOptions,
    eSummary
};

const entt::entity welcome = ui_create_page_root(view.ui, view.root);
const entt::entity options = ui_create_page_root(view.ui, view.root);
const entt::entity summary = ui_create_page_root(view.ui, view.root);

const UiPageStackHandle pages = ui_create_page_stack(
    view.ui.scene(),
    {
        { ui_page_id(Page::eWelcome), welcome },
        { ui_page_id(Page::eOptions), options },
        { ui_page_id(Page::eSummary), summary }
    },
    { .initialPage = ui_page_id(Page::eWelcome) });

UiIconButtonOptions backOptions {};
backOptions.useHistory = true;
backOptions.historyDirection = UiNavigationDirection::eBack;
backOptions.initialPage = ui_page_id(Page::eWelcome);
backOptions.onNavigatePage =
    ui_page_stack_navigation_callback(view.ui.scene(), pages);
```

Build controls beneath the corresponding root, whether their builders are in
the same translation unit or another `.cpp`. Forward links call
`ui_navigation_push_history`; direct, non-history selection can call
`ui_page_stack_show`. `ui_page_stack_add_page` supports a root registered after
the stack is created. Duplicate IDs/roots and unknown destinations are rejected
without hiding the active page.

### Lay out wrapped text blocks

`<vibranceUI/ui/text_block.h>` adds document-style text without requiring apps
to insert manual newlines. Block placement and line alignment are independent,
so a centered fixed-width document can still use a shared left edge:

```cpp
UiTextBlockOptions body {};
body.width = 520.0f;
body.fontSize = 14.0f;
body.wrapMode = TextWrapMode2D::eWord;
body.textAlignment = TextHorizontalAlignment2D::eStart;
body.lineHeightMultiplier = 0.96f;
body.lineSpacing = -0.5f;
body.paragraphSpacing = 6.0f;
body.characterSpacing = 0.0f;
body.wordSpacing = 0.0f;
body.placement = UiAlignment::eCenter;
body.style = make_flat_text_style("#55585EFF");

UiTextBlockHandle paragraph = ui_create_text_block(
    view.ui,
    view.fontAtlas,
    pageRoot,
    "A long paragraph wraps automatically inside the authored width.",
    body);
```

Use `eStart`, `eCenter`, `eEnd`, or `eJustify` for each line and `eWord`,
`eCharacter`, or `eNone` for wrapping. Start/End follow right-to-left text
direction. Explicit newlines receive `paragraphSpacing`; automatically wrapped
lines receive the chosen line height and line spacing. `set_text_entity`
reflows an existing block, including after localization changes, and
`ui_set_text_block_layout` updates its editorial settings in logical pixels.
Use a shared-width `layout_group` or `UiStackLayout` when headings and body
blocks need the same edge and controlled spacing between differently styled
sections.

## Reuse liquid merge transitions

`UiLiquidMergeOptions` in `<vibranceUI/ui/surfaces.h>` exposes the analytic
liquid connection used by detachable interfaces. It works between any two
circular surfaces or equal-height pill end caps and is not tied to an island or
timer. Endpoint centres and diameter use framebuffer coordinates so the two
surfaces do not need a common layout parent.

```cpp
UiLiquidMergeOptions merge {};
merge.enabled = liquidMergeEnabled;
merge.firstCenter = mainSurfaceCenter;
merge.secondCenter = detachedSurfaceCenter;
merge.diameter = surfaceDiameter;
merge.amount = mergeProgress; // 0.0 detached, 1.0 merged
merge.opacity = mergeOpacity;
merge.layer = -1;

entt::entity bridge = ui_create_liquid_merge(ui, merge);

// Update the same entity while either endpoint moves.
ui_update_liquid_merge(ui.scene(), bridge, merge);
```

Set `enabled` to `false`, or call `ui_set_liquid_merge_enabled`, when an
interface should render without the effect. Paint, ordering, always-on-top
behaviour, and dynamic-cache participation are also regular options.

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

`GlassMaterial` separates the two ownership models. `eSystemGlass` is the only
material accepted by `SystemBackdropRegion`; it builds one shaped HostBackdrop
blur/saturation/tint graph with rectangle, per-corner rounded rectangle,
squircle, notch, or ellipse clipping. `eLiquid` is represented separately by
`LiquidGlassComponent` and is never forwarded to the OS compositor. The engine
renderer backend and the reserved `eMacOSNative` backend currently report
unavailable, so an `eLiquid` request safely leaves the entity's normal
translucent paint visible. On non-Windows systems system-glass regions
resolve to off. Native DWM
Acrylic and Mica providers are also exposed on Windows, but the raw Win32 DWM
API is window-scoped; the engine therefore accepts them only for a full-window
rectangle and rejects requests that could leak outside their requested shape.

Stock buttons, icon buttons, search fields, and navigation clusters expose a
`UiGlassOptions glass` member. Custom shapes can use the same route directly:

```cpp
UiGlassOptions glass = ui_system_glass_options(
    8.0f,
    1.0f,
    { 1.0f, 1.0f, 1.0f, 0.03f });
ui_apply_glass_material(ui, entity, glass);
```

Use `ui_liquid_glass_options()` for an engine-owned liquid request. On a future
macOS implementation the same call will select `eMacOSNative`; no macOS Liquid
Glass code is compiled for now.

MinGW builds the reusable engine normally and invokes MSVC only for the small
`vibrance_win32_composition.dll` and `vibrance_win32_interop.dll` companions.
Installing the engine places them beside the engine DLL. The exported CMake
package publishes their paths and
`vibrance_engine_copy_runtime_companions(target)` so downstream applications
need no MSVC discovery or custom build/copy rules.

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

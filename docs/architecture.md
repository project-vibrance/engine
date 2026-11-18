# Architecture

The engine owns reusable mechanisms: scenes, rendering, input, controls, windows,
media/audio services and native integration. An application owns its assets,
preferences, document schemas, navigation destinations and feature behaviour.

## Source map

```text
cmake/
  build/                 Platform selection, release metadata and build options
  dependencies/          Dependency discovery and source-library setup
  toolchains/            Cross-compilation toolchains
engine/
  CMakeLists.txt         Shared-library assembly and test entry point
  cmake/build/           Source selection, target settings, linking and installation
  cmake/platform/        Windows Composition and WinRT companion build rules
  include/vibranceUI/    Installed public headers
  src/                   Engine implementation and private headers
  shaders/               Vulkan GLSL and Windows glass HLSL
  tests/                 Unit, renderer, platform and SDK checks
  third_party/           Bundled dependency source and its notices
  licences/              Additional dependency notices
scripts/                 Build diagnostics, developer tools and application template
```

`build/`, `install/` and `dist/` contain generated artefacts. Public headers have
one source of truth in `engine/include/vibranceUI`; installation copies that tree.

## Responsibilities

| Area | Responsibility |
| --- | --- |
| `core` | Files, logging, process services, credentials, single-instance handling and runtime identity |
| `display` | Monitor selection, placement, foreground state and panel scaling |
| `glfw` | Native window ownership, event routing, UI surfaces and hosted panels |
| `graphics` | Backdrop contracts, CPU colour/image utilities and SVG colour templates |
| `renderer/common` | Retained scenes, render planning, fonts, text positioning and media preparation |
| `renderer/vulkan` | Vulkan device resources, commands, shaders and presentation |
| `renderer/metal` | Metal resources, shaders, presentation and AppKit glass integration |
| `ui` | Layout, input, styles, controls, dialogs, modal lifecycle and colour picking |
| `platform` | File dialogs, clipboard/eyedropper services, wallpaper, trays and OS bridges |
| `audio` | Playback, process audio capture and spectrum processing |
| `media` | Session metadata, transport commands, artwork and presentation helpers |
| `notifications` | Provider snapshots, callback-driven cards, text and stack layout |
| `localisation` | Locale dictionaries, text resolution and interface direction |
| `time` | Countdown state and presentation-independent timer behaviour |

Native image, file and font bodies live under `platform/win32`, `platform/macos`
and `platform/portable`. Common image selection and font-table inspection remain
in their shared modules. `engine/cmake/build/Sources.cmake` selects the appropriate
implementations. Platform-specific source files do not provide duplicate fallbacks
for other operating systems.

## Public API structure

`vibranceUI/engine.h` remains an umbrella include for existing consumers.
`ui/controls.h` also remains supported; it includes the focused headers under
`ui/controls/`. New code can include the control families it actually uses.
The control implementations retain their existing global types, functions and
inline definitions.

Public APIs describe generic state and operations. Private headers under `src/`
may describe backend policies and native companion protocols; applications should
use installed headers. Platform helpers are reached through the public facade
for their service.

## Rendering and lifetime

A `Renderer2DScene` retains entities, layout, paint, media references and input
components. UI builders author those entities. Renderer backends resolve layout,
produce a render plan and submit it to the selected surface. Dirty tracking and
dynamic caches allow unchanged content to be retained between frames.

`UiWindow` owns a complete ordinary application window. `GlfwPanelWindowHost`
provides the lower-level panel/window/renderer lifecycle used by secondary panels.
Renderer resources belong to the engine that created them. Keep media handles and
entity IDs within that engine/scene lifetime, and refresh stored entity IDs when
a scene is rebuilt.

CPU image decoding may run on a worker with cancellation. Scene mutation, media
uploads and control updates are coordinated by the window's UI loop. Native
providers publish snapshots; application code decides how to apply them.

On Windows, the engine's MinGW library and its MSVC companions communicate through
a versioned ABI. Composition presentation and media/notification interop are
separate companions. The DWM glass extension and helper executables are separate
runtime artefacts with their own deployment lifecycle.

## Keeping boundaries useful

Add a control to its focused control header. Add a native implementation to its
platform directory and source-selection branch. Keep backend command generation
inside the backend and shared scene logic inside `renderer/common`.

Theme role bindings accept application-provided palettes. Font inspection reports
embedded names. The colour picker accepts fonts, artwork and localisation from
its caller. Applications decide how to save documents, confirm destructive actions
and map domain-specific theme roles. Product-specific island/notch behaviour and
preview composition remain outside the engine.

The engine is distributed as one shared library plus its required companions.
These source modules organise ownership without introducing extra runtime layers.

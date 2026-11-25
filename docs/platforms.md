# Platforms

The build selects one renderer backend and one set of native providers. Service
availability is queried separately from renderer readiness. A valid API call can
return an unsupported or unavailable result when its provider is absent.

## Rendering

| Host | Renderer | Contract |
| --- | --- | --- |
| Windows | Vulkan with optional Windows Composition presentation | Vulkan 1.2 through 1.4 |
| Linux | Vulkan | Vulkan 1.2 through 1.4 |
| macOS | Metal | Metal 3; deployment target macOS 13 or newer |

The Vulkan loader and physical device are negotiated independently. The supported
range is defined in `renderer/vulkan/vulkan_api_policy.h`; newer loaders remain
capped at the implemented 1.4 contract. Vulkan 1.2 uses SPIR-V 1.5 compatibility
variants. Vulkan 1.3/1.4 use the modern SPIR-V 1.6 variants. Selection occurs during
initialisation; the existing frame and rendering command paths are shared.

The Vulkan allocator resolves functions dynamically using the negotiated device
version. The Windows engine DLL must not statically import Vulkan 1.3 allocator
entry points: an older loader would reject the DLL before version negotiation
or the Vulkan 1.2 compatibility path can run. The loader-import regression test
inspects the built DLL as well as exercising renderer initialisation.

The renderer also requires suitable hardware limits and formats. In particular,
it checks for at least 11 compute-stage storage-image descriptors. The current
workloads also use 64-invocation compute groups, 128-byte push constants, three
bound descriptor sets and `rgba8`, `rgba16f` and `r32ui` storage images.
A Vulkan version alone does not guarantee those limits.

Legacy hardware qualification must use the actual device/driver reports and
physical rendering tests. Requested targets include Kepler-based GTX 600 and
GCN-based Radeon HD 7000 devices with capable drivers; those marketing series
contain variants, so a series name is not a device capability check.

Set `VIBRANCE_FORCE_VULKAN_12=1` in a test process to exercise the compatibility
path on a newer GPU. This does not reproduce an older GPU's limits or performance.
The shader/profile and renderer tests validate both profiles. See
[Diagnostics](diagnostics.md#renderer-checks) for targeted commands.

On Wayland, the compositor controls window placement. The engine uses the primary
monitor for windowed monitor queries that need screen coordinates. Open Wayland
windows share compatible Vulkan instances to avoid a crash on affected NVIDIA
drivers when another window closes.

On X11 and XWayland, the optional GLFW source build selects an ARGB visual for
transparent Vulkan windows. Undecorated windows using an opaque visual fall
back to X Shape, with binary transparency at their edges.

Linux desktop glass combines Vulkan tint and rim paint with compositor backdrop
blur. X11 uses the KDE blur region hint for ARGB windows.
Native Wayland uses the background effect protocol when the compositor advertises
blur. The [Vibrance KWin effect](liquid-glass.md#linux-desktop-backdrop) adds
desktop refraction and colour separation for Liquid Glass on Wayland and
XWayland. Vulkan supplies optical effects directly for materials that sample
the app's own scene.

## Glass and presentation

`GlassMaterial` selects off, system glass or Liquid Glass. Backend selection and
surface/presentation support determine the resulting implementation. Retained UI,
controls and material state are shared across the rendering paths.

macOS can use native AppKit Liquid Glass on macOS 26 or later. Earlier supported
macOS releases retain the applicable renderer/system-glass path. On Windows,
Vulkan renders UI and can share its result with the D3D11/Composition presenter.
D3D11 presentation is separate from Vulkan UI command generation.

## Windows Liquid Glass

The native Windows optical path uses a DWM compositor texture and HLSL shader
passes. It does not use desktop screen capture as its shader source. This path
has a protected runtime containing the compositor extension, broker, observer,
shader objects and symbol tools. Its helper deployment uses administrator consent;
ordinary engine windows and solid controls do not require that path.

The runtime stages a versioned immutable bundle under the machine application
directory, with a fixed scheduled observer action. Account/session identity and
bundle fingerprints are checked before reusing an active generation. An updated
bundle can be staged while the loaded generation remains active until Windows
sign-out. The helper does not restart DWM to force a replacement.

The public `core/liquid_glass_runtime.h` query exposes whether a verified staged
runtime needs a session restart. Applications decide how to offer that action.
The SDK's deployment/installation helpers carry the required companion files.
Inspect the actual runtime and optical submission status when diagnosing this path.

## Native services

| Service | Windows | macOS | Portable/Linux implementation |
| --- | --- | --- | --- |
| File picker | Native file dialog | AppKit open panel | Zenity or KDialog |
| Directory opening | Shell | NSWorkspace | `xdg-open` |
| Image clipboard and decoding | Clipboard/WIC | Pasteboard/ImageIO | stb decoding and wl-clipboard or xclip |
| Screen colour selection | Cursor sampling | Native colour sampler | Desktop Screenshot portal |
| Configured monitor wallpaper | Desktop wallpaper provider | NSWorkspace wallpaper | Empty image |
| Global media session | WinRT companion | MediaRemote companion | MPRIS over the session bus |
| Audio loopback capture | WASAPI | CoreAudio and ScreenCaptureKit | PulseAudio and PipeWire's PulseAudio service |
| System tray | Notification area | Status menu | StatusNotifierItem and D-Bus menu |
| Secure credential storage | Windows Credential Manager | Keychain | Desktop Secret Service |
| System notification provider | WinRT companion and access checks | Unsupported | Unsupported |

`native_desktop_wallpaper(position, size)` retrieves the configured monitor
wallpaper without capturing application windows. It returns an empty image if a
provider cannot supply it. `native_open_directory` creates a missing directory
only when its caller explicitly enables that policy.

`GlobalMediaSession` exposes metadata, transport and timeline capabilities.
Windows media control and audio capture can use different processes: the engine
routes capture through the app's audio-rendering process/session relationships.
Transport and seeking still depend on source capabilities and provider support.
A process-specific workaround belongs behind that service boundary.

`AudioLoopbackCapture` and `AudioSpectrumProcessor` expose audio capture and
processing. On Linux, capture uses the PCM monitors exposed by PulseAudio or
PipeWire's PulseAudio service. Per-player capture follows matching playback
streams to their current outputs, and system capture combines the available
output monitors. Audio routed directly to a device needs a monitor exposed by
one of these services to be captured.

Notification providers publish neutral items and access/readiness state.
Applications own inbox models, actions, persistence and display policy.

A platform-supported provider can still be unavailable because its companion is
missing, its ABI is incompatible or access was denied. Inspect the service result
and [diagnostic logs](diagnostics.md) before advertising it in an application.

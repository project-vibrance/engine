# Windows Liquid Glass implementation

The island exposes Solid, Acrylic/Mica, and Liquid Glass in Settings. Windows
notification cards can use the same Liquid Glass renderer with their own tint.
Liquid Glass uses a DX11 pixel shader with a live DWM compositor texture as its input.
It does not obtain its shader input through screen capture. Controls, text and
pointer interaction remain in the normal UI. Separate glass surfaces retain
their black top gradient there; connected surfaces apply their cap gradient
once to the combined native mask.

## Current rendering path

Windows startup uses a per-user Task Scheduler helper. Selecting Liquid Glass
configures it with one administrator consent prompt, deferred while a fullscreen
application is active. Subsequent app starts, logons and DWM restarts reuse that
approval. A changed runtime bundle needs a new approval to deploy its binaries.
The signed-in account must be an administrator; approval using another account
cannot supply an elevated interactive token for the original user's session.

The console broker, windowless observer, extension, five shaders, `dbghelp.dll` and `symsrv.dll` are hashed
together and copied to a versioned directory under `%ProgramFiles%\VibranceLiquidGlass`.
Only Administrators and SYSTEM can modify that directory or the task's fixed
`--watch` action. The symbol cache and reports live under
`%ProgramData%\VibranceLiquidGlass\<user SID>`. The observer retries temporary
symbol-server/access failures, bounds extension-load failures per DWM process,
and defers new injection during fullscreen applications. It never restarts DWM.
Uninstall removes the startup task; loaded immutable runtimes remain pinned
until the Windows session ends.

`liquid_glass_dwm_broker.exe --status` reports readiness and, when no status
mapping exists, the last launcher/setup/broker reports and task result. Win32
error 2 here means that the session mapping does not exist; it does not establish
that a DLL is missing. `--auto-config` prints the intended task XML without
registering or running anything; `--run-auto` starts an already configured task;
an elevated `--remove-auto-run` stops and unregisters it. Native ARM64 DWM
requires the ARM64 release; an emulated x64 broker is rejected before injection.
The build requires matching Debugging Tools DLLs instead of silently packaging
a runtime that only works with a previously downloaded symbol cache.

1. The app publishes each lens region's HWND, allocation identity, monitor and
   client bounds, shape, opacity, optical controls, tint, optional contour union
   and exclusion in a versioned shared region table. Different surfaces in the
   same HWND have separate identities.
2. An elevated broker verifies the installed `dwmcore.dll` SHA-256 hash, PE
   architecture, and matching Microsoft PDB symbols before loading the
   extension into DWM. Missing symbols or a binary mismatch fail closed.
3. When the extension is ready, each lens places a raw Composition backdrop
   dependency below a translucent marker visual. The backdrop effect is an
   identity saturation operation: it makes independently presented desktop
   layers available to DWM without applying Acrylic, Mica, or system blur.
   Its bounds include the lens's sampling margin.
4. The extension recognises the marker during native colour-brush drawing and
   tags its draw-list entry. Retained entries are checked against their complete
   recorded identity, including vtable, geometry, effect, colour resource, tint,
   and bounds. The bounded tag table expires inactive entries so reused native
   addresses cannot classify unrelated content as glass.
   Its captured shape remains paired with the queued native geometry, while
   current optical controls, tint and mask uniforms can update retained entries
   independently when their native draw bounds remain compatible.
5. When DWM executes that entry's rendering batch, the extension flushes earlier
   geometry and reads the actual bound DX11 render-target view. It copies the
   local background and sampling margin to a GPU texture, then replaces the
   marker draw with the two-pass blur and optical HLSL pass. Blur and tint apply
   across the complete background, including the lens centre. Native geometry,
   transforms, viewport, and scissor determine the lens's position and clipping.
   The DX11 state is restored before DWM continues drawing.
6. DWM renders the island's sharp controls and text once through its normal
   window content. The native optical pass supplies the cap gradient for a
   connected contour union. There is no late Present pass that
   replays the UI. The same marker execution path runs when DWM renders display,
   Windows Graphics Capture, and Desktop Duplication targets.

The app never captures the screen for shader input. The background copy comes
directly from DWM's in-process DX11 render target at the marker's native draw
position. Diagnostic captures verify output and are not part of the runtime
path. If the private extension is unavailable, a shaped Composition blur
fallback remains available; that fallback does not provide shader refraction.

The shared protocol supports up to 64 simultaneous lens regions. Slots belong
to individual glass draws, not HWNDs: an island window can contain its main
surface and detached companions, or combine connected contours in one draw;
a notification window can contain multiple cards. Each allocation receives a
distinct lifetime identity, so removing a
card or reusing a slot cannot associate old cached geometry with a new lens.
The test machine has a 5120×1440 primary display and a 1920×1080 secondary
display; target-local geometry determines where each shader draw occurs.

## Lens geometry and controls

The optical mask follows the island's actual shape, including independent
corner radii, rounded-to-squircle transitions, and the animated notch's concave
cut-outs and flared corners. Marker and sampling bounds include the flares.
The shader derives its edge normal from the complete signed-distance function
with a finite gradient, so refraction and the living rim follow the same
outline. Interior coverage remains continuous across corner transitions.

Partially off-screen lenses retain their full body size, radii and signed
distance function. Only the draw viewport and background copy intersect the
compositor target; shader origin offsets preserve the cropped shape. The copy
also retains the available sampling margin. A lens entirely outside the target
is consumed without drawing or recording a geometry failure.

Detached merge necks use the native Liquid Bridge contour, driven by the same
merge amount as the sharp renderer. With compact translucency enabled, connected
main, detached and bridge surfaces share one lens mask containing up to three
contours. Its bounds include notch flares, and the bridge contour retains its
own dispatch bounds inside the union. One optical pass refracts and blurs the
combined shape, avoiding independently painted glass at the joins. The cap
gradient is applied once against the same union. The main surface keeps its
allocation identity while participating peer regions are removed from the
native draw list. The user confirmed matching notch and detached transition
contours in the preceding build; the latest combined-mask changes still need
an on-screen check.

Notification front cards, visible backing silhouettes and Dismiss all use glass.
A backing card publishes an exclusion contour for the card in front, so only
its exposed stack silhouette is processed. Sharp content and interaction remain
in their normal window order.

Windows Settings > Appearance shows a Liquid Glass card when that material is
selected. Its sliders save integer percentages from 0 to 100 in preference
schema 13:

| Slider | Preference key | Default |
|---|---|---|
| Refraction | `liquidGlassRefraction` | 70% |
| Background blur | `liquidGlassBlur` | 15% |
| Rim brightness | `liquidGlassRim` | 60% |
| Colour separation | `liquidGlassDispersion` | 35% |
| Tint intensity | `liquidGlassTint` | 0% |

Tint follows the light/dark surface palette; no tint colour controls are exposed.
Legacy RGB preferences remain readable for compatibility and do not select the
current shader tint. Compact islands remain solid by default. The
**Translucent compact island** toggle saves `liquidGlassCompactTransparent` and
enables the connected glass cap gradient; expanded surfaces retain glass.
Reset restores the five optical values and switches compact translucency off.
Notification tint has an independent intensity slider, defaulting to 80%, and
its own reset. Schema 13 upgrades the previous default blur/tint amounts while
retaining other custom values.

Changes update existing backdrop components live without rebuilding their
scenes. `SystemBackdropRegion` carries normalised optical controls and a
separate `liquidTint` RGBA value. The bridge maps them to native refraction and
its edge band, Gaussian blur across the complete background, rim intensity,
chromatic dispersion and tint. The separable Gaussian blur uses a dense 61-tap
kernel with 31 bilinear fetches per axis at full source resolution. Adjacent
weights share a fetch; blur does not stretch a sparse kernel's texel stride or
downsample its input. Native region change detection includes these
values explicitly, so 1% slider updates no longer wait for a resize or shape
change. The Settings panel retains pointer capture during dragging. Solid and
Acrylic/Mica keep their existing behaviour.

The native Composition ABI is version 22 and the DWM shared protocol is version
13. Each ABI region is 400 bytes, each shared region is 432 bytes, and the optical
constant buffer is 416 bytes. Public, ABI and shared contours are 56 bytes; the
constant buffer packs each contour into 64 bytes. They carry up to three union
contours, an exclusion contour and cap-gradient uniforms alongside per-corner
radii, squircle/notch geometry, draw bounds, tint and optical parameters. Their
layouts are identical on x64 and ARM64. The DWM hook configuration remains 296
bytes. Protocol mappings use `V13` and the diagnostic status mapping uses `V16`.
The optical shader uses the `ps_4_0` profile.

## Compatibility and builds

The Composition bridge currently builds against Windows SDK 10.0.19041.0
headers. The Windows App SDK 2.5.1 package is available to the separate probe,
but is not required by the bridge. The reference LiquidGlassWinUI private hook
targets SDK 2.2.0 and is not used. The adapted
shader geometry is covered by the vendored MIT notice in
`engine/licences/liquid-glass-winui.txt`.

Earlier runtime proof is Windows 11 build 26100 on x64. Independent Desktop
Duplication and Windows Graphics Capture probes show displaced high-contrast
stripes and sharp island controls on both outputs. The user has confirmed that
glass is visible on both displays and captured by OBS Display Capture. After
adding the raw backdrop dependency, the user also confirmed primary-display
Wallpaper Engine refraction and OBS capture. These checks apply to the tested
display arrangement and Windows build; they do not establish compatibility
with every capture tool or display configuration. The user also confirmed
that Snipping Tool dims and blocks interaction on both displays and that the
glass is stable. The user subsequently confirmed that the corner seams are
gone and the optical outline follows the notch. They later confirmed matching
contours during notch and detached transitions. These user confirmations cover
the earlier tested builds; latest OBS and Snipping Tool checks remain pending.

The preceding x64 integration also passed direct Desktop Duplication and
Windows Graphics Capture output checks on the primary and secondary displays.
Both providers show matching warped rims and background blur with sharp UI and
no duplicated island. The live region table simultaneously held the primary
island in slot 0, three notification front cards in slots 1–3 within one HWND,
and the secondary island in slot 4. This verifies multiple active lens regions
in one notification window as well as both island outputs. It does not replace
the user's pending checks of the latest compact option, dense blur, combined
mask, cap gradient and notification stack changes.

The extension uses private DWM symbols and must be revalidated when Windows
changes. The preceding full Release engine and app builds passed for x64 and ARM64,
including the native DX11 bridge, broker and DWM extension. Final shader bytecode
was rebuilt and installed for both architectures, and the app's native companion
hashes match the installed engine. GPU tests against the installed x64 shaders
passed centre blur, tint endpoints, notch/cropped coverage, merge neck/cropped
bridge geometry and native state restoration. Focused app preference/island
layout and engine stretch/notification card tests also passed. The current
x64 direct capture results are described above. The protocol 12 / ABI 21 integration and subsequent neutral-rim/control update
build successfully in Release for x64 and ARM64. Installed x64 shaders pass
union contour cropping, exclusion coverage, cap gradients, dense blur, tint
endpoints and state restoration. Focused preference migration, control paint
restoration, island layout, stretch and notification tests pass. The revised
x64 output has also been inspected through Desktop Duplication and Windows
Graphics Capture; the user reports that the borders and refraction look better.
ARM64 runtime remains untested; injection and visual output require an ARM64
Windows machine for verification.

Windows 10 22H2 x64 native hook activation is restored with a separate session
and device-lifetime guard. Older Windows 10 builds and Windows 10 ARM64 retain
the fallback. Windows 11 retains its existing activation and rendering path.
The supplied minidump reports `SYSTEM_THREAD_EXCEPTION_NOT_HANDLED`
(`0x1000007E`, exception `0xC0000005`) in
`dxgkrnl!ADAPTER_RENDER::SignalCrossAdapterSyncObjects+0x69`, reading address
`0x8`. Its stack includes `DXGGLOBAL::NotifyAdapterRemoval` and
`DpiPowerArbiterThread`; the process is System. The Plug and Play blackbox
records an in-progress event for an RDP indirect display adapter, problem code
24. The user confirmed a GPU-P VM and an RDP/Enhanced Session transition at
the crash. This supports a graphics-adapter transition as the immediate
trigger; it does not establish whether the native optical pass contributed to
the failure. This dump does not prove that the new guards prevent that kernel
fault; live GPU-P session-transition validation remains outstanding. An already
loaded older extension requires a new Windows session.

The Windows 10 22H2 x64 (OS build 19045) port has a separate native hook adapter.
It scopes the sentinel through `CBrush::Draw` and `CHWDrawListEntry::Render`,
reads the first queued primitive's transform, then replaces the marked geometry
in the common and brush effect callbacks and consumes that primitive. Windows
10 passes a `CBaseDrawListEntry` to these
callbacks; Windows 11 passes a rendering command. Each decoder uses its own
object layout and the common DX11 optical renderer retains native draw order.
The Windows 11 symbol resolver, callback selection and 296-byte hook
configuration remain intact. Windows 10 publishes an additional 88-byte V2 layout
mapping, bound to the same SHA-256 fingerprint and checked again inside DWM.

The broker verifies the Windows 10 field accesses and virtual-base table in
the matching DWM image before activation. Unknown layouts retain the
Composition fallback. Windows 10 ARM64 and older Windows 10 releases retain
their existing fallback; this adapter is for the requested 22H2 x64 target.
The cache permissions and versioned runtime deployment also apply to this path.

The Windows 10 observer runs on the extension's existing worker thread. Its
hidden window receives session, display, device and power notifications; a
250 ms sampler also checks WTS connection/protocol state and DXGI factory
currency. Stable console and RDP/Enhanced Sessions can use native glass.
Session and display transitions pause native optical drawing and reset the
readiness event, so the app selects its existing Composition fallback.
The observer retires cached backgrounds, shader resources and retained marker
tags after an active draw finishes, using a non-blocking mutex attempt.
Native drawing resumes only after two seconds of stable active-session samples and
completed resource retirement. A stale observer or outdated generation cannot
reopen rendering. On WTS query access denied, DWM's virtual account can use
physical console attachment or its own remote-session metric. Disconnect,
lock and logoff notifications override this fallback until a corresponding
connect, unlock or logon notification. Other query errors pause rendering.
Notifications for other sessions do not suspend this session. Query operations
run outside the app's frame path; optical callbacks use the fresh admitted
generation rather than requiring RDP to be the physical console.

Windows 10 retains the native context/device for each optical pass, checks the
device-removal reason before flushing and around rendering, and verifies that
the native target belongs to the same COM device identity. Device loss revokes
readiness and starts a new generation. Re-entrant geometry callbacks forward
to the original emitter without repeating optical work or double-consuming a
queued primitive. Windows 11 keeps its original flush and locking order.
These guards follow the documented
[WTS session protocol](https://learn.microsoft.com/en-us/windows/win32/api/wtsapi32/ne-wtsapi32-wts_info_class),
[console attachment](https://learn.microsoft.com/en-us/windows/win32/api/winbase/nf-winbase-wtsgetactiveconsolesessionid),
[DXGI factory currency](https://learn.microsoft.com/en-us/windows/win32/api/dxgi/nf-dxgi-idxgifactory1-iscurrent)
and [D3D11 device-loss](https://learn.microsoft.com/en-us/windows/win32/api/d3d11/nf-d3d11-id3d11device-getdeviceremovedreason)
APIs. Console use includes Moonlight while it streams that console session;
native refraction also resumes in an active RDP session after settling. The
former presenter check on `SM_REMOTESESSION` prevented the broker from starting
there, leaving `--status` without a mapping (exit 13). That permanent startup
block has been removed while retaining transition and device guards.

Offline symbol and layout validation passed against Microsoft's
[`dwmcore.dll` 10.0.19041.7725](https://msdl.microsoft.com/download/symbols/dwmcore.dll/F1A0D9C4376000/dwmcore.dll),
SHA-256 `b7488eb96e3ad2a845fdf0cd03d2d729e970eca81e08c087ce7a6deea8d2cd9d`,
and its matching PDB `72052572E107EC32B9A529B7B7487E09`, age 1. An older 22H2
[`dwmcore.dll` 10.0.19041.3636](https://msdl.microsoft.com/download/symbols/dwmcore.dll/35B83791372000/dwmcore.dll)
also passed with its matching symbols and fingerprint
`122e00a501847a334c68ae5d9296a072d1c9989b568163a9fbf3e4475a4ca59f`.
Windows 10 22H2 uses the 19041 DWM binary family. To validate a target module without modifying
the running compositor, use:

```powershell
& '.\liquid_glass_dwm_broker.exe' --resolve-windows10 `
    'C:\Windows\System32\dwmcore.dll' "$env:LOCALAPPDATA\VibranceDwmSymbols"
```

The initial VM diagnostics showed loaded hooks and marker scopes but zero
optical draws. The adapter had treated temporary primitive-vector endpoints as
entry identity: the first empty queue failed tagging, and each subsequent
append invalidated its tag. Identity now uses the stable geometry, effect and
bounds; an observed ordinary brush draw removes a previous tag. The transform
comes from the queued primitive, since DWM sets the execution context's matrix
inside the original emitter, after our interception. Each override erases
exactly one 76-byte primitive, matching the native queue lifecycle.

Windows 10 also omits the padded identity-backdrop visual, which can resolve
to an opaque black intermediate surface. The legacy pass samples the live
native target, and its rim clock invalidates the marker visual itself. Windows
11 retains its existing identity backdrop and clock.

Tests exercise the production Windows 10 marker adapter, initially empty and
mutating queues, retained-entry invalidation, queued matrix decoding, native
lens placement, repeated queue consumption and ordinary draw forwarding.
The same fixture checks Windows 11 command decoding and brush colours. ABI
mutation tests reject changed fields, invalid RVAs and unsupported OS/CPU
combinations. Live Windows 10 display, capture and multi-monitor checks still
require a Windows 10 22H2 machine; they cannot be verified on the Windows 11 host.

For development, launch the app with `VIBRANCE_DISABLE_LIQUID_DWM_EXTENSION=1`
in its environment. In PowerShell, run:

```powershell
$env:VIBRANCE_DISABLE_LIQUID_DWM_EXTENSION = '1'
& '.\build\windows-x64\Release\app\Vibrance.exe'
```

This uses the native Composition fallback and suppresses extension activation,
including when an extension is already running. Remove the environment variable
to enable it again. Disabling does not unload the pinned DWM module.

The broker loads a verified, versioned copy of the DLL and its five shaders from
the symbol cache's `runtime` subdirectory (normally
`%LOCALAPPDATA%\VibranceDwmSymbols\runtime`), keeping build and install
files available for replacement.
The cache grants the Window Manager group read and execute access to each
version directory, DLL and shader, preserving the owner's existing permissions.
Existing cached bundles have these permissions repaired before activation.
A build encountering an older mapped DLL
renames that image to an `.in-use-*` sibling and installs the new DLL at the
original path. It reports errors rather than packaging an outdated DLL.
The retained image can be deleted after signing out. Native extension updates
take effect in a new Windows session; the build does not restart DWM.

To check the running extension, use `liquid_glass_dwm_broker.exe --region` and
`--status` from the architecture's app output. Status word 147 counts recognised
native batch markers, 151 counts successful shader draws, 152 counts rendering
failures, and 153 counts rejected target or geometry validation. Words 6, 120,
and 111 classify successful draws as display, Windows Graphics Capture, and
Desktop Duplication respectively when that output scope is available. Counters
must be paired with visible displaced background in the actual output; they
alone do not prove that glass is displayed or captured correctly.
Status words 160–223 count successful draws for the 64 lens slots.
On Windows 10, words 224/225 count accepted/rejected marker tags, 226 counts
missing or malformed primitive queues, 227 counts complete queued markers,
and 228 counts queue-consumption failures forwarded to the original emitter.
Words 146 and 156 count calls to its common and brush geometry hooks. Word
253 is 19045 when the Windows 10 adapter is active.
Word 229 is 1 when the observer admits native drawing, 230 counts skipped or
busy optical passes, 231 counts device-loss suspensions, 232 counts rejected
target/device identities, and 233 stores the last device-removal HRESULT.
Words 234/235 count transition invalidations/resumptions, 236 counts completed
GPU resource retirements, and 237 stores an observer initialisation error.
Word 238 reports session evidence (0 unknown, 1 inactive, 2 console, 3 remote),
239 is 1 when disconnect/lock/logoff notifications block admission, and 240
is 1 when the DXGI factory is current. Exit 13 now reports a missing status
mapping and its Windows error explicitly.
Lifecycle regressions cover console-to-RDP recovery, disconnect/lock overrides,
stale samples, concurrent
invalidation, clock changes and device generations. The production fixture
uses separate WARP devices to verify identity checks, exercises retirement
while another thread holds the draw lock, and checks readiness revocation and
re-entrant forwarding without resetting a GPU or installing live DWM hooks.

The D3D11 shader test verifies that optical drawing changes pixels only inside
the island/scissor intersection, agrees with the unclipped result there, and
restores the original viewports, scissors, rasteriser state and render target.
Its coverage regression caught 1,932 interior holes with the old corner mask.
The corrected mask passes coverage checks for corner exponents 2, 4, and 6,
plus notched body coverage, flared corners, concave cut-outs, and bottom-corner
clipping.
Partial off-screen regression checks compare negative and overflowing
placements against the full lens reference and ensure fully off-screen draws
leave the target unchanged. The preference suite checks tint and
compact-translucency round trips, normalisation, migration defaults and
independent island/notification changes.
The GPU suite also checks blur at the lens centre, tint endpoints, Liquid Bridge
neck/cap coverage, cropped bridge and union geometry, exclusion masks and native
cap gradients, alongside native state restoration.

## Controls and neutral rims

Rims use neutral highlights in both the native HLSL shader and UI shape shader;
no blue outline is forced by the glass material. Native edge displacement is
bounded by the lens dimensions, with a broader curved band to fold background
features near the rim. User colour separation still controls chromatic fringes.

`ui_update_liquid_controls` now leaves individual buttons, text fields, slider
tracks/fills, toggle tracks and viewports in their normal material. Slider, switch and
scrollbar thumbs use glass only while dragged. Intentionally invisible media
seek thumbs retain their transparent paint, including during dragging. Idle/released thumbs are white.
Slider geometry uses a small damped spring (25% target expansion, with slight
overshoot), and both input mapping and painting reserve space for the expanded
thumb at the slider endpoints. Release removes glass immediately and lets the
white thumb settle back to its normal size. The input value is independent of
this animation.

`UiLiquidControlSurface` explicitly marks a shared navigation cluster surface.
The island's two navigation buttons remain ordinary sharp controls above that
single rounded lens. Shared controls and dragged thumbs use the portable Vulkan
app-content pass on Windows and Unix, sampling their interface background.
Their parent paint stays intact. The main island and notification surfaces
still use the native desktop optical path. Unix desktop refraction and ARM64 runtime remain unverified.

The native raw identity backdrop has a Composition-owned opacity animation.
Blending that identity over its identical source preserves the background;
the changing property invalidates the optical dependency at display cadence,
without requiring focus, pointer movement or republishing sharp UI content.
The clock stops when the material is invisible or rim intensity is zero.
The cached UI outline no longer runs a second glint animation, avoiding a
stepping highlight over the continuously animated optical rim.

Switch knobs support captured horizontal dragging, commit the nearest On/Off
state once on release, and retain normal click toggling. Grab expansion and
release use the same exact damped spring as slider thumbs. Their horizontal
elastic deformation also follows the existing stretch dynamics and settles
after release. The navigation cluster inherits its two buttons as sharp
children and has its own stretch dynamics with a spring return. Actual drags
consume the release click, preventing a menu activation or island collapse
from hiding the post-drag bounce. Ordinary clicks remain unchanged.

The latest x64 diagnostic Desktop Duplication and Windows Graphics Capture
images show the exposed navigation lens and the media seek bar without a
thumb. The native table holds separate main-island and navigation-cluster lens
regions in the same HWND, with no render failures recorded. Focused tests cover
switch drag commits in both directions, normal clicks, hidden media thumbs,
release overshoot at 30/60/144 Hz, and full-precision notch parameters when the
cap is cut out. Release engine/app builds pass for x64 and ARM64. User checks
confirm the switch feel, hidden media seek thumb and navigation spring return.
ARM64 runtime and Unix desktop refraction remain unverified.


Shared navigation and search controls use the same uniform clear lens paint
as a grabbed thumb, without a cap gradient. Windows and Unix use the portable
GPU app-content pass for these lenses. Panel paint remains in the source
texture, so the controls refract the interface beneath them rather than
opening holes onto the desktop. Leaving Liquid Glass restores search focus
styles. Connected native island unions supply their known cap gradient under
child-lens source samples because Composition owns that paint instead of the
UI texture. Sharp foreground source pixels are preserved above the cap.

Composition ABI 21 uses a previously reserved region flag to clip the raw
identity dependency paint for opt-in native controls. The sampling halo stays
unchanged; the main native surfaces retain their existing dependency. The
DWM protocol remains 12 and region payload sizes stay unchanged.

The search surface explicitly opts into text-input stretching; dragging keeps
its focus and query. A Settings switch inside a scroll viewport captures its
own gesture rather than letting the viewport cancel it. Slider and switch
thumbs grow to 125% while held, overshoot slightly through the damped spring,
and return to white on release. Slider paint and hit mapping reserve the same
132% maximum size, so endpoint geometry stays contained. Hover growth keeps
the track centre fixed; initial placement and hit mapping use the same fixed
travel inset before and after enabling the material. Switch deformation uses the authored offset and track
centre rather than the previous frame's resized knob layout.

The island itself retains its cap: its upper half is opaque, with a fade to
the exposed Liquid Glass or Acrylic/Mica material over the lower half.

The user confirmed captured switch dragging, control clipping and the Settings
column join after the switch-input update. The subsequent interface-source
change keeps those input and clipping rules. Temporary input trace logging
was removed from the engine runtime.

## Themed caps and control refinements

ABI 22 / protocol 13 encode the cap RGB8 colour above the enable byte in the
existing `paintCapGradient` word. Region and constant-buffer sizes remain
400/432/416 bytes. Dark, light and custom island caps retain the theme surface
colour, including connected unions; portable child lenses use the same colour
in their source samples. The direct D3D11 regression renders both black and
light caps across overlapping contours and checks their pixel continuity.

Slider fill uses the same inset travel as its thumb, removing a low-value gap.
Slider and switch knobs have a faint outer shadow with zero spread. Shared
control lenses also receive a faint outer shadow. Traffic lights preserve
their colour and capture stretch gestures with 15% grab expansion, a damped
release bounce and click suppression after dragging.

The SDK installs matching Debugging Tools `dbghelp.dll` and `symsrv.dll` when
available, and exports them as runtime companions. A freshly rebuilt broker
using only OS DbgHelp failed symbol resolution (error 1168); the matching
Debugging Tools pair resolved the current DWM PDB and restored the extension.

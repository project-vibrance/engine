# Windows Liquid Glass implementation

The island exposes Solid, Acrylic/Mica, and Liquid Glass in Settings. Windows
notification cards can use the same Liquid Glass renderer with their own tint.
Liquid Glass uses a DX11 pixel shader with a live DWM compositor texture as its input.
It does not obtain its shader input through screen capture. Controls, text and
pointer interaction remain in the normal UI. Separate glass surfaces retain
their black top gradient there; connected surfaces apply their cap gradient
once to the combined native mask.

## Current rendering path

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

To check the running extension, use `liquid_glass_dwm_broker.exe --region` and
`--status` from the architecture's app output. Status word 147 counts recognised
native batch markers, 151 counts successful shader draws, 152 counts rendering
failures, and 153 counts rejected target or geometry validation. Words 6, 120,
and 111 classify successful draws as display, Windows Graphics Capture, and
Desktop Duplication respectively when that output scope is available. Counters
must be paired with visible displaced background in the actual output; they
alone do not prove that glass is displayed or captured correctly.
Status words 160–223 count successful draws for the 64 lens slots.

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

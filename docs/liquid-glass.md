# Liquid Glass

The engine owns the shared material, optical sampling and control response.
Applications own their content and behaviour. Island/notch implementation remains
in the application. This implementation is original C++ and GPU shader code;
none of the repositories below is included as a dependency or copied into the
engine.

## Reference review

Reviewed on 12 November 2026. Published observations and comparison images were
used as evidence. Fidelity claims apply to each project's stated test conditions,
not every macOS material or interaction.

| Repository | Relevant observations and use |
| --- | --- |
| [medfa12/liquid-glass-react](https://github.com/medfa12/liquid-glass-react) | Separates backdrop acquisition from rendering and discusses material formation and adaptive luminance. Its README explicitly describes remaining fidelity gaps. No WebGL or shader implementation was imported. |
| [emribilemir/liquid-glass-research](https://github.com/emribilemir/liquid-glass-research) | Provides macOS captures with matching backdrops, focus-state observations and measured pointer response. These captures are the principal comparison fixtures for face tone and rim sampling. |
| [AlexStrNik/ShatteredGlass](https://github.com/AlexStrNik/ShatteredGlass) | Describes separate shape, backdrop and opposing highlight responsibilities. Its roadmap identifies incomplete tuning and lens work; it is architectural evidence rather than a finished visual reference. |
| [shayann07/liquidglass](https://github.com/shayann07/liquidglass) | Distinguishes material roles and resting, tracked and released states. Its measured-model and replay documents identify remaining motion differences. Mobile-specific deformation is not imposed on desktop controls. |
| [tomacco/amazing-glass](https://github.com/tomacco/amazing-glass) | Describes a flat face with curved edge optics and distinguishes mouse clicks from held manipulation. Published comparison errors are specific to its calibration scenes. |
| [IchimTeodor/LiquidGlassTabBar](https://github.com/IchimTeodor/LiquidGlassTabBar) | Highlights synchronising presented geometry with sampled content, avoiding repeated captures during a gesture and filtering lens displacement. Its tab bar behaviour is not transplanted into application navigation. |
| [Athena2s/liquidglasskit](https://github.com/Athena2s/liquidglasskit) | Documents separate resting and lifted thumb states, spring return and bounded dragging. It also lists capture and implementation limitations. No private Apple API integration was introduced. |

## Material

The face no longer scales the entire background towards its centre. An original
bounded radial profile displaces content near the contour and returns to identity
inside. Its parameters were fitted against native reference images. A small
sampling footprint filters fine detail and expands along the contour normal where
the mapping compresses content.

The clear material retains fine detail while a wider neighbourhood reduces bright
local contrast. A luminance response compresses highlights and lifts black without
applying separate nonlinear curves to the red, green and blue channels. Frost
blends towards the diffuse backdrop. Clear material's neighbourhood is a contrast
input, not a uniform blur of the face. Tint remains independent.

At zero blur, an unpainted lens samples the sharp backdrop at its refracted
coordinates. Refraction alone does not enable the Gaussian neighbourhood or
the multi-sample smoothing footprint. Positive blur retains the existing material
filter, and rim, tint and opaque cap settings remain independent.

Individual surfaces can retain backdrop RGB with
`LiquidGlassComponent::preserveBackdropColour`. Native Windows backdrops expose
the same policy through `SystemBackdropRegion::liquidPreserveBackdropColour`.
This bypasses the material's neutral colour coating independently of optical
blur, refraction and rim settings. The packed native material flags retain this
policy when the compositor resolves a region or a connected cap.

Windows advertises optional material controls through the native runtime's
capability word. A pinned older runtime must never receive unsupported controls:
its existing glass lens remains active while the replacement is staged. Signing
out and back in loads the replacement compositor runtime and enables its new
material controls.

Top and bottom highlights follow the contour normal. Sides use optical darkening;
there is no separately drawn Liquid Glass outline. Opaque caps bypass optics, and
strongly tinted scene surfaces keep their optical finish above their paint. Their
colour is preserved rather than treated as a neutral material face.

Light neutral panel controls preserve their themed translucent finish and render
their optical response above that paint. They do not use the platter's highlight
compression, which would turn a white control grey. Their upper and lower rims
retain headroom above the face. Button states retain the converted glass paint;
hover, press and release cannot restore an older solid fill over the material.
Dark control overlays do not select this light finish simply because their RGB
channels are white: their opacity and explicit surface appearance are considered.
Preserved-colour controls keep their face sharp at every panel blur setting; the
optical finish still supplies their reflected rims.

`graphics/liquid_glass_profile.h` defines shared sampling and edge sizes. The
portable compute shader and Windows optical shader use the same material model.
Blur neighbourhoods participate in source bounds, replay bounds and native scratch
preparation so clipping cannot leave uninitialised samples. Metal's generated shader
is checked against the GLSL source.

## Interaction

Mouse thumbs distinguish a quick click from a hold. Movement starts lens formation
immediately; a stationary press waits 75 ms. Geometry, transparent paint and optics
follow formation together. Release retains the material while its spring settles,
then restores the normal thumb. Input values and commit callbacks retain their
existing semantics.

Slider and switch thumbs use clear glass regardless of the containing panel's tint and
diffusion. Their optical pass preserves backdrop RGB and brightness, bypassing
the platter's tone curve and local contrast reduction. On Windows, these lenses
sample interface content, including the track and fill, rather than the desktop.
Their lifted shape expands vertically and stretches horizontally in
response to drag speed, measured in thumb heights per second. The deformation
retains its velocity on release and returns to the current resting paint, including
colour-picker swatches. Transparent thumbs retain the track's inset fill geometry.
Switch thumbs lift to roughly 185% of their resting height while held or dragged,
with bounded spring overshoot, a full pill contour and a centred vertical position.
Stationary expansion grows the rounded ends and straight body independently, so
capsules retain a visible centre section and circular thumbs remain circular.
Movement adds a separate deformation to that held shape. Slider construction and
input mapping reserve the complete expanded width before interaction begins.
Control thumbs use a thinner lens than large panels, retaining a clear collar
around the track instead of magnifying its colour across the entire held shape.

Standard glass buttons without an authored stretch gesture gain a small press
expansion with separate press and release springs. Existing authored gestures retain
ownership. Idle and hover states do not start this motion. Finished springs stop
invalidating retained frames. Solid controls retain their authored geometry.
Shared panel controls opt into `StretchDynamicsOptions::dragResponse`: pointer
events update shape targets, while a spring updates the visible shape and pivot on
the animation clock. Regrabbing a returning surface retains its scale and velocity.
Setting this response to zero retains direct deformation for functional gestures.

The application continues to own panel opening/closing policy and the island's
behaviour. These changes do not introduce an iOS tab bar, hover illumination or
scroll-driven resizing into desktop settings.

## Validation and limits

The native macOS fixtures from `emribilemir/liquid-glass-research` are stored only in
the ignored development build directory. They are not shipped as engine assets.
The reference-render mode of `vibrance_liquid_material_render_tests` accepts an
RGBA backdrop and emits the engine's result at explicitly supplied geometry.

For the focused, 1402 by 110 pixel capsule fixtures, mean absolute RGB error on the
face was measured on a 0–255 scale:

| Backdrop | Previous material | Revised material |
| --- | ---: | ---: |
| Greyscale bands | 21.551 | 1.109 |
| Checkerboard | 98.753 | 3.269 |
| Vertical stripes | 58.424 | 0.833 |

Two additional native app-content scenes, not used to fit the profile, measured
5.149 and 5.042 levels of face error respectively.

Rim error remains greater: approximately 15.2 levels on the fine checkerboard and
2.1–3.9 on the other two patterns. These measurements are calibration evidence,
not a claim of universal pixel parity. Focus-dependent native appearance, exact
corner geometry, material roles and arbitrary desktop content still require native
comparisons. The native reference data was captured by the referenced project;
Vibrance's comparisons ran on Windows.

Automated coverage includes native greyscale landmarks, clear/frosted/tinted
surfaces, colour-preserving rims, source clipping, backing-card exclusions, opaque
caps, HDR/SDR scratch reuse and retained blur. Interaction tests cover quick clicks,
held formation, release, display-rate consistency, press springs and settled-frame
retention. Vulkan 1.2 shader validation runs independently. ARM64 packages are built
on Windows; native macOS execution and the oldest supported GPUs are not validated
by those checks.

# User interface

The engine provides retained scene controls and window/panel mechanisms.
Applications supply content, assets and editing policy. Existing code can keep
including `vibranceUI/ui/controls.h`; focused headers expose the same API with
smaller control-family dependencies.

## Control headers

All paths below are relative to `vibranceUI/ui/controls/`.

| Header | Contents |
| --- | --- |
| `buttons.h` | Text/icon buttons, submenu rows and button options |
| `dropdowns.h` | Dropdown options, selection, menu visibility and outside-click dismissal |
| `sliders.h` | Slider construction and themed slider options |
| `switches.h` | Switch state, construction, dragging and knob animation |
| `selection.h` | Checkboxes and radio controls |
| `search.h` | Search fields, indexing, scoring and retained results |
| `navigation.h` | Navigation clusters and sidebar rows |
| `navigation_history.h` | Back/forward history state and commands |
| `shell.h` | Two-pane shell construction and layout slots |
| `scrolling.h` | Scroll viewport construction and scrollbar presets |
| `progress.h` | Circular progress and content colouring |
| `icons.h` | Common icon loading helpers |
| `theme_preview.h` | Generic theme preview controls |
| `common.h` | Control handles, text fitting and shared paint helpers |

Options, callbacks and returned entity handles retain their existing names.
The compatibility aliases `UiSettingsShellOptions`/`UiSplitShellOptions` and their
handles remain available.

`ui/control_appearance.h` supplies the shared panel presets:
`ui_panel_button_options`, `ui_panel_dropdown_options`, `ui_panel_slider_options`
and `ui_create_panel_text_button`. Buttons share the dropdown's geometry and
fixed labels are truncated within their bounds. Standard dialog actions use
their dialog styling. Dropdowns close on an outside press or window focus loss.

## Text editing

All `TextInputComponent` fields use a shared caret and selection model, including
search fields. Typing and pasting replace the selection. UTF-8 editing moves and
deletes whole graphemes, retaining combining accents and joined emoji. Long
single-line values scroll horizontally to keep the caret visible.
Empty single-line fields centre the caret vertically beside the placeholder,
independently of the font's blank-line height.

| Input | Action |
| --- | --- |
| Left / Right | Move one character; collapse an existing selection |
| Ctrl+Left / Right | Move by word |
| Up / Down | Move vertically, retaining the preferred column; start/end in single-line fields |
| Ctrl+Up / Down | Move to the previous/next paragraph boundary |
| Home / End | Move to the current line's start/end |
| Ctrl+Home / End, Ctrl+Page Up / Down | Move to the value's start/end |
| Page Up / Down | Move by the visible height in multiline fields |
| Shift with any movement above | Extend or reverse selection from its original anchor |
| Ctrl+A | Select the entire value, including text outside the visible window |
| Backspace / Delete | Delete the selection or previous/next character |
| Ctrl+Backspace / Delete | Delete the selection or previous/next word |
| Ctrl+C / X / V | Copy, cut and paste |
| Ctrl+Insert, Shift+Delete, Shift+Insert | Copy, cut and paste aliases |
| Ctrl+Z, Alt+Backspace | Undo |
| Ctrl+Y, Ctrl+Shift+Z, Alt+Shift+Backspace | Redo |
| Ctrl+U / K | Delete to the start/end of the line |
| Insert | Toggle overwrite mode |
| Tab / Shift+Tab | Focus the next/previous visible, enabled text field |
| Enter | Submit single-line input; insert a newline in multiline input |
| Ctrl+Enter | Submit multiline input |
| Escape | Release text focus |
| Click / drag | Place the caret / select text |
| Shift-click | Extend the current selection |
| Double-click / double-click and drag | Select a word / extend selection by whole words |
| Triple-click | Select the entire value |

On macOS, Command supplies select-all, clipboard and undo/redo shortcuts;
Option+Left/Right and Option+Backspace/Delete operate by word. Command+Left/Right
and Command+Backspace/Delete operate to line boundaries. Command+Up/Down moves to
the value's start/end. Shift extends these movements as well. Windows AltGr input
passes through to character entry without triggering Ctrl shortcuts.

`caretByte` and `selectionAnchorByte` are UTF-8 byte offsets normalised to grapheme
boundaries. `ui/text_editing.h` exposes the editing model, while
`ui/text_input_interactions.h` supplies hit testing, pointer selection and vertical
movement. Custom hosts can dispatch `UiKeyInput` with their key map, clipboard
reader/writer and monotonic event time. GLFW supplies these automatically.

Set `multiline` for explicit newline navigation, `readOnly` to retain selection and
copying while preventing edits, and `onSubmitted` for submission. Search options
also expose `readOnly`, `onSubmitted` and `selectionColour`. The default selection
is translucent blue and does not intercept pointer events. `onChanged` fires only
for changes to the value. Undo/redo restores the caret and selection; adjacent
typing or deletion is grouped. Assigning `value` externally resets editing history
on the next visual/input synchronisation.

## Panels and transitions

`GlfwPanelWindowTemplateOptions` uses `UiPanelAppearance` by default: a squircle
contour, 20-unit window insets, a soft shadow, a 0.75-unit outer stroke and matching
traffic-light proportions. Content and outline colours come from the theme's
panel palette. Default panel text uses weight 450; centred titles use 19 units
at weight 650. Artwork remains caller-supplied.

`UiTheme::PanelPalette`, `panel_palette()`, `panel_card_style`,
`panel_sidebar_style` and `panel_content_style` expose generic palette names.
The existing `settingsPanel` JSON field and older accessors remain compatible.

`ui_panel_transitions` configures the shared opening/closing scale and opacity
motion. The panel host schedules transitions against the renderer's clock,
preserves an active opening transition across a rebuild and waits for the final
closing frame before releasing an automatically owned window.

For an owner-managed panel, set `deferCloseToOwner = true`. A native close request
is exposed through `should_close()`. After accepting it, call `request_close()`,
continue `tick()`, and release panel resources before `close()` once
`close_ready()` is true. Use `is_closing()` to distinguish animation from editing.
A destructor or explicit immediate teardown can release resources without waiting
for an animation.

## Dialog lifecycle

`ui/dialog_controls.h` builds confirmation and other dialog controls.
`UiModalLifecycle` in `ui/modal_lifecycle.h` owns the surrounding input/transition
mechanism:

1. Build the dimming surface and card, then call `show(scene, dim, card)`.
2. Call `begin_exit(scene)` after the user chooses an action.
3. Wait for `exit_ready(engine)` before applying that action and removing the card.
4. Call `restore_input(scene)` to restore the previous enabled and focus states.

The lifecycle blocks background input, releases background text focus and disables
repeated actions during dismissal. An optional predicate allows selected background
controls, such as a window-close action. `awaiting_presentation()` marks the interval
when frame submission should pause while the final transparent frame completes.
`reset()` discards handles before replacing their scene. Save/delete/close decisions
remain with the owner.

`Renderer2DScene::display_transition_finished(entity)` queries completion against
the last renderer-resolved clock. This avoids clearing a transparent transition
before its final frame has been presented.

## Materials and interaction

`UiGlassOptions` selects a material and its optical parameters.
`UiSurfaceMaterialPaint` and `ui_apply_surface_material` retain authored paints
while switching material or tint. `ui_update_liquid_controls` reconciles control
surfaces and thumb paint after material changes.

Liquid Glass uses a curved lens profile, softly varying optical rims and a shared
blur response from `graphics/liquid_glass_profile.h`. Zero blur produces clear
glass; positive blur diffuses the entire backdrop, including its refracted colour
channels. Tint remains independent of diffusion. Strongly tinted scene surfaces
retain their optical rims above their paint. Liquid surfaces do not add a separate
outline stroke.

`UiLiquidControlSurface::appearance` can select a light or dark control finish.
Automatic selection considers opacity as well as RGB, so faint white overlays on
dark surfaces retain the dark finish. Preserved-colour controls keep their own
paint sharp independently of the surrounding panel's diffusion setting.
`UiTheme::trafficLightClose`, `trafficLightMinimise` and `trafficLightMaximise`
provide window-control colours independently of warning, error and success colours.

Clear glass retains fine detail with local contrast treatment; frost blends towards
the diffuse backdrop. The face stays geometrically stable while the contour bends
light. See [Liquid Glass](liquid-glass.md) for the material contract and backend behaviour.

Thumbs distinguish quick mouse clicks from held or moving gestures and retain
their material through spring release. Standard glass buttons without an authored
stretch gesture use a small press spring. `ui_update_liquid_control_motion` is part
of GLFW's timed-control update; custom hosts can call it on their UI clock.
Slider and switch thumbs form an untinted, undiffused lens during manipulation.
`LiquidGlassComponent::preserveBackdropColour` preserves the underlying brightness
and colour by bypassing platter colour grading. Portable optical batches carry
this policy independently of paint sampling and retain their normal refraction
and rims. Their lift
and velocity-sensitive deformation share the animation clock, without delaying
input values. `ui_set_liquid_thumb_resting_style` updates a mutable swatch without
painting over its active lens. `StretchDynamicsOptions::dragResponse` opts a
surface into spring-driven dragging and preserves momentum when recaptured.
Switch thumbs lift to roughly 185% height, retaining their pill contour and
vertical centre through the release spring.
`ui_expanded_capsule_size` expands the rounded ends without consuming the straight
centre section. Sliders and switches share this geometry for stationary holds;
directional movement adds its deformation independently.

Editors with independently authored preview materials can pass
`updateAuthoredSurfaces = false` to `ui_update_liquid_controls`. This updates the
panel's controls without replacing the previews' optical parameters.

`StretchDynamicsOptions::requireLiquidGlass` defaults to true. Optical stretching
requires Liquid Glass on the object or an enclosing glass surface. Solid and
system-glass controls keep their authored size. Removing Liquid Glass restores
scale, blur and slider/switch geometry immediately; functional drag/release
actions continue. An explicitly material-independent elastic gesture can set
`requireLiquidGlass = false`.

## Colour picking and retained themes

`UiColourPalettePanel` in `ui/colour_palette_panel.h` implements the wheel,
channel sliders, named colours, image palettes and pencils. Create information
accepts a concrete theme, locale/dictionary directories, fonts/panel callbacks,
tab/control icons and optional recolourable pencil artwork. Empty artwork uses
the geometric presentation. Call `tick()` from your UI frame loop and use
`set_appearance(theme, locale)` when the caller's appearance changes.

`request_open` accepts the current RGB colour, target label, preview callback and
commit callback. Previews coalesce at `previewIntervalSeconds`, suppress unchanged
colours and flush the final edit. `livePreview = false` enables commit-only editing.
The picker retains controls, caches wheel/spectrum images, bounds imported
palettes to four images and supports cancellable decoding. The closed picker
performs no rendering.

`graphics/colour_palette.h` exposes the CPU conversions, hex parsing, sampling and
image library separately. `graphics/svg_colour_template.h` provides
`vibrance::SvgColourTemplate`: a bounded SVG source with a caller-selected
six-digit colour marker. Every matching region is recoloured, and source/marker/
colour fingerprints distinguish cached variants. Applications retain the source
artwork and its asset path.

`ui/theme_bindings.h` provides `ui_recolour_scene`. Callers supply previous/next
colour arrays, role-priority arrays and an optional entity filter. Retained
bindings update shapes, button states, text, masked media and saved glass paint,
preserve authored alpha and invalidate affected material caches. A distinct
binding component type isolates an independently edited preview. The engine does
not define application-specific theme roles or document storage.

## Fonts and localisation

`renderer/font_file.h` validates font files and exposes their original family,
style and display names, including faces in a collection and Unicode names.
`font_files::font_display_name` selects the first face's embedded name with a
filename fallback. Reading/validation is bounded to 64 MB; Windows and macOS also
check native support. Inspection does not rename or install a font.

Applications assign global and locale-specific fonts and configure primary,
fallback and emoji paths. Glyph fallback covers mixed-script text. Choose whether
a locale-specific face becomes primary through `useLocalePrimaryFont`.
Locale dictionaries and reading direction belong to the localisation service.

Font size, face, weight, optical metrics, backing scale and rasterisation all
affect apparent text size. Use consistent logical sizes and matching font files
when comparing platforms. The renderer's metrics and positioning tests cover
cross-font size consistency; operating-system text rasterisation can still differ.

For fixed-width buttons, `truncateLabel` reserves padding and icons before fitting
text; `fitContentWidth` expands to the measured label instead. Search/numeric fields
can set `textAlignment` to start, centre or end. Explicit alignment accounts for
reading direction and keeps clipping/caret placement consistent.

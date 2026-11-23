# Liquid Glass

The engine provides shared glass materials and control responses. Applications
own their content, material choices and opening/closing behaviour.

## Material contract

`UiGlassOptions` selects the material and its backend. Liquid surfaces expose
independent refraction, blur, rim, dispersion and RGBA tint controls. Backdrop
sampling is bounded by the surface geometry and the reserved source halo.

At zero blur, glass samples the sharp backdrop at its refracted coordinates;
refraction does not enable a Gaussian filter. Positive blur diffuses the backdrop.
Opaque caps bypass optical sampling. Foreground labels and icons retain their
normal paint order above the material.

`LiquidGlassComponent::preserveBackdropColour` preserves source RGB independently
of refraction, blur and rim. The native Windows equivalent is
`SystemBackdropRegion::liquidPreserveBackdropColour`. Colour-preserving and
painted surfaces retain their separate material policies.

The portable renderer and Windows optical shader share the refraction profile.
Generated Metal shaders are checked against the GLSL source. Source, replay and
scratch bounds include the complete optical sampling footprint; viewport and
shape exclusions constrain output without changing the optical contour.

## Control response

Shared surfaces own their lens. Slider and switch thumbs use a clear material
during held manipulation and retain it while their release spring settles.
Existing input values, commit callbacks and authored gestures retain ownership.
Settled controls stop invalidating retained frames. Solid controls retain their
normal geometry and paint.

## Linux desktop backdrop

Frosted Glass uses the compositor's standard blur request. Liquid Glass uses
the Vibrance Liquid Glass effect on KWin 6.8 for real desktop refraction on
Wayland and XWayland. The effect takes the refraction, blur and colour
separation values from Vibrance and uses the shared Vibrance refraction
profile. KWin samples the desktop and updates the lens with the island's
animated frame. Vulkan draws the cap gradient, tint and controls. A blur
setting of zero preserves sharp detail through the lens.
Without the effect, a compositor that offers standard blur can provide a
blurred backdrop for translucent surfaces.

With the KWin 6.8 development packages installed, build and update the effect
from the engine directory.

```sh
./unixBuild.sh Release --install-kwin-glass
```

The script downloads and verifies the pinned [KWin Glass release](https://github.com/4v3ngR/kwin-effects-glass/releases/tag/20261016-6.8.0-support),
applies the Vibrance patch and stages the module in the engine SDK. It uses
`sudo` to copy a changed module into Qt's system plug-in directory and
keeps a numbered backup when replacing it. Use `--kwin-glass` to stage the
module in the SDK for packaging. For an offline build, point
`VIBRANCE_KWIN_GLASS_ARCHIVE` at the cached release archive. If the KDE
development packages are in a separate prefix, set
`VIBRANCE_KWIN_GLASS_CMAKE_PREFIX_PATH` to that prefix.

Enable **Vibrance Liquid Glass** in Plasma's Desktop Effects settings on
first use. After replacing a loaded module, sign out and back in so KWin
loads the update. The effect runs alongside stock Blur using requests for
Vibrance surfaces only.
Use the [GLFW source option](building.md#linux) for per-pixel transparency
on XWayland.

## Runtime and validation

Windows reports optional native controls through its runtime capability word.
Unsupported controls are withheld from pinned older runtimes while a verified
replacement is staged. Signing out and back in activates the replacement.

Automated checks cover optical bounds, clear/blurred/tinted surfaces, colour
preservation, opaque caps, exclusions, shader parity and control interactions.
Vulkan 1.2 compatibility is checked separately. API support does not establish
performance or visual correctness on every device; see [Platforms](platforms.md).

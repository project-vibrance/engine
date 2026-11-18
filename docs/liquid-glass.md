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

## Runtime and validation

Windows reports optional native controls through its runtime capability word.
Unsupported controls are withheld from pinned older runtimes while a verified
replacement is staged. Signing out and back in activates the replacement.

Automated checks cover optical bounds, clear/blurred/tinted surfaces, colour
preservation, opaque caps, exclusions, shader parity and control interactions.
Vulkan 1.2 compatibility is checked separately. API support does not establish
performance or visual correctness on every device; see [Platforms](platforms.md).

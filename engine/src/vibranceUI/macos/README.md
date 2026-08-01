# macOS material backend slot

`LiquidGlassBackend::eMacOSNative` is reserved for an Objective-C++ adapter to
Apple's native Liquid Glass APIs. The public material routing already selects
that backend for Apple targets, but `liquid_glass_backend_supported()` remains
false and no `.mm` implementation is present yet.

The future adapter belongs in this directory and must consume the existing
`LiquidGlassComponent` request without changing application call sites.

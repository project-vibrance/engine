#include <vibranceUI/graphics/backdrop.h>

const char* glass_material_name(GlassMaterial material)
{
    switch (material)
    {
    case GlassMaterial::eSystemGlass:
        return "system-glass";
    case GlassMaterial::eLiquid:
        return "liquid";
    case GlassMaterial::eOff:
    default:
        return "off";
    }
}

const char* system_backdrop_provider_name(SystemBackdropProvider provider)
{
    switch (provider)
    {
    case SystemBackdropProvider::eWindowsAcrylic:
        return "windows-acrylic";
    case SystemBackdropProvider::eWindowsMica:
        return "windows-mica";
    case SystemBackdropProvider::eEngine:
    default:
        return "engine-composition";
    }
}

bool system_backdrop_platform_supported()
{
#if defined(_WIN32)
    return true;
#else
    return false;
#endif
}

LiquidGlassBackend default_liquid_glass_backend()
{
#if defined(__APPLE__)
    // The selector is stable now so macOS can gain its native implementation
    // later without changing application material declarations.
    return LiquidGlassBackend::eMacOSNative;
#else
    return LiquidGlassBackend::eEngineRenderer;
#endif
}

bool liquid_glass_backend_supported(LiquidGlassBackend backend)
{
    // Both slots are architectural reservations at present. In particular,
    // macOS native Liquid Glass is intentionally not implemented yet.
    (void)backend;
    return false;
}

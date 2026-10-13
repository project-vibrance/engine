#include <vibranceUI/graphics/backdrop.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../platform/win32/liquid_glass_dwm_protocol.h"
#endif

#if defined(__APPLE__)
bool vibrance_macos_native_liquid_glass_available();
#endif

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
    return LiquidGlassBackend::eMacOSNative;
#elif defined(_WIN32)
    return LiquidGlassBackend::eWindowsCompositionShader;
#else
    return LiquidGlassBackend::eEngineRenderer;
#endif
}

bool liquid_glass_backend_supported(LiquidGlassBackend backend)
{
#if defined(_WIN32)
    if (backend == LiquidGlassBackend::eEngineRenderer)
    {
        return true;
    }
    if (backend != LiquidGlassBackend::eWindowsCompositionShader)
    {
        return false;
    }
    const HANDLE event = OpenEventW(SYNCHRONIZE, FALSE,
        vibrance::win32::liquidDwmReadyName);
    if (!event)
    {
        return false;
    }
    const bool ready = WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
    CloseHandle(event);
    return ready;
#elif defined(__APPLE__)
    return backend == LiquidGlassBackend::eMacOSNative &&
        vibrance_macos_native_liquid_glass_available();
#else
    (void)backend;
    return false;
#endif
}

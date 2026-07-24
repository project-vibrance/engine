#include <vibranceUI/graphics/backdrop.h>

const char* system_backdrop_material_name(SystemBackdropMaterial material)
{
    switch (material)
    {
    case SystemBackdropMaterial::eBlur:
        return "blur";
    case SystemBackdropMaterial::eFrosted:
        return "frosted";
    case SystemBackdropMaterial::eLiquid:
        return "liquid";
    case SystemBackdropMaterial::eOff:
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


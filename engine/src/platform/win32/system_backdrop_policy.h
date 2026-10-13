#pragma once

#include <cstdint>

namespace vibrance::win32
{
    enum class SystemBackdropImplementation
    {
        eCompositionAcrylic,
        eDwmAcrylic,
        eDwmMica
    };

    // DWMWA_SYSTEMBACKDROP_TYPE is supported from Windows 11 build 22621.
    // Older systems use the shaped Acrylic Composition graph, including when
    // a caller requested Mica. Build 0 also takes the compatible fallback.
    inline constexpr SystemBackdropImplementation system_backdrop_implementation(
        std::uint32_t provider, std::uint32_t windowsBuild)
    {
        if (windowsBuild >= 22621u)
        {
            if (provider == 1u) return SystemBackdropImplementation::eDwmAcrylic;
            if (provider == 2u) return SystemBackdropImplementation::eDwmMica;
        }
        return SystemBackdropImplementation::eCompositionAcrylic;
    }

    inline constexpr bool system_backdrop_uses_host_brush(std::uint32_t windowsBuild)
    {
        return windowsBuild >= 22000u;
    }
}

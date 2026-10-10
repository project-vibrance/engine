#include "../src/platform/win32/system_backdrop_policy.h"

#include <iostream>

int main()
{
    using namespace vibrance::win32;
    int failures = 0;
    for (const auto build : { 0u, 17763u, 19041u, 19044u, 19045u, 20348u, 22000u })
    for (const auto provider : { 0u, 1u, 2u })
        if (system_backdrop_implementation(provider, build) != SystemBackdropImplementation::eCompositionAcrylic)
        {
            std::cerr << "Older Windows builds must resolve every system material request to Composition Acrylic\n";
            ++failures;
        }
    for (const auto build : { 22621u, 22631u, 26100u })
    {
        if (system_backdrop_implementation(1u, build) != SystemBackdropImplementation::eDwmAcrylic ||
            system_backdrop_implementation(2u, build) != SystemBackdropImplementation::eDwmMica ||
            system_backdrop_implementation(0u, build) != SystemBackdropImplementation::eCompositionAcrylic)
        {
            std::cerr << "Supported Windows 11 builds must retain the requested material provider\n";
            ++failures;
        }
    }
    if (system_backdrop_uses_host_brush(19045u) || system_backdrop_uses_host_brush(0u) ||
        !system_backdrop_uses_host_brush(22000u))
    {
        std::cerr << "Windows 10 must use its live backdrop brush instead of the Windows 11 host brush\n";
        ++failures;
    }
    return failures ? 1 : 0;
}

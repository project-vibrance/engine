#include <vibranceUI/notifications/provider.h>
#include <vibranceUI/platform/win32_interop_abi.h>

#include <iostream>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "portable notification provider test failed: "
                << message << '\n';
        }
        return condition;
    }
}

int main()
{
    bool passed = true;
    SystemNotificationProvider provider;

    passed &= expect(
        !SystemNotificationProvider::platform_supported(),
        "the forced-portable implementation should not advertise Win32");
    passed &= expect(
        !provider.bridge_loaded(),
        "the portable implementation should not load a Win32 DLL");
    passed &= expect(
        !provider.request_access(),
        "portable access requests should fail closed");
    passed &= expect(
        !provider.refresh(),
        "portable refresh should remain a safe no-op");
    passed &= expect(
        !provider.dismiss(1u),
        "portable dismissal should fail closed");
    passed &= expect(
        !provider.activate_source("example.application"),
        "portable activation should fail closed");
    passed &= expect(
        provider.resolve_application_icon("example.application").path.empty(),
        "portable icon resolution should return no path");
    passed &= expect(
        provider.snapshot().access == SystemNotificationAccess::eUnsupported,
        "the portable snapshot should report unsupported access");
    passed &= expect(
        !provider.snapshot().diagnostic.empty(),
        "the portable snapshot should explain unavailability");

    return passed ? 0 : 1;
}

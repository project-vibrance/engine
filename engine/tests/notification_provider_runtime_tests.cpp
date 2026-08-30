#include <vibranceUI/notifications/provider.h>

#include <iostream>

int main()
{
    SystemNotificationProvider provider;

#if defined(_WIN32)
    if (!SystemNotificationProvider::platform_supported())
    {
        std::cerr << "Win32 notification provider was not enabled\n";
        return 1;
    }
    if (!provider.bridge_loaded())
    {
        std::cerr << "Win32 interop companion did not load: "
            << provider.snapshot().diagnostic << '\n';
        return 1;
    }
#else
    if (SystemNotificationProvider::platform_supported() ||
        provider.bridge_loaded())
    {
        std::cerr << "portable notification provider advertised Win32\n";
        return 1;
    }
#endif

    return 0;
}

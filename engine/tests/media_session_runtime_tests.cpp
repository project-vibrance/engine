#include <vibranceUI/media/session.h>

#include <iostream>

int main()
{
    GlobalMediaSession session;

#if defined(_WIN32)
    if (!GlobalMediaSession::platform_supported())
    {
        std::cerr << "Win32 media-session provider was not enabled\n";
        return 1;
    }
    if (!session.bridge_loaded())
    {
        std::cerr << "Win32 interop companion did not load: "
            << session.snapshot().diagnostic << '\n';
        return 1;
    }
#else
    if (GlobalMediaSession::platform_supported() || session.bridge_loaded())
    {
        std::cerr << "portable media-session provider advertised Win32\n";
        return 1;
    }
#endif

    return 0;
}

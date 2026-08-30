#include <vibranceUI/media/session.h>
#include <vibranceUI/platform/win32_interop_abi.h>

#include <iostream>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "portable media session test failed: "
                << message << '\n';
        }
        return condition;
    }
}

int main()
{
    bool passed = true;
    GlobalMediaSession session;

    passed &= expect(
        !GlobalMediaSession::platform_supported(),
        "the forced-portable implementation should not advertise Win32");
    passed &= expect(
        !session.bridge_loaded(),
        "the portable implementation should not load a Win32 DLL");
    passed &= expect(
        !session.refresh(),
        "portable refresh should remain a safe no-op");
    passed &= expect(
        !session.send(MediaSessionCommand::eTogglePlayPause),
        "portable commands should fail closed");
    passed &= expect(
        !session.seek(1000),
        "portable seeking should fail closed");
    passed &= expect(
        !session.snapshot().diagnostic.empty(),
        "the portable snapshot should explain unavailability");

    return passed ? 0 : 1;
}

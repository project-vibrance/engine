#include <vibranceUI/platform/native_tray.h>
#if defined(VIBRANCE_FORCE_PORTABLE_NATIVE_TRAY) || (!defined(_WIN32) && !defined(__APPLE__))


#include <utility>

struct NativeTray::Impl
{
    explicit Impl(NativeTrayOptions options) : options(std::move(options)) {}
    NativeTrayOptions options {};
};

NativeTray::NativeTray(NativeTrayOptions options) :
    impl(std::make_unique<Impl>(std::move(options)))
{
}

NativeTray::~NativeTray() = default;

bool NativeTray::initialise()
{
    return false;
}

void NativeTray::shutdown()
{
}

bool NativeTray::available() const
{
    return false;
}

NativeTrayEvent NativeTray::take_event()
{
    return NativeTrayEvent { NativeTrayEventKind::eNone };
}

NativeTrayAnchor NativeTray::menu_anchor() const
{
    return {};
}

void NativeTray::set_custom_menu_enabled(bool enabled)
{
    if (impl)
    {
        impl->options.preferCustomMenu = enabled;
    }
}

bool NativeTray::custom_menu_enabled() const
{
    return impl && impl->options.preferCustomMenu;
}

void NativeTray::show_native_menu()
{
}

#endif

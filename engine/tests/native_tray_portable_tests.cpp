#include <vibranceUI/platform/native_tray.h>
#include "../src/platform/composition_presenter.h"

int main()
{
    NativeTray tray({ .tooltip = "Example", .menu = { { 42, "Open" } } });
    if (tray.initialise() || tray.available() || tray.menu_anchor().valid ||
        tray.take_event().kind != NativeTrayEventKind::eNone) return 1;
    tray.set_custom_menu_enabled(true);
    if (!tray.custom_menu_enabled()) return 1;
    tray.show_native_menu();
    tray.shutdown();
    tray.shutdown();
    if (tray.initialise()) return 1;
    CompositionPresenter presenter;
    if (presenter.initialise(nullptr, {}, {}, 1, 1, 1, 0, false, false) ||
        presenter.available() || presenter.gpu_interop() || presenter.buffer_count() ||
        presenter.image(0) || presenter.memory(0) || presenter.acquire(0) ||
        presenter.set_regions({}) || presenter.present(0) != CompositionPresenter::PresentResult::eFailed)
        return 1;
    presenter.shutdown({});
    return 0;
}

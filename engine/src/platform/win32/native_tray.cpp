#include <vibranceUI/platform/native_tray.h>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <string>
#include <utility>

namespace
{
    constexpr UINT kTrayCallbackMessage = WM_APP + 42u;
    constexpr UINT kTrayIconId = 1u;
    constexpr wchar_t kTrayWindowClass[] =
        L"VibranceNativeTrayWindow";
    constexpr wchar_t kTrayPopupOwnerWindowClass[] =
        L"VibranceNativeTrayPopupOwnerWindow";

    std::wstring widen(const std::string& value)
    {
        if (value.empty())
        {
            return {};
        }
        const int size = MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            nullptr,
            0);
        if (size <= 0)
        {
            return std::wstring(value.begin(), value.end());
        }
        std::wstring result(static_cast<std::size_t>(size), L'\0');
        MultiByteToWideChar(
            CP_UTF8,
            MB_ERR_INVALID_CHARS,
            value.data(),
            static_cast<int>(value.size()),
            result.data(),
            size);
        return result;
    }

    HICON create_tray_icon(const std::filesystem::path& iconPath)
    {
        if (!iconPath.empty())
        {
            if (HICON loaded = static_cast<HICON>(LoadImageW(nullptr,
                    iconPath.c_str(), IMAGE_ICON, 0, 0,
                    LR_DEFAULTSIZE | LR_LOADFROMFILE))) return loaded;
        }
        if (HICON embedded = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1)))
            return CopyIcon(embedded);
        return CopyIcon(LoadIconW(nullptr, MAKEINTRESOURCEW(32512)));
    }

    bool point_is_on_virtual_screen(POINT point)
    {
        const LONG left = GetSystemMetrics(SM_XVIRTUALSCREEN);
        const LONG top = GetSystemMetrics(SM_YVIRTUALSCREEN);
        const LONG right = left + GetSystemMetrics(SM_CXVIRTUALSCREEN);
        const LONG bottom = top + GetSystemMetrics(SM_CYVIRTUALSCREEN);
        return point.x >= left && point.x < right &&
            point.y >= top && point.y < bottom;
    }
}

struct NativeTray::Impl
{
    explicit Impl(NativeTrayOptions options) :
        options(std::move(options))
    {
    }

    static LRESULT CALLBACK window_proc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam)
    {
        Impl* self = reinterpret_cast<Impl*>(
            GetWindowLongPtrW(window, GWLP_USERDATA));
        if (message == WM_NCCREATE)
        {
            const auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
            self = static_cast<Impl*>(create->lpCreateParams);
            SetWindowLongPtrW(
                window,
                GWLP_USERDATA,
                reinterpret_cast<LONG_PTR>(self));
        }
        if (!self)
        {
            return DefWindowProcW(window, message, wParam, lParam);
        }
        if (message == self->taskbarCreatedMessage)
        {
            // Explorer discarded the previous registration while restarting.
            // Reflect the real state if re-registering the icon fails.
            self->iconAdded = false;
            if (self->options.registerWithShell) self->add_icon();
            return 0;
        }
        if (message == kTrayCallbackMessage)
        {
            const UINT event = LOWORD(lParam);
            // Explorer versions differ on whether a version-4 icon receives
            // a legacy button-up in addition to NIN_SELECT. Accept both event
            // families, deduplicate them below, and gate native action
            // consumption until the physical mouse release has completed.
            const bool activated =
                event == WM_LBUTTONUP || event == WM_RBUTTONUP ||
                event == WM_CONTEXTMENU || event == NIN_SELECT ||
                event == NIN_KEYSELECT;
            if (activated)
            {
                const ULONGLONG now = GetTickCount64();
                if (self->options.preferCustomMenu ?
                    now < self->ignoreCustomActivationUntil :
                    (self->nativeMenuOpen || self->nativeMenuRequestPending ||
                        now < self->ignoreNativeActivationUntil))
                {
                    return 0;
                }
                // Third-party trays may send legacy payloads even after
                // NIM_SETVERSION succeeds. A semantic event alone does not
                // establish that wParam contains packed screen coordinates.
                const bool semanticActivation =
                    event == WM_CONTEXTMENU || event == NIN_SELECT ||
                    event == NIN_KEYSELECT;
                const bool packedActivation = semanticActivation &&
                    HIWORD(lParam) == kTrayIconId;
                POINT point {
                    static_cast<SHORT>(LOWORD(wParam)),
                    static_cast<SHORT>(HIWORD(wParam))
                };
                const bool keyboardActivation = event == NIN_KEYSELECT ||
                    (packedActivation && event == WM_CONTEXTMENU &&
                        point.x == -1 && point.y == -1);
                bool callbackAnchorValid = packedActivation &&
                    !(point.x == -1 && point.y == -1) &&
                    point_is_on_virtual_screen(point);
                if (!keyboardActivation)
                {
                    // Capture the cursor now, before the deferred popup opens.
                    // Explorer's icon rectangle can belong to a different tray
                    // from the Finder icon that actually received the click.
                    POINT cursor {};
                    if (GetCursorPos(&cursor) && point_is_on_virtual_screen(cursor))
                    {
                        point = cursor;
                        callbackAnchorValid = true;
                    }
                }
                self->nativeMenuAnchorValid = callbackAnchorValid;
                if (callbackAnchorValid)
                {
                    self->nativeMenuAnchor = point;
                    self->lastAnchor = { point.x, point.y, 1, 1, true };
                }
                else
                {
                    self->lastAnchor = {};
                }
                if (self->options.preferCustomMenu)
                {
                    self->ignoreCustomActivationUntil = now + 150u;
                    if (!callbackAnchorValid &&
                        !self->update_anchor(false))
                    {
                        self->update_anchor(true);
                    }
                    self->actions.push_back(
                        NativeTrayEvent { NativeTrayEventKind::eToggleCustomMenu });
                }
                else
                {
                    // Opening TrackPopupMenu from inside Explorer's tray
                    // callback leaves its modal loop inside the shell/focus
                    // hand-off. Defer it to the normal application loop, just
                    // like the custom popup, so an existing panel cannot
                    // immediately cancel the native menu while regaining focus.
                    self->nativeMenuRequestPending = true;
                    self->nativeMenuReadyAt = now + 75u;
                    self->actions.push_back(
                        NativeTrayEvent { NativeTrayEventKind::eOpenNativeMenu });
                }
                return 0;
            }
        }
        if (message == WM_COMMAND)
        {
            self->queue_command(LOWORD(wParam));
            return 0;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }

    bool initialise()
    {
        if (window)
        {
            return true;
        }
        if (options.menu.size() > 65535u) return false;

        WNDCLASSEXW windowClass = {};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.lpfnWndProc = window_proc;
        windowClass.hInstance = GetModuleHandleW(nullptr);
        windowClass.lpszClassName = kTrayWindowClass;
        if (!RegisterClassExW(&windowClass) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        WNDCLASSEXW popupOwnerClass = {};
        popupOwnerClass.cbSize = sizeof(popupOwnerClass);
        popupOwnerClass.lpfnWndProc = DefWindowProcW;
        popupOwnerClass.hInstance = GetModuleHandleW(nullptr);
        popupOwnerClass.lpszClassName = kTrayPopupOwnerWindowClass;
        if (!RegisterClassExW(&popupOwnerClass) &&
            GetLastError() != ERROR_CLASS_ALREADY_EXISTS)
        {
            return false;
        }

        window = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
            kTrayWindowClass,
            L"Native status item",
            WS_POPUP,
            0,
            0,
            0,
            0,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            this);
        if (!window)
        {
            return false;
        }

        // A native popup needs a visible, activatable foreground owner. Keep
        // that concern in a dedicated transparent tool window so the tray
        // never focuses, enumerates, reorders, or otherwise mutates an application
        // window.
        popupOwnerWindow = CreateWindowExW(
            WS_EX_TOOLWINDOW | WS_EX_LAYERED,
            kTrayPopupOwnerWindowClass,
            L"Native status item popup owner",
            WS_POPUP,
            -32000,
            -32000,
            1,
            1,
            nullptr,
            nullptr,
            GetModuleHandleW(nullptr),
            nullptr);
        if (!popupOwnerWindow)
        {
            shutdown();
            return false;
        }
        SetLayeredWindowAttributes(
            popupOwnerWindow,
            0u,
            0u,
            LWA_ALPHA);

        taskbarCreatedMessage = RegisterWindowMessageW(
            L"TaskbarCreated");
        icon = create_tray_icon(options.iconPath);
        if (options.registerWithShell && !add_icon())
        {
            shutdown();
            return false;
        }
        return true;
    }

    bool add_icon()
    {
        if (!window || !icon)
        {
            return false;
        }

        NOTIFYICONDATAW data = {};
        data.cbSize = sizeof(data);
        data.hWnd = window;
        data.uID = kTrayIconId;
        data.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP;
#ifdef NIF_SHOWTIP
        data.uFlags |= NIF_SHOWTIP;
#endif
        data.uCallbackMessage = kTrayCallbackMessage;
        data.hIcon = icon;
        const std::wstring tooltip = widen(options.tooltip);
        const std::size_t count = std::min<std::size_t>(
            tooltip.size(),
            std::size(data.szTip) - 1u);
        std::copy_n(tooltip.data(), count, data.szTip);
        data.szTip[count] = L'\0';
        if (!Shell_NotifyIconW(NIM_ADD, &data))
        {
            return false;
        }
        data.uVersion = NOTIFYICON_VERSION_4;
        version4Callbacks =
            Shell_NotifyIconW(NIM_SETVERSION, &data) != FALSE;
        iconAdded = true;
        return true;
    }

    void shutdown()
    {
        if (window && iconAdded)
        {
            NOTIFYICONDATAW data = {};
            data.cbSize = sizeof(data);
            data.hWnd = window;
            data.uID = kTrayIconId;
            Shell_NotifyIconW(NIM_DELETE, &data);
            iconAdded = false;
        }
        version4Callbacks = false;
        nativeMenuOpen = false;
        nativeMenuRequestPending = false;
        nativeMenuReadyAt = 0u;
        ignoreNativeActivationUntil = 0u;
        nativeMenuAnchor = {};
        nativeMenuAnchorValid = false;
        ignoreCustomActivationUntil = 0u;
        lastAnchor = {};
        if (popupOwnerWindow)
        {
            DestroyWindow(popupOwnerWindow);
            popupOwnerWindow = nullptr;
        }
        if (window)
        {
            DestroyWindow(window);
            window = nullptr;
        }
        if (icon)
        {
            DestroyIcon(icon);
            icon = nullptr;
        }
        actions.clear();
    }

    void show_menu()
    {
        nativeMenuRequestPending = false;
        nativeMenuReadyAt = 0u;
        if (nativeMenuOpen || !popupOwnerWindow)
        {
            return;
        }

        POINT point = nativeMenuAnchor;
        bool pointValid = nativeMenuAnchorValid &&
            point_is_on_virtual_screen(point);
        nativeMenuAnchorValid = false;
        if (!pointValid && update_anchor(false))
        {
            point = {
                lastAnchor.x + lastAnchor.width / 2,
                lastAnchor.y + lastAnchor.height / 2
            };
            pointValid = point_is_on_virtual_screen(point);
        }
        if (!pointValid)
        {
            pointValid = GetCursorPos(&point) != FALSE &&
                point_is_on_virtual_screen(point);
        }
        if (!pointValid)
        {
            return;
        }

        HMENU menu = CreatePopupMenu();
        if (!menu)
        {
            return;
        }
        for (std::size_t index = 0; index < options.menu.size(); ++index)
        {
            const auto& item = options.menu[index];
            const std::wstring label = widen(item.label);
            AppendMenuW(menu, item.separator ? MF_SEPARATOR :
                (MF_STRING | (item.enabled ? MF_ENABLED : MF_GRAYED)),
                static_cast<UINT_PTR>(index + 1u), item.separator ? nullptr : label.c_str());
        }

        nativeMenuOpen = true;

        // TrackPopupMenu requires a foreground owner. Briefly show the fully
        // transparent, off-screen owner and activate only that window. It is
        // neither topmost nor part of the taskbar/Alt-Tab lists, and no GLFW
        // surface participates in this native popup lifecycle.
        SetWindowPos(
            popupOwnerWindow,
            HWND_TOP,
            -32000,
            -32000,
            1,
            1,
            SWP_NOACTIVATE | SWP_SHOWWINDOW | SWP_NOSENDCHANGING);
        SetForegroundWindow(popupOwnerWindow);
        const UINT command = TrackPopupMenuEx(
            menu,
            TPM_RIGHTBUTTON | TPM_RETURNCMD | TPM_NONOTIFY | TPM_WORKAREA,
            point.x,
            point.y,
            popupOwnerWindow,
            nullptr);
        nativeMenuOpen = false;
        // Some shells deliver a semantic selection notification after the
        // button-up callback. Keep that tail from immediately reopening the
        // menu that has just been dismissed or selected.
        ignoreNativeActivationUntil =
            GetTickCount64() + std::max<UINT>(GetDoubleClickTime() / 2u, 150u);
        DestroyMenu(menu);
        PostMessageW(popupOwnerWindow, WM_NULL, 0u, 0);
        ShowWindow(popupOwnerWindow, SW_HIDE);

        if (iconAdded)
        {
            NOTIFYICONDATAW data = {};
            data.cbSize = sizeof(data);
            data.hWnd = window;
            data.uID = kTrayIconId;
            Shell_NotifyIconW(NIM_SETFOCUS, &data);
        }

        queue_command(command);
    }

    bool update_anchor(bool allowCursorFallback = true)
    {
        NOTIFYICONIDENTIFIER identifier = {};
        identifier.cbSize = sizeof(identifier);
        identifier.hWnd = window;
        identifier.uID = kTrayIconId;
        RECT rect = {};
        if (SUCCEEDED(Shell_NotifyIconGetRect(&identifier, &rect)))
        {
            lastAnchor.x = rect.left;
            lastAnchor.y = rect.top;
            lastAnchor.width = std::max(rect.right - rect.left, 1L);
            lastAnchor.height = std::max(rect.bottom - rect.top, 1L);
            lastAnchor.valid = true;
            return true;
        }

        if (!allowCursorFallback)
        {
            return false;
        }

        POINT point = {};
        if (GetCursorPos(&point))
        {
            lastAnchor = {
                point.x,
                point.y,
                1,
                1,
                true
            };
            return true;
        }
        lastAnchor = {};
        return false;
    }

    void queue_command(UINT command)
    {
        if (command == 0 || command > options.menu.size()) return;
        const auto& item = options.menu[command - 1u];
        if (item.enabled && !item.separator)
            actions.push_back({ NativeTrayEventKind::eCommand, item.command });
    }

    NativeTrayOptions options {};
    HWND window = nullptr;
    HWND popupOwnerWindow = nullptr;
    HICON icon = nullptr;
    UINT taskbarCreatedMessage = 0u;
    bool iconAdded = false;
    bool version4Callbacks = false;
    bool nativeMenuOpen = false;
    bool nativeMenuRequestPending = false;
    ULONGLONG nativeMenuReadyAt = 0u;
    ULONGLONG ignoreNativeActivationUntil = 0u;
    POINT nativeMenuAnchor {};
    bool nativeMenuAnchorValid = false;
    std::deque<NativeTrayEvent> actions {};
    ULONGLONG ignoreCustomActivationUntil = 0u;
    NativeTrayAnchor lastAnchor {};
};

NativeTray::NativeTray(NativeTrayOptions options) :
    impl(std::make_unique<Impl>(std::move(options)))
{
}

NativeTray::~NativeTray()
{
    shutdown();
}

bool NativeTray::initialise()
{
    return impl && impl->initialise();
}

void NativeTray::shutdown()
{
    if (impl)
    {
        impl->shutdown();
    }
}

bool NativeTray::available() const
{
    return impl && impl->window && impl->iconAdded;
}

NativeTrayEvent NativeTray::take_event()
{
    if (!impl || impl->actions.empty())
    {
        return NativeTrayEvent { NativeTrayEventKind::eNone };
    }
    const NativeTrayEvent action = impl->actions.front();
    if (action.kind == NativeTrayEventKind::eOpenNativeMenu &&
        (GetTickCount64() < impl->nativeMenuReadyAt ||
            (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_MBUTTON) & 0x8000) != 0))
    {
        // Leave the request queued. The application loop will ask again on
        // its next tick, after the shell's activating input has drained.
        return NativeTrayEvent { NativeTrayEventKind::eNone };
    }
    impl->actions.pop_front();
    return action;
}

NativeTrayAnchor NativeTray::menu_anchor() const
{
    if (!impl)
    {
        return {};
    }
    // An activation belongs to the tray that received it, which need not be
    // Explorer's tray. Only query Explorer before the first activation.
    if (!impl->lastAnchor.valid)
    {
        impl->update_anchor(false);
    }
    return impl->lastAnchor;
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
    if (impl)
    {
        impl->show_menu();
    }
}

#endif

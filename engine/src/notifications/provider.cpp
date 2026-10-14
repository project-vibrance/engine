#include <vibranceUI/notifications/provider.h>

#include <algorithm>
#include <bit>
#include <utility>

#if defined(_WIN32) && !defined(VIBRANCE_FORCE_PORTABLE_NOTIFICATION_PROVIDER)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vibranceUI/platform/win32_interop_abi.h>

#include <array>
#endif


#if !defined(VIBRANCE_FORCE_PORTABLE_NOTIFICATION_PROVIDER)
#include <vibranceUI/core/logger.h>
#endif
namespace
{
void provider_diagnostic(bool failure, const std::string& message)
{
#if !defined(VIBRANCE_FORCE_PORTABLE_NOTIFICATION_PROVIDER)
    Logger::fetch_logger()->log(failure ? LogLevel::eWarning : LogLevel::eInfo,
        "SystemNotificationProvider: " + message);
#else
    (void)failure;
    (void)message;
#endif
}
}

struct SystemNotificationProvider::Impl
{
    SystemNotificationSnapshot current;

#if defined(_WIN32) && !defined(VIBRANCE_FORCE_PORTABLE_NOTIFICATION_PROVIDER)
    HMODULE module = nullptr;
    VibranceWin32InteropHandle handle = nullptr;
    VibranceWin32InteropDestroyFn destroy = nullptr;
    VibranceWin32InteropRequestNotificationAccessFn requestAccess = nullptr;
    VibranceWin32InteropGetNotificationsFn getNotifications = nullptr;
    VibranceWin32InteropDismissNotificationFn dismissNotification = nullptr;
    VibranceWin32InteropActivateNotificationSourceFn activateSource = nullptr;
    VibranceWin32InteropActivateNotificationFn activateNotification = nullptr;
    VibranceWin32InteropResolveApplicationIconFn resolveIcon = nullptr;
    bool hasNotificationSnapshot = false;
    // Keep the item storage off the refresh stack: even a metadata-only return
    // would otherwise probe a large Win32 stack frame on every call.
    std::array<VibranceWin32NotificationItem, 64u> bridgeItems {};

    static std::wstring bridge_path()
    {
        std::array<wchar_t, 32768u> executablePath {};
        const DWORD length = GetModuleFileNameW(
            nullptr,
            executablePath.data(),
            static_cast<DWORD>(executablePath.size()));
        if (length == 0u || length >= executablePath.size())
        {
            return L"vibrance_win32_interop.dll";
        }

        std::wstring path(executablePath.data(), length);
        const std::size_t separator = path.find_last_of(L"\\/");
        path.resize(separator == std::wstring::npos ? 0u : separator + 1u);
        path += L"vibrance_win32_interop.dll";
        return path;
    }

    template <typename Function>
    Function load(const char* symbol) const
    {
        const FARPROC address = GetProcAddress(module, symbol);
        static_assert(sizeof(Function) == sizeof(address));
        return address ? std::bit_cast<Function>(address) : Function {};
    }

    Impl()
    {
        module = LoadLibraryW(bridge_path().c_str());
        if (!module)
        {
            current.access = SystemNotificationAccess::eUnavailable;
            current.diagnostic =
                "vibrance_win32_interop.dll was not found beside the "
                "current executable.";
            return;
        }

        const auto abiVersion = load<VibranceWin32InteropAbiVersionFn>(
            VIBRANCE_WIN32_INTEROP_ABI_VERSION_SYMBOL);
        const auto create = load<VibranceWin32InteropCreateWithFeaturesFn>(
            VIBRANCE_WIN32_INTEROP_CREATE_WITH_FEATURES_SYMBOL);
        destroy = load<VibranceWin32InteropDestroyFn>(
            VIBRANCE_WIN32_INTEROP_DESTROY_SYMBOL);
        requestAccess = load<VibranceWin32InteropRequestNotificationAccessFn>(
            VIBRANCE_WIN32_INTEROP_REQUEST_NOTIFICATION_ACCESS_SYMBOL);
        getNotifications = load<VibranceWin32InteropGetNotificationsFn>(
            VIBRANCE_WIN32_INTEROP_GET_NOTIFICATIONS_SYMBOL);
        dismissNotification = load<VibranceWin32InteropDismissNotificationFn>(
            VIBRANCE_WIN32_INTEROP_DISMISS_NOTIFICATION_SYMBOL);
        activateSource =
            load<VibranceWin32InteropActivateNotificationSourceFn>(
                VIBRANCE_WIN32_INTEROP_ACTIVATE_NOTIFICATION_SOURCE_SYMBOL);
        resolveIcon = load<VibranceWin32InteropResolveApplicationIconFn>(
            VIBRANCE_WIN32_INTEROP_RESOLVE_APPLICATION_ICON_SYMBOL);
        activateNotification = load<VibranceWin32InteropActivateNotificationFn>(
            VIBRANCE_WIN32_INTEROP_ACTIVATE_NOTIFICATION_SYMBOL);

        if (!abiVersion || !create || !destroy || !requestAccess ||
            !getNotifications || !dismissNotification || !activateSource ||
            !resolveIcon || !activateNotification ||
            abiVersion() != VIBRANCE_WIN32_INTEROP_ABI_VERSION)
        {
            current.access = SystemNotificationAccess::eUnavailable;
            current.diagnostic =
                "The Win32 notification companion has an incompatible ABI.";
            FreeLibrary(module);
            module = nullptr;
            return;
        }

        handle = create(VIBRANCE_WIN32_INTEROP_NOTIFICATIONS);
        if (!handle)
        {
            current.access = SystemNotificationAccess::eError;
            current.diagnostic =
                "The Win32 notification companion could not be started.";
            FreeLibrary(module);
            module = nullptr;
            return;
        }

        current.bridgeLoaded = true;
        current.access = SystemNotificationAccess::eUnspecified;
    }

    ~Impl()
    {
        if (handle && destroy)
        {
            destroy(handle);
            handle = nullptr;
        }
        if (module)
        {
            FreeLibrary(module);
            module = nullptr;
        }
    }

    bool request_access()
    {
        if (!handle || !requestAccess)
        {
            return false;
        }
        const bool requested = requestAccess(handle) != 0u;
        refresh();
        return requested;
    }

    bool refresh()
    {
        if (!handle || !getNotifications)
        {
            return false;
        }

        VibranceWin32NotificationSnapshot bridgeSnapshot {};
        // The ABI supports a metadata-only read. Avoid clearing/copying the
        // 150 KB item buffer and rebuilding strings on unchanged UI ticks.
        if (getNotifications(
                handle,
                &bridgeSnapshot,
                sizeof(bridgeSnapshot),
                nullptr,
                sizeof(VibranceWin32NotificationItem),
                0u) == 0u ||
            bridgeSnapshot.structSize != sizeof(bridgeSnapshot))
        {
            return false;
        }
        if (hasNotificationSnapshot && bridgeSnapshot.revision == current.revision)
        {
            return false;
        }

        if (getNotifications(
                handle,
                &bridgeSnapshot,
                sizeof(bridgeSnapshot),
                bridgeItems.data(),
                sizeof(VibranceWin32NotificationItem),
                static_cast<std::uint32_t>(bridgeItems.size())) == 0u ||
            bridgeSnapshot.structSize != sizeof(bridgeSnapshot))
        {
            return false;
        }

        const std::uint64_t previousRevision = current.revision;
        const SystemNotificationAccess previousAccess = current.access;
        const std::string previousDiagnostic = current.diagnostic;
        switch (bridgeSnapshot.accessStatus)
        {
            case VIBRANCE_WIN32_NOTIFICATIONS_ALLOWED:
                current.access = SystemNotificationAccess::eAllowed;
                break;
            case VIBRANCE_WIN32_NOTIFICATIONS_DENIED:
                current.access = SystemNotificationAccess::eDenied;
                break;
            case VIBRANCE_WIN32_NOTIFICATIONS_UNAVAILABLE:
                current.access = SystemNotificationAccess::eUnavailable;
                break;
            case VIBRANCE_WIN32_NOTIFICATIONS_ERROR:
                current.access = SystemNotificationAccess::eError;
                break;
            default:
                current.access = SystemNotificationAccess::eUnspecified;
                break;
        }
        current.bridgeLoaded = true;
        hasNotificationSnapshot = true;
        current.revision = bridgeSnapshot.revision;
        current.diagnostic = bridgeSnapshot.diagnostic;

        const std::uint32_t count = std::min<std::uint32_t>(
            bridgeSnapshot.itemCount,
            static_cast<std::uint32_t>(bridgeItems.size()));
        current.items.clear();
        current.items.reserve(count);
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            const VibranceWin32NotificationItem& bridgeItem =
                bridgeItems[index];
            if (bridgeItem.structSize != sizeof(bridgeItem))
            {
                continue;
            }
            SystemNotificationItem item {};
            item.providerId = bridgeItem.notificationId;
            item.creationUnixMilliseconds =
                bridgeItem.creationUnixMilliseconds;
            item.iconRevision = bridgeItem.iconRevision;
            item.source = bridgeItem.sourceApp;
            item.title = bridgeItem.title;
            item.message = bridgeItem.message;
            item.activationTarget = bridgeItem.appUserModelId;
            item.activationArguments = bridgeItem.activationArguments;
            item.activationKind = static_cast<SystemNotificationActivationKind>(bridgeItem.activationKind);
            item.iconPath = bridgeItem.iconPath;
            current.items.push_back(std::move(item));
        }

        return current.revision != previousRevision ||
            current.access != previousAccess ||
            current.diagnostic != previousDiagnostic;
    }

    bool dismiss(std::uint32_t providerId)
    {
        return handle && dismissNotification &&
            dismissNotification(handle, providerId) != 0u;
    }

    bool activate_source(std::string_view activationTarget)
    {
        if (!handle || !activateSource || activationTarget.empty())
        {
            return false;
        }
        const std::string target(activationTarget);
        return activateSource(handle, target.c_str()) != 0u;
    }

    bool activate_notification(std::string_view target, std::string_view arguments, SystemNotificationActivationKind kind)
    {
        if (!handle || !activateNotification || target.empty()) return false;
        const std::string identity(target), payload(arguments);
        const bool invoked = activateNotification(handle, identity.c_str(), payload.c_str(), static_cast<std::uint32_t>(kind)) != 0u;
        if (!invoked)
            provider_diagnostic(true, "Original notification activation failed for " + identity +
                "; kind=" + std::to_string(static_cast<std::uint32_t>(kind)) +
                "; argument bytes=" + std::to_string(payload.size()));
        return invoked;
    }

    SystemApplicationIcon resolve_application_icon(
        std::string_view activationTarget)
    {
        if (!handle || !resolveIcon || activationTarget.empty())
        {
            return {};
        }
        const std::string target(activationTarget);
        VibranceWin32ApplicationIcon icon {};
        if (resolveIcon(
                handle,
                target.c_str(),
                &icon,
                sizeof(icon)) == 0u ||
            icon.structSize != sizeof(icon) || icon.path[0] == '\0')
        {
            return {};
        }
        return { icon.path, icon.revision };
    }
#else
    Impl()
    {
        current.diagnostic =
            "System notification capture is available on Win32 only.";
    }

    bool request_access() { return false; }
    bool refresh() { return false; }
    bool dismiss(std::uint32_t) { return false; }
    bool activate_source(std::string_view) { return false; }
    bool activate_notification(std::string_view, std::string_view, SystemNotificationActivationKind) { return false; }
    SystemApplicationIcon resolve_application_icon(std::string_view)
    {
        return {};
    }
#endif
};

SystemNotificationProvider::SystemNotificationProvider() :
    impl(std::make_unique<Impl>())
{
    provider_diagnostic(!impl->current.bridgeLoaded, impl->current.diagnostic.empty() ?
        "Provider initialised." : impl->current.diagnostic);
}

SystemNotificationProvider::~SystemNotificationProvider() = default;
SystemNotificationProvider::SystemNotificationProvider(
    SystemNotificationProvider&&) noexcept = default;
SystemNotificationProvider& SystemNotificationProvider::operator=(
    SystemNotificationProvider&&) noexcept = default;

bool SystemNotificationProvider::platform_supported()
{
#if defined(_WIN32) && !defined(VIBRANCE_FORCE_PORTABLE_NOTIFICATION_PROVIDER)
    return true;
#else
    return false;
#endif
}

bool SystemNotificationProvider::bridge_loaded() const
{
    return impl && impl->current.bridgeLoaded;
}

bool SystemNotificationProvider::request_access()
{
    const bool succeeded = impl && impl->request_access();
    provider_diagnostic(!succeeded, "request_access: " + std::string(succeeded ? "succeeded" : "failed"));
    return succeeded;
}

bool SystemNotificationProvider::refresh()
{
    if (!impl) return false;
    const auto previousDiagnostic = impl->current.diagnostic;
    const bool changed = impl->refresh();
    if (previousDiagnostic != impl->current.diagnostic)
        provider_diagnostic(!impl->current.diagnostic.empty(), impl->current.diagnostic.empty() ?
            "Provider recovered." : impl->current.diagnostic);
    return changed;
}

bool SystemNotificationProvider::dismiss(std::uint32_t providerId)
{
    const bool succeeded = impl && impl->dismiss(providerId);
    provider_diagnostic(!succeeded, "dismiss: " + std::string(succeeded ? "succeeded" : "failed"));
    return succeeded;
}

bool SystemNotificationProvider::activate_source(
    std::string_view activationTarget)
{
    const bool succeeded = impl && impl->activate_source(activationTarget);
    provider_diagnostic(!succeeded, "activate_source: " + std::string(succeeded ? "succeeded" : "failed"));
    return succeeded;
}

bool SystemNotificationProvider::activate_notification(std::string_view target,
    std::string_view arguments, SystemNotificationActivationKind kind)
{
    const bool succeeded = impl && impl->activate_notification(target, arguments, kind);
    if (!succeeded) provider_diagnostic(true, "The notification destination could not be invoked.");
    return succeeded;
}

SystemApplicationIcon SystemNotificationProvider::resolve_application_icon(
    std::string_view activationTarget)
{
    return impl ? impl->resolve_application_icon(activationTarget) :
        SystemApplicationIcon {};
}

const SystemNotificationSnapshot& SystemNotificationProvider::snapshot() const
{
    static const SystemNotificationSnapshot empty;
    return impl ? impl->current : empty;
}

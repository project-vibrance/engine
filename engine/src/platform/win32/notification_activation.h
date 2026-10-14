#pragma once

#include <windows.h>
#include <notificationactivationcallback.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <knownfolders.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Storage.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <filesystem>
#include <fstream>

namespace vibrance::win32
{
struct ToastDestination
{
    std::string arguments;
    std::uint32_t kind = 0u;
};

inline std::optional<ToastDestination> parse_toast_destination(winrt::hstring const& xml)
{
    using namespace winrt::Windows::Data::Xml::Dom;
    try
    {
        XmlDocument document;
        XmlLoadSettings settings;
        settings.ProhibitDtd(true);
        settings.ResolveExternals(false);
        document.LoadXml(xml, settings);
        const auto toast = document.DocumentElement();
        if (!toast || toast.TagName() != L"toast") return {};
        ToastDestination result;
        const auto type = toast.GetAttribute(L"activationType");
        if (type.empty() || type == L"foreground") result.kind = 1u;
        else if (type == L"protocol") result.kind = 2u;
        else if (type == L"background") result.kind = 3u;
        else return {};
        result.arguments = winrt::to_string(toast.GetAttribute(L"launch"));
        // A targeted protocol requires LaunchUriAsync's target-package option.
        // Until that is represented in the transport, report unavailable
        // rather than launching a different registered handler.
        if (result.kind == 2u && !toast.GetAttribute(L"protocolActivationTargetApplicationPfn").empty()) return {};
        if (result.arguments.size() >= 4096u || (result.kind == 2u && result.arguments.empty())) return {};
        return result;
    }
    catch (...) { return {}; }
}

inline bool parse_packaged_toast_activator(winrt::hstring const& xml,
    std::wstring_view applicationId, CLSID& clsid)
{
    using namespace winrt::Windows::Data::Xml::Dom;
    try
    {
        XmlDocument manifest;
        XmlLoadSettings settings;
        settings.ProhibitDtd(true);
        settings.ResolveExternals(false);
        manifest.LoadXml(xml, settings);
        const auto applications = manifest.SelectNodes(
            L"/*[local-name()='Package']/*[local-name()='Applications']/*[local-name()='Application']");
        for (const auto& application : applications)
        {
            const auto element = application.as<XmlElement>();
            if (element.GetAttribute(L"Id") != applicationId) continue;
            const auto extensions = element.SelectNodes(
                L"./*[local-name()='Extensions']/*[local-name()='Extension']");
            for (const auto& extension : extensions)
            {
                const auto registration = extension.as<XmlElement>();
                if (registration.GetAttribute(L"Category") != L"windows.toastNotificationActivation") continue;
                const auto activation = registration.SelectSingleNode(L"./*[local-name()='ToastNotificationActivation']");
                if (!activation) continue;
                std::wstring encoded(activation.as<XmlElement>().GetAttribute(L"ToastActivatorCLSID"));
                if (encoded.size() == 36u) encoded = L"{" + encoded + L"}";
                if (encoded.size() == 38u && SUCCEEDED(CLSIDFromString(encoded.c_str(), &clsid))) return true;
            }
        }
    }
    catch (...) {}
    return false;
}

inline bool packaged_toast_activator(const wchar_t* appId, CLSID& clsid)
{
    const std::wstring_view identity(appId);
    const auto separator = identity.find(L'!');
    if (separator == std::wstring_view::npos || separator == 0u || separator + 1u == identity.size()) return false;
    try
    {
        const auto info = winrt::Windows::ApplicationModel::AppInfo::GetFromAppUserModelId(appId);
        if (info.PackageFamilyName() != identity.substr(0, separator)) return false;
        const auto folder = info.Package().InstalledLocation().Path();
        std::ifstream input(std::filesystem::path(folder.c_str()) / L"AppxManifest.xml", std::ios::binary | std::ios::ate);
        if (!input) return false;
        const auto length = input.tellg();
        if (length <= 0 || length > 4 * 1024 * 1024) return false;
        std::string xml(static_cast<std::size_t>(length), '\0');
        input.seekg(0);
        if (!input.read(xml.data(), static_cast<std::streamsize>(xml.size()))) return false;
        return parse_packaged_toast_activator(winrt::to_hstring(xml), identity.substr(separator + 1u), clsid);
    }
    catch (...) { return false; }
}

inline bool notification_activator_clsid(const wchar_t* appId, CLSID& clsid)
{
    if (!appId || !*appId) return false;
    wchar_t value[64] {};
    DWORD bytes = sizeof(value);
    const std::wstring key = L"Software\\Classes\\AppUserModelId\\" + std::wstring(appId);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"CustomActivator", RRF_RT_REG_SZ,
            nullptr, value, &bytes) == ERROR_SUCCESS && SUCCEEDED(CLSIDFromString(value, &clsid))) return true;
    bytes = sizeof(value);
    const std::wstring classesKey = L"AppUserModelId\\" + std::wstring(appId);
    if (RegGetValueW(HKEY_CLASSES_ROOT, classesKey.c_str(), L"CustomActivator", RRF_RT_REG_SZ,
            nullptr, value, &bytes) == ERROR_SUCCESS && SUCCEEDED(CLSIDFromString(value, &clsid))) return true;
    if (packaged_toast_activator(appId, clsid)) return true;
    // Older desktop senders register the activation CLSID on their shortcut.
    winrt::com_ptr<IPropertyStore> properties;
    const std::wstring target = L"shell:AppsFolder\\" + std::wstring(appId);
    const auto readClsid = [&clsid](IPropertyStore* store) {
        PROPVARIANT property {};
        const HRESULT read = store->GetValue(PKEY_AppUserModel_ToastActivatorCLSID, &property);
        const bool found = SUCCEEDED(read) && property.vt == VT_CLSID && property.puuid;
        if (found) clsid = *property.puuid;
        PropVariantClear(&property);
        return found;
    };
    if (SUCCEEDED(SHGetPropertyStoreFromParsingName(target.c_str(), nullptr, GPS_DEFAULT,
        IID_PPV_ARGS(properties.put()))) && readClsid(properties.get())) return true;
    // Desktop AUMIDs are not necessarily AppsFolder entries. Windows uses the
    // sender's Start Menu shortcut; read its identity before accepting a CLSID.
    for (const auto& folderId : { FOLDERID_StartMenu, FOLDERID_CommonStartMenu })
    {
        PWSTR folder = nullptr;
        if (FAILED(SHGetKnownFolderPath(folderId, KF_FLAG_DEFAULT, nullptr, &folder))) continue;
        const std::filesystem::path root(folder);
        CoTaskMemFree(folder);
        std::error_code error;
        std::filesystem::recursive_directory_iterator entry(root,
            std::filesystem::directory_options::skip_permission_denied, error), end;
        for (std::size_t visited = 0u; !error && entry != end && visited < 4096u; entry.increment(error), ++visited)
        {
            if (entry->path().extension() != L".lnk") continue;
            winrt::com_ptr<IPropertyStore> shortcut;
            if (FAILED(SHGetPropertyStoreFromParsingName(entry->path().c_str(), nullptr, GPS_DEFAULT,
                IID_PPV_ARGS(shortcut.put())))) continue;
            PROPVARIANT identity {};
            const HRESULT read = shortcut->GetValue(PKEY_AppUserModel_ID, &identity);
            const bool matches = SUCCEEDED(read) && identity.vt == VT_LPWSTR && identity.pwszVal &&
                std::wstring_view(identity.pwszVal) == appId;
            PropVariantClear(&identity);
            if (matches && readClsid(shortcut.get())) return true;
        }
    }
    return false;
}

inline bool dispatch_toast_callback(INotificationActivationCallback* callback,
    const wchar_t* appId, const wchar_t* arguments)
{
    return callback && appId && arguments && SUCCEEDED(callback->Activate(appId, arguments, nullptr, 0u));
}

inline bool registered_electron_toast_activator(REFCLSID clsid)
{
    wchar_t encoded[40] {}, name[128] {};
    if (!StringFromGUID2(clsid, encoded, 40)) return false;
    const std::wstring key = L"CLSID\\" + std::wstring(encoded);
    DWORD bytes = sizeof(name), marker = 0u, markerBytes = sizeof(marker);
    return RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), nullptr, RRF_RT_REG_SZ,
        nullptr, name, &bytes) == ERROR_SUCCESS &&
        std::wstring_view(name) == L"Electron Notification Activator" &&
        RegGetValueW(HKEY_CLASSES_ROOT, key.c_str(), L"CustomActivator", RRF_RT_REG_DWORD,
            nullptr, &marker, &markerBytes) == ERROR_SUCCESS && marker == 1u;
}

inline bool registered_electron_toast_sender(const std::string& appId)
{
    try
    {
        CLSID clsid {};
        const auto identity = winrt::to_hstring(appId);
        return notification_activator_clsid(identity.c_str(), clsid) && registered_electron_toast_activator(clsid);
    }
    catch (...) { return false; }
}

inline std::optional<ToastDestination> electron_tagged_toast_destination(
    ToastDestination destination, std::string_view tag)
{
    // Electron's generated body XML can omit launch arguments. Its registered
    // activator looks up the original in-memory Notification by its native tag.
    // Preserve explicit destinations and reject query delimiters in the tag.
    if (destination.kind != 1u || !destination.arguments.empty()) return destination;
    if (tag.empty() || tag.size() > 128u || !std::all_of(tag.begin(), tag.end(), [](unsigned char value) {
        return (value >= '0' && value <= '9') || (value >= 'A' && value <= 'Z') ||
            (value >= 'a' && value <= 'z') || value == '-' || value == '_';
    })) return {};
    destination.arguments = "type=click&tag=" + std::string(tag);
    return destination;
}

inline bool invoke_toast_destination(const std::string& appId, const std::string& arguments, std::uint32_t kind)
{
    if (appId.empty() || arguments.size() >= 4096u) return false;
    const auto identity = winrt::to_hstring(appId), payload = winrt::to_hstring(arguments);
    if (kind == 2u)
    {
        const auto separator = arguments.find(':');
        if (separator == std::string::npos || separator == 0u) return false;
        return reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", payload.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32;
    }
    if (kind != 1u && kind != 3u) return false;
    // Empty arguments are valid for registered callbacks. Calling the actual
    // receiver remains distinct from opening the source application.
    CLSID clsid {};
    if (!notification_activator_clsid(identity.c_str(), clsid)) return false;
    // Electron acknowledges empty COM calls but cannot find the original
    // notification. Older saved cards without a tag must use source opening.
    if (kind == 1u && arguments.empty() && registered_electron_toast_activator(clsid)) return false;
    winrt::com_ptr<INotificationActivationCallback> callback;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(callback.put())))) return false;
    (void)CoAllowSetForegroundWindow(callback.get(), nullptr);
    return dispatch_toast_callback(callback.get(), identity.c_str(), payload.c_str());
}
}

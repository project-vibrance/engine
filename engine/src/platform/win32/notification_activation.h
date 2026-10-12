#pragma once

#include <windows.h>
#include <notificationactivationcallback.h>
#include <propkey.h>
#include <propvarutil.h>
#include <shobjidl.h>
#include <shellapi.h>
#include <winrt/Windows.Data.Xml.Dom.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vibrance::win32
{
struct ToastDestination
{
    std::string title, message, arguments;
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
        if (result.arguments.size() >= 4096u || (result.kind == 2u && result.arguments.empty())) return {};
        const auto text = document.SelectNodes(L"/toast/visual/binding/text");
        for (std::uint32_t index = 0u; index < text.Size(); ++index)
        {
            const auto value = winrt::to_string(text.GetAt(index).InnerText());
            if (index == 0u) result.title = value;
            else { if (!result.message.empty()) result.message += '\n'; result.message += value; }
        }
        // Match the listener's bounded visual snapshot without truncating the
        // activation payload itself. Ambiguous matches are rejected below.
        result.title.resize(std::min(result.title.size(), std::size_t(383u)));
        result.message.resize(std::min(result.message.size(), std::size_t(767u)));
        return result;
    }
    catch (...) { return {}; }
}

inline std::vector<ToastDestination> read_toast_destinations(winrt::hstring const& appId)
{
    std::vector<ToastDestination> result;
    try
    {
        for (const auto& toast : winrt::Windows::UI::Notifications::ToastNotificationManager::History().GetHistory(appId))
            if (const auto destination = parse_toast_destination(toast.Content().GetXml())) result.push_back(*destination);
    }
    catch (...) { /* Some applications do not expose their toast history. */ }
    return result;
}

inline std::optional<ToastDestination> match_toast_destination(
    const std::vector<ToastDestination>& candidates, const std::string& title, const std::string& message)
{
    std::optional<ToastDestination> result;
    for (const auto& candidate : candidates)
    {
        if (candidate.title != title || candidate.message != message) continue;
        if (result && (result->arguments != candidate.arguments || result->kind != candidate.kind)) return {};
        result = candidate;
    }
    return result;
}

inline bool notification_activator_clsid(const wchar_t* appId, CLSID& clsid)
{
    wchar_t value[64] {};
    DWORD bytes = sizeof(value);
    const std::wstring key = L"Software\\Classes\\AppUserModelId\\" + std::wstring(appId);
    if (RegGetValueW(HKEY_CURRENT_USER, key.c_str(), L"CustomActivator", RRF_RT_REG_SZ,
            nullptr, value, &bytes) == ERROR_SUCCESS && SUCCEEDED(CLSIDFromString(value, &clsid))) return true;
    // Older desktop senders register the activation CLSID on their shortcut.
    winrt::com_ptr<IPropertyStore> properties;
    const std::wstring target = L"shell:AppsFolder\\" + std::wstring(appId);
    if (FAILED(SHGetPropertyStoreFromParsingName(target.c_str(), nullptr, GPS_DEFAULT, IID_PPV_ARGS(properties.put())))) return false;
    PROPVARIANT property {};
    const HRESULT read = properties->GetValue(PKEY_AppUserModel_ToastActivatorCLSID, &property);
    const bool found = SUCCEEDED(read) && property.vt == VT_CLSID && property.puuid;
    if (found) clsid = *property.puuid;
    PropVariantClear(&property);
    return found;
}

inline bool dispatch_toast_callback(INotificationActivationCallback* callback,
    const wchar_t* appId, const wchar_t* arguments)
{
    return callback && appId && arguments && SUCCEEDED(callback->Activate(appId, arguments, nullptr, 0u));
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
    CLSID clsid {};
    if (!notification_activator_clsid(identity.c_str(), clsid)) return false;
    winrt::com_ptr<INotificationActivationCallback> callback;
    if (FAILED(CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER, IID_PPV_ARGS(callback.put())))) return false;
    (void)CoAllowSetForegroundWindow(callback.get(), nullptr);
    return dispatch_toast_callback(callback.get(), identity.c_str(), payload.c_str());
}
}

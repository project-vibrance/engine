#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vibrance::win32
{
inline std::wstring monitor_hardware_code(std::wstring_view path)
{
    // DisplayConfig supplies the monitor device-interface path, whose device
    // component contains its PnP manufacturer and product identifiers.
    const auto start = path.find(L'#');
    if (start == path.npos) return {};
    const auto end = path.find(L'#', start + 1u);
    if (end == path.npos || end - start != 8u) return {};
    std::wstring code(path.substr(start + 1u, 7u));
    for (auto& ch : code) if (ch >= L'a' && ch <= L'z') ch -= L'a' - L'A';
    for (std::size_t i = 0u; i < code.size(); ++i)
        if (i < 3u ? (code[i] < L'A' || code[i] > L'Z') :
            !((code[i] >= L'0' && code[i] <= L'9') || (code[i] >= L'A' && code[i] <= L'F')))
            return {};
    return code;
}

inline std::wstring monitor_manufacturer(std::wstring_view code)
{
    // Common PnP manufacturer IDs. Unknown vendors retain their real ID;
    // never infer a brand from a model number or the graphics adapter.
    static constexpr std::pair<std::wstring_view, std::wstring_view> vendors[] {
        { L"ACI", L"ASUS" }, { L"AUS", L"ASUS" }, { L"ACR", L"Acer" },
        { L"AOC", L"AOC" }, { L"APP", L"Apple" }, { L"AUO", L"AU Optronics" },
        { L"BNQ", L"BenQ" }, { L"BOE", L"BOE" }, { L"CMN", L"Innolux" },
        { L"DEL", L"Dell" }, { L"GBT", L"Gigabyte" }, { L"GSM", L"LG" },
        { L"HPN", L"HP" }, { L"HWP", L"HP" }, { L"LEN", L"Lenovo" },
        { L"LGD", L"LG" }, { L"MSI", L"MSI" }, { L"NEC", L"NEC" },
        { L"PHL", L"Philips" }, { L"SAM", L"Samsung" }, { L"SDC", L"Samsung" },
        { L"SHP", L"Sharp" }, { L"SNY", L"Sony" }, { L"VSC", L"ViewSonic" }
    };
    for (const auto& [id, name] : vendors) if (id == code) return std::wstring(name);
    return std::wstring(code);
}

inline std::wstring monitor_display_name(std::wstring_view model, std::wstring_view path)
{
    const auto first = model.find_first_not_of(L" \t\r\n");
    model = first == model.npos ? std::wstring_view {} :
        model.substr(first, model.find_last_not_of(L" \t\r\n") - first + 1u);
    if (_wcsicmp(std::wstring(model).c_str(), L"Generic PnP Monitor") == 0 ||
        _wcsicmp(std::wstring(model).c_str(), L"Generic Non-PnP Monitor") == 0)
        model = {};
    const auto code = monitor_hardware_code(path);
    if (code.empty()) return std::wstring(model);
    const auto vendor = monitor_manufacturer(std::wstring_view(code).substr(0u, 3u));
    if (model.empty()) return vendor + L" " + code.substr(3u);
    // Preserve names which already contain the manufacturer, including case.
    const auto containsVendor = std::search(model.begin(), model.end(), vendor.begin(), vendor.end(),
        [](wchar_t a, wchar_t b) {
            const auto lower = [](wchar_t ch) { return ch >= L'A' && ch <= L'Z' ? ch + L'a' - L'A' : ch; };
            return lower(a) == lower(b);
        }) != model.end();
    return containsVendor ? std::wstring(model) : vendor + L" " + std::wstring(model);
}

inline std::string monitor_name_utf8(std::wstring_view value)
{
    const int length = WideCharToMultiByte(CP_UTF8, 0u, value.data(),
        static_cast<int>(value.size()), nullptr, 0, nullptr, nullptr);
    if (length <= 0) return {};
    std::string result(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(CP_UTF8, 0u, value.data(), static_cast<int>(value.size()),
        result.data(), length, nullptr, nullptr);
    return result;
}

struct MonitorHardwareName
{
    std::wstring adapter;
    std::wstring devicePath;
    std::string name;
};

inline std::vector<MonitorHardwareName> active_monitor_hardware_names()
{
    // Query one active topology, matching each target to its GDI source.
    // Bound retries if a hot-plug races the two DisplayConfig calls.
    constexpr UINT32 flags = QDC_ONLY_ACTIVE_PATHS;
    for (unsigned attempt = 0u; attempt < 3u; ++attempt)
    {
        UINT32 pathCount = 0u, modeCount = 0u;
        if (GetDisplayConfigBufferSizes(flags, &pathCount, &modeCount) != ERROR_SUCCESS) return {};
        std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
        std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
        const LONG status = QueryDisplayConfig(flags, &pathCount, paths.data(), &modeCount, modes.data(), nullptr);
        if (status == ERROR_INSUFFICIENT_BUFFER) continue;
        if (status != ERROR_SUCCESS) return {};
        std::vector<MonitorHardwareName> result;
        for (UINT32 i = 0u; i < pathCount; ++i)
        {
            DISPLAYCONFIG_SOURCE_DEVICE_NAME source {};
            source.header = { DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME, sizeof(source),
                paths[i].sourceInfo.adapterId, paths[i].sourceInfo.id };
            DISPLAYCONFIG_TARGET_DEVICE_NAME target {};
            target.header = { DISPLAYCONFIG_DEVICE_INFO_GET_TARGET_NAME, sizeof(target),
                paths[i].targetInfo.adapterId, paths[i].targetInfo.id };
            if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
                DisplayConfigGetDeviceInfo(&target.header) != ERROR_SUCCESS) continue;
            const auto label = monitor_display_name(target.monitorFriendlyDeviceName, target.monitorDevicePath);
            if (!label.empty()) result.push_back({ source.viewGdiDeviceName, target.monitorDevicePath,
                monitor_name_utf8(label) });
        }
        return result;
    }
    return {};
}

inline std::string hardware_name_for_monitor(const std::vector<MonitorHardwareName>& names,
    const char* adapter, const char* monitor)
{
    if (!adapter || !monitor) return {};
    const std::wstring source(adapter, adapter + std::char_traits<char>::length(adapter));
    // Match physical interfaces as well as sources: cloned displays may share
    // a GDI source but have different manufacturers/models.
    for (DWORD index = 0u; ; ++index)
    {
        DISPLAY_DEVICEW device {};
        device.cb = sizeof(device);
        if (!EnumDisplayDevicesW(source.c_str(), index, &device, EDD_GET_DEVICE_INTERFACE_NAME)) break;
        if (monitor_name_utf8(device.DeviceName) != monitor) continue;
        for (const auto& name : names)
            if (_wcsicmp(name.devicePath.c_str(), device.DeviceID) == 0) return name.name;
        break;
    }
    const MonitorHardwareName* only = nullptr;
    for (const auto& name : names)
        if (_wcsicmp(name.adapter.c_str(), source.c_str()) == 0)
        {
            if (only) return {}; // Ambiguous clone; keep GLFW's fallback.
            only = &name;
        }
    return only ? only->name : std::string {};
}
}

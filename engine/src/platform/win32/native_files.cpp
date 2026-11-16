// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/native_dialog.h>
#include <array>
#include <cerrno>
#include <thread>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>

std::optional<std::filesystem::path> show_native_file_dialog(const NativeFileDialogOptions& options, std::string& error)
{
    std::vector<wchar_t> path(32768u, L'\0');
    OPENFILENAMEW dialog {};
    dialog.lStructSize = sizeof(dialog);
    dialog.hwndOwner = static_cast<HWND>(options.parentWindow);
    const auto title = std::filesystem::path(std::u8string(options.title.begin(), options.title.end())).wstring();
    std::wstring filter;
    for (const auto& entry : options.filters)
    {
        filter += std::filesystem::path(std::u8string(entry.name.begin(), entry.name.end())).wstring();
        filter += L'\0';
        for (std::size_t index = 0u; index < entry.extensions.size(); ++index)
        {
            if (index) filter += L';';
            filter += L"*." + std::filesystem::path(entry.extensions[index]).wstring();
        }
        filter += L'\0';
    }
    if (options.allowAllFiles || filter.empty()) filter += std::wstring(L"All files\0*.*\0", 14u);
    filter += L'\0';
    const auto initial = options.initialDirectory.wstring();
    dialog.lpstrTitle = title.c_str();
    dialog.lpstrFilter = filter.c_str();
    dialog.lpstrInitialDir = initial.empty() ? nullptr : initial.c_str();
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_DONTADDTORECENT;
    if (!GetOpenFileNameW(&dialog))
    { error = CommDlgExtendedError() ? "The file selection window could not be opened." : ""; return {}; }
    error.clear();
    return std::filesystem::path(path.data());
}

bool native_open_directory(const std::filesystem::path& directory, std::string& error, bool createIfMissing)
{
    std::error_code fileError;
    if (createIfMissing && !directory.empty()) std::filesystem::create_directories(directory, fileError);
    if (directory.empty() || fileError || !std::filesystem::is_directory(directory, fileError))
    { error = "The requested directory is unavailable."; return false; }
    const auto result = reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", directory.c_str(),
        nullptr, nullptr, SW_SHOWNORMAL));
    if (result <= 32) { error = "The directory could not be opened."; return false; }
    error.clear();
    return true;
}

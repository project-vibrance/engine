#pragma once
#include "crash_diagnostics.h"
#include <shlobj.h>
#include <string>
#include <string_view>
#include <mutex>

// The single-file bootstrap must log before the engine DLL is extracted.
namespace windows_diagnostics
{
inline HANDLE logFile = INVALID_HANDLE_VALUE;
inline std::mutex logMutex;
inline void log(std::wstring_view message) noexcept
{
    if (logFile == INVALID_HANDLE_VALUE) return;
    try
    {
        const std::lock_guard lock(logMutex);
        SYSTEMTIME time {};
        GetSystemTime(&time);
        char timestamp[64] {};
        std::snprintf(timestamp, sizeof(timestamp), "[%04u-%02u-%02uT%02u:%02u:%02uZ] ",
            time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond);
        const int length = WideCharToMultiByte(CP_UTF8, 0, message.data(),
            static_cast<int>(message.size()), nullptr, 0, nullptr, nullptr);
        std::string line(length, '\0');
        WideCharToMultiByte(CP_UTF8, 0, message.data(), static_cast<int>(message.size()),
            line.data(), length, nullptr, nullptr);
        line = timestamp + line + "\n";
        DWORD written = 0;
        WriteFile(logFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        FlushFileBuffers(logFile);
    }
    catch (...) {}
}
inline void initialise(std::wstring_view component = L"installer-bootstrap") noexcept
{
    try
    {
        PWSTR documents = nullptr;
        if (FAILED(SHGetKnownFolderPath(FOLDERID_Documents, 0, nullptr, &documents))) return;
        auto directory = std::filesystem::path(documents) / "Vibrance";
        CoTaskMemFree(documents);
        std::filesystem::create_directories(directory);
        SYSTEMTIME time {};
        GetSystemTime(&time);
        wchar_t name[128] {};
        std::swprintf(name, 128, L"vibrance-%ls-%04u%02u%02u-%02u%02u%02u-%lu.log",
            std::wstring(component).c_str(), time.wYear, time.wMonth, time.wDay,
            time.wHour, time.wMinute, time.wSecond, GetCurrentProcessId());
        const auto path = directory / name;
        logFile = CreateFileW(path.c_str(), FILE_APPEND_DATA, FILE_SHARE_READ, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        crash_diagnostics::initialise(path);
        log(L"SESSION START: " + std::wstring(component));
    }
    catch (...) { OutputDebugStringA("Vibrance bootstrap diagnostics could not be initialised.\n"); }
}

template<class Action>
int run_session(std::wstring_view component, Action action)
{
    initialise(component);
    try
    {
        const int result = action();
        log(L"SESSION END: exit code " + std::to_wstring(result));
        return result;
    }
    catch (const std::exception& error)
    {
        crash_diagnostics::write("FATAL: unhandled helper exception: ");
        crash_diagnostics::write(error.what());
        crash_diagnostics::write("\n");
        log(L"FATAL: unhandled exception; inspect the crash report.");
    }
    catch (...)
    {
        crash_diagnostics::write("FATAL: non-standard helper exception.\n");
        log(L"FATAL: non-standard exception.");
    }
    return EXIT_FAILURE;
}
}

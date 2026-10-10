#pragma once
#include "crash_diagnostics.h"
#include <shlobj.h>
#include <string>
#include <string_view>
#include <mutex>
#include "runtime_log_storage.h"

// The single-file bootstrap must log before the engine DLL is extracted.
namespace windows_diagnostics
{
inline HANDLE logFile = INVALID_HANDLE_VALUE;
inline std::mutex logMutex;
inline std::filesystem::path logPath;
inline std::uintmax_t writtenBytes = 0;
inline std::chrono::steady_clock::time_point nextRetentionCheck {};
inline std::chrono::steady_clock::time_point nextRotationAttempt {};
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
        if (line.size() > 64u * 1024u) line.resize(64u * 1024u);
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextRetentionCheck)
        {
            runtime_log_storage::prune(logPath.parent_path(), logPath);
            nextRetentionCheck = now + std::chrono::minutes(1);
        }
        if (writtenBytes + line.size() > runtime_log_storage::fileLimit)
        {
            if (now < nextRotationAttempt) return;
            CloseHandle(logFile);
            logFile = INVALID_HANDLE_VALUE;
            const bool rotated = runtime_log_storage::rotate(logPath);
            logFile = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                rotated ? CREATE_ALWAYS : OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
            if (!rotated)
            {
                if (logFile != INVALID_HANDLE_VALUE) SetFilePointer(logFile, 0, nullptr, FILE_END);
                nextRotationAttempt = now + std::chrono::minutes(1);
                return;
            }
            if (logFile == INVALID_HANDLE_VALUE) return;
            writtenBytes = 0;
            runtime_log_storage::prune(logPath.parent_path(), logPath);
        }
        DWORD written = 0;
        WriteFile(logFile, line.data(), static_cast<DWORD>(line.size()), &written, nullptr);
        writtenBytes += written;
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
        logPath = directory / name;
        logFile = CreateFileW(logPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        LARGE_INTEGER size {};
        if (logFile != INVALID_HANDLE_VALUE)
        {
            if (!GetFileSizeEx(logFile, &size)) { CloseHandle(logFile); logFile = INVALID_HANDLE_VALUE; }
            else { writtenBytes = static_cast<std::uintmax_t>(size.QuadPart); SetFilePointer(logFile, 0, nullptr, FILE_END); }
        }
        crash_diagnostics::initialise(logPath);
        runtime_log_storage::prune(directory, logPath);
        nextRetentionCheck = std::chrono::steady_clock::now() + std::chrono::minutes(1);
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

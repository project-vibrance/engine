#pragma once

// Crash reporting deliberately bypasses the logger's locks and C++ streams.
// The emergency descriptor is opened while the process is healthy.
#include <cstdio>
#include <cstring>
#include <exception>
#include <csignal>
#include <filesystem>
#include <cstdlib>
#include <initializer_list>
#include <atomic>
#include "runtime_log_storage.h"
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dbghelp.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

namespace crash_diagnostics
{
inline std::atomic<unsigned> emergencyBytes { 0 };
static_assert(std::atomic<unsigned>::is_always_lock_free);
#if defined(_WIN32)
inline HANDLE emergencyFile = INVALID_HANDLE_VALUE;
inline std::filesystem::path dumpPath;
using WriteDump = BOOL (WINAPI*)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
    PMINIDUMP_EXCEPTION_INFORMATION, PMINIDUMP_USER_STREAM_INFORMATION,
    PMINIDUMP_CALLBACK_INFORMATION);
inline WriteDump writeDump = nullptr;
#else
inline int emergencyFile = -1;
#endif

inline void write(const char* text) noexcept
{
    unsigned length = 0;
    while (length < 4096 && text[length]) ++length;
    constexpr unsigned limit = 1024u * 1024u;
    unsigned previous = emergencyBytes.load(std::memory_order_relaxed);
    for (;;)
    {
        if (previous >= limit) return;
        const unsigned bytes = std::min(length, limit - previous);
        if (emergencyBytes.compare_exchange_weak(previous, previous + bytes, std::memory_order_relaxed))
        {
            length = bytes;
            break;
        }
    }
#if defined(_WIN32)
    if (emergencyFile != INVALID_HANDLE_VALUE)
    {
        DWORD written = 0;
        WriteFile(emergencyFile, text, length, &written, nullptr);
        FlushFileBuffers(emergencyFile);
    }
    OutputDebugStringA(text);
#else
    if (emergencyFile >= 0)
    {
        const auto ignored = ::write(emergencyFile, text, length);
        (void)ignored;
    }
#endif
}

#if defined(_WIN32)
inline BOOL CALLBACK dump_callback(PVOID context, PMINIDUMP_CALLBACK_INPUT input,
    MINIDUMP_CALLBACK_OUTPUT* output) noexcept
{
    if (input->CallbackType == CancelCallback)
    {
        LARGE_INTEGER size {};
        output->CheckCancel = TRUE;
        output->Cancel = GetFileSizeEx(static_cast<HANDLE>(context), &size) &&
            static_cast<std::uintmax_t>(size.QuadPart) > runtime_log_storage::fileLimit;
    }
    return TRUE;
}

inline LONG WINAPI unhandled(EXCEPTION_POINTERS* exception) noexcept
{
    char message[256] {};
    std::snprintf(message, sizeof(message),
        "FATAL CRASH: Windows exception 0x%08lX at %p; process %lu, thread %lu.\n",
        exception->ExceptionRecord->ExceptionCode,
        exception->ExceptionRecord->ExceptionAddress, GetCurrentProcessId(), GetCurrentThreadId());
    write(message);
    if (writeDump)
    {
        HANDLE dump = CreateFileW(dumpPath.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
            nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (dump != INVALID_HANDLE_VALUE)
        {
            MINIDUMP_EXCEPTION_INFORMATION info { GetCurrentThreadId(), exception, FALSE };
            MINIDUMP_CALLBACK_INFORMATION callback { dump_callback, dump };
            const BOOL saved = writeDump(GetCurrentProcess(), GetCurrentProcessId(), dump,
                MiniDumpNormal, &info, nullptr, &callback);
            LARGE_INTEGER size {};
            const bool withinLimit = GetFileSizeEx(dump, &size) &&
                static_cast<std::uintmax_t>(size.QuadPart) <= runtime_log_storage::fileLimit;
            FlushFileBuffers(dump);
            CloseHandle(dump);
            if (!saved || !withinLimit) DeleteFileW(dumpPath.c_str());
            write(saved && withinLimit ? "Crash minidump saved beside this report.\n" :
                "Crash minidump unavailable or exceeded the 5 MiB limit; exception details are in this report.\n");
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
#endif

inline void signal_handler(int signal) noexcept
{
    switch (signal)
    {
        case SIGABRT: write("FATAL CRASH: SIGABRT (abort/assertion failure).\n"); break;
        case SIGSEGV: write("FATAL CRASH: SIGSEGV (invalid memory access).\n"); break;
        case SIGILL: write("FATAL CRASH: SIGILL (illegal instruction).\n"); break;
        case SIGFPE: write("FATAL CRASH: SIGFPE (arithmetic exception).\n"); break;
        default: write("FATAL CRASH: fatal signal.\n"); break;
    }
    std::signal(signal, SIG_DFL);
    std::raise(signal);
    std::_Exit(128 + signal);
}

inline void terminate_handler() noexcept
{
    write("FATAL CRASH: std::terminate (uncaught exception or termination during unwinding).\n");
    if (const auto exception = std::current_exception())
    {
        try { std::rethrow_exception(exception); }
        catch (const std::exception& error) { write(error.what()); write("\n"); }
        catch (...) { write("Non-standard C++ exception.\n"); }
    }
    std::abort();
}

inline void initialise(const std::filesystem::path& logPath)
{
    emergencyBytes.store(0, std::memory_order_relaxed);
    auto report = logPath;
    report += ".crash.txt";
#if defined(_WIN32)
    if (emergencyFile != INVALID_HANDLE_VALUE) CloseHandle(emergencyFile);
    emergencyFile = CreateFileW(report.c_str(), FILE_APPEND_DATA,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    dumpPath = logPath;
    dumpPath += ".dmp";
    static HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    if (dbghelp) writeDump = reinterpret_cast<WriteDump>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
    SetUnhandledExceptionFilter(unhandled);
#else
    if (emergencyFile >= 0) ::close(emergencyFile);
    emergencyFile = ::open(report.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
#endif
    std::set_terminate(terminate_handler);
#if defined(_WIN32)
    // Native faults retain their exception code/address and minidump. Registering
    // SIGSEGV on Windows intercepts them before the unhandled exception filter.
    std::signal(SIGABRT, signal_handler);
#else
    for (int signal : { SIGABRT, SIGSEGV, SIGILL, SIGFPE }) std::signal(signal, signal_handler);
#endif
}
}

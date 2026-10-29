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
inline std::atomic<LPTOP_LEVEL_EXCEPTION_FILTER> previousUnhandledFilter { nullptr };
inline std::atomic_flag previousFilterSaved = ATOMIC_FLAG_INIT;
inline std::atomic<DWORD> nativeCrashOwner { 0 };
static_assert(std::atomic<DWORD>::is_always_lock_free);
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

inline bool continuable_gnu_exception(const EXCEPTION_RECORD& record) noexcept
{
    // MinGW's top-level filter returns these transport exceptions to libunwind,
    // which then invokes std::terminate for an uncaught C++ exception. Treating
    // them as native faults hides the exception's original diagnostic.
    return (record.ExceptionCode == 0x20474343u || record.ExceptionCode == 0x22474343u) &&
        !(record.ExceptionFlags & EXCEPTION_NONCONTINUABLE);
}

inline void write_fault_module(const void* address) noexcept
{
    MEMORY_BASIC_INFORMATION memory {};
    if (!VirtualQuery(address, &memory, sizeof(memory)) || memory.Type != MEM_IMAGE) return;
    wchar_t modulePath[MAX_PATH] {};
    const DWORD length = GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase),
        modulePath, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    char moduleUtf8[MAX_PATH * 3 + 1] {};
    if (!WideCharToMultiByte(CP_UTF8, 0, modulePath, static_cast<int>(length), moduleUtf8,
        static_cast<int>(sizeof(moduleUtf8) - 1), nullptr, nullptr)) return;
    char message[sizeof(moduleUtf8) + 96] {};
    const auto offset = reinterpret_cast<std::uintptr_t>(address) -
        reinterpret_cast<std::uintptr_t>(memory.AllocationBase);
    std::snprintf(message, sizeof(message), "Fault module: %s + 0x%llX (base %p).\n",
        moduleUtf8, static_cast<unsigned long long>(offset), memory.AllocationBase);
    write(message);
}

inline LONG WINAPI unhandled(EXCEPTION_POINTERS* exception) noexcept
{
    if (!exception || !exception->ExceptionRecord) return EXCEPTION_CONTINUE_SEARCH;
    if (continuable_gnu_exception(*exception->ExceptionRecord))
    {
        if (const auto previous = previousUnhandledFilter.load(std::memory_order_relaxed);
            previous && previous != unhandled)
        {
            const LONG result = previous(exception);
            if (result != EXCEPTION_CONTINUE_SEARCH) return result;
        }
#if defined(__MINGW32__)
        // The runtime's filter may have been replaced before ours was installed.
        // Only resume its known, continuable C++ transport exceptions.
        return EXCEPTION_CONTINUE_EXECUTION;
#endif
    }
    DWORD expectedOwner = 0;
    if (!nativeCrashOwner.compare_exchange_strong(expectedOwner, GetCurrentThreadId(),
        std::memory_order_relaxed)) return EXCEPTION_CONTINUE_SEARCH;
    // DbgHelp is not re-entrant. A secondary fault must not reopen/truncate the
    // first dump or recursively attempt another stack walk while it is written.
    char message[256] {};
    std::snprintf(message, sizeof(message),
        "FATAL CRASH: Windows exception 0x%08lX at %p; process %lu, thread %lu.\n",
        exception->ExceptionRecord->ExceptionCode,
        exception->ExceptionRecord->ExceptionAddress, GetCurrentProcessId(), GetCurrentThreadId());
    write(message);
    write_fault_module(exception->ExceptionRecord->ExceptionAddress);
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
    const auto previous = SetUnhandledExceptionFilter(unhandled);
    // The EXE and engine DLL each install this header's handler. Retain the
    // original predecessor when logs rotate; replacing it can form DLL/EXE
    // forwarding cycles after either module installs its handler again.
    if (previous != unhandled && !previousFilterSaved.test_and_set(std::memory_order_relaxed))
        previousUnhandledFilter.store(previous, std::memory_order_relaxed);
#else
    if (emergencyFile >= 0) ::close(emergencyFile);
    emergencyFile = ::open(report.c_str(), O_CREAT | O_WRONLY | O_APPEND, 0600);
#endif
    std::set_terminate(terminate_handler);
#if defined(_WIN32)
    // Native faults retain their exception code/address and minidump. Registering
    // SIGSEGV on Windows intercepts them before the unhandled exception filter.
    std::signal(SIGABRT, signal_handler);
    // Register the emergency handlers before optional DLL initialisation.
    // Load the OS copy explicitly: a stale app-local debugger DLL must not
    // become a prerequisite for starting the application or recording a fault.
    static HMODULE dbghelp = LoadLibraryExW(L"dbghelp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (dbghelp) writeDump = reinterpret_cast<WriteDump>(GetProcAddress(dbghelp, "MiniDumpWriteDump"));
#else
    for (int signal : { SIGABRT, SIGSEGV, SIGILL, SIGFPE }) std::signal(signal, signal_handler);
#endif
}
}

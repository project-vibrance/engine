#include <vibranceUI/core/process.h>
#include <cstdint>
#include <string>
#include <vector>
#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
std::filesystem::path current_executable_path()
{
    std::vector<wchar_t> buffer(32768u, L'\0');
    const DWORD length = GetModuleFileNameW(
        nullptr,
        buffer.data(),
        static_cast<DWORD>(buffer.size()));
    return length == 0u || length >= buffer.size() ?
        std::filesystem::path {} :
        std::filesystem::path(std::wstring(buffer.data(), length));
}

bool relaunch_current_process(int argc, char* argv[])
{
    (void)argc;
    (void)argv;

    const wchar_t* currentCommandLine = GetCommandLineW();
    if (!currentCommandLine || currentCommandLine[0] == L'\0')
    {
        return false;
    }

    std::wstring commandLine(currentCommandLine);
    STARTUPINFOW startupInfo = {};
    startupInfo.cb = sizeof(startupInfo);
    PROCESS_INFORMATION processInfo = {};
    const BOOL launched = CreateProcessW(
        nullptr,
        commandLine.data(),
        nullptr,
        nullptr,
        FALSE,
        CREATE_UNICODE_ENVIRONMENT | CREATE_NEW_PROCESS_GROUP,
        nullptr,
        nullptr,
        &startupInfo,
        &processInfo);
    if (!launched)
    {
        return false;
    }

    CloseHandle(processInfo.hThread);
    CloseHandle(processInfo.hProcess);
    return true;
}

#elif defined(__APPLE__) || defined(__unix__)
#include <spawn.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <mach-o/dyld.h>
#include <crt_externs.h>
#else
extern char** environ;
#endif

std::filesystem::path current_executable_path()
{
#if defined(__APPLE__)
    std::uint32_t size = 0;
    _NSGetExecutablePath(nullptr, &size);
    if (!size) return {};
    std::vector<char> buffer(size);
    if (_NSGetExecutablePath(buffer.data(), &size) != 0) return {};
    std::error_code error;
    auto path = std::filesystem::weakly_canonical(buffer.data(), error);
    return error ? std::filesystem::path {} : path;
#elif defined(__linux__)
    for (std::size_t size = 1024; size <= 1024 * 1024; size *= 2)
    {
        std::string buffer(size, '\0');
        const auto length = readlink("/proc/self/exe", buffer.data(), buffer.size());
        if (length < 0) return {};
        if (static_cast<std::size_t>(length) < size)
        {
            buffer.resize(static_cast<std::size_t>(length));
            return buffer;
        }
    }
    return {};
#else
    return {};
#endif
}

bool relaunch_current_process(int argc, char* argv[])
{
    if (argc <= 0 || !argv) return false;
    std::vector<char*> arguments;
    for (int index = 0; index < argc; ++index)
    {
        if (!argv[index]) return false;
        arguments.push_back(argv[index]);
    }
    arguments.push_back(nullptr);
    const auto executable = current_executable_path();
    const std::string command = executable.empty() ? std::string(argv[0]) : executable.string();
    if (command.empty()) return false;
    pid_t child = 0;
#if defined(__APPLE__)
    char** environment = *_NSGetEnviron();
#else
    char** environment = environ;
#endif
    // posix_spawn reports exec errors and avoids allocation in a forked child.
    return posix_spawnp(&child, command.c_str(), nullptr, nullptr,
        arguments.data(), environment) == 0;
}
#else
std::filesystem::path current_executable_path() { return {}; }
bool relaunch_current_process(int, char*[]) { return false; }
#endif

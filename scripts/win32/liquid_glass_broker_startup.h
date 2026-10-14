#pragma once

#include "../../engine/src/platform/win32/liquid_glass_dwm_startup.h"
#include <iostream>

namespace vibrance::win32::liquid_startup
{
inline int install_helper(const wchar_t* requestedSid)
{
    if (user_sid() != requestedSid)
    {
        std::wcerr << L"Automatic startup requires approval by the signed-in administrator. "
            L"Approval with another account cannot register a helper for this session.\n";
        return 11;
    }
    const auto source = std::filesystem::path(module_path(nullptr)).parent_path();
    const auto key = bundle_key(source);
    const auto destination = installed_directory(key);
    const auto cache = cache_directory();
    if (key.empty() || destination.empty() || cache.empty())
    {
        std::wcerr << L"The Liquid Glass bundle is incomplete. Reinstall the matching native "
            L"architecture build, including dbghelp.dll and symsrv.dll.\n";
        return 3;
    }
    if (!secure_directory(destination.parent_path()) || !secure_directory(destination) ||
        !secure_directory(cache.parent_path()) || !secure_directory(cache))
    {
        std::wcerr << L"Cannot create protected Liquid Glass helper directories (Win32 error "
            << GetLastError() << L").\n";
        return 3;
    }
    // Versioned bundles are immutable. An older observer or DWM may still hold
    // its files open; never overwrite a loaded binary during an app update.
    for (const auto* name : bundleNames)
    {
        const auto target = destination / name;
        if (!CopyFileW((source / name).c_str(), target.c_str(), TRUE) &&
            GetLastError() != ERROR_FILE_EXISTS)
        {
            std::wcerr << L"Cannot deploy " << name << L" (Win32 error " << GetLastError() << L").\n";
            return 3;
        }
        const DWORD flags = GetFileAttributesW(target.c_str());
        if (flags == INVALID_FILE_ATTRIBUTES || (flags & FILE_ATTRIBUTE_REPARSE_POINT)) return 3;
        // CopyFile can copy the source DACL too. Explicitly remove permissions
        // inherited from a portable/user-writable app before scheduling it.
        PSECURITY_DESCRIPTOR descriptor = nullptr;
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"O:BAG:BAD:P(A;;FA;;;SY)(A;;FA;;;BA)(A;;GRGX;;;BU)(A;;GRGX;;;S-1-5-90-0)",
            SDDL_REVISION_1, &descriptor, nullptr)) return 3;
        const bool secured = SetFileSecurityW(target.c_str(), OWNER_SECURITY_INFORMATION |
            DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, descriptor) != FALSE;
        LocalFree(descriptor);
        if (!secured) return 3;
    }
    if (bundle_key(destination) != key)
    {
        std::wcerr << L"Protected helper bundle verification failed.\n";
        return 3;
    }
    const HRESULT result = register_task(destination);
    std::ofstream setupReport(cache / L"setup.log");
    setupReport << "Automatic helper registration HRESULT: 0x" << std::hex <<
        static_cast<unsigned long>(result) << "\n";
    if (FAILED(result))
    {
        std::wcerr << L"Cannot register/start the Liquid Glass helper (HRESULT 0x" <<
            std::hex << static_cast<unsigned long>(result) << L").\n";
        return 14;
    }
    std::wcout << L"Automatic Liquid Glass startup configured for this user.\n";
    return 0;
}

inline const wchar_t* exit_description(DWORD code)
{
    switch (code)
    {
    case 0: return L"Native Liquid Glass is ready.";
    case 3: return L"Runtime files, cache access or permissions are unavailable.";
    case 4: return L"Matching Microsoft DWM symbols are unavailable. Check symbol-server access and dbghelp.dll/symsrv.dll.";
    case 5: return L"No compositor was found in the interactive session.";
    case 8: return L"Windows denied access to DWM; check elevation and device security policy.";
    case 9: return L"DWM could not load the extension; check runtime permissions, dependencies and device security policy.";
    case 10: return L"The extension loaded but its hooks did not become ready. This DWM layout may be unsupported.";
    case 11: return L"The helper requires an elevated administrator token.";
    case 16: return L"Startup is deferred while a fullscreen application is active.";
    case 23: return L"This Windows build or process architecture does not support the native extension.";
    case 24: return L"The matching DWM PDB is available, but this compositor lacks required renderer symbols. See the individual lookup failures above.";
    default: return L"Liquid Glass broker startup failed; see the output above.";
    }
}

inline int watch_session(DWORD (*findDwm)())
{
    const auto executable = std::filesystem::path(module_path(nullptr)).parent_path() / bundleNames[0];
    const auto cache = cache_directory();
    if (cache.empty()) return 3;
    // A fixed protected action takes no caller-supplied executable, DLL or cache
    // path. Child brokers inherit the same administrator token and session.
    const auto log = cache / L"broker.log";
    DWORD lastPid = 0;
    unsigned attempts = 0;
    ULONGLONG retryAt = 0;
    for (;;)
    {
        const DWORD pid = findDwm();
        if (pid != lastPid) { lastPid = pid; attempts = 0; retryAt = 0; }
        if (pid && attempts < 3 && GetTickCount64() >= retryAt && !fullscreen_active())
        {
            const HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, liquidDwmReadyName);
            const bool running = ready && WaitForSingleObject(ready, 0) == WAIT_OBJECT_0;
            if (ready) CloseHandle(ready);
            if (!running)
            {
                ++attempts;
                SECURITY_ATTRIBUTES inherit { sizeof(inherit), nullptr, TRUE };
                const HANDLE output = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ,
                    &inherit, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
                const HANDLE input = CreateFileW(L"NUL", GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                    &inherit, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
                std::wstring command = L"\"" + executable.wstring() + L"\" \"" +
                    (executable.parent_path() / L"vibrance_liquid_dwm_extension.dll").wstring() +
                    L"\" \"" + cache.wstring() + L"\"";
                STARTUPINFOW startup { sizeof(startup) };
                startup.dwFlags = STARTF_USESTDHANDLES;
                startup.hStdInput = input;
                startup.hStdOutput = startup.hStdError = output;
                PROCESS_INFORMATION child {};
                DWORD code = 3;
                if (output != INVALID_HANDLE_VALUE && input != INVALID_HANDLE_VALUE &&
                    CreateProcessW(executable.c_str(), command.data(), nullptr, nullptr, TRUE,
                        CREATE_NO_WINDOW, nullptr, executable.parent_path().c_str(), &startup, &child))
                {
                    CloseHandle(child.hThread);
                    // A symbol server can be slow. Leave time for a download,
                    // then terminate only this broker, never the compositor.
                    if (WaitForSingleObject(child.hProcess, 300000) != WAIT_OBJECT_0)
                        TerminateProcess(child.hProcess, ERROR_TIMEOUT);
                    WaitForSingleObject(child.hProcess, 5000);
                    GetExitCodeProcess(child.hProcess, &code);
                    CloseHandle(child.hProcess);
                }
                if (input != INVALID_HANDLE_VALUE) CloseHandle(input);
                if (output != INVALID_HANDLE_VALUE) CloseHandle(output);
                std::ofstream report(log, std::ios::app);
                report << "\nBroker exit code: " << code << "; DWM process: " << pid <<
                    "; attempt: " << attempts << "\n";
                const std::wstring description = exit_description(code);
                for (const wchar_t character : description) report.put(static_cast<char>(character));
                report << "\n";
                if (code == 0) attempts = 0;
                // Offline symbol servers and temporarily denied process access
                // can recover within the same session. Retry those failures
                // slowly; bound actual load/hook failures to avoid crash loops.
                const bool transient = code == 3 || code == 4 || code == 5 || code == 8 || code == 15 || code == 16;
                if (transient) attempts = 0;
                retryAt = GetTickCount64() + (code == 16 ? 2000 : transient ? 300000 : 60000);
            }
        }
        Sleep(2000);
    }
}

inline void report_startup_failure()
{
    const auto cache = cache_directory();
    if (cache.empty()) return;
    const auto local = known_folder(FOLDERID_LocalAppData);
    bool found = false;
    for (const auto& path : { local / L"VibranceDwmSymbols" / L"launcher.log",
             cache / L"setup.log", cache / L"broker.log" })
    {
        std::ifstream log(path);
        if (!log) continue;
        found = true;
        std::wcout << L"Last automatic startup report (" << path.wstring() << L"):\n";
        std::string line;
        unsigned count = 0;
        while (count++ < 80 && std::getline(log, line))
            std::wcout << std::wstring(line.begin(), line.end()) << L'\n';
    }
    if (!found)
    {
        std::wcerr << L"No automatic startup report is available. Open Vibrance and select "
            L"Liquid Glass to configure the helper once.\n";
    }
    ComScope com;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    ComPtr<IRegisteredTask> task;
    if (SUCCEEDED(scheduler(service, folder)) &&
        SUCCEEDED(folder->GetTask(Bstr(task_name()), &task)))
    {
        TASK_STATE state {};
        LONG result = 0;
        task->get_State(&state);
        task->get_LastTaskResult(&result);
        std::wcout << L"Automatic helper task state: " << state << L"; last result: " << result << L"\n";
        if (state != TASK_STATE_RUNNING && result != 0)
            std::wcout << exit_description(static_cast<DWORD>(result)) << L"\n";
    }
}
}

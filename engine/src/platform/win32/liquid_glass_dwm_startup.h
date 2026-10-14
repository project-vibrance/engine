#pragma once

// Shared by the MSVC broker and Composition bridge; Windows only.
#include "liquid_glass_dwm_identity.h"
#include <aclapi.h>
#include <sddl.h>
#include <shlobj.h>
#include <KnownFolders.h>
#include <shellapi.h>
#include <taskschd.h>
#include <wrl/client.h>
#include <filesystem>
#include <fstream>

namespace vibrance::win32
{
using Microsoft::WRL::ComPtr;
namespace liquid_startup
{
struct Bstr
{
    BSTR value;
    explicit Bstr(const wchar_t* text) : value(SysAllocString(text)) {}
    explicit Bstr(const std::wstring& text) : Bstr(text.c_str()) {}
    ~Bstr() { SysFreeString(value); }
    Bstr(const Bstr&) = delete;
    operator BSTR() const { return value; }
};

struct ComScope
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    ~ComScope() { if (SUCCEEDED(result)) CoUninitialize(); }
};

inline std::wstring user_sid()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) return {};
    DWORD size = 0;
    GetTokenInformation(token, TokenUser, nullptr, 0, &size);
    std::vector<BYTE> bytes(size);
    LPWSTR sid = nullptr;
    if (GetTokenInformation(token, TokenUser, bytes.data(), size, &size))
        ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(bytes.data())->User.Sid, &sid);
    CloseHandle(token);
    std::wstring result = sid ? sid : L"";
    if (sid) LocalFree(sid);
    return result;
}

inline std::filesystem::path known_folder(REFKNOWNFOLDERID folder)
{
    PWSTR text = nullptr;
    if (FAILED(SHGetKnownFolderPath(folder, 0, nullptr, &text))) return {};
    std::filesystem::path result(text);
    CoTaskMemFree(text);
    return result;
}

inline std::filesystem::path cache_directory()
{
    const auto root = known_folder(FOLDERID_ProgramData);
    const auto sid = user_sid();
    return root.empty() || sid.empty() ? std::filesystem::path {} :
        root / L"VibranceLiquidGlass" / sid;
}

inline constexpr wchar_t observerExecutable[] = L"liquid_glass_dwm_observer.exe";
inline constexpr std::array<const wchar_t*, 10> bundleNames {
    L"liquid_glass_dwm_broker.exe", L"vibrance_liquid_dwm_extension.dll",
    L"liquid_glass_win32.cso", L"liquid_glass_ui_win32.cso",
    L"liquid_glass_fullscreen.cso", L"liquid_glass_blur_horizontal.cso",
    L"liquid_glass_blur_vertical.cso", L"dbghelp.dll", L"symsrv.dll", observerExecutable
};

inline std::wstring bundle_key(const std::filesystem::path& source)
{
    std::uint8_t hashes[bundleNames.size()][32] {}, digest[32] {};
    for (std::size_t index = 0; index < bundleNames.size(); ++index)
        if (!file_sha256((source / bundleNames[index]).wstring(), hashes[index])) return {};
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        return {};
    const bool ok = BCryptHash(algorithm, nullptr, 0, &hashes[0][0], sizeof(hashes),
        digest, sizeof(digest)) >= 0;
    BCryptCloseAlgorithmProvider(algorithm, 0);
    if (!ok) return {};
    std::wstring result;
    for (const auto byte : digest)
    {
        result += L"0123456789abcdef"[byte >> 4];
        result += L"0123456789abcdef"[byte & 15];
    }
    return result;
}

inline std::filesystem::path installed_directory(const std::wstring& key)
{
    const auto root = known_folder(FOLDERID_ProgramFiles);
    return root.empty() || key.empty() ? std::filesystem::path {} :
        root / L"VibranceLiquidGlass" / key;
}

// Never persist an elevated action in a user-writable installation or follow
// reparse points. Ownership also belongs to Administrators, not the user SID.
inline bool secure_directory(const std::filesystem::path& path)
{
    PSECURITY_DESCRIPTOR descriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
        L"O:BAG:BAD:P(A;OICI;FA;;;SY)(A;OICI;FA;;;BA)"
        L"(A;OICI;GRGX;;;BU)(A;OICI;GRGX;;;S-1-5-90-0)",
        SDDL_REVISION_1, &descriptor, nullptr)) return false;
    SECURITY_ATTRIBUTES attributes { sizeof(attributes), descriptor, FALSE };
    const bool created = CreateDirectoryW(path.c_str(), &attributes) != FALSE;
    const DWORD error = GetLastError();
    const DWORD flags = GetFileAttributesW(path.c_str());
    bool ok = (created || error == ERROR_ALREADY_EXISTS) &&
        flags != INVALID_FILE_ATTRIBUTES && (flags & FILE_ATTRIBUTE_DIRECTORY) &&
        !(flags & FILE_ATTRIBUTE_REPARSE_POINT);
    if (ok && !created)
    {
        PSID owner = nullptr;
        PSECURITY_DESCRIPTOR existing = nullptr;
        std::wstring name = path.wstring();
        ok = GetNamedSecurityInfoW(name.data(), SE_FILE_OBJECT, OWNER_SECURITY_INFORMATION,
            &owner, nullptr, nullptr, nullptr, &existing) == ERROR_SUCCESS;
        BYTE admin[SECURITY_MAX_SID_SIZE], system[SECURITY_MAX_SID_SIZE];
        DWORD adminSize = sizeof(admin), systemSize = sizeof(system);
        ok = ok && CreateWellKnownSid(WinBuiltinAdministratorsSid, nullptr, admin, &adminSize) &&
            CreateWellKnownSid(WinLocalSystemSid, nullptr, system, &systemSize) &&
            (EqualSid(owner, admin) || EqualSid(owner, system));
        if (existing) LocalFree(existing);
    }
    if (ok) ok = SetFileSecurityW(path.c_str(),
        OWNER_SECURITY_INFORMATION | DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION,
        descriptor) != FALSE;
    LocalFree(descriptor);
    return ok;
}

inline HRESULT scheduler(ComPtr<ITaskService>& service, ComPtr<ITaskFolder>& folder)
{
    HRESULT result = CoCreateInstance(CLSID_TaskScheduler, nullptr, CLSCTX_INPROC_SERVER,
        IID_PPV_ARGS(&service));
    VARIANT empty {};
    if (SUCCEEDED(result)) result = service->Connect(empty, empty, empty, empty);
    if (SUCCEEDED(result)) result = service->GetFolder(Bstr(L"\\"), &folder);
    return result;
}

inline std::wstring task_name()
{
    return L"Vibrance Liquid Glass " + user_sid();
}

inline HRESULT run_task(const std::filesystem::path& expectedDirectory = {})
{
    ComScope com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) return com.result;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    HRESULT result = scheduler(service, folder);
    ComPtr<IRegisteredTask> task;
    if (SUCCEEDED(result)) result = folder->GetTask(Bstr(task_name()), &task);
    if (FAILED(result)) return result;
    if (!expectedDirectory.empty())
    {
        ComPtr<ITaskDefinition> definition;
        ComPtr<IActionCollection> actions;
        ComPtr<IAction> action;
        ComPtr<IExecAction> exec;
        if (FAILED(task->get_Definition(&definition)) ||
            FAILED(definition->get_Actions(&actions)) || FAILED(actions->get_Item(1, &action)) ||
            FAILED(action.As(&exec))) return E_FAIL;
        BSTR path = nullptr, arguments = nullptr;
        const HRESULT pathResult = exec->get_Path(&path);
        const HRESULT argsResult = exec->get_Arguments(&arguments);
        const auto expected = expectedDirectory / observerExecutable;
        const bool matches = SUCCEEDED(pathResult) && SUCCEEDED(argsResult) && path && arguments &&
            _wcsicmp(path, expected.c_str()) == 0 && wcscmp(arguments, L"--watch") == 0;
        SysFreeString(path);
        SysFreeString(arguments);
        if (!matches) return HRESULT_FROM_WIN32(ERROR_REVISION_MISMATCH);
    }
    TASK_STATE state {};
    if (SUCCEEDED(task->get_State(&state)) && state == TASK_STATE_RUNNING) return S_OK;
    ComPtr<IRunningTask> running;
    VARIANT empty {};
    DWORD session = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &session)) return HRESULT_FROM_WIN32(GetLastError());
    return task->RunEx(empty, TASK_RUN_USE_SESSION_ID, session, Bstr(L""), &running);
}

inline HRESULT make_task_definition(ITaskService* service,
    const std::filesystem::path& directory, ComPtr<ITaskDefinition>& definition)
{
    HRESULT result = S_OK;
    if (FAILED(result = service->NewTask(0, &definition))) return result;
    ComPtr<IPrincipal> principal;
    ComPtr<ITaskSettings> settings;
    ComPtr<ITriggerCollection> triggers;
    ComPtr<ITrigger> trigger;
    ComPtr<ILogonTrigger> logon;
    ComPtr<IActionCollection> actions;
    ComPtr<IAction> action;
    ComPtr<IExecAction> exec;
    const auto sid = user_sid();
    if (sid.empty()) return E_FAIL;
    if (FAILED(result = definition->get_Principal(&principal)) ||
        FAILED(result = principal->put_UserId(Bstr(sid))) ||
        FAILED(result = principal->put_LogonType(TASK_LOGON_INTERACTIVE_TOKEN)) ||
        FAILED(result = principal->put_RunLevel(TASK_RUNLEVEL_HIGHEST)) ||
        FAILED(result = definition->get_Settings(&settings)) ||
        FAILED(result = settings->put_DisallowStartIfOnBatteries(VARIANT_FALSE)) ||
        FAILED(result = settings->put_StopIfGoingOnBatteries(VARIANT_FALSE)) ||
        FAILED(result = settings->put_ExecutionTimeLimit(Bstr(L"PT0S"))) ||
        FAILED(result = settings->put_MultipleInstances(TASK_INSTANCES_IGNORE_NEW)) ||
        FAILED(result = settings->put_StartWhenAvailable(VARIANT_TRUE)) ||
        FAILED(result = definition->get_Triggers(&triggers)) ||
        FAILED(result = triggers->Create(TASK_TRIGGER_LOGON, &trigger)) ||
        FAILED(result = trigger.As(&logon)) ||
        FAILED(result = logon->put_UserId(Bstr(sid))) ||
        FAILED(result = logon->put_Delay(Bstr(L"PT30S"))) ||
        FAILED(result = definition->get_Actions(&actions)) ||
        FAILED(result = actions->Create(TASK_ACTION_EXEC, &action)) ||
        FAILED(result = action.As(&exec)) ||
        FAILED(result = exec->put_Path(Bstr((directory / observerExecutable).wstring()))) ||
        FAILED(result = exec->put_Arguments(Bstr(L"--watch"))) ||
        FAILED(result = exec->put_WorkingDirectory(Bstr(directory.wstring())))) return result;
    return S_OK;
}

inline HRESULT register_task(const std::filesystem::path& directory)
{
    ComScope com;
    if (FAILED(com.result) && com.result != RPC_E_CHANGED_MODE) return com.result;
    ComPtr<ITaskService> service;
    ComPtr<ITaskFolder> folder;
    HRESULT result = scheduler(service, folder);
    if (FAILED(result)) return result;
    ComPtr<ITaskDefinition> definition;
    if (FAILED(result = make_task_definition(service.Get(), directory, definition))) return result;
    const auto sid = user_sid();
    // The ordinary token can read/run the task, but cannot edit its elevated action.
    Bstr security(L"O:BAG:BAD:P(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGX;;;" + sid + L")");
    VARIANT user {}, empty {}, descriptor {};
    Bstr userId(sid);
    user.vt = VT_BSTR;
    user.bstrVal = userId;
    descriptor.vt = VT_BSTR;
    descriptor.bstrVal = security;
    ComPtr<IRegisteredTask> registered;
    // Stop the old observer so an updated task can start its new broker.
    if (SUCCEEDED(folder->GetTask(Bstr(task_name()), &registered)))
    {
        TASK_STATE state {};
        registered->get_State(&state);
        if ((state == TASK_STATE_RUNNING || state == TASK_STATE_QUEUED) &&
            FAILED(result = registered->Stop(0))) return result;
        for (unsigned attempt = 0; attempt < 50; ++attempt)
        {
            if (FAILED(registered->get_State(&state)) || state != TASK_STATE_RUNNING) break;
            Sleep(100);
        }
        if (state == TASK_STATE_RUNNING) return HRESULT_FROM_WIN32(ERROR_BUSY);
    }
    registered.Reset();
    result = folder->RegisterTaskDefinition(Bstr(task_name()), definition.Get(),
        TASK_CREATE_OR_UPDATE | TASK_DONT_ADD_PRINCIPAL_ACE, user, empty,
        TASK_LOGON_INTERACTIVE_TOKEN, descriptor, &registered);
    return FAILED(result) ? result : run_task(directory);
}

inline bool fullscreen_active()
{
    QUERY_USER_NOTIFICATION_STATE state {};
    if (SUCCEEDED(SHQueryUserNotificationState(&state)) &&
        (state == QUNS_RUNNING_D3D_FULL_SCREEN || state == QUNS_PRESENTATION_MODE)) return true;
    const HWND foreground = GetForegroundWindow();
    if (!foreground) return true; // Includes desktop and display-mode transitions.
    wchar_t windowClass[64] {};
    GetClassNameW(foreground, windowClass, ARRAYSIZE(windowClass));
    if (wcscmp(windowClass, L"Progman") == 0 || wcscmp(windowClass, L"WorkerW") == 0)
        return false;
    RECT bounds {};
    MONITORINFO display { sizeof(display) };
    return GetWindowRect(foreground, &bounds) &&
        GetMonitorInfoW(MonitorFromWindow(foreground, MONITOR_DEFAULTTONEAREST), &display) &&
        bounds.left <= display.rcMonitor.left + 2 && bounds.top <= display.rcMonitor.top + 2 &&
        bounds.right >= display.rcMonitor.right - 2 && bounds.bottom >= display.rcMonitor.bottom - 2;
}
}
}

// Resolves the running DWM's private entry points from its matching PDB,
// fingerprints the module, and starts the optional lens extension.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#include <tlhelp32.h>

#include "../../engine/src/platform/win32/liquid_glass_dwm_identity.h"
#include "../../engine/src/platform/win32/liquid_glass_dwm_protocol.h"
#include "../../engine/src/platform/win32/liquid_glass_dwm_security.h"
#include "../../engine/src/platform/win32/liquid_glass_dwm_windows10_layout.h"
#include "liquid_glass_runtime_cache.h"
#include "liquid_glass_broker_startup.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <iostream>
#include <string>

namespace
{
using vibrance::win32::LiquidDwmHookConfig;
using vibrance::win32::LiquidDwmWindows10Config;

DWORD windows_build_number()
{
    using GetVersionFn = LONG (WINAPI*)(OSVERSIONINFOW*);
    const auto getVersion = reinterpret_cast<GetVersionFn>(GetProcAddress(
        GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version { sizeof(version) };
    return getVersion && getVersion(&version) == 0 ? version.dwBuildNumber : 0u;
}

bool native_hooks_allowed()
{
#if defined(_M_ARM64)
    constexpr std::uint32_t machine = IMAGE_FILE_MACHINE_ARM64;
#else
    constexpr std::uint32_t machine = IMAGE_FILE_MACHINE_AMD64;
#endif
    USHORT processMachine = 0, nativeMachine = 0;
    return IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine) &&
        nativeMachine == machine &&
        vibrance::win32::liquid_dwm_native_hooks_allowed(windows_build_number(), machine);
}

struct MappingHandle
{
    HANDLE value = nullptr;
    ~MappingHandle() { if (value) CloseHandle(value); }
};

bool enable_debug_privilege()
{
    HANDLE token = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(),
            TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
    {
        return false;
    }
    LUID privilege {};
    TOKEN_PRIVILEGES adjustment {};
    bool enabled = LookupPrivilegeValueW(nullptr, L"SeDebugPrivilege",
        &privilege) != 0;
    if (enabled)
    {
        adjustment.PrivilegeCount = 1;
        adjustment.Privileges[0].Luid = privilege;
        adjustment.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
        SetLastError(ERROR_SUCCESS);
        enabled = AdjustTokenPrivileges(token, FALSE, &adjustment, 0,
            nullptr, nullptr) != 0 && GetLastError() == ERROR_SUCCESS;
    }
    CloseHandle(token);
    return enabled;
}

DWORD find_session_dwm()
{
    DWORD currentSession = 0;
    if (!ProcessIdToSessionId(GetCurrentProcessId(), &currentSession))
    {
        std::wcerr << L"Current session lookup failed: " <<
            GetLastError() << L"\n";
        return 0;
    }
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        std::wcerr << L"Process snapshot failed: " << GetLastError() <<
            L"\n";
        return 0;
    }
    PROCESSENTRY32W entry { sizeof(entry) };
    DWORD found = 0;
    DWORD inaccessible = 0;
    unsigned inaccessibleCount = 0;
    if (Process32FirstW(snapshot, &entry))
    {
        do
        {
            DWORD session = 0;
            if (_wcsicmp(entry.szExeFile, L"dwm.exe") == 0)
            {
                const BOOL valid = ProcessIdToSessionId(
                    entry.th32ProcessID, &session);
                if (valid && session == currentSession)
                {
                    found = entry.th32ProcessID;
                    break;
                }
                if (!valid)
                {
                    inaccessible = entry.th32ProcessID;
                    ++inaccessibleCount;
                }
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    // A standard user token can be denied ProcessIdToSessionId for DWM.
    // One candidate is unambiguous; OpenProcess still enforces access rights.
    return found ? found : inaccessibleCount == 1 ? inaccessible : 0;
}

bool resolve_symbol(HANDLE process, const char* name, DWORD64 base,
                    std::uint64_t& rva)
{
    std::array<char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> bytes {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(bytes.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    if (!SymFromName(process, name, symbol) ||
        symbol->Address < base)
    {
        return false;
    }
    rva = symbol->Address - base;
    return true;
}

bool resolve_config(const std::wstring& symbolCache,
                    LiquidDwmHookConfig& config)
{
    wchar_t systemDirectory[MAX_PATH] {};
    if (!GetSystemDirectoryW(systemDirectory, MAX_PATH))
    {
        return false;
    }
    const std::wstring path = std::wstring(systemDirectory) +
        L"\\dwmcore.dll";
    if (!vibrance::win32::file_sha256(path,
            config.dwmcoreSha256))
    {
        std::wcerr << L"Cannot hash " << path << L"\n";
        return false;
    }
#if defined(_M_ARM64)
    config.moduleMachine = IMAGE_FILE_MACHINE_ARM64;
#else
    config.moduleMachine = IMAGE_FILE_MACHINE_AMD64;
#endif
    const std::wstring searchPath = symbolCache + L";srv*" + symbolCache +
        L"*https://msdl.microsoft.com/download/symbols";
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS);
    if (!SymInitializeW(process, searchPath.c_str(), FALSE))
    {
        std::wcerr << L"SymInitialize failed: " << GetLastError() << L"\n";
        return false;
    }
    const DWORD64 base = SymLoadModuleExW(process, nullptr,
        path.c_str(), L"dwmcore", 0, 0, nullptr, 0);
    if (!base)
    {
        std::wcerr << L"SymLoadModuleEx failed: " << GetLastError() << L"\n";
    }
    const bool present = base && resolve_symbol(process,
            "dwmcore!COverlayContext::Present", base,
            config.presentRva);
    const bool display = base && resolve_symbol(process,
            "dwmcore!??_7CDDisplaySwapChain@@6BIDeviceResource@@@", base,
            config.displaySwapchainVtableRva) &&
        resolve_symbol(process,
            "dwmcore!?GetPhysicalBackBuffer@CDDisplaySwapChain@@UEBAPEAVISwapChainBuffer@@XZ",
            base, config.displayPhysicalRva) &&
        resolve_symbol(process,
            "dwmcore!??_7CDDisplaySwapChainBuffer@@6B@", base,
            config.displayBufferVtableRva) &&
        resolve_symbol(process,
            "dwmcore!?GetD3D11Resource@CDDisplaySwapChainBuffer@@UEAAPEAUID3D11Resource@@XZ",
            base, config.displayResourceRva);
    if (base && (!present || !display))
    {
        std::wcerr << L"DWM symbols: present=" << present <<
            L", display=" << display << L", error=" << GetLastError() <<
            L"\n";
    }
    if (base)
    {
        (void)resolve_symbol(process,
            "dwmcore!??_7CLegacySwapChain@@6B@", base,
            config.legacySwapchainVtableRva);
        (void)resolve_symbol(process,
            "dwmcore!?GetPhysicalBackBuffer@CLegacySwapChain@@UEBAPEAVISwapChainBuffer@@XZ",
            base, config.legacyPhysicalRva);
        (void)resolve_symbol(process,
            "dwmcore!??_7CLegacySwapChainBuffer@@6B@", base,
            config.legacyBufferVtableRva);
        (void)resolve_symbol(process,
            "dwmcore!?GetD3D11Resource@CLegacySwapChainBuffer@@UEAAPEAUID3D11Resource@@XZ",
            base, config.legacyResourceRva);
        (void)resolve_symbol(process,
            "dwmcore!?RenderContent@CWindowNode@@UEAAJPEAVCDrawingContext@@PEA_N@Z",
            base, config.windowRenderRva);
        (void)resolve_symbol(process,
            "dwmcore!?GetHwnd@CWindowNode@@MEBAPEAUHWND__@@XZ",
            base, config.windowGetHwndRva);
        (void)resolve_symbol(process,
            "dwmcore!?FlushD2D@CDrawingContext@@QEAAJXZ",
            base, config.drawingFlushRva);
        (void)resolve_symbol(process,
            "dwmcore!?Draw@CContent@@UEAAJPEAVCDrawingContext@@AEBUD2D_SIZE_F@@PEAVCDrawListCache@@@Z",
            base, config.contentDrawRva);
        (void)resolve_symbol(process,
            "dwmcore!??_7CColorBrush@@6BCContent@@@",
            base, config.colourBrushVtableRva);
        (void)resolve_symbol(process,
            "dwmcore!?ApplyRenderStateInternal@CDrawingContext@@AEAAJ_N@Z",
            base, config.applyRenderStateRva);
        (void)resolve_symbol(process, "dwmcore!?Render@CDrawListEntry@@QEAAJPEAVCDrawingContext@@AEBVCMILMatrix@@MW4Enum@BlendMode@@@Z",
            base, config.drawListEntryRenderRva);
        (void)resolve_symbol(process, "dwmcore!?EmitGeometry@CRenderingBatchCommand@@QEBAJPEAVCD3DBatchExecutionContext@@_N@Z",
            base, config.batchCommandEmitRva);
        (void)resolve_symbol(process, "dwmcore!?EmitGeometry@CCommonRenderingEffect@@UEBAJPEAVCD3DBatchExecutionContext@@PEBVCRenderingBatchCommand@@@Z",
            base, config.commonEffectEmitRva);
        (void)resolve_symbol(process, "dwmcore!?EmitGeometry@CBrushRenderingEffect@@UEBAJPEAVCD3DBatchExecutionContext@@PEBVCRenderingBatchCommand@@@Z",
            base, config.brushEffectEmitRva);
        (void)resolve_symbol(process, "dwmcore!?Flush@CD3DBatchExecutionContext@@QEAAXW4FlushReason@@@Z",
            base, config.batchFlushRva);
        (void)resolve_symbol(process, "dwmcore!??_7CDrawListEntry@@6B@",
            base, config.drawListEntryVtableRva);
        (void)resolve_symbol(process,
            "dwmcore!?Draw@CBrush@@UEAAJPEAVCDrawingContext@@AEBUD2D_SIZE_F@@PEAVCDrawListCache@@@Z",
            base, config.brushDrawRva);
        (void)resolve_symbol(process,
            "dwmcore!?RenderTargetDirty@CCaptureRenderTarget@@MEAAJPEAVCDrawingContext@@PEA_N@Z",
            base, config.captureRenderRva);
        (void)resolve_symbol(process,
            "dwmcore!?RenderTargetDirty@COffScreenRenderTarget@@MEAAJPEAVCDrawingContext@@PEA_N@Z",
            base, config.offscreenRenderRva);
        (void)resolve_symbol(process,
            "dwmcore!?Present@CCaptureRenderTarget@@MEAAJXZ",
            base, config.capturePresentRva);
        (void)resolve_symbol(process,
            "dwmcore!?GetCurrentFrameBuffer@CCaptureRenderTarget@@IEAAPEAUIDXGIResource@@XZ",
            base, config.captureCurrentFrameBufferRva);
        (void)resolve_symbol(process,
            "dwmcore!?Render@CDDARenderTarget@@IEAAJPEAVCDrawingContext@@@Z",
            base, config.ddaRenderRva);
        (void)resolve_symbol(process,
            "dwmcore!?Present@CDDARenderTarget@@IEAAJXZ",
            base, config.ddaPresentRva);
        (void)resolve_symbol(process,
            "dwmcore!?GetBackBuffer@CDDASwapChain@@UEBAPEAVIDeviceTarget@@XZ",
            base, config.ddaGetBackBufferRva);
        (void)resolve_symbol(process,
            "dwmcore!?GetTexture2D@CDeviceTextureTarget@@UEAAPEAUID3D11Texture2D@@XZ",
            base, config.ddaGetTexture2DRva);
        (void)resolve_symbol(process,
            "dwmcore!??_7CDDASwapChain@@6B@",
            base, config.ddaSwapchainVtableRva);
        (void)resolve_symbol(process,
            "dwmcore!??_7CDeviceTextureTarget@@6BIDeviceResource@@IDeviceTarget@@@",
            base, config.ddaTargetVtableRva);
    }
    const bool resolved = present && display && config.windowRenderRva &&
        config.windowGetHwndRva && config.drawingFlushRva &&
        config.contentDrawRva && config.colourBrushVtableRva &&
        config.applyRenderStateRva && config.drawListEntryRenderRva &&
        config.batchCommandEmitRva && config.commonEffectEmitRva &&
        config.brushEffectEmitRva && config.batchFlushRva &&
        config.drawListEntryVtableRva && config.brushDrawRva;
    SymCleanup(process);
    return resolved;
}

bool resolve_windows10_config(const std::wstring& path, const std::wstring& symbolCache,
    LiquidDwmHookConfig& config, LiquidDwmWindows10Config& layout)
{
    // Separate resolution avoids treating Windows 10 entry pointers as
    // Windows 11 rendering-command pointers. No Windows 11 symbols are changed.
    if (!vibrance::win32::file_sha256(path, config.dwmcoreSha256)) return false;
    config.moduleMachine = IMAGE_FILE_MACHINE_AMD64;
    const HANDLE process = GetCurrentProcess();
    const auto searchPath = symbolCache + L";srv*" + symbolCache + L"*https://msdl.microsoft.com/download/symbols";
    SymSetOptions(SYMOPT_DEFERRED_LOADS);
    if (!SymInitializeW(process, searchPath.c_str(), FALSE)) return false;
    const DWORD64 base = SymLoadModuleExW(process, nullptr, path.c_str(), L"dwmcore", 0, 0, nullptr, 0);
    const auto resolve = [&](const char* name, std::uint64_t& rva) {
        return base && resolve_symbol(process, name, base, rva);
    };
    const bool resolved =
        resolve("dwmcore!?Draw@CBrush@@UEAAJPEAVCDrawingContext@@AEBUD2D_SIZE_F@@PEAVCDrawListCache@@@Z", config.brushDrawRva) &&
        resolve("dwmcore!??_7CColorBrush@@6BIUnknown@@@", config.colourBrushVtableRva) &&
        resolve("dwmcore!?Render@CHWDrawListEntry@@UEAAJPEAVCDrawingContext@@AEBVCMILMatrix@@MW4Enum@MilCompositingMode@@@Z", config.drawListEntryRenderRva) &&
        resolve("dwmcore!??_7CHWDrawListEntry@@6BCBaseDrawListEntry@@@", config.drawListEntryVtableRva) &&
        resolve("dwmcore!?EmitGeometry@CCommonRenderingEffect@@UEBAJPEAVCD3DBatchExecutionContext@@PEBVCBaseDrawListEntry@@@Z", config.commonEffectEmitRva) &&
        resolve("dwmcore!?EmitGeometry@CBrushRenderingEffect@@UEBAJPEAVCD3DBatchExecutionContext@@PEBVCBaseDrawListEntry@@@Z", config.brushEffectEmitRva) &&
        resolve("dwmcore!?Flush@CD3DBatchExecutionContext@@QEAAXW4FlushReason@@@Z", config.batchFlushRva) &&
        resolve("dwmcore!?SetLocalToWorld@CD3DBatchExecutionContext@@QEAAXAEBVCMILMatrix@@@Z", layout.setLocalToWorldRva) &&
        resolve("dwmcore!?GetBrushParameters@CColorBrush@@UEBAJPEAVCBrushDrawListGenerator@@@Z", layout.brushParametersRva) &&
        resolve("dwmcore!??0CColorBrush@@QEAA@PEAVCComposition@@AEBU_D3DCOLORVALUE@@@Z", layout.colourConstructorRva) &&
        resolve("dwmcore!?GetBounds@CHWDrawListEntry@@UEBA?AUD2D_RECT_F@@XZ", layout.entryBoundsRva) &&
        resolve("dwmcore!?GetRenderingEffect@CHWDrawListEntry@@UEBAPEAVCRenderingEffect@@XZ", layout.entryEffectRva) &&
        resolve("dwmcore!?EmitGeometryImpl@CRenderingEffect@@KAJPEAVCD3DBatchExecutionContext@@PEBVCHWDrawListEntry@@_N@Z", layout.emitGeometryImplRva);
    SymCleanup(process);
    if (!resolved) return false;
    std::memcpy(layout.dwmcoreSha256, config.dwmcoreSha256, 32u);
    MappingHandle file { CreateFileW(path.c_str(), GENERIC_READ,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr) };
    if (file.value == INVALID_HANDLE_VALUE) { file.value = nullptr; return false; }
    MappingHandle mapping { CreateFileMappingW(file.value, nullptr,
        PAGE_READONLY | SEC_IMAGE_NO_EXECUTE, 0, 0, nullptr) };
    if (!mapping.value) return false;
    const auto* image = static_cast<const std::uint8_t*>(MapViewOfFile(mapping.value, FILE_MAP_READ, 0, 0, 0));
    if (!image) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(image + dos->e_lfanew);
    const bool valid = nt->FileHeader.Machine == IMAGE_FILE_MACHINE_AMD64 &&
        vibrance::win32::validate_liquid_windows10_layout({ image, nt->OptionalHeader.SizeOfImage },
            layout, config.commonEffectEmitRva, config.brushDrawRva, config.drawListEntryRenderRva);
    UnmapViewOfFile(image);
    return valid;
}

std::uintptr_t remote_module_base(DWORD pid, const wchar_t* name)
{
    const HANDLE snapshot = CreateToolhelp32Snapshot(
        TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
    {
        return 0;
    }
    MODULEENTRY32W entry { sizeof(entry) };
    std::uintptr_t base = 0;
    if (Module32FirstW(snapshot, &entry))
    {
        do
        {
            if (_wcsicmp(entry.szModule, name) == 0)
            {
                base = reinterpret_cast<std::uintptr_t>(entry.modBaseAddr);
                break;
            }
        } while (Module32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return base;
}

bool load_remote(HANDLE process, DWORD pid,
                 const std::wstring& extension)
{
    const auto localLoad = reinterpret_cast<std::uintptr_t>(
        GetProcAddress(GetModuleHandleW(L"kernel32.dll"),
            "LoadLibraryW"));
    HMODULE localModule = nullptr;
    if (!localLoad || !GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(localLoad), &localModule))
    {
        return false;
    }
    const std::wstring module = vibrance::win32::module_path(localModule);
    const auto separator = module.find_last_of(L"\\/");
    if (separator == std::wstring::npos)
    {
        return false;
    }
    const auto remoteModule = remote_module_base(pid,
        module.c_str() + separator + 1);
    if (!remoteModule)
    {
        return false;
    }
    const auto remoteLoad = remoteModule + localLoad -
        reinterpret_cast<std::uintptr_t>(localModule);
    const SIZE_T bytes = (extension.size() + 1) * sizeof(wchar_t);
    void* remotePath = VirtualAllocEx(process, nullptr, bytes,
        MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!remotePath)
    {
        return false;
    }
    SIZE_T written = 0;
    bool success = WriteProcessMemory(process, remotePath,
        extension.c_str(), bytes, &written) && written == bytes;
    HANDLE thread = nullptr;
    if (success)
    {
        thread = CreateRemoteThread(process, nullptr, 0,
            reinterpret_cast<LPTHREAD_START_ROUTINE>(remoteLoad),
            remotePath, 0, nullptr);
        success = thread != nullptr;
    }
    if (thread)
    {
        const DWORD wait = WaitForSingleObject(thread, 15000);
        // The loader may still be reading this path. Freeing it after a timeout
        // can crash DWM. Its process owns the allocation until it exits.
        if (wait != WAIT_OBJECT_0)
        {
            CloseHandle(thread);
            std::wcerr << L"DWM loader did not complete (wait result " << wait << L").\n";
            return false;
        }
        DWORD result = 0;
        success = GetExitCodeThread(thread, &result) && result != 0 &&
            success;
        if (!success) std::wcerr << L"DWM LoadLibrary returned " << result << L".\n";
        CloseHandle(thread);
    }
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    return success;
}
}

int wmain(int argc, wchar_t** argv)
{
    // A GUI subsystem prevents Task Scheduler from opening a console (and
    // possibly changing focus) for the observer. Manual commands still attach
    // to their caller's console; redirected diagnostic/test pipes are retained.
    if (!(argc == 2 && _wcsicmp(argv[1], L"--watch") == 0))
    {
        const auto missing = [](DWORD stream) {
            const HANDLE handle = GetStdHandle(stream);
            return !handle || handle == INVALID_HANDLE_VALUE;
        };
        const bool missingOutput = missing(STD_OUTPUT_HANDLE);
        const bool missingError = missing(STD_ERROR_HANDLE);
        if ((missingOutput || missingError) && AttachConsole(ATTACH_PARENT_PROCESS))
        {
            FILE* stream = nullptr;
            if (missingOutput) _wfreopen_s(&stream, L"CONOUT$", L"w", stdout);
            if (missingError) _wfreopen_s(&stream, L"CONOUT$", L"w", stderr);
        }
    }
    namespace startup = vibrance::win32::liquid_startup;
    if (argc == 3 && _wcsicmp(argv[1], L"--bundle-key") == 0)
    {
        const auto key = startup::bundle_key(argv[2]);
        if (key.empty()) return 3;
        std::wcout << key << L"\n";
        return 0;
    }
    if (argc == 2 && _wcsicmp(argv[1], L"--auto-config") == 0)
    {
        // Read-only diagnostic: generate the real scheduler definition without
        // registering it or starting a broker, useful on managed devices.
        startup::ComScope com;
        vibrance::win32::ComPtr<ITaskService> service;
        vibrance::win32::ComPtr<ITaskFolder> folder;
        HRESULT result = startup::scheduler(service, folder);
        vibrance::win32::ComPtr<ITaskDefinition> definition;
        const auto source = std::filesystem::path(vibrance::win32::module_path(nullptr)).parent_path();
        const auto directory = startup::installed_directory(startup::bundle_key(source));
        if (directory.empty()) return 3;
        if (SUCCEEDED(result)) result = startup::make_task_definition(service.Get(), directory, definition);
        BSTR xml = nullptr;
        if (SUCCEEDED(result)) result = definition->get_XmlText(&xml);
        if (xml) { std::wcout << xml << L"\n"; SysFreeString(xml); }
        if (FAILED(result)) std::wcerr << L"Cannot inspect automatic startup configuration (HRESULT 0x" <<
            std::hex << static_cast<unsigned long>(result) << L").\n";
        return SUCCEEDED(result) ? 0 : 14;
    }
    if (argc == 3 && _wcsicmp(argv[1], L"--install-auto-run") == 0)
    {
        if (!native_hooks_allowed()) return 23;
        return startup::install_helper(argv[2]);
    }
    if (argc == 2 && _wcsicmp(argv[1], L"--watch") == 0)
    {
        const auto directory = std::filesystem::path(vibrance::win32::module_path(nullptr)).parent_path();
        if (!native_hooks_allowed() || !enable_debug_privilege()) return 11;
        if (directory != startup::installed_directory(startup::bundle_key(directory))) return 3;
        return startup::watch_session(&find_session_dwm);
    }
    if (argc == 2 && _wcsicmp(argv[1], L"--run-auto") == 0)
    {
        const HRESULT result = startup::run_task();
        if (FAILED(result)) std::wcerr << L"Automatic helper is unavailable (HRESULT 0x" <<
            std::hex << static_cast<unsigned long>(result) << L").\n";
        return SUCCEEDED(result) ? 0 : 14;
    }
    if (argc == 2 && _wcsicmp(argv[1], L"--remove-auto-run") == 0)
    {
        startup::ComScope com;
        vibrance::win32::ComPtr<ITaskService> service;
        vibrance::win32::ComPtr<ITaskFolder> folder;
        HRESULT result = startup::scheduler(service, folder);
        vibrance::win32::ComPtr<IRegisteredTask> task;
        if (SUCCEEDED(result) && SUCCEEDED(folder->GetTask(startup::Bstr(startup::task_name()), &task)))
            task->Stop(0);
        if (SUCCEEDED(result)) result = folder->DeleteTask(startup::Bstr(startup::task_name()), 0);
        return SUCCEEDED(result) || result == HRESULT_FROM_WIN32(ERROR_FILE_NOT_FOUND) ? 0 : 14;
    }
    if (argc == 4 && _wcsicmp(argv[1], L"--resolve-windows10") == 0)
    {
        LiquidDwmHookConfig config {};
        LiquidDwmWindows10Config layout {};
        if (!resolve_windows10_config(argv[2], argv[3], config, layout))
        {
            std::wcerr << L"Windows 10 22H2 DWM symbols or object layout are unsupported\n";
            return 4;
        }
        std::wcout << L"Validated Windows 10 22H2 x64 entry pipeline; brush=0x" << std::hex <<
            config.brushDrawRva << L" entry=0x" << config.drawListEntryRenderRva <<
            L" common=0x" << config.commonEffectEmitRva << L" matrix=0x" << layout.setLocalToWorldRva << L"\n";
        return 0;
    }
    if (argc == 4 && _wcsicmp(argv[1], L"--symbol-rva") == 0)
    {
        wchar_t systemDirectory[MAX_PATH] {};
        if (!GetSystemDirectoryW(systemDirectory, MAX_PATH)) return 20;
        const std::wstring path = std::wstring(systemDirectory) +
            L"\\dwmcore.dll";
        const std::wstring cache = std::wstring(argv[2]) + L";srv*" +
            argv[2] + L"*https://msdl.microsoft.com/download/symbols";
        const HANDLE process = GetCurrentProcess();
        if (!SymInitializeW(process, cache.c_str(), FALSE)) return 21;
        const DWORD64 base = SymLoadModuleExW(process, nullptr,
            path.c_str(), L"dwmcore", 0, 0, nullptr, 0);
        std::uint64_t rva = 0;
        std::string name = "dwmcore!";
        for (const wchar_t* character = argv[3]; *character; ++character)
        {
            if (*character > 0x7f)
            {
                SymCleanup(process);
                return 22;
            }
            name.push_back(static_cast<char>(*character));
        }
        const bool found = base && resolve_symbol(process,
            name.c_str(), base, rva);
        if (found) std::cout << "0x" << std::hex << rva << "\n";
        SymCleanup(process);
        return found ? 0 : 22;
    }
    if (argc == 4 && _wcsicmp(argv[1], L"--symbol-at") == 0)
    {
        wchar_t systemDirectory[MAX_PATH] {};
        if (!GetSystemDirectoryW(systemDirectory, MAX_PATH)) return 20;
        const std::wstring path = std::wstring(systemDirectory) +
            L"\\dwmcore.dll";
        const std::wstring cache = std::wstring(argv[2]) + L";srv*" +
            argv[2] + L"*https://msdl.microsoft.com/download/symbols";
        const HANDLE process = GetCurrentProcess();
        if (!SymInitializeW(process, cache.c_str(), FALSE)) return 21;
        const DWORD64 base = SymLoadModuleExW(process, nullptr,
            path.c_str(), L"dwmcore", 0, 0, nullptr, 0);
        const DWORD64 address = base + _wcstoui64(argv[3], nullptr, 0);
        std::array<char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> bytes {};
        auto* symbol = reinterpret_cast<SYMBOL_INFO*>(bytes.data());
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = MAX_SYM_NAME;
        DWORD64 displacement = 0;
        const bool found = base && SymFromAddr(process,
            address, &displacement, symbol);
        if (found)
            std::cout << symbol->Name << "+0x" << std::hex <<
                displacement << "\n";
        SymCleanup(process);
        return found ? 0 : 22;
    }
    if (argc == 3 && _wcsicmp(argv[1], L"--resolve-only") == 0)
    {
        LiquidDwmHookConfig config {};
        if (!resolve_config(argv[2], config)) return 19;
        std::wcout << L"Present RVA=0x" << std::hex << config.presentRva
            << L" display vtable=0x" << config.displaySwapchainVtableRva
            << L" physical=0x" << config.displayPhysicalRva
            << L" buffer vtable=0x" << config.displayBufferVtableRva
            << L" resource=0x" << config.displayResourceRva
            << L" window render=0x" << config.windowRenderRva
            << L" window HWND=0x" << config.windowGetHwndRva
            << L" drawing flush=0x" << config.drawingFlushRva
            << L" brush draw=0x" << config.contentDrawRva
            << L" brush fill=0x" << config.colourBrushVtableRva
            << L" native command=0x" << config.batchCommandEmitRva
            << L" native common=0x" << config.commonEffectEmitRva
            << L" native brush=0x" << config.brushEffectEmitRva
            << L" native flush=0x" << config.batchFlushRva
            << L" capture present=0x" << config.capturePresentRva
            << L" capture buffer=0x" <<
                config.captureCurrentFrameBufferRva
            << L" DDA render=0x" << config.ddaRenderRva
            << L" DDA present=0x" << config.ddaPresentRva
            << L" DDA back buffer=0x" << config.ddaGetBackBufferRva
            << L" DDA texture=0x" << config.ddaGetTexture2DRva
            << L" DDA swapchain vtable=0x" << config.ddaSwapchainVtableRva
            << L" DDA target vtable=0x" << config.ddaTargetVtableRva
            << L"\n";
        return 0;
    }
    if (argc == 2 && _wcsicmp(argv[1], L"--present-status") == 0)
    {
        const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE,
            L"Local\\VibranceDwmPresentProbeV4");
        if (!mapping)
        {
            std::wcerr << L"No present probe mapping: " << GetLastError() << L"\n";
            return 15;
        }
        const auto* values = static_cast<const LONG*>(MapViewOfFile(mapping,
            FILE_MAP_READ, 0, 0, 10 * sizeof(LONG)));
        if (!values)
        {
            CloseHandle(mapping);
            return 15;
        }
        std::wcout << L"stage=" << values[0] << L" calls=" << values[1]
            << L" vtableRva=0x" << std::hex << values[2] << std::dec
            << L" error=" << values[3] << L" bufferVtableRva=0x"
            << std::hex << values[4] << std::dec << L" texture="
            << values[5] << L"x" << values[6] << L" format="
            << values[7] << L" bindFlags=0x" << std::hex <<
            values[8] << std::dec << L" rtvResult=" << values[9] << L"\n";
        UnmapViewOfFile(values);
        CloseHandle(mapping);
        return 0;
    }
    if (argc == 3 && _wcsicmp(argv[1], L"--load-only") == 0)
    {
        if (!native_hooks_allowed())
        {
            std::wcerr << L"Native Liquid Glass hooks are unavailable on this Windows build or architecture\n";
            return 23;
        }
        const std::wstring extension = argv[2];
        if (GetFileAttributesW(extension.c_str()) == INVALID_FILE_ATTRIBUTES ||
            !enable_debug_privilege())
        {
            std::wcerr << L"Extension unavailable or debug privilege denied\n";
            return 16;
        }
        const DWORD pid = find_session_dwm();
        const HANDLE dwm = pid ? OpenProcess(PROCESS_CREATE_THREAD |
            PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
            PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid) : nullptr;
        if (!dwm)
        {
            std::wcerr << L"Cannot open DWM: " << GetLastError() << L"\n";
            return 17;
        }
        const bool loaded = load_remote(dwm, pid, extension);
        CloseHandle(dwm);
        std::wcout << L"Probe load=" << loaded << L" DWM=" << pid << L"\n";
        return loaded ? 0 : 18;
    }
    if (argc == 2 && _wcsicmp(argv[1], L"--region") == 0)
    {
        const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE,
            vibrance::win32::liquidDwmRegionName);
        if (!mapping)
        {
            std::wcerr << L"No published region: " <<
                GetLastError() << L"\n";
            return 14;
        }
        const auto* table = static_cast<const vibrance::win32::LiquidDwmRegionTable*>(
            MapViewOfFile(mapping, FILE_MAP_READ, 0, 0,
                sizeof(vibrance::win32::LiquidDwmRegionTable)));
        if (!table)
        {
            CloseHandle(mapping);
            return 14;
        }
        for (std::uint32_t index = 0;
             index < vibrance::win32::liquidDwmRegionSlots; ++index)
        {
        const auto* region = &table->regions[index];
        if (!region->hwnd) continue;
        std::wcout << L"slot=" << index << L" sequence=" << region->sequence <<
            L" active=" << region->active <<
            L" HWND=0x" << std::hex << region->hwnd << std::dec <<
            L" rectangle=" << region->x << L"," << region->y <<
            L" " << region->width << L"x" << region->height <<
            L" client=" << region->screenX << L"," <<
                region->screenY << L" " << region->clientWidth <<
                L"x" << region->clientHeight <<
            L" monitor=" << region->monitorLeft << L"," <<
                region->monitorTop << L" " << region->monitorWidth <<
                L"x" << region->monitorHeight <<
            L" opacity=" << region->opacity <<
            L" blur=" << region->blurRadius <<
            L" refraction=" << region->refractionStrength <<
            L" owner=" << region->ownerPid << L"\n";
        }
        UnmapViewOfFile(table);
        CloseHandle(mapping);
        return 0;
    }
    if (argc == 2 && _wcsicmp(argv[1], L"--status") == 0)
    {
        const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE,
            vibrance::win32::liquidDwmStatusName);
        if (!mapping)
        {
            const DWORD error = GetLastError();
            std::wcerr << L"Native Liquid Glass status is unavailable in this session (Win32 error "
                << error << L").\n";
            if (error == ERROR_FILE_NOT_FOUND)
                std::wcerr << L"The session status mapping does not exist; this is not a missing-DLL diagnosis.\n";
            if (!native_hooks_allowed())
                std::wcerr << L"Use a native architecture build on a supported Windows version. "
                    L"An emulated x64 broker cannot load an ARM64 compositor.\n";
            startup::report_startup_failure();
            return 13;
        }
        const auto* values = static_cast<const LONG*>(MapViewOfFile(mapping,
            FILE_MAP_READ, 0, 0,
            vibrance::win32::liquidDwmStatusWords * sizeof(LONG)));
        if (!values)
        {
            const auto error = GetLastError();
            CloseHandle(mapping);
            std::wcerr << L"Cannot read native Liquid Glass status (Win32 error " << error << L").\n";
            return 13;
        }
        for (std::uint32_t index = 0;
             index < vibrance::win32::liquidDwmStatusWords; ++index)
        {
            std::wcout << index << L": " << values[index] << L"\n";
        }
        UnmapViewOfFile(values);
        CloseHandle(mapping);
        const HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, vibrance::win32::liquidDwmReadyName);
        std::wcout << L"Native renderer: " << (ready && WaitForSingleObject(ready, 0) == WAIT_OBJECT_0 ?
            L"ready" : L"not ready (startup failure or session transition)") << L"\n";
        if (ready) CloseHandle(ready);
        return 0;
    }
    if (argc != 3 && argc != 4)
    {
        std::wcerr << L"Usage: liquid_glass_dwm_broker "
            L"EXTENSION_DLL SYMBOL_CACHE_DIRECTORY\n"
            L"  --status | --run-auto | --install-auto-run USER_SID | --remove-auto-run\n";
        return 2;
    }
    if (argc == 4 && _wcsicmp(argv[1], L"--stage-runtime") == 0)
    {
        const auto cached = vibrance::win32::cache_liquid_runtime(argv[2], argv[3]);
        if (cached.empty())
        {
            std::wcerr << L"Cannot stage the Liquid Glass runtime bundle\n";
            return 3;
        }
        std::wcout << cached << L"\n";
        return 0;
    }
    if (!native_hooks_allowed())
    {
        std::wcerr << L"Native Liquid Glass hooks are unavailable on this Windows build or architecture\n";
        return 23;
    }
    // Serialise all broker entry points in this session, including manual runs.
    vibrance::win32::PublicSynchroniseSecurity mutexSecurity;
    MappingHandle startupMutex;
    startupMutex.value = CreateMutexW(mutexSecurity.get(), FALSE,
        L"Local\\VibranceLiquidGlassBrokerStartupV1");
    const DWORD mutexWait = startupMutex.value ? WaitForSingleObject(startupMutex.value, 0) : WAIT_FAILED;
    if (mutexWait != WAIT_OBJECT_0 && mutexWait != WAIT_ABANDONED)
    {
        std::wcerr << L"Another Liquid Glass broker is already starting in this session.\n";
        return 15;
    }
    struct MutexRelease { HANDLE value; ~MutexRelease() { ReleaseMutex(value); } } release { startupMutex.value };
    if (!enable_debug_privilege())
    {
        std::wcerr << L"Debug privilege is unavailable; the broker requires an elevated administrator token\n";
        return 11;
    }
    const DWORD pid = find_session_dwm();
    if (!pid) { std::wcerr << L"No DWM process in this session\n"; return 5; }
    if (remote_module_base(pid, L"vibrance_liquid_dwm_extension.dll"))
    {
        const HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE, vibrance::win32::liquidDwmReadyName);
        const bool running = ready && WaitForSingleObject(ready, 0) == WAIT_OBJECT_0;
        if (ready) CloseHandle(ready);
        std::wcout << L"The extension is already loaded; preserving its hooks and diagnostics.\n";
        return running ? 0 : 10;
    }
    const std::wstring sourceExtension = argv[1];
    const std::wstring symbolCache = argv[2];
    if (GetFileAttributesW(sourceExtension.c_str()) ==
        INVALID_FILE_ATTRIBUTES ||
        !CreateDirectoryW(symbolCache.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
    {
        std::wcerr << L"Extension or symbol cache path is unavailable\n";
        return 3;
    }
    const auto extension = vibrance::win32::cache_liquid_runtime(
        sourceExtension, (std::filesystem::path(symbolCache) / L"runtime").wstring());
    if (extension.empty())
    {
        std::wcerr << L"Cannot stage the Liquid Glass runtime bundle\n";
        return 3;
    }
    LiquidDwmHookConfig config {};
    LiquidDwmWindows10Config windows10Layout {};
#if defined(_M_ARM64)
    const bool windows10 = false;
#else
    const bool windows10 = vibrance::win32::liquid_dwm_uses_windows10_adapter(
        windows_build_number(), IMAGE_FILE_MACHINE_AMD64);
#endif
    wchar_t systemDirectory[MAX_PATH] {};
    GetSystemDirectoryW(systemDirectory, ARRAYSIZE(systemDirectory));
    if (!(windows10 ? resolve_windows10_config(std::wstring(systemDirectory) + L"\\dwmcore.dll",
            symbolCache, config, windows10Layout) : resolve_config(symbolCache, config)))
    {
        std::wcerr << L"Matching dwmcore PDB symbols are unavailable\n";
        return 4;
    }
    // Symbol downloads can outlast the observer's initial desktop check.
    // Recheck immediately before touching the compositor's startup mappings.
    if (find_session_dwm() != pid)
    {
        std::wcerr << L"The compositor restarted during symbol resolution; retrying the new process is required.\n";
        return 5;
    }
    if (startup::fullscreen_active())
    {
        std::wcerr << L"Liquid Glass startup deferred until the fullscreen application yields.\n";
        return 16;
    }
    PSECURITY_DESCRIPTOR statusDescriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GR;;;WD)(A;;GA;;;BA)(A;;GA;;;SY)(A;;GRGW;;;S-1-5-90-0)", SDDL_REVISION_1,
            &statusDescriptor, nullptr))
    {
        return 12;
    }
    SECURITY_ATTRIBUTES statusSecurity {
        sizeof(SECURITY_ATTRIBUTES), statusDescriptor, FALSE
    };
    const HANDLE statusMapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
        &statusSecurity, PAGE_READWRITE, 0,
        vibrance::win32::liquidDwmStatusWords * sizeof(LONG),
        vibrance::win32::liquidDwmStatusName);
    LocalFree(statusDescriptor);
    if (!statusMapping)
    {
        return 12;
    }
    auto* status = static_cast<volatile LONG*>(MapViewOfFile(statusMapping,
        FILE_MAP_READ | FILE_MAP_WRITE, 0, 0,
        vibrance::win32::liquidDwmStatusWords * sizeof(LONG)));
    if (!status)
    {
        CloseHandle(statusMapping);
        return 12;
    }
    for (std::uint32_t index = 0;
         index < vibrance::win32::liquidDwmStatusWords; ++index)
    {
        InterlockedExchange(status + index, 0);
    }
    vibrance::win32::PublicReadSecurity security;
    MappingHandle windows10Mapping;
    if (windows10)
    {
        windows10Mapping.value = CreateFileMappingW(INVALID_HANDLE_VALUE, security.get(),
            PAGE_READWRITE, 0, sizeof(windows10Layout), vibrance::win32::liquidDwmWindows10ConfigName);
        auto* layoutView = windows10Mapping.value ? MapViewOfFile(windows10Mapping.value,
            FILE_MAP_WRITE, 0, 0, sizeof(windows10Layout)) : nullptr;
        if (!layoutView)
        {
            UnmapViewOfFile(const_cast<LONG*>(status));
            CloseHandle(statusMapping);
            return 6;
        }
        std::memcpy(layoutView, &windows10Layout, sizeof(windows10Layout));
        UnmapViewOfFile(layoutView);
    }
    const HANDLE mapping = CreateFileMappingW(INVALID_HANDLE_VALUE,
        security.get(), PAGE_READWRITE, 0, sizeof(config),
        vibrance::win32::liquidDwmConfigName);
    if (!mapping)
    {
        UnmapViewOfFile(const_cast<LONG*>(status));
        CloseHandle(statusMapping);
        return 6;
    }
    auto* view = MapViewOfFile(mapping, FILE_MAP_WRITE, 0, 0,
        sizeof(config));
    if (!view)
    {
        CloseHandle(mapping);
        UnmapViewOfFile(const_cast<LONG*>(status));
        CloseHandle(statusMapping);
        return 7;
    }
    std::memcpy(view, &config, sizeof(config));
    UnmapViewOfFile(view);
    const HANDLE dwm = OpenProcess(PROCESS_CREATE_THREAD |
        PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
        PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!dwm)
    {
        std::wcerr << L"Cannot open DWM for extension loading: " <<
            GetLastError() << L"\n";
        CloseHandle(mapping);
        UnmapViewOfFile(const_cast<LONG*>(status));
        CloseHandle(statusMapping);
        return 8;
    }
    // Bind the remote load to the verified process handle, even if Windows
    // recycled the discovered PID between the snapshot and OpenProcess.
    std::wstring targetImage(32768, L'\0');
    DWORD targetLength = static_cast<DWORD>(targetImage.size());
    const bool validTarget = QueryFullProcessImageNameW(dwm, 0, targetImage.data(), &targetLength) &&
        _wcsicmp(targetImage.c_str(), (std::wstring(systemDirectory) + L"\\dwm.exe").c_str()) == 0;
    if (!validTarget)
    {
        std::wcerr << L"The compositor process identity changed; no extension was loaded.\n";
        CloseHandle(dwm);
        CloseHandle(mapping);
        UnmapViewOfFile(const_cast<LONG*>(status));
        CloseHandle(statusMapping);
        return 5;
    }
    const bool loaded = load_remote(dwm, pid, extension);
    CloseHandle(dwm);
    if (!loaded)
    {
        std::wcerr << L"DWM did not load the extension\n";
        CloseHandle(mapping);
        UnmapViewOfFile(const_cast<LONG*>(status));
        CloseHandle(statusMapping);
        return 9;
    }
    bool ready = false;
    for (int attempt = 0; attempt < 50; ++attempt)
    {
        const HANDLE event = OpenEventW(SYNCHRONIZE, FALSE,
            vibrance::win32::liquidDwmReadyName);
        if (event)
        {
            ready = WaitForSingleObject(event, 0) == WAIT_OBJECT_0;
            CloseHandle(event);
            if (ready)
            {
                break;
            }
        }
        Sleep(100);
    }
    CloseHandle(mapping);
    if (!ready)
    {
        const LONG reached = InterlockedCompareExchange(status, 0, 0);
        std::wcerr << L"Extension loaded but did not become ready "
            L"(stage " << reached <<
            L")\n";
        if (argc == 4)
        {
            const HANDLE report = CreateFileW(argv[3], GENERIC_WRITE,
                FILE_SHARE_READ, nullptr, CREATE_ALWAYS,
                FILE_ATTRIBUTE_NORMAL, nullptr);
            if (report != INVALID_HANDLE_VALUE)
            {
                const std::string value = std::to_string(reached);
                DWORD written = 0;
                WriteFile(report, value.data(),
                    static_cast<DWORD>(value.size()), &written, nullptr);
                CloseHandle(report);
            }
        }
        UnmapViewOfFile(const_cast<LONG*>(status));
        CloseHandle(statusMapping);
        return 10;
    }
    UnmapViewOfFile(const_cast<LONG*>(status));
    CloseHandle(statusMapping);
    std::wcout << L"Liquid Glass DWM extension ready in process " <<
        pid << L"\n";
    return 0;
}

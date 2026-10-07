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

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <string>

namespace
{
using vibrance::win32::LiquidDwmHookConfig;

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
        success = WaitForSingleObject(thread, 15000) == WAIT_OBJECT_0 &&
            success;
        DWORD result = 0;
        success = GetExitCodeThread(thread, &result) && result != 0 &&
            success;
        CloseHandle(thread);
    }
    VirtualFreeEx(process, remotePath, 0, MEM_RELEASE);
    return success;
}
}

int wmain(int argc, wchar_t** argv)
{
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
            return 13;
        }
        const auto* values = static_cast<const LONG*>(MapViewOfFile(mapping,
            FILE_MAP_READ, 0, 0,
            vibrance::win32::liquidDwmStatusWords * sizeof(LONG)));
        if (!values)
        {
            CloseHandle(mapping);
            return 13;
        }
        for (std::uint32_t index = 0;
             index < vibrance::win32::liquidDwmStatusWords; ++index)
        {
            std::wcout << index << L": " << values[index] << L"\n";
        }
        UnmapViewOfFile(values);
        CloseHandle(mapping);
        return 0;
    }
    if (argc != 3 && argc != 4)
    {
        std::wcerr << L"Usage: liquid_glass_dwm_broker "
            L"EXTENSION_DLL SYMBOL_CACHE_DIRECTORY\n";
        return 2;
    }
    const std::wstring extension = argv[1];
    const std::wstring symbolCache = argv[2];
    if (GetFileAttributesW(extension.c_str()) ==
        INVALID_FILE_ATTRIBUTES ||
        !CreateDirectoryW(symbolCache.c_str(), nullptr) &&
        GetLastError() != ERROR_ALREADY_EXISTS)
    {
        std::wcerr << L"Extension or symbol cache path is unavailable\n";
        return 3;
    }
    LiquidDwmHookConfig config {};
    if (!resolve_config(symbolCache, config))
    {
        std::wcerr << L"Matching dwmcore PDB symbols are unavailable\n";
        return 4;
    }
    if (!enable_debug_privilege())
    {
        std::wcerr << L"Debug privilege is unavailable; the broker "
            L"requires an elevated administrator token\n";
        return 11;
    }
    const DWORD pid = find_session_dwm();
    if (!pid)
    {
        std::wcerr << L"No DWM process in this session\n";
        return 5;
    }
    PSECURITY_DESCRIPTOR statusDescriptor = nullptr;
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;WD)", SDDL_REVISION_1,
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

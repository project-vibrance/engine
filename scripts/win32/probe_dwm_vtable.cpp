#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#include <array>
#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        std::wcerr << L"Usage: probe_dwm_vtable PDB-cache-directory\n";
        return 2;
    }
    wchar_t modulePath[MAX_PATH] {};
    if (!GetSystemDirectoryW(modulePath, MAX_PATH))
    {
        return 3;
    }
    const std::wstring path = std::wstring(modulePath) + L"\\dwmcore.dll";
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS);
    if (!SymInitializeW(process, argv[1], FALSE))
    {
        std::cerr << "SymInitialize failed: " << GetLastError() << '\n';
        return 4;
    }
    const DWORD64 base = SymLoadModuleExW(process, nullptr, path.c_str(),
                                           L"dwmcore", 0, 0, nullptr, 0);
    if (!base)
    {
        std::cerr << "SymLoadModuleEx failed: " << GetLastError() << '\n';
        SymCleanup(process);
        return 5;
    }
    std::array<char, sizeof(SYMBOL_INFO) + MAX_SYM_NAME> storage {};
    auto* symbol = reinterpret_cast<SYMBOL_INFO*>(storage.data());
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = MAX_SYM_NAME;
    const char* names[] = {
        "dwmcore!??_7CColorBrush@@6BCContent@@@",
        "??_7CColorBrush@@6BCContent@@@"
    };
    bool found = false;
    for (const char* name : names)
    {
        if (SymFromName(process, name, symbol))
        {
            found = true;
            break;
        }
    }
    if (!found)
    {
        std::cerr << "CColorBrush vtable symbol missing: " << GetLastError()
                  << '\n';
        SymEnumSymbols(process, base, "*CColorBrush*",
            [](PSYMBOL_INFO candidate, ULONG, PVOID) -> BOOL {
                std::cout << candidate->Name << " RVA=0x" << std::hex
                          << candidate->Address - candidate->ModBase << '\n';
                return TRUE;
            }, nullptr);
        SymCleanup(process);
        return 6;
    }
    std::cout << "dwmcore base: 0x" << std::hex << base
              << ", CColorBrush vtable RVA: 0x" << symbol->Address - base
              << ", draw slot RVA: 0x"
              << symbol->Address - base + 26 * sizeof(void*) << '\n';
    SymEnumSymbols(process, base, "*FlushD2D*",
        [](PSYMBOL_INFO candidate, ULONG, PVOID) -> BOOL {
            std::cout << candidate->Name << " RVA=0x" << std::hex
                      << candidate->Address - candidate->ModBase << '\n';
            return TRUE;
        }, nullptr);
    SymEnumSymbols(process, base, "*COverlayContext*Present*",
        [](PSYMBOL_INFO candidate, ULONG, PVOID) -> BOOL {
            std::cout << candidate->Name << " RVA=0x" << std::hex
                      << candidate->Address - candidate->ModBase << '\n';
            return TRUE;
        }, nullptr);
    for (const char* pattern : {
             "*GetPhysicalBackBuffer*",
             "*GetD3D11Resource*",
             "*CLegacySwapChain*",
             "*CDDisplaySwapChain*" })
    {
        SymEnumSymbols(process, base, pattern,
            [](PSYMBOL_INFO candidate, ULONG, PVOID) -> BOOL {
                if (std::string(candidate->Name).find("??_7") == 0 ||
                    std::string(candidate->Name).find("GetPhysicalBackBuffer") !=
                        std::string::npos ||
                    std::string(candidate->Name).find("GetD3D11Resource") !=
                        std::string::npos)
                {
                    std::cout << candidate->Name << " RVA=0x" << std::hex
                              << candidate->Address - candidate->ModBase << '\n';
                }
                return TRUE;
            }, nullptr);
    }
    std::cout << "FlushD2D named lookup: " <<
        SymFromName(process,
            "dwmcore!?FlushD2D@CDrawingContext@@QEAAJXZ", symbol) <<
        ", error " << GetLastError() << '\n';
    const HMODULE mapped = LoadLibraryExW(path.c_str(), nullptr,
        DONT_RESOLVE_DLL_REFERENCES);
    if (mapped)
    {
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(mapped);
        const auto scan = [&](const char* label, std::uint64_t tableRva,
                              std::uint64_t targetRva) {
            const auto* table = reinterpret_cast<void* const*>(bytes + tableRva);
            for (int index = 0; index < 64; ++index)
            {
                if (table[index] == bytes + targetRva)
                {
                    std::cout << label << " vtable slot=" << std::dec <<
                        index << " offset=" << index * sizeof(void*) << '\n';
                }
            }
        };
        scan("Legacy physical", 0x30e9a8, 0x115aa0);
        scan("Legacy base physical", 0x30e780, 0x115aa0);
        scan("Display physical", 0x30f678, 0x116800);
        scan("Display device physical", 0x30f720, 0x116800);
        scan("Legacy resource", 0x30e608, 0x2082d0);
        scan("Display resource", 0x3178d0, 0x2298a0);
        FreeLibrary(mapped);
    }
    SymCleanup(process);
    return 0;
}

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <dbghelp.h>

#include <iostream>
#include <string>

int wmain(int argc, wchar_t** argv)
{
    if (argc != 2)
    {
        std::wcerr << L"Usage: probe_wuceffects_symbols SYMBOL_CACHE\n";
        return 2;
    }
    wchar_t systemDirectory[MAX_PATH] {};
    if (!GetSystemDirectoryW(systemDirectory, MAX_PATH))
    {
        return 3;
    }
    const std::wstring path = std::wstring(systemDirectory) +
        L"\\wuceffects.dll";
    const std::wstring searchPath = std::wstring(argv[1]) + L";srv*" +
        argv[1] + L"*https://msdl.microsoft.com/download/symbols";
    const HANDLE process = GetCurrentProcess();
    SymSetOptions(SYMOPT_DEFERRED_LOADS);
    if (!SymInitializeW(process, searchPath.c_str(), FALSE))
    {
        std::cerr << "SymInitialize failed " << GetLastError() << '\n';
        return 4;
    }
    const DWORD64 base = SymLoadModuleExW(process, nullptr,
        path.c_str(), L"wuceffects", 0, 0, nullptr, 0);
    if (!base)
    {
        std::cerr << "SymLoadModuleEx failed " << GetLastError() << '\n';
        SymCleanup(process);
        return 5;
    }
    IMAGEHLP_MODULE64 module {};
    module.SizeOfStruct = sizeof(module);
    if (SymGetModuleInfo64(process, base, &module))
    {
        std::cout << "Symbol type " << module.SymType << " PDB " <<
            module.LoadedPdbName << '\n';
    }
    const char* patterns[] = {
        "*EffectType*FromGuid*",
        "*FromGuid*",
        "*EffectType*",
        "*CompileEffectDescription*",
        "*EffectType*Table*",
        "*DirectPropertyUpdater*"
    };
    for (const char* pattern : patterns)
    {
        std::cout << pattern << "\n";
        SymEnumSymbols(process, base, pattern,
            [](PSYMBOL_INFO symbol, ULONG, PVOID) -> BOOL {
                std::cout << "  " << symbol->Name << " RVA=0x" <<
                    std::hex << symbol->Address - symbol->ModBase <<
                    std::dec << '\n';
                return TRUE;
            }, nullptr);
    }
    SymCleanup(process);
    return 0;
}

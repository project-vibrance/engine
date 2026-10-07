// Read-only DWM present callback probe. It never samples or changes pixels.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>

#include "detours.h"
#include "../../engine/src/platform/win32/liquid_glass_dwm_identity.h"
#include "../../engine/src/platform/win32/liquid_glass_dwm_security.h"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

namespace
{
using PresentFn = std::uint64_t (*)(void*, void*, std::uint32_t,
    const void*, std::uint32_t, bool*, bool);
constexpr wchar_t mappingName[] = L"Local\\VibranceDwmPresentProbeV4";
constexpr std::uint64_t presentRva = 0x113eb0;
constexpr std::array<std::uint8_t, 32> supportedHash {{
    0x2c,0xb4,0xed,0xa2,0x49,0x29,0x8b,0x0f,
    0xb0,0x09,0x1c,0x04,0xf2,0xb4,0x9a,0xeb,
    0x7e,0xd5,0xed,0x0a,0xfd,0x34,0xe3,0x8c,
    0x27,0x89,0xdf,0x80,0x5a,0x51,0x5b,0x70
}};
PresentFn originalPresent = nullptr;
HANDLE mapping = nullptr;
volatile LONG* counters = nullptr;
HMODULE dwmcoreModule = nullptr;

std::uint64_t present_hook(void* self, void* swapchain,
                           std::uint32_t flags, const void* dirtyRects,
                           std::uint32_t unknown, bool* value,
                           bool legacy)
{
    if (counters)
    {
        InterlockedIncrement(counters + 1);
        __try
        {
            const auto vtable = reinterpret_cast<std::uintptr_t>(
                *reinterpret_cast<void**>(swapchain));
            const auto module = reinterpret_cast<std::uintptr_t>(dwmcoreModule);
            if (vtable >= module && vtable - module <= 0x400000)
            {
                InterlockedExchange(counters + 2,
                    static_cast<LONG>(vtable - module));
                if (vtable - module == 0x30f720)
                {
                    using PhysicalFn = void* (*)(void*);
                    using ResourceFn = ID3D11Resource* (*)(void*);
                    const auto physical = reinterpret_cast<PhysicalFn>(
                        reinterpret_cast<void**>(vtable)[24]);
                    void* buffer = physical(swapchain);
                    if (!buffer)
                    {
                        InterlockedExchange(counters + 3, 3);
                    }
                    else
                    {
                        const auto bufferVtable = reinterpret_cast<void**>(
                            *reinterpret_cast<void**>(buffer));
                        const auto bufferRva = reinterpret_cast<std::uintptr_t>(
                            bufferVtable) - module;
                        InterlockedExchange(counters + 4,
                            static_cast<LONG>(bufferRva));
                        if (bufferRva == 0x3178d0)
                        {
                            const auto resource = reinterpret_cast<ResourceFn>(
                                bufferVtable[14])(buffer);
                            if (resource)
                            {
                                ID3D11Texture2D* texture = nullptr;
                                if (SUCCEEDED(resource->QueryInterface(
                                        __uuidof(ID3D11Texture2D),
                                        reinterpret_cast<void**>(&texture))) &&
                                    texture)
                                {
                                    D3D11_TEXTURE2D_DESC desc {};
                                    texture->GetDesc(&desc);
                                    InterlockedExchange(counters + 5,
                                        static_cast<LONG>(desc.Width));
                                    InterlockedExchange(counters + 6,
                                        static_cast<LONG>(desc.Height));
                                    InterlockedExchange(counters + 7,
                                        static_cast<LONG>(desc.Format));
                                    InterlockedExchange(counters + 8,
                                        static_cast<LONG>(desc.BindFlags));
                                    ID3D11Device* device = nullptr;
                                    texture->GetDevice(&device);
                                    if (device)
                                    {
                                        ID3D11RenderTargetView* target = nullptr;
                                        const HRESULT result =
                                            device->CreateRenderTargetView(
                                                texture, nullptr, &target);
                                        InterlockedExchange(counters + 9,
                                            static_cast<LONG>(result));
                                        if (target) target->Release();
                                        device->Release();
                                    }
                                    texture->Release();
                                }
                                else
                                {
                                    InterlockedExchange(counters + 3, 4);
                                }
                            }
                            else
                            {
                                InterlockedExchange(counters + 3, 5);
                            }
                        }
                    }
                }
            }
            else
            {
                InterlockedExchange(counters + 3, 1);
            }
        }
        __except (EXCEPTION_EXECUTE_HANDLER)
        {
            InterlockedExchange(counters + 3, 2);
        }
    }
    return originalPresent(self, swapchain, flags,
        dirtyRects, unknown, value, legacy);
}

void count_stage(LONG stage)
{
    if (counters)
    {
        InterlockedExchange(counters, stage);
    }
}

bool start_probe()
{
    vibrance::win32::PublicReadSecurity security;
    mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, security.get(),
        PAGE_READWRITE, 0, 10 * sizeof(LONG), mappingName);
    if (!mapping)
    {
        return false;
    }
    counters = static_cast<volatile LONG*>(MapViewOfFile(mapping,
        FILE_MAP_WRITE, 0, 0, 10 * sizeof(LONG)));
    count_stage(1);
    const HMODULE dwmcore = GetModuleHandleW(L"dwmcore.dll");
    if (!dwmcore)
    {
        return false;
    }
    count_stage(2);
    dwmcoreModule = dwmcore;
    const std::wstring path = vibrance::win32::module_path(dwmcore);
    std::uint8_t digest[32] {};
    if (path.empty() || !vibrance::win32::file_sha256(path, digest) ||
        std::memcmp(digest, supportedHash.data(), 32) != 0)
    {
        return false;
    }
    count_stage(3);
    originalPresent = reinterpret_cast<PresentFn>(
        reinterpret_cast<std::uint8_t*>(dwmcore) + presentRva);
    if (DetourTransactionBegin() != NO_ERROR)
    {
        return false;
    }
    DetourUpdateThread(GetCurrentThread());
    std::vector<HANDLE> threads;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snapshot != INVALID_HANDLE_VALUE)
    {
        THREADENTRY32 entry { sizeof(entry) };
        if (Thread32First(snapshot, &entry))
        {
            do
            {
                if (entry.th32OwnerProcessID != GetCurrentProcessId() ||
                    entry.th32ThreadID == GetCurrentThreadId())
                {
                    continue;
                }
                const HANDLE thread = OpenThread(THREAD_GET_CONTEXT |
                    THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME,
                    FALSE, entry.th32ThreadID);
                if (thread)
                {
                    DetourUpdateThread(thread);
                    threads.push_back(thread);
                }
            } while (Thread32Next(snapshot, &entry));
        }
        CloseHandle(snapshot);
    }
    const LONG attached = DetourAttach(
        reinterpret_cast<PVOID*>(&originalPresent),
        reinterpret_cast<PVOID>(&present_hook));
    const LONG committed = attached == NO_ERROR ?
        DetourTransactionCommit() : DetourTransactionAbort();
    for (HANDLE thread : threads)
    {
        CloseHandle(thread);
    }
    if (attached != NO_ERROR || committed != NO_ERROR)
    {
        return false;
    }
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&present_hook), &pinned);
    count_stage(4);
    return true;
}

DWORD WINAPI worker(void*)
{
    start_probe();
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        DisableThreadLibraryCalls(instance);
        const HANDLE thread = CreateThread(nullptr, 0, &worker,
            nullptr, 0, nullptr);
        if (thread)
        {
            CloseHandle(thread);
        }
    }
    return TRUE;
}

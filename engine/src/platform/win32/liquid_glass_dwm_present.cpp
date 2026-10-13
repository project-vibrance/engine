// Private DWM present integration. A matching PDB and module fingerprint are
// required before the broker loads this DLL into the session compositor.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>
#include <d3d11.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <d2d1_1.h>

#include "detours.h"
#include "liquid_glass_d3d11.h"
#include "liquid_glass_dwm_identity.h"
#include "liquid_glass_dwm_protocol.h"
#include "liquid_glass_dwm_security.h"
#include "liquid_glass_dwm_windows10_layout.h"
#include "liquid_glass_dwm_windows10_lifecycle_policy.h"
#include "liquid_glass_dwm_windows10_session.h"
#include <dxgi1_2.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

namespace
{
using namespace vibrance::win32;
using Microsoft::WRL::ComPtr;
using PresentFn = std::uint64_t (*)(void*, void*, std::uint32_t,
    const std::vector<RECT>&, std::uint32_t, bool*, bool);
using PhysicalFn = void* (*)(void*);
using ResourceFn = ID3D11Resource* (*)(void*);
using WindowRenderFn = HRESULT (*)(void*, void*, bool*);
using WindowHwndFn = HWND (*)(void*);
using DrawingFlushFn = HRESULT (*)(void*);
using DdaRenderFn = HRESULT (*)(void*, void*);
using DdaPresentFn = HRESULT (*)(void*);
using DdaBackBufferFn = void* (*)(void*);
using DdaTextureFn = ID3D11Texture2D* (*)(void*);
using CapturePresentFn = HRESULT (*)(void*);
using CaptureBufferFn = IDXGIResource* (*)(void*);
using TextureFn = ID3D11Texture2D* (*)(void*, UINT*);
using TargetFn = ID3D11RenderTargetView* (*)(void*);
using ContentDrawFn = HRESULT (*)(void*, void*, const float*, void*);
using ApplyRenderStateFn = HRESULT (*)(void*, bool);
using BatchCommandEmitFn = HRESULT (*)(void*, void*, bool);
using EffectEmitFn = HRESULT (*)(void*, void*, void*);
using NativeBatchFlushFn = void (*)(void*, std::uint32_t);
using DrawListEntryRenderFn = HRESULT (*)(void*, void*, const void*, float, std::uint32_t);

HMODULE extensionModule = nullptr;
HMODULE dwmcoreModule = nullptr;
PresentFn originalPresent = nullptr;
WindowRenderFn originalWindowRender = nullptr;
WindowRenderFn originalCaptureRender = nullptr;
WindowRenderFn originalOffscreenRender = nullptr;
WindowHwndFn windowHwnd = nullptr;
DrawingFlushFn drawingFlush = nullptr;
ContentDrawFn originalContentDraw = nullptr;
ContentDrawFn originalBrushDraw = nullptr;
DrawListEntryRenderFn originalDrawListEntryRender = nullptr;
ApplyRenderStateFn applyRenderState = nullptr;
BatchCommandEmitFn originalBatchCommandEmit = nullptr;
EffectEmitFn originalCommonEffectEmit = nullptr;
EffectEmitFn originalBrushEffectEmit = nullptr;
NativeBatchFlushFn nativeBatchFlush = nullptr;
DdaRenderFn originalDdaRender = nullptr;
DdaPresentFn originalDdaPresent = nullptr;
DdaBackBufferFn ddaGetBackBuffer = nullptr;
DdaTextureFn ddaGetTexture2D = nullptr;
CapturePresentFn originalCapturePresent = nullptr;
CaptureBufferFn captureCurrentFrameBuffer = nullptr;
LiquidDwmHookConfig hookConfig {};
LiquidDwmWindows10Config windows10Config {};
bool windows10Adapter = false;
HANDLE readyEvent = nullptr;
HANDLE statusMapping = nullptr;
volatile LONG* statusView = nullptr;
HANDLE regionMapping = nullptr;
const LiquidDwmRegionTable* regionView = nullptr;
std::mutex regionMutex;
std::mutex rendererMutex;
LiquidGlassD3D11 renderer;
ComPtr<ID3D11Device> rendererDevice;
ComPtr<ID3D11Device1> uiDevice;
ComPtr<ID3D11Texture2D> uiSource;
ComPtr<IDXGIKeyedMutex> uiMutex;
ComPtr<ID3D11Texture2D> uiSnapshot;
ComPtr<ID3D11ShaderResourceView> uiView;
std::uint32_t uiOwner = 0;
std::uint64_t uiWindow = 0;
std::uint32_t uiIndex = liquidDwmUiBuffers;
std::uint32_t uiCopiedFrame = 0;
std::uint32_t uiTransportOwner = 0;
std::uint64_t uiTransportWindow = 0;
std::uint32_t gpuUnavailableOwner = 0;
HANDLE cpuUiMapping = nullptr;
const LiquidDwmCpuUiHeader* cpuUiView = nullptr;
std::uint32_t cpuUiWidth = 0;
std::uint32_t cpuUiHeight = 0;
std::uint64_t cpuUiWindow = 0;
std::vector<std::uint8_t> cpuUiPixels;
struct BackgroundSnapshot
{
    ComPtr<ID3D11Texture2D> texture;
    void* owner = nullptr;
    ULONGLONG tick = 0;
    std::int32_t originX = 0;
    std::int32_t originY = 0;
};
std::array<BackgroundSnapshot, liquidDwmRegionSlots> captureBackgrounds;
std::array<BackgroundSnapshot, liquidDwmRegionSlots> displayBackgrounds;
std::array<BackgroundSnapshot, liquidDwmRegionSlots> ddaBackgrounds;
thread_local bool inCaptureRender = false;
thread_local void* activeCaptureTarget = nullptr;
thread_local void* activeDdaTarget = nullptr;

void status(std::uint32_t index) noexcept
{
    if (statusView && index < liquidDwmStatusWords)
    {
        InterlockedIncrement(statusView + index);
    }
}

void stage(LONG value) noexcept
{
    if (statusView) InterlockedExchange(statusView, value);
}

void detail(std::uint32_t index, LONG value) noexcept
{
    if (statusView && index < liquidDwmStatusWords)
        InterlockedExchange(statusView + index, value);
}

DXGI_FORMAT backdrop_copy_format(DXGI_FORMAT resource,
    DXGI_FORMAT view) noexcept
{
    switch (resource)
    {
    case DXGI_FORMAT_B8G8R8A8_TYPELESS:
        return view == DXGI_FORMAT_B8G8R8A8_UNORM_SRGB ? view :
            DXGI_FORMAT_B8G8R8A8_UNORM;
    case DXGI_FORMAT_R8G8B8A8_TYPELESS:
        return view == DXGI_FORMAT_R8G8B8A8_UNORM_SRGB ? view :
            DXGI_FORMAT_R8G8B8A8_UNORM;
    case DXGI_FORMAT_R10G10B10A2_TYPELESS:
        return DXGI_FORMAT_R10G10B10A2_UNORM;
    case DXGI_FORMAT_R16G16B16A16_TYPELESS:
        return DXGI_FORMAT_R16G16B16A16_FLOAT;
    case DXGI_FORMAT_B8G8R8A8_UNORM:
    case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
    case DXGI_FORMAT_R8G8B8A8_UNORM:
    case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
    case DXGI_FORMAT_R10G10B10A2_UNORM:
    case DXGI_FORMAT_R16G16B16A16_FLOAT:
        return resource;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

bool snapshot_background(BackgroundSnapshot& snapshot,
    ID3D11Texture2D* source, void* owner,
    const LiquidGlassRenderRegion& region,
    DXGI_FORMAT viewFormat = DXGI_FORMAT_UNKNOWN) noexcept
{
    if (!source || !owner) return false;
    D3D11_TEXTURE2D_DESC desc {};
    source->GetDesc(&desc);
    const auto format = backdrop_copy_format(desc.Format, viewFormat);
    if (format == DXGI_FORMAT_UNKNOWN ||
        desc.ArraySize != 1u || desc.SampleDesc.Count != 1u ||
        desc.Width == 0u || desc.Height == 0u ||
        static_cast<std::uint64_t>(desc.Width) * desc.Height >
            32u * 1024u * 1024u)
        return false;
    const float reach = std::max({ region.blurRadius * 5.0f,
        region.refractionStrength + region.dispersionStrength + 2.0f,
        4.0f });
    if (!std::isfinite(reach) || reach > 2048.0f ||
        region.width == 0u || region.height == 0u ||
        static_cast<std::int64_t>(region.x) + region.width <= 0 ||
        static_cast<std::int64_t>(region.y) + region.height <= 0 ||
        static_cast<std::int64_t>(region.x) >= desc.Width ||
        static_cast<std::int64_t>(region.y) >= desc.Height)
        return false;
    const auto margin = static_cast<std::int64_t>(std::ceil(reach));
    const auto left = std::max<std::int64_t>(0, region.x - margin);
    const auto top = std::max<std::int64_t>(0, region.y - margin);
    const auto right = std::min<std::int64_t>(desc.Width,
        static_cast<std::int64_t>(region.x) + region.width + margin);
    const auto bottom = std::min<std::int64_t>(desc.Height,
        static_cast<std::int64_t>(region.y) + region.height + margin);
    if (right <= left || bottom <= top) return false;
    const auto width = static_cast<UINT>(right - left);
    const auto height = static_cast<UINT>(bottom - top);
    ComPtr<ID3D11Device> device;
    source->GetDevice(&device);
    if (!device) return false;
    ComPtr<ID3D11DeviceContext> context;
    device->GetImmediateContext(&context);
    if (!context) return false;
    D3D11_TEXTURE2D_DESC existing {};
    ComPtr<ID3D11Device> existingDevice;
    if (snapshot.texture)
    {
        snapshot.texture->GetDesc(&existing);
        snapshot.texture->GetDevice(&existingDevice);
    }
    if (!snapshot.texture || existingDevice.Get() != device.Get() ||
        existing.Width != width || existing.Height != height ||
        existing.Format != format)
    {
        snapshot.texture.Reset();
        auto copy = desc;
        copy.Width = width;
        copy.Height = height;
        copy.MipLevels = 1u;
        copy.ArraySize = 1u;
        copy.Format = format;
        copy.Usage = D3D11_USAGE_DEFAULT;
        copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        copy.CPUAccessFlags = 0u;
        copy.MiscFlags = 0u;
        if (FAILED(device->CreateTexture2D(&copy, nullptr,
                &snapshot.texture))) return false;
    }
    D3D11_BOX box {};
    box.left = static_cast<UINT>(left);
    box.top = static_cast<UINT>(top);
    box.right = static_cast<UINT>(right);
    box.bottom = static_cast<UINT>(bottom);
    box.back = 1u;
    context->CopySubresourceRegion(snapshot.texture.Get(), 0u,
        0u, 0u, 0u, source, 0u, &box);
    snapshot.owner = owner;
    snapshot.tick = GetTickCount64();
    snapshot.originX = static_cast<std::int32_t>(left);
    snapshot.originY = static_cast<std::int32_t>(top);
    return true;
}

void trace_chain(void* self, void* chain,
    const D3D11_TEXTURE2D_DESC& description, bool rendered) noexcept
{
    if (!statusView) return;
    constexpr std::uint32_t first = 24;
    constexpr std::uint32_t stride = 7;
    const LONG identity = static_cast<LONG>(
        reinterpret_cast<std::uintptr_t>(chain) & 0xffffffffu);
    if (!identity) return;
    for (std::uint32_t index = 0; index < 8; ++index)
    {
        volatile LONG* entry = statusView + first + index * stride;
        const LONG found = InterlockedCompareExchange(entry, identity, 0);
        if (found != identity && found != 0) continue;
        InterlockedExchange(entry + 1, static_cast<LONG>(
            reinterpret_cast<std::uintptr_t>(self) & 0xffffffffu));
        InterlockedExchange(entry + 2, static_cast<LONG>(description.Width));
        InterlockedExchange(entry + 3, static_cast<LONG>(description.Height));
        InterlockedIncrement(entry + 4);
        if (rendered) InterlockedIncrement(entry + 5);
        InterlockedExchange(entry + 6, static_cast<LONG>(description.Format));
        return;
    }
}

bool read_region(const D3D11_TEXTURE2D_DESC& texture,
    LiquidDwmRegion& result, std::uint32_t& slot) noexcept
{
    {
        std::lock_guard<std::mutex> lock(regionMutex);
        if (!regionView)
        {
            regionMapping = OpenFileMappingW(FILE_MAP_READ, FALSE,
                liquidDwmRegionName);
            if (!regionMapping) return false;
            regionView = static_cast<const LiquidDwmRegionTable*>(MapViewOfFile(
                regionMapping, FILE_MAP_READ, 0, 0,
                sizeof(LiquidDwmRegionTable)));
            if (!regionView)
            {
                CloseHandle(regionMapping);
                regionMapping = nullptr;
                return false;
            }
        }
    }
    for (std::uint32_t index = 0; index < liquidDwmRegionSlots; ++index)
    {
        const auto& candidate = regionView->regions[index];
        auto& sequence = const_cast<std::uint32_t&>(candidate.sequence);
        std::atomic_ref<std::uint32_t> publishedSequence(sequence);
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const std::uint32_t before = publishedSequence.load(
                std::memory_order_acquire);
            if (before & 1) continue;
            std::memcpy(&result, &candidate, sizeof(result));
            MemoryBarrier();
            if (before != publishedSequence.load(std::memory_order_acquire))
                continue;
            if (result.version == liquidDwmProtocolVersion &&
                result.active == 1 && result.width > 0 &&
                result.height > 0 &&
                result.monitorWidth == texture.Width &&
                result.monitorHeight == texture.Height &&
                result.uiBufferIndex < liquidDwmUiBuffers)
            {
                slot = index;
                return true;
            }
            break;
        }
    }
    return false;
}

bool read_capture_region(const D3D11_TEXTURE2D_DESC& texture,
    std::uint32_t index, LiquidDwmRegion& result) noexcept
{
    if (index >= liquidDwmRegionSlots) return false;
    // read_region also opens the read-only table on first use. A virtual
    // desktop capture will not match any individual monitor dimensions.
    LiquidDwmRegion unused {};
    std::uint32_t unusedSlot = liquidDwmRegionSlots;
    (void)read_region(texture, unused, unusedSlot);
    if (!regionView) return false;
    const auto& candidate = regionView->regions[index];
    auto& sequence = const_cast<std::uint32_t&>(candidate.sequence);
    std::atomic_ref<std::uint32_t> publishedSequence(sequence);
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        const auto before = publishedSequence.load(std::memory_order_acquire);
        if (before & 1u) continue;
        std::memcpy(&result, &candidate, sizeof(result));
        MemoryBarrier();
        if (before != publishedSequence.load(std::memory_order_acquire))
            continue;
        return result.version == liquidDwmProtocolVersion &&
            result.active == 1u && result.width > 0u &&
            result.height > 0u && result.uiBufferIndex < liquidDwmUiBuffers;
    }
    return false;
}

bool read_window_region(HWND window, LiquidDwmRegion& result,
    std::uint32_t& slot) noexcept
{
    if (!window) return false;
    std::lock_guard<std::mutex> lock(regionMutex);
    if (!regionView) return false;
    for (std::uint32_t index = 0; index < liquidDwmRegionSlots; ++index)
    {
        const auto& candidate = regionView->regions[index];
        if (candidate.hwnd != reinterpret_cast<std::uint64_t>(window))
            continue;
        auto& sequence = const_cast<std::uint32_t&>(candidate.sequence);
        std::atomic_ref<std::uint32_t> publishedSequence(sequence);
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto before = publishedSequence.load(
                std::memory_order_acquire);
            if (before & 1) continue;
            std::memcpy(&result, &candidate, sizeof(result));
            MemoryBarrier();
            if (before != publishedSequence.load(std::memory_order_acquire))
                continue;
            if (result.version == liquidDwmProtocolVersion &&
                result.active == 1 && result.hwnd ==
                    reinterpret_cast<std::uint64_t>(window))
            {
                slot = index;
                return true;
            }
            break;
        }
    }
    return false;
}

std::uint32_t find_slot(std::uint64_t tableRva,
                        std::uint64_t methodRva) noexcept
{
    if (!tableRva || !methodRva) return UINT32_MAX;
    const auto* base = reinterpret_cast<const std::uint8_t*>(dwmcoreModule);
    const auto* table = reinterpret_cast<void* const*>(base + tableRva);
    for (std::uint32_t index = 0; index < 64; ++index)
    {
        if (table[index] == base + methodRva) return index;
    }
    return UINT32_MAX;
}

bool texture_from_swapchain(void* chain,
                            ID3D11Texture2D** output) noexcept
{
    *output = nullptr;
    const auto base = reinterpret_cast<std::uintptr_t>(dwmcoreModule);
    __try
    {
        const auto* table = *reinterpret_cast<void***>(chain);
        const auto tableRva = reinterpret_cast<std::uintptr_t>(table) - base;
        std::uint64_t physicalRva = 0;
        std::uint64_t bufferTableRva = 0;
        std::uint64_t resourceRva = 0;
        if (tableRva == hookConfig.displaySwapchainVtableRva)
        {
            physicalRva = hookConfig.displayPhysicalRva;
            bufferTableRva = hookConfig.displayBufferVtableRva;
            resourceRva = hookConfig.displayResourceRva;
        }
        else if (tableRva == hookConfig.legacySwapchainVtableRva)
        {
            physicalRva = hookConfig.legacyPhysicalRva;
            bufferTableRva = hookConfig.legacyBufferVtableRva;
            resourceRva = hookConfig.legacyResourceRva;
        }
        else
        {
            return false;
        }
        const auto physicalSlot = find_slot(tableRva, physicalRva);
        const auto resourceSlot = find_slot(bufferTableRva, resourceRva);
        if (physicalSlot == UINT32_MAX || resourceSlot == UINT32_MAX)
            return false;
        void* buffer = reinterpret_cast<PhysicalFn>(table[physicalSlot])(chain);
        if (!buffer || reinterpret_cast<std::uintptr_t>(
                *reinterpret_cast<void***>(buffer)) - base != bufferTableRva)
            return false;
        auto* resource = reinterpret_cast<ResourceFn>(
            (*reinterpret_cast<void***>(buffer))[resourceSlot])(buffer);
        return resource && SUCCEEDED(resource->QueryInterface(
            __uuidof(ID3D11Texture2D),
            reinterpret_cast<void**>(output)));
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        return false;
    }
}

bool translate_region(const LiquidDwmRegion& published,
                      const D3D11_TEXTURE2D_DESC& desc,
                      LiquidGlassRenderRegion& region,
                      RECT& uiRectangle) noexcept
{
    const HWND window = reinterpret_cast<HWND>(published.hwnd);
    DWORD owner = 0;
    POINT origin {};
    if (!IsWindow(window) ||
        !GetWindowThreadProcessId(window, &owner) ||
        owner != published.ownerPid ||
        published.monitorWidth == 0 || published.monitorHeight == 0 ||
        published.clientWidth == 0 || published.clientHeight == 0 ||
        desc.Width != published.monitorWidth ||
        desc.Height != published.monitorHeight)
    {
        return false;
    }
    origin.x = published.screenX - published.monitorLeft;
    origin.y = published.screenY - published.monitorTop;
    region.x = origin.x + published.x;
    region.y = origin.y + published.y;
    region.width = published.width;
    region.height = published.height;
    region.cornerRadius = published.cornerRadius;
    region.edgeThickness = published.edgeThickness;
    region.refractionStrength = published.refractionStrength;
    region.dispersionStrength = published.dispersionStrength;
    region.blurRadius = published.blurRadius;
    region.rimStrength = published.rimStrength;
    region.opacity = published.opacity;
    region.cornerExponent = published.cornerExponent;
    std::copy(std::begin(published.viewportRect), std::end(published.viewportRect), region.viewportRect.begin());
    std::copy(std::begin(published.viewportFade), std::end(published.viewportFade), region.viewportFade.begin());
    region.lightPhase = static_cast<float>(GetTickCount64() % 6000) *
        (6.2831853f / 6000.0f);
    uiRectangle = { origin.x, origin.y,
        origin.x + static_cast<LONG>(published.clientWidth),
        origin.y + static_cast<LONG>(published.clientHeight) };
    return region.x >= 0 && region.y >= 0 &&
        static_cast<std::uint64_t>(region.x) + region.width <= desc.Width &&
        static_cast<std::uint64_t>(region.y) + region.height <= desc.Height &&
        uiRectangle.left >= 0 && uiRectangle.top >= 0 &&
        uiRectangle.right <= static_cast<LONG>(desc.Width) &&
        uiRectangle.bottom <= static_cast<LONG>(desc.Height);
}

bool translate_capture_region(LiquidDwmRegion& published,
    const D3D11_TEXTURE2D_DESC& desc, LiquidGlassRenderRegion& region,
    RECT& uiRectangle) noexcept
{
    const bool monitorFrame = desc.Width == published.monitorWidth &&
        desc.Height == published.monitorHeight;
    const int desktopWidth = GetSystemMetrics(SM_CXVIRTUALSCREEN);
    const int desktopHeight = GetSystemMetrics(SM_CYVIRTUALSCREEN);
    const bool desktopFrame = desktopWidth > 0 && desktopHeight > 0 &&
        desc.Width == static_cast<UINT>(desktopWidth) &&
        desc.Height == static_cast<UINT>(desktopHeight);
    if (!monitorFrame && !desktopFrame) return false;
    if (desktopFrame && !monitorFrame)
    {
        published.monitorLeft = GetSystemMetrics(SM_XVIRTUALSCREEN);
        published.monitorTop = GetSystemMetrics(SM_YVIRTUALSCREEN);
        published.monitorWidth = desc.Width;
        published.monitorHeight = desc.Height;
    }
    return translate_region(published, desc, region, uiRectangle);
}

bool ambiguous_capture_monitor(const D3D11_TEXTURE2D_DESC& desc,
    const LiquidDwmRegion& selected) noexcept
{
    if (desc.Width != selected.monitorWidth ||
        desc.Height != selected.monitorHeight ||
        (desc.Width == static_cast<UINT>(GetSystemMetrics(
            SM_CXVIRTUALSCREEN)) &&
         desc.Height == static_cast<UINT>(GetSystemMetrics(
            SM_CYVIRTUALSCREEN)))) return false;
    for (std::uint32_t index = 0; index < liquidDwmRegionSlots; ++index)
    {
        LiquidDwmRegion other {};
        if (!read_capture_region(desc, index, other) ||
            other.monitorWidth != desc.Width ||
            other.monitorHeight != desc.Height) continue;
        if (other.monitorLeft != selected.monitorLeft ||
            other.monitorTop != selected.monitorTop) return true;
    }
    return false;
}

void reset_ui() noexcept
{
    uiView.Reset();
    uiSnapshot.Reset();
    uiMutex.Reset();
    uiSource.Reset();
    uiDevice.Reset();
    uiOwner = 0;
    uiWindow = 0;
    uiIndex = liquidDwmUiBuffers;
    uiCopiedFrame = 0;
}

void reset_cpu_ui() noexcept
{
    if (cpuUiView) UnmapViewOfFile(cpuUiView);
    if (cpuUiMapping) CloseHandle(cpuUiMapping);
    cpuUiView = nullptr;
    cpuUiMapping = nullptr;
    cpuUiWidth = 0;
    cpuUiHeight = 0;
    cpuUiWindow = 0;
    cpuUiPixels.clear();
}

bool update_ui_gpu(ID3D11Device* device, ID3D11DeviceContext* context,
               const LiquidDwmRegion& region, const RECT& rectangle)
{
    if (uiOwner != region.ownerPid || uiWindow != region.hwnd ||
        uiIndex != region.uiBufferIndex ||
        !uiSource)
    {
        reset_ui();
        const HRESULT query = device->QueryInterface(IID_PPV_ARGS(&uiDevice));
        detail(12, query);
        if (FAILED(query))
            return false;
        const std::wstring name = liquid_dwm_ui_name(region.ownerPid,
            region.hwnd, region.uiBufferIndex);
        const HRESULT openResult = uiDevice->OpenSharedResourceByName(name.c_str(),
                DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                IID_PPV_ARGS(&uiSource));
        detail(12, openResult);
        const HRESULT mutexResult = SUCCEEDED(openResult) ?
            uiSource.As(&uiMutex) : E_FAIL;
        detail(13, mutexResult);
        if (FAILED(openResult) || FAILED(mutexResult))
        {
            reset_ui();
            return false;
        }
        uiOwner = region.ownerPid;
        uiWindow = region.hwnd;
        uiIndex = region.uiBufferIndex;
    }
    D3D11_TEXTURE2D_DESC source {};
    uiSource->GetDesc(&source);
    detail(14, static_cast<LONG>(source.Width));
    detail(15, static_cast<LONG>(source.Height));
    detail(16, rectangle.right - rectangle.left);
    detail(17, rectangle.bottom - rectangle.top);
    detail(19, static_cast<LONG>(source.Format));
    if (source.Width != static_cast<UINT>(rectangle.right - rectangle.left) ||
        source.Height != static_cast<UINT>(rectangle.bottom - rectangle.top) ||
        source.Format != DXGI_FORMAT_B8G8R8A8_UNORM)
        return false;
    if (region.uiFrame != uiCopiedFrame)
    {
        const HRESULT acquired = uiMutex->AcquireSync(1, 0);
        detail(18, acquired);
        if (acquired != S_OK)
            return uiView != nullptr;
        if (!uiSnapshot)
        {
            D3D11_TEXTURE2D_DESC copy = source;
            copy.MiscFlags = 0;
            copy.BindFlags = D3D11_BIND_SHADER_RESOURCE;
            const HRESULT created = device->CreateTexture2D(&copy,
                nullptr, &uiSnapshot);
            detail(20, created);
            if (FAILED(created) ||
                FAILED(device->CreateShaderResourceView(uiSnapshot.Get(),
                    nullptr, &uiView)))
            {
                uiMutex->ReleaseSync(0);
                return false;
            }
        }
        context->CopyResource(uiSnapshot.Get(), uiSource.Get());
        uiMutex->ReleaseSync(0);
        uiCopiedFrame = region.uiFrame;
    }
    return uiView != nullptr;
}

bool update_ui_cpu(ID3D11Device* device, ID3D11DeviceContext* context,
    const LiquidDwmRegion& region, const RECT& rectangle)
{
    const auto width = static_cast<std::uint32_t>(
        rectangle.right - rectangle.left);
    const auto height = static_cast<std::uint32_t>(
        rectangle.bottom - rectangle.top);
    const std::uint64_t bytes = static_cast<std::uint64_t>(width) *
        height * 4u;
    if (!bytes || bytes > 64u * 1024u * 1024u) return false;
    if (!cpuUiView || cpuUiWindow != region.hwnd ||
        cpuUiWidth != width || cpuUiHeight != height)
    {
        reset_cpu_ui();
        const std::wstring name = liquid_dwm_cpu_ui_name(
            region.ownerPid, region.hwnd, width, height);
        cpuUiMapping = OpenFileMappingW(FILE_MAP_READ, FALSE,
            name.c_str());
        if (!cpuUiMapping)
        {
            detail(21, -static_cast<LONG>(GetLastError()));
            return false;
        }
        cpuUiView = static_cast<const LiquidDwmCpuUiHeader*>(
            MapViewOfFile(cpuUiMapping, FILE_MAP_READ, 0, 0,
                sizeof(LiquidDwmCpuUiHeader) +
                    static_cast<SIZE_T>(bytes)));
        if (!cpuUiView)
        {
            detail(21, -static_cast<LONG>(GetLastError()));
            reset_cpu_ui();
            return false;
        }
        cpuUiWidth = width;
        cpuUiHeight = height;
        cpuUiWindow = region.hwnd;
        detail(21, 1);
    }
    if (cpuUiView->version != liquidDwmProtocolVersion ||
        cpuUiView->ownerPid != region.ownerPid ||
        cpuUiView->hwnd != region.hwnd ||
        cpuUiView->width != width || cpuUiView->height != height ||
        cpuUiView->rowPitch != width * 4u)
    {
        status(23);
        return false;
    }
    auto& sequence = const_cast<std::uint32_t&>(cpuUiView->sequence);
    std::atomic_ref<std::uint32_t> publishedSequence(sequence);
    const std::uint32_t before = publishedSequence.load(
        std::memory_order_acquire);
    if ((before & 1u) || cpuUiView->frame == 0)
        return uiView != nullptr;
    if (cpuUiView->frame == uiCopiedFrame && uiView)
        return true;
    cpuUiPixels.resize(static_cast<std::size_t>(bytes));
    std::memcpy(cpuUiPixels.data(), cpuUiView + 1,
        static_cast<std::size_t>(bytes));
    const std::uint32_t frame = cpuUiView->frame;
    if (before != publishedSequence.load(std::memory_order_acquire))
    {
        status(23);
        return uiView != nullptr;
    }
    if (!uiSnapshot)
    {
        D3D11_TEXTURE2D_DESC description {};
        description.Width = width;
        description.Height = height;
        description.MipLevels = 1;
        description.ArraySize = 1;
        description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        description.SampleDesc.Count = 1;
        description.Usage = D3D11_USAGE_DEFAULT;
        description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
        if (FAILED(device->CreateTexture2D(&description, nullptr,
                &uiSnapshot)) ||
            FAILED(device->CreateShaderResourceView(uiSnapshot.Get(),
                nullptr, &uiView)))
        {
            status(23);
            return false;
        }
    }
    context->UpdateSubresource(uiSnapshot.Get(), 0, nullptr,
        cpuUiPixels.data(), width * 4u, 0);
    uiCopiedFrame = frame;
    status(22);
    return true;
}

bool update_ui(ID3D11Device* device, ID3D11DeviceContext* context,
    const LiquidDwmRegion& region, const RECT& rectangle)
{
    if (uiTransportOwner != region.ownerPid ||
        uiTransportWindow != region.hwnd)
    {
        reset_ui();
        reset_cpu_ui();
        gpuUnavailableOwner = 0;
        uiTransportOwner = region.ownerPid;
        uiTransportWindow = region.hwnd;
    }
    if (gpuUnavailableOwner != region.ownerPid && region.uiFrame > 0)
    {
        if (update_ui_gpu(device, context, region, rectangle))
            return true;
        gpuUnavailableOwner = region.ownerPid;
        reset_ui();
    }
    return update_ui_cpu(device, context, region, rectangle);
}

bool ensure_renderer(ID3D11Device* device)
{
    if (rendererDevice.Get() != device)
    {
        reset_ui();
        reset_cpu_ui();
        uiTransportOwner = 0;
        uiTransportWindow = 0;
        gpuUnavailableOwner = 0;
        renderer = LiquidGlassD3D11 {};
        const std::wstring path = module_path(extensionModule);
        const auto separator = path.find_last_of(L"\\/");
        rendererDevice = separator != std::wstring::npos &&
            renderer.initialise(device, path.substr(0, separator)) ?
                device : nullptr;
        detail(20, rendererDevice ? 100 :
            -100 - renderer.initialisation_stage());
    }
    return rendererDevice != nullptr;
}

std::uint64_t present_hook(void* self, void* chain, std::uint32_t flags,
    const std::vector<RECT>& dirtyRects, std::uint32_t unknown,
    bool* value, bool legacy)
{
    status(1);
    // Window content owns the optical draw. Replaying the UI here bypasses
    // DWM's window order and leaves retained copies on secondary displays.
    return originalPresent(self, chain, flags, dirtyRects,
        unknown, value, legacy);
}

bool source_from_context(void* drawingContext,
    ID3D11Texture2D*& texture,
    ID3D11RenderTargetView*& target) noexcept
{
    texture = nullptr;
    target = nullptr;
    __try
    {
        auto* deviceTarget = reinterpret_cast<void* const*>(
            drawingContext)[4];
        if (!deviceTarget) return false;
        auto* textureInterface = reinterpret_cast<std::uint8_t*>(
            deviceTarget) - 16;
        auto* textureTable = *reinterpret_cast<void***>(textureInterface);
        auto* targetTable = *reinterpret_cast<void***>(deviceTarget);
        texture = reinterpret_cast<TextureFn>(textureTable[15])(
            textureInterface, nullptr);
        target = reinterpret_cast<TargetFn>(targetTable[22])(
            deviceTarget);
        if (!texture || !target) return false;
        ID3D11Resource* targetResource = nullptr;
        target->GetResource(&targetResource);
        const bool same = targetResource == texture;
        if (targetResource) targetResource->Release();
        return same;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status(104);
        texture = nullptr;
        target = nullptr;
        return false;
    }
}

IDXGIResource* current_capture_buffer(void* self) noexcept;

ID2D1DeviceContext* d2d_context_from_drawing(void* drawingContext) noexcept
{
    __try
    {
        auto* renderDevice = reinterpret_cast<void* const*>(drawingContext)[5];
        if (!renderDevice) return nullptr;
        auto* context = *reinterpret_cast<ID2D1DeviceContext**>(
            static_cast<std::uint8_t*>(renderDevice) + 0xd8);
        if (context) context->AddRef();
        return context;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool current_d2d_target(void* drawingContext, ComPtr<ID3D11Texture2D>& texture,
    ComPtr<ID3D11RenderTargetView>& target)
{
    ComPtr<ID2D1DeviceContext> context;
    context.Attach(d2d_context_from_drawing(drawingContext));
    if (!context) return false;
    ComPtr<ID2D1Image> image;
    context->GetTarget(&image);
    ComPtr<ID2D1Bitmap1> bitmap;
    ComPtr<IDXGISurface> surface;
    ComPtr<IDXGISurface2> surface2;
    UINT subresource = 0;
    if (!image || FAILED(image.As(&bitmap)) ||
        FAILED(bitmap->GetSurface(&surface)) || FAILED(surface.As(&surface2)) ||
        FAILED(surface2->GetResource(IID_PPV_ARGS(&texture), &subresource)) ||
        subresource != 0u) return false;
    ComPtr<ID3D11Device> device;
    texture->GetDevice(&device);
    return device && SUCCEEDED(device->CreateRenderTargetView(texture.Get(),
        nullptr, &target));
}

struct WindowDrawState
{
    LiquidDwmRegion published {};
    std::uint32_t slot = liquidDwmRegionSlots;
};
thread_local WindowDrawState* activeWindowDraw = nullptr;

HRESULT window_render_hook(void* self, void* drawingContext, bool* rendered)
{
    status(96);
    const HWND hwnd = windowHwnd ? windowHwnd(self) : nullptr;
    WindowDrawState state {};
    const bool hasRegion = read_window_region(hwnd, state.published, state.slot);
    if (hasRegion) status(97);
    auto* previous = activeWindowDraw;
    activeWindowDraw = hasRegion ? &state : nullptr;
    const HRESULT result = originalWindowRender(self, drawingContext, rendered);
    activeWindowDraw = previous;
    return result;
}

std::uint32_t marker_slot(const float* colour) noexcept
{
    constexpr float alpha = liquidMarkerAlpha / 255.0f;
    if (!colour || std::abs(colour[3] - alpha) > 0.00001f)
        return liquidDwmRegionSlots;
    for (std::uint32_t slot = 0; slot < liquidDwmRegionSlots; ++slot)
    {
        const float encoded[3] = { (liquidMarkerRed + slot) / 255.0f,
            liquidMarkerGreen / 255.0f, liquidMarkerBlue / 255.0f };
        for (const bool linear : { false, true })
            for (const float scale : { 1.0f, alpha })
            {
                bool matches = true;
                for (int index = 0; index < 3; ++index)
                {
                    const float channel = !linear ? encoded[index] :
                        encoded[index] <= 0.04045f ? encoded[index] / 12.92f :
                        std::pow((encoded[index] + 0.055f) / 1.055f, 2.4f);
                    matches = matches &&
                        std::abs(colour[index] - channel * scale) < 0.000001f;
                }
                if (matches) return slot;
            }
    }
    return liquidDwmRegionSlots;
}

bool read_colour_brush(void* content, float (&colour)[4]) noexcept
{
    __try
    {
        const auto expected = reinterpret_cast<std::uintptr_t>(dwmcoreModule) +
            hookConfig.colourBrushVtableRva;
        if (*reinterpret_cast<const std::uintptr_t*>(content) != expected)
            return false;
        std::memcpy(colour, static_cast<const std::uint8_t*>(content) +
            (windows10Adapter ? -0x18 : 0x68),
            sizeof(colour));
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool windows10_optical_render_ready() noexcept;
void windows10_suspend_optical_render(HRESULT reason) noexcept;
bool windows10_device_matches(ID3D11Device* first, ID3D11Device* second) noexcept;
#include "liquid_glass_dwm_batch.h"
#include "liquid_glass_dwm_windows10.h"
#include "liquid_glass_dwm_windows10_lifecycle.h"

struct MarkerContentState
{
    std::uint32_t slot = liquidDwmRegionSlots;
    float width = 0.0f;
    float height = 0.0f;
    LiquidDwmRegion published {};
};
thread_local const MarkerContentState* activeMarkerContent = nullptr;
thread_local bool inContentDraw = false;

HRESULT scoped_content_draw(ContentDrawFn original, void* self,
    void* drawingContext, const float* size, void* cache)
{
    MarkerContentState state {};
    float colour[4] {};
    if (size && read_colour_brush(self, colour))
    {
        state.slot = marker_slot(colour);
        state.width = size[0];
        state.height = size[1];
        D3D11_TEXTURE2D_DESC unused {};
        if (!read_capture_region(unused, state.slot, state.published))
            state.slot = liquidDwmRegionSlots;
    }
    const auto* previous = activeMarkerContent;
    const bool previousContentDraw = inContentDraw;
    activeMarkerContent = state.slot != liquidDwmRegionSlots ? &state : nullptr;
    inContentDraw = true;
    const HRESULT result = original(self, drawingContext, size, cache);
    activeMarkerContent = previous;
    inContentDraw = previousContentDraw;
    return result;
}

HRESULT content_draw_hook(void* self, void* drawingContext,
    const float* size, void* cache)
{
    return scoped_content_draw(originalContentDraw, self, drawingContext, size, cache);
}

HRESULT brush_draw_hook(void* self, void* drawingContext, const float* size,
    void* cache)
{
    return scoped_content_draw(originalBrushDraw, self, drawingContext, size, cache);
}

HRESULT draw_list_entry_render_hook(void* entry, void* drawingContext,
    const void* matrix, float opacity, std::uint32_t blendMode)
{
    status(149);
    if (activeMarkerContent)
    {
        status(150);
        tag_native_marker_entry(entry, activeMarkerContent->slot,
            activeMarkerContent->width, activeMarkerContent->height,
            activeMarkerContent->published);
    }
    else if (windows10Adapter && inContentDraw)
    {
        // Reusing an entry for an ordinary brush must clear its old marker,
        // even when DWM retains the geometry and shared rendering effect.
        untag_native_marker_entry(entry);
    }
    else (void)preserve_native_marker_entry(entry);
    return originalDrawListEntryRender(entry, drawingContext, matrix, opacity, blendMode);
}

HRESULT batch_command_emit_hook(void* command, void* executionContext, bool value)
{
    status(145);
    if (render_batch_marker(command, executionContext)) return S_OK;
    return originalBatchCommandEmit(command, executionContext, value);
}

HRESULT common_effect_emit_hook(void* self, void* executionContext, void* command)
{
    status(146);
    if (render_batch_marker(command, executionContext)) return S_OK;
    return originalCommonEffectEmit(self, executionContext, command);
}

HRESULT brush_effect_emit_hook(void* self, void* executionContext, void* command)
{
    status(156);
    if (render_batch_marker(command, executionContext)) return S_OK;
    return originalBrushEffectEmit(self, executionContext, command);
}

HRESULT capture_render_hook(void* self, void* drawingContext, bool* rendered)
{
    status(106);
    const bool previous = inCaptureRender;
    void* previousTarget = activeCaptureTarget;
    inCaptureRender = true;
    activeCaptureTarget = self;
    const HRESULT result = originalCaptureRender(self, drawingContext,
        rendered);
    inCaptureRender = previous;
    activeCaptureTarget = previousTarget;
    return result;
}

// Keep the DDA target identity while its window nodes are being composed.
HRESULT dda_render_hook(void* self, void* drawingContext)
{
    status(108);
    void* previous = activeDdaTarget;
    activeDdaTarget = self;
    const HRESULT result = originalDdaRender(self, drawingContext);
    activeDdaTarget = previous;
    return result;
}

HRESULT dda_present_hook(void* self)
{
    status(132);
    return originalDdaPresent(self);
}

IDXGIResource* current_capture_buffer(void* self) noexcept
{
    __try
    {
        return captureCurrentFrameBuffer ?
            captureCurrentFrameBuffer(self) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER)
    {
        status(119);
        return nullptr;
    }
}

// This callback runs on DWM's capture output, before it is handed to desktop
// capture clients. Inspect the buffer first; only draw after its layout and
// ownership have been verified on the installed DWM build.
HRESULT capture_present_hook(void* self)
{
    status(112);
    return originalCapturePresent(self);
}

HRESULT offscreen_render_hook(void* self, void* drawingContext,
    bool* rendered)
{
    status(107);
    return originalOffscreenRender(self, drawingContext, rendered);
}

HRESULT drawing_flush_hook(void* self)
{
    const HRESULT result = drawingFlush(self);
    if (!inCaptureRender || FAILED(result)) return result;
    status(108);
    ID3D11Texture2D* texture = nullptr;
    ID3D11RenderTargetView* target = nullptr;
    if (source_from_context(self, texture, target))
    {
        status(109);
        D3D11_TEXTURE2D_DESC desc {};
        texture->GetDesc(&desc);
        detail(110, static_cast<LONG>(desc.Width));
        detail(111, static_cast<LONG>(desc.Height));
    }
    return result;
}

bool start_windows10_hooks(const std::uint8_t* base, std::uint32_t imageSize)
{
    const HANDLE mapping = OpenFileMappingW(FILE_MAP_READ, FALSE, liquidDwmWindows10ConfigName);
    if (!mapping) return false;
    const auto* view = static_cast<const LiquidDwmWindows10Config*>(MapViewOfFile(
        mapping, FILE_MAP_READ, 0, 0, sizeof(LiquidDwmWindows10Config)));
    if (view) { windows10Config = *view; UnmapViewOfFile(view); }
    CloseHandle(mapping);
    if (!view || std::memcmp(windows10Config.dwmcoreSha256, hookConfig.dwmcoreSha256, 32u) != 0 ||
        !validate_liquid_windows10_layout({ base, imageSize }, windows10Config,
            hookConfig.commonEffectEmitRva, hookConfig.brushDrawRva,
            hookConfig.drawListEntryRenderRva)) return false;
    for (auto rva : { hookConfig.brushDrawRva, hookConfig.drawListEntryRenderRva,
            hookConfig.drawListEntryVtableRva, hookConfig.colourBrushVtableRva,
            hookConfig.commonEffectEmitRva, hookConfig.brushEffectEmitRva, hookConfig.batchFlushRva })
        if (!rva || rva >= imageSize) return false;
    originalBrushDraw = reinterpret_cast<ContentDrawFn>(const_cast<std::uint8_t*>(base) + hookConfig.brushDrawRva);
    originalDrawListEntryRender = reinterpret_cast<DrawListEntryRenderFn>(const_cast<std::uint8_t*>(base) + hookConfig.drawListEntryRenderRva);
    originalCommonEffectEmit = reinterpret_cast<EffectEmitFn>(const_cast<std::uint8_t*>(base) + hookConfig.commonEffectEmitRva);
    originalBrushEffectEmit = reinterpret_cast<EffectEmitFn>(const_cast<std::uint8_t*>(base) + hookConfig.brushEffectEmitRva);
    nativeBatchFlush = reinterpret_cast<NativeBatchFlushFn>(const_cast<std::uint8_t*>(base) + hookConfig.batchFlushRva);
    if (!windows10Lifecycle.initialise()) return false;
    PublicSynchroniseSecurity security;
    readyEvent = CreateEventW(security.get(), TRUE, FALSE, liquidDwmReadyName);
    if (!readyEvent || DetourTransactionBegin() != NO_ERROR) return false;
    if (DetourUpdateThread(GetCurrentThread()) != NO_ERROR) { DetourTransactionAbort(); return false; }
    std::vector<HANDLE> threads;
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    bool synchronised = snapshot != INVALID_HANDLE_VALUE;
    if (synchronised)
    {
        THREADENTRY32 entry { sizeof(entry) };
        if (Thread32First(snapshot, &entry)) do
        {
            if (entry.th32OwnerProcessID != GetCurrentProcessId() ||
                entry.th32ThreadID == GetCurrentThreadId()) continue;
            const HANDLE thread = OpenThread(THREAD_GET_CONTEXT | THREAD_SET_CONTEXT |
                THREAD_SUSPEND_RESUME, FALSE, entry.th32ThreadID);
            if (!thread) { synchronised = false; break; }
            threads.push_back(thread);
            if (DetourUpdateThread(thread) != NO_ERROR) { synchronised = false; break; }
        } while (Thread32Next(snapshot, &entry));
        else synchronised = false;
        CloseHandle(snapshot);
    }
    LONG result = synchronised ? NO_ERROR : ERROR_ACCESS_DENIED;
    const auto attach = [&](auto* original, auto hook) {
        if (result == NO_ERROR) result = DetourAttach(reinterpret_cast<PVOID*>(original), reinterpret_cast<PVOID>(hook));
    };
    attach(&originalBrushDraw, &brush_draw_hook);
    attach(&originalDrawListEntryRender, &draw_list_entry_render_hook);
    attach(&originalCommonEffectEmit, &windows10_common_emit_hook);
    attach(&originalBrushEffectEmit, &windows10_brush_emit_hook);
    if (result == NO_ERROR) result = DetourTransactionCommit();
    else DetourTransactionAbort();
    for (HANDLE thread : threads) CloseHandle(thread);
    if (result != NO_ERROR) { detail(254, result); return false; }
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&windows10_common_emit_hook), &pinned);
    detail(253, 19045);
    stage(5);
    windows10Lifecycle.sample();
    stage(11);
    return true;
}

bool start_extension()
{
    statusMapping = OpenFileMappingW(FILE_MAP_WRITE, FALSE,
        liquidDwmStatusName);
    if (statusMapping)
    {
        statusView = static_cast<volatile LONG*>(MapViewOfFile(statusMapping,
            FILE_MAP_WRITE, 0, 0, liquidDwmStatusWords * sizeof(LONG)));
    }
    stage(1);
    const HANDLE configMapping = OpenFileMappingW(FILE_MAP_READ, FALSE,
        liquidDwmConfigName);
    if (!configMapping) return false;
    const auto* config = static_cast<const LiquidDwmHookConfig*>(
        MapViewOfFile(configMapping, FILE_MAP_READ, 0, 0,
            sizeof(LiquidDwmHookConfig)));
    if (config)
    {
        hookConfig = *config;
        UnmapViewOfFile(config);
    }
    CloseHandle(configMapping);
    stage(2);
#if defined(_M_ARM64)
    constexpr std::uint32_t machine = IMAGE_FILE_MACHINE_ARM64;
#else
    constexpr std::uint32_t machine = IMAGE_FILE_MACHINE_AMD64;
#endif
    using GetVersionFn = LONG (WINAPI*)(OSVERSIONINFOW*);
    const auto getVersion = reinterpret_cast<GetVersionFn>(GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion"));
    OSVERSIONINFOW version { sizeof(version) };
    if (!getVersion || getVersion(&version) != 0 ||
        !liquid_dwm_native_hooks_allowed(version.dwBuildNumber, machine))
    {
        stage(23);
        return false;
    }
    windows10Adapter = liquid_dwm_uses_windows10_adapter(version.dwBuildNumber, machine);
    if (!config || hookConfig.version != liquidDwmProtocolVersion || hookConfig.moduleMachine != machine) return false;
    if (!windows10Adapter && (!hookConfig.presentRva || !hookConfig.displaySwapchainVtableRva ||
        !hookConfig.displayPhysicalRva || !hookConfig.displayBufferVtableRva ||
        !hookConfig.displayResourceRva || !hookConfig.contentDrawRva ||
        !hookConfig.colourBrushVtableRva || !hookConfig.applyRenderStateRva ||
        !hookConfig.batchCommandEmitRva || !hookConfig.commonEffectEmitRva ||
        !hookConfig.brushEffectEmitRva || !hookConfig.batchFlushRva ||
        !hookConfig.drawListEntryVtableRva || !hookConfig.brushDrawRva ||
        !hookConfig.drawListEntryRenderRva))
        return false;
    dwmcoreModule = GetModuleHandleW(L"dwmcore.dll");
    if (!dwmcoreModule) return false;
    std::uint8_t digest[32] {};
    const std::wstring path = module_path(dwmcoreModule);
    if (path.empty() || !file_sha256(path, digest) ||
        std::memcmp(digest, hookConfig.dwmcoreSha256, 32) != 0)
        return false;
    stage(3);
    const auto* base = reinterpret_cast<const std::uint8_t*>(dwmcoreModule);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    const auto imageSize = nt->OptionalHeader.SizeOfImage;
    if (windows10Adapter) return start_windows10_hooks(base, imageSize);
    const std::array<std::uint64_t, 5> mandatory {{
        hookConfig.presentRva,
        hookConfig.displaySwapchainVtableRva + 64 * sizeof(void*),
        hookConfig.displayPhysicalRva,
        hookConfig.displayBufferVtableRva + 64 * sizeof(void*),
        hookConfig.displayResourceRva
    }};
    for (const auto rva : mandatory)
        if (rva >= imageSize) return false;
    if (hookConfig.windowRenderRva >= imageSize ||
        hookConfig.windowGetHwndRva >= imageSize ||
        hookConfig.drawingFlushRva >= imageSize ||
        hookConfig.contentDrawRva >= imageSize ||
        hookConfig.colourBrushVtableRva >= imageSize ||
        hookConfig.applyRenderStateRva >= imageSize ||
        hookConfig.batchCommandEmitRva >= imageSize ||
        hookConfig.commonEffectEmitRva >= imageSize ||
        hookConfig.brushEffectEmitRva >= imageSize ||
        hookConfig.batchFlushRva >= imageSize ||
        hookConfig.drawListEntryVtableRva >= imageSize ||
        hookConfig.brushDrawRva >= imageSize ||
        hookConfig.drawListEntryRenderRva >= imageSize ||
        hookConfig.captureRenderRva >= imageSize ||
        hookConfig.offscreenRenderRva >= imageSize ||
        hookConfig.capturePresentRva >= imageSize ||
        hookConfig.captureCurrentFrameBufferRva >= imageSize ||
        hookConfig.ddaRenderRva >= imageSize ||
        hookConfig.ddaPresentRva >= imageSize ||
        hookConfig.ddaGetBackBufferRva >= imageSize ||
        hookConfig.ddaGetTexture2DRva >= imageSize ||
        hookConfig.ddaSwapchainVtableRva >= imageSize ||
        hookConfig.ddaTargetVtableRva >= imageSize)
        return false;
    if (find_slot(hookConfig.displaySwapchainVtableRva,
            hookConfig.displayPhysicalRva) == UINT32_MAX ||
        find_slot(hookConfig.displayBufferVtableRva,
            hookConfig.displayResourceRva) == UINT32_MAX)
        return false;
    stage(4);
    originalPresent = reinterpret_cast<PresentFn>(
        const_cast<std::uint8_t*>(base) + hookConfig.presentRva);
    originalContentDraw = reinterpret_cast<ContentDrawFn>(
        const_cast<std::uint8_t*>(base) + hookConfig.contentDrawRva);
    originalBrushDraw = reinterpret_cast<ContentDrawFn>(
        const_cast<std::uint8_t*>(base) + hookConfig.brushDrawRva);
    originalDrawListEntryRender = reinterpret_cast<DrawListEntryRenderFn>(
        const_cast<std::uint8_t*>(base) + hookConfig.drawListEntryRenderRva);
    applyRenderState = reinterpret_cast<ApplyRenderStateFn>(
        const_cast<std::uint8_t*>(base) + hookConfig.applyRenderStateRva);
    originalBatchCommandEmit = reinterpret_cast<BatchCommandEmitFn>(
        const_cast<std::uint8_t*>(base) + hookConfig.batchCommandEmitRva);
    originalCommonEffectEmit = reinterpret_cast<EffectEmitFn>(
        const_cast<std::uint8_t*>(base) + hookConfig.commonEffectEmitRva);
    originalBrushEffectEmit = reinterpret_cast<EffectEmitFn>(
        const_cast<std::uint8_t*>(base) + hookConfig.brushEffectEmitRva);
    nativeBatchFlush = reinterpret_cast<NativeBatchFlushFn>(
        const_cast<std::uint8_t*>(base) + hookConfig.batchFlushRva);
    if (hookConfig.windowRenderRva && hookConfig.windowGetHwndRva &&
        hookConfig.drawingFlushRva)
    {
        originalWindowRender = reinterpret_cast<WindowRenderFn>(
            const_cast<std::uint8_t*>(base) + hookConfig.windowRenderRva);
        windowHwnd = reinterpret_cast<WindowHwndFn>(
            const_cast<std::uint8_t*>(base) + hookConfig.windowGetHwndRva);
        drawingFlush = reinterpret_cast<DrawingFlushFn>(
            const_cast<std::uint8_t*>(base) + hookConfig.drawingFlushRva);
    }
    if (hookConfig.captureRenderRva)
        originalCaptureRender = reinterpret_cast<WindowRenderFn>(
            const_cast<std::uint8_t*>(base) + hookConfig.captureRenderRva);
    if (hookConfig.ddaRenderRva)
        originalDdaRender = reinterpret_cast<DdaRenderFn>(
            const_cast<std::uint8_t*>(base) + hookConfig.ddaRenderRva);
    if (hookConfig.ddaPresentRva &&
        hookConfig.ddaGetBackBufferRva &&
        hookConfig.ddaGetTexture2DRva &&
        hookConfig.ddaSwapchainVtableRva &&
        hookConfig.ddaTargetVtableRva &&
        find_slot(hookConfig.ddaSwapchainVtableRva,
            hookConfig.ddaGetBackBufferRva) != UINT32_MAX)
    {
        originalDdaPresent = reinterpret_cast<DdaPresentFn>(
            const_cast<std::uint8_t*>(base) + hookConfig.ddaPresentRva);
        ddaGetBackBuffer = reinterpret_cast<DdaBackBufferFn>(
            const_cast<std::uint8_t*>(base) +
                hookConfig.ddaGetBackBufferRva);
        ddaGetTexture2D = reinterpret_cast<DdaTextureFn>(
            const_cast<std::uint8_t*>(base) +
                hookConfig.ddaGetTexture2DRva);
    }
    if (hookConfig.offscreenRenderRva)
        originalOffscreenRender = reinterpret_cast<WindowRenderFn>(
            const_cast<std::uint8_t*>(base) + hookConfig.offscreenRenderRva);
    if (hookConfig.capturePresentRva &&
        hookConfig.captureCurrentFrameBufferRva)
    {
        originalCapturePresent = reinterpret_cast<CapturePresentFn>(
            const_cast<std::uint8_t*>(base) + hookConfig.capturePresentRva);
        captureCurrentFrameBuffer = reinterpret_cast<CaptureBufferFn>(
            const_cast<std::uint8_t*>(base) +
                hookConfig.captureCurrentFrameBufferRva);
    }
    PublicSynchroniseSecurity security;
    readyEvent = CreateEventW(security.get(), TRUE, FALSE,
        liquidDwmReadyName);
    if (!readyEvent || DetourTransactionBegin() != NO_ERROR) return false;
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
                    entry.th32ThreadID == GetCurrentThreadId()) continue;
                const HANDLE thread = OpenThread(THREAD_GET_CONTEXT |
                    THREAD_SET_CONTEXT | THREAD_SUSPEND_RESUME, FALSE,
                    entry.th32ThreadID);
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
    // Scope the marker to the island's native draw in every output path.
    const LONG windowAttached = attached == NO_ERROR && originalWindowRender ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalWindowRender),
            reinterpret_cast<PVOID>(&window_render_hook)) : NO_ERROR;
    const LONG captureAttached = attached == NO_ERROR &&
        windowAttached == NO_ERROR && originalCaptureRender ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalCaptureRender),
            reinterpret_cast<PVOID>(&capture_render_hook)) : NO_ERROR;
    const LONG offscreenAttached = attached == NO_ERROR &&
        windowAttached == NO_ERROR && captureAttached == NO_ERROR &&
        originalOffscreenRender ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalOffscreenRender),
            reinterpret_cast<PVOID>(&offscreen_render_hook)) : NO_ERROR;
    const LONG flushAttached = attached == NO_ERROR &&
        windowAttached == NO_ERROR && captureAttached == NO_ERROR &&
        offscreenAttached == NO_ERROR && drawingFlush ?
        DetourAttach(reinterpret_cast<PVOID*>(&drawingFlush),
            reinterpret_cast<PVOID>(&drawing_flush_hook)) : NO_ERROR;
    const LONG capturePresentAttached = attached == NO_ERROR &&
        windowAttached == NO_ERROR && captureAttached == NO_ERROR &&
        offscreenAttached == NO_ERROR && flushAttached == NO_ERROR &&
        originalCapturePresent ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalCapturePresent),
            reinterpret_cast<PVOID>(&capture_present_hook)) : NO_ERROR;
    const LONG ddaAttached = attached == NO_ERROR &&
        windowAttached == NO_ERROR && captureAttached == NO_ERROR &&
        offscreenAttached == NO_ERROR && flushAttached == NO_ERROR &&
        capturePresentAttached == NO_ERROR && originalDdaRender ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalDdaRender),
            reinterpret_cast<PVOID>(&dda_render_hook)) : NO_ERROR;
    const LONG ddaPresentAttached = attached == NO_ERROR &&
        windowAttached == NO_ERROR && captureAttached == NO_ERROR &&
        offscreenAttached == NO_ERROR && flushAttached == NO_ERROR &&
        capturePresentAttached == NO_ERROR && ddaAttached == NO_ERROR &&
        originalDdaPresent ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalDdaPresent),
            reinterpret_cast<PVOID>(&dda_present_hook)) : NO_ERROR;
    const LONG markerAttached = attached == NO_ERROR &&
        windowAttached == NO_ERROR && captureAttached == NO_ERROR &&
        offscreenAttached == NO_ERROR && flushAttached == NO_ERROR &&
        capturePresentAttached == NO_ERROR && ddaAttached == NO_ERROR &&
        ddaPresentAttached == NO_ERROR ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalContentDraw),
            reinterpret_cast<PVOID>(&content_draw_hook)) : ERROR_INVALID_FUNCTION;
    const LONG commandAttached = markerAttached == NO_ERROR ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalBatchCommandEmit),
            reinterpret_cast<PVOID>(&batch_command_emit_hook)) : ERROR_INVALID_FUNCTION;
    const LONG commonAttached = commandAttached == NO_ERROR ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalCommonEffectEmit),
            reinterpret_cast<PVOID>(&common_effect_emit_hook)) : ERROR_INVALID_FUNCTION;
    const LONG brushAttached = commonAttached == NO_ERROR ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalBrushEffectEmit),
            reinterpret_cast<PVOID>(&brush_effect_emit_hook)) : ERROR_INVALID_FUNCTION;
    const LONG sourceBrushAttached = brushAttached == NO_ERROR ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalBrushDraw),
            reinterpret_cast<PVOID>(&brush_draw_hook)) : ERROR_INVALID_FUNCTION;
    const LONG entryAttached = sourceBrushAttached == NO_ERROR ?
        DetourAttach(reinterpret_cast<PVOID*>(&originalDrawListEntryRender),
            reinterpret_cast<PVOID>(&draw_list_entry_render_hook)) : ERROR_INVALID_FUNCTION;
    const LONG committed = attached == NO_ERROR && windowAttached == NO_ERROR &&
        captureAttached == NO_ERROR && offscreenAttached == NO_ERROR &&
        flushAttached == NO_ERROR && capturePresentAttached == NO_ERROR &&
        ddaAttached == NO_ERROR && ddaPresentAttached == NO_ERROR &&
        markerAttached == NO_ERROR && commandAttached == NO_ERROR &&
        commonAttached == NO_ERROR && brushAttached == NO_ERROR &&
        sourceBrushAttached == NO_ERROR && entryAttached == NO_ERROR ?
        DetourTransactionCommit() : DetourTransactionAbort();
    for (HANDLE thread : threads) CloseHandle(thread);
    if (attached != NO_ERROR || windowAttached != NO_ERROR ||
        captureAttached != NO_ERROR || offscreenAttached != NO_ERROR ||
        flushAttached != NO_ERROR || capturePresentAttached != NO_ERROR ||
        ddaAttached != NO_ERROR || ddaPresentAttached != NO_ERROR ||
        markerAttached != NO_ERROR || commandAttached != NO_ERROR ||
        commonAttached != NO_ERROR || brushAttached != NO_ERROR ||
        sourceBrushAttached != NO_ERROR || entryAttached != NO_ERROR ||
        committed != NO_ERROR) return false;
    HMODULE pinned = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&present_hook), &pinned);
    stage(5);
    if (!SetEvent(readyEvent)) return false;
    stage(11);
    return true;
}

DWORD WINAPI worker(void*)
{
    if (start_extension() && windows10Adapter) windows10Lifecycle.run();
    else if (windows10Adapter) windows10Lifecycle.stop();
    return 0;
}
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, void*)
{
    if (reason == DLL_PROCESS_ATTACH)
    {
        extensionModule = instance;
        DisableThreadLibraryCalls(instance);
        const HANDLE thread = CreateThread(nullptr, 0, &worker,
            nullptr, 0, nullptr);
        if (thread) CloseHandle(thread);
    }
    return TRUE;
}

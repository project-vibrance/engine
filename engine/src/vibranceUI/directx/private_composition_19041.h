#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dcomp.h>
#include <dwmapi.h>
#include <inspectable.h>
#include <unknwn.h>

// The interfaces and exports in this file are private Windows ABI. They are
// deliberately isolated from vibranceUI's public headers and must only be used
// after runtime capability checks. None of these calls reads compositor pixels
// back to CPU or exposes a capture texture.
namespace vibrance::directx::private_composition_19041
{
MIDL_INTERFACE("e7894c70-af56-4f52-b382-4b3cd263dc6f")
IInteropCompositorPartner : public IUnknown
{
public:
    virtual HRESULT STDMETHODCALLTYPE MarkDirty() = 0;
    virtual HRESULT STDMETHODCALLTYPE ClearCallback() = 0;
    virtual HRESULT STDMETHODCALLTYPE CreateManipulationTransform(
        IDCompositionTransform* transform,
        REFIID iid,
        void** result) = 0;
    virtual HRESULT STDMETHODCALLTYPE RealClose() = 0;
};

struct IInteropCompositorPartnerCallback;

MIDL_INTERFACE("22118adf-23f1-4801-bcfa-66cbf48cc51b")
IInteropCompositorFactoryPartner : public IInspectable
{
public:
    virtual HRESULT STDMETHODCALLTYPE CreateInteropCompositor(
        IUnknown* renderingDevice,
        IInteropCompositorPartnerCallback* callback,
        REFIID iid,
        void** instance) = 0;
    virtual HRESULT STDMETHODCALLTYPE CheckEnabled(
        boolean* interopCompositorEnabled,
        boolean* exposeVisualEnabled) = 0;
};

using CreateSharedMultiWindowVisualFn = HRESULT(WINAPI*)(
    HWND destination,
    void* compositionDevice,
    void** visual,
    HTHUMBNAIL* thumbnail);

using CreateSharedThumbnailVisualFn = HRESULT(WINAPI*)(
    HWND destination,
    HWND source,
    DWORD flags,
    const DWM_THUMBNAIL_PROPERTIES* properties,
    void* compositionDevice,
    void** visual,
    HTHUMBNAIL* thumbnail);

using UpdateSharedVirtualDesktopVisualFn = HRESULT(WINAPI*)(
    HTHUMBNAIL thumbnail,
    HWND* includeWindows,
    DWORD includeCount,
    HWND* excludeWindows,
    DWORD excludeCount,
    RECT* source,
    SIZE* destinationSize);

using UpdateSharedMultiWindowVisualFn = HRESULT(WINAPI*)(
    HTHUMBNAIL thumbnail,
    HWND* includeWindows,
    DWORD includeCount,
    HWND* excludeWindows,
    DWORD excludeCount,
    RECT* source,
    SIZE* destinationSize,
    DWORD flags);

enum class WindowCompositionAttribute : DWORD
{
    ExcludedFromLivePreview = 13u
};

struct WindowCompositionAttributeData
{
    WindowCompositionAttribute attribute;
    void* data;
    SIZE_T size;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI*)(
    HWND window,
    WindowCompositionAttributeData* data);

constexpr WORD createSharedMultiWindowVisualOrdinal = 163u;
constexpr WORD updateSharedMultiWindowVisualOrdinal = 164u;
constexpr WORD createSharedThumbnailVisualOrdinal = 147u;
constexpr DWORD thumbnailEnable3D = 0x04000000u;
constexpr DWORD firstModernMultiWindowBuild = 20000u;
constexpr DWORD minimumSupportedBuild = 19041u;
}

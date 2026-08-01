#pragma once

#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <dcomp.h>
#include <inspectable.h>
#include <unknwn.h>

// The interfaces and exports in this file are private Windows ABI. They are
// isolated from vibranceUI's public headers and runtime-probed only to create a
// compatible desktop Composition target on Windows builds where the public
// target is already occupied. Material rendering uses public Visual Layer APIs.
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

constexpr DWORD minimumSupportedBuild = 19041u;
}

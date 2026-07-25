#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <initguid.h>

#include "composition_bridge_abi.h"
#include "private_composition_19041.h"

#include <DispatcherQueue.h>
#include <d2d1_2.h>
#include <d2d1helper.h>
#include <d2d1effects.h>
#include <d3d11_4.h>
#include <dwmapi.h>
#include <dxgi1_4.h>
#include <shobjidl.h>
#include <wincodec.h>
#include <windows.graphics.effects.interop.h>
#include <windows.graphics.interop.h>
#include <windows.ui.composition.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.DirectX.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/base.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <utility>
#include <vector>

// These DWM declarations were published after SDK 19041. Keep their stable
// numeric ABI locally so the bridge can compile against 19041 while probing
// support on the running Windows build before it uses them.
#if !defined(DWMWA_USE_HOSTBACKDROPBRUSH)
#define DWMWA_USE_HOSTBACKDROPBRUSH static_cast<DWMWINDOWATTRIBUTE>(17)
#endif
#if !defined(DWMWA_SYSTEMBACKDROP_TYPE)
#define DWMWA_SYSTEMBACKDROP_TYPE static_cast<DWMWINDOWATTRIBUTE>(38)
enum DWM_SYSTEMBACKDROP_TYPE
{
    DWMSBT_AUTO = 0,
    DWMSBT_NONE = 1,
    DWMSBT_MAINWINDOW = 2,
    DWMSBT_TRANSIENTWINDOW = 3,
    DWMSBT_TABBEDWINDOW = 4
};
#endif

namespace abi_effects = ABI::Windows::Graphics::Effects;
namespace composition = winrt::Windows::UI::Composition;
namespace composition_directx = winrt::Windows::Graphics::DirectX;
namespace effects = winrt::Windows::Graphics::Effects;
namespace private_composition = vibrance::directx::private_composition_19041;

template <>
inline constexpr winrt::guid winrt::impl::guid_v<
    abi_effects::IGraphicsEffectD2D1Interop> {
    0x2FC57384,
    0xA068,
    0x44D7,
    { 0xA3, 0x31, 0x30, 0x98, 0x2F, 0xCF, 0x71, 0x77 }
};

namespace
{
thread_local std::string lastCompositionError;

DWORD windows_build_number() noexcept
{
    using RtlGetVersionFn = LONG(WINAPI*)(OSVERSIONINFOW*);
    const HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    const auto rtlGetVersion = ntdll ?
        reinterpret_cast<RtlGetVersionFn>(GetProcAddress(ntdll, "RtlGetVersion")) :
        nullptr;
    OSVERSIONINFOW version { sizeof(version) };
    return rtlGetVersion && rtlGetVersion(&version) >= 0 ?
        version.dwBuildNumber : 0u;
}

void set_last_error(const char* stage, const winrt::hresult_error& error)
{
    lastCompositionError = std::string(stage) + ": " +
        winrt::to_string(error.message()) + " (HRESULT " +
        std::to_string(static_cast<std::int32_t>(error.code())) + ")";
}

winrt::Windows::UI::Color color(
    float red,
    float green,
    float blue,
    float alpha)
{
    const auto byte = [](float value) {
        return static_cast<std::uint8_t>(
            std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    return { byte(alpha), byte(red), byte(green), byte(blue) };
}

// Windows Composition only supplies rectangle, rounded-rectangle, and ellipse
// geometry directly. This adapter exposes a D2D superellipse path through the
// WinRT geometry-source contract so Composition can clip HostBackdrop to the
// same squircle used by the Vulkan renderer.
class SquircleGeometrySource :
    public winrt::implements<
        SquircleGeometrySource,
        winrt::Windows::Graphics::IGeometrySource2D,
        ABI::Windows::Graphics::IGeometrySource2DInterop>
{
public:
    SquircleGeometrySource(
        float width,
        float height,
        float topLeftRadius,
        float topRightRadius,
        float bottomRightRadius,
        float bottomLeftRadius,
        float amount,
        float power) :
        width_(std::max(width, 0.0f)),
        height_(std::max(height, 0.0f)),
        topLeftRadius_(topLeftRadius),
        topRightRadius_(topRightRadius),
        bottomRightRadius_(bottomRightRadius),
        bottomLeftRadius_(bottomLeftRadius),
        amount_(std::clamp(amount, 0.0f, 1.0f)),
        power_(std::clamp(power, 2.0f, 5.0f))
    {
        winrt::check_hresult(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_MULTI_THREADED,
            factory_.put()));
    }

    HRESULT STDMETHODCALLTYPE GetGeometry(ID2D1Geometry** value) noexcept override
    {
        return create_geometry(factory_.get(), value);
    }

    HRESULT STDMETHODCALLTYPE TryGetGeometryUsingFactory(
        ID2D1Factory* factory,
        ID2D1Geometry** value) noexcept override
    {
        return create_geometry(factory, value);
    }

private:
    float corner_distance(float xDirection, float yDirection, float radius) const noexcept
    {
        if (radius <= 0.0f)
        {
            return 0.0f;
        }
        const float x = std::abs(xDirection);
        const float y = std::abs(yDirection);
        const float effectivePower = 2.0f + (power_ - 2.0f) * amount_;
        const float powered = std::pow(
            std::pow(x, effectivePower) + std::pow(y, effectivePower),
            1.0f / effectivePower);
        const float mixedNorm = (1.0f - amount_) + amount_ * powered;
        return radius / std::max(mixedNorm, 0.0001f);
    }

    HRESULT create_geometry(ID2D1Factory* factory, ID2D1Geometry** value) const noexcept
    {
        if (!value)
        {
            return E_POINTER;
        }
        *value = nullptr;
        if (!factory || width_ <= 0.0f || height_ <= 0.0f)
        {
            return E_INVALIDARG;
        }

        winrt::com_ptr<ID2D1PathGeometry> geometry;
        HRESULT result = factory->CreatePathGeometry(geometry.put());
        if (FAILED(result))
        {
            return result;
        }
        winrt::com_ptr<ID2D1GeometrySink> sink;
        result = geometry->Open(sink.put());
        if (FAILED(result))
        {
            return result;
        }

        constexpr std::uint32_t segmentsPerCorner = 24u;
        constexpr float halfPi = 1.57079632679489661923f;
        const float maximum = std::min(width_, height_) * 0.5f;
        const float topLeft = std::clamp(topLeftRadius_, 0.0f, maximum);
        const float topRight = std::clamp(topRightRadius_, 0.0f, maximum);
        const float bottomRight = std::clamp(bottomRightRadius_, 0.0f, maximum);
        const float bottomLeft = std::clamp(bottomLeftRadius_, 0.0f, maximum);
        const auto point = [](float x, float y) {
            return D2D1::Point2F(x, y);
        };
        const auto add_corner = [this, &sink](
            float centerX,
            float centerY,
            float radius,
            auto direction) {
            for (std::uint32_t index = 1u; index <= segmentsPerCorner; ++index)
            {
                const float angle = halfPi *
                    static_cast<float>(index) /
                    static_cast<float>(segmentsPerCorner);
                const auto [xDirection, yDirection] = direction(angle);
                const float distance = corner_distance(xDirection, yDirection, radius);
                sink->AddLine(D2D1::Point2F(
                    centerX + xDirection * distance,
                    centerY + yDirection * distance));
            }
        };

        sink->SetFillMode(D2D1_FILL_MODE_WINDING);
        sink->BeginFigure(point(topLeft, 0.0f), D2D1_FIGURE_BEGIN_FILLED);
        sink->AddLine(point(width_ - topRight, 0.0f));
        add_corner(width_ - topRight, topRight, topRight, [](float angle) {
            return std::pair { std::sin(angle), -std::cos(angle) };
        });
        sink->AddLine(point(width_, height_ - bottomRight));
        add_corner(width_ - bottomRight, height_ - bottomRight, bottomRight, [](float angle) {
            return std::pair { std::cos(angle), std::sin(angle) };
        });
        sink->AddLine(point(bottomLeft, height_));
        add_corner(bottomLeft, height_ - bottomLeft, bottomLeft, [](float angle) {
            return std::pair { -std::sin(angle), std::cos(angle) };
        });
        sink->AddLine(point(0.0f, topLeft));
        add_corner(topLeft, topLeft, topLeft, [](float angle) {
            return std::pair { -std::cos(angle), -std::sin(angle) };
        });
        sink->EndFigure(D2D1_FIGURE_END_CLOSED);
        result = sink->Close();
        if (FAILED(result))
        {
            return result;
        }
        *value = geometry.detach();
        return S_OK;
    }

    float width_ = 0.0f;
    float height_ = 0.0f;
    float topLeftRadius_ = 0.0f;
    float topRightRadius_ = 0.0f;
    float bottomRightRadius_ = 0.0f;
    float bottomLeftRadius_ = 0.0f;
    float amount_ = 1.0f;
    float power_ = 4.0f;
    winrt::com_ptr<ID2D1Factory> factory_;
};

class GraphicsEffectBase
{
public:
    explicit GraphicsEffectBase(effects::IGraphicsEffectSource source) :
        source_(std::move(source))
    {
    }

    winrt::hstring Name() const
    {
        return name_;
    }

    void Name(const winrt::hstring& value)
    {
        name_ = value;
    }

protected:
    HRESULT source_at(
        UINT index,
        abi_effects::IGraphicsEffectSource** destination) noexcept
    {
        if (!destination)
        {
            return E_POINTER;
        }
        *destination = nullptr;
        if (index != 0u || !source_)
        {
            return E_INVALIDARG;
        }
        return winrt::get_unknown(source_)->QueryInterface(
            __uuidof(abi_effects::IGraphicsEffectSource),
            reinterpret_cast<void**>(destination));
    }

    static HRESULT source_count(UINT* count) noexcept
    {
        if (!count)
        {
            return E_POINTER;
        }
        *count = 1u;
        return S_OK;
    }

    winrt::hstring name_;
    effects::IGraphicsEffectSource source_ { nullptr };
};

struct GaussianBlurEffect :
    GraphicsEffectBase,
    winrt::implements<
        GaussianBlurEffect,
        effects::IGraphicsEffect,
        effects::IGraphicsEffectSource,
        abi_effects::IGraphicsEffectD2D1Interop>
{
    GaussianBlurEffect(
        effects::IGraphicsEffectSource source,
        float amount) :
        GraphicsEffectBase(std::move(source)),
        amount_(std::clamp(amount, 0.0f, 250.0f))
    {
        name_ = L"GaussianBlur";
    }

    winrt::hstring Name() const { return GraphicsEffectBase::Name(); }
    void Name(const winrt::hstring& value) { GraphicsEffectBase::Name(value); }

    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) noexcept override
    {
        if (!id)
        {
            return E_POINTER;
        }
        *id = CLSID_D2D1GaussianBlur;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(
        LPCWSTR name,
        UINT* index,
        abi_effects::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept override
    {
        if (!name || !index || !mapping)
        {
            return E_POINTER;
        }
        if (std::wcscmp(name, L"BlurAmount") == 0)
        {
            *index = D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION;
            *mapping = abi_effects::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
            return S_OK;
        }
        return E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) noexcept override
    {
        if (!count)
        {
            return E_POINTER;
        }
        *count = 3u;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(
        UINT index,
        ABI::Windows::Foundation::IPropertyValue** value) noexcept override
    {
        if (!value)
        {
            return E_POINTER;
        }
        *value = nullptr;
        winrt::Windows::Foundation::IPropertyValue property { nullptr };
        if (index == D2D1_GAUSSIANBLUR_PROP_STANDARD_DEVIATION)
        {
            property = winrt::Windows::Foundation::PropertyValue::CreateSingle(amount_)
                .as<winrt::Windows::Foundation::IPropertyValue>();
        }
        else if (index == D2D1_GAUSSIANBLUR_PROP_OPTIMIZATION)
        {
            property = winrt::Windows::Foundation::PropertyValue::CreateUInt32(
                static_cast<std::uint32_t>(D2D1_GAUSSIANBLUR_OPTIMIZATION_BALANCED))
                .as<winrt::Windows::Foundation::IPropertyValue>();
        }
        else if (index == D2D1_GAUSSIANBLUR_PROP_BORDER_MODE)
        {
            property = winrt::Windows::Foundation::PropertyValue::CreateUInt32(
                static_cast<std::uint32_t>(D2D1_BORDER_MODE_HARD))
                .as<winrt::Windows::Foundation::IPropertyValue>();
        }
        else
        {
            return E_INVALIDARG;
        }
        *value = reinterpret_cast<ABI::Windows::Foundation::IPropertyValue*>(
            winrt::detach_abi(property));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSource(
        UINT index,
        abi_effects::IGraphicsEffectSource** source) noexcept override
    {
        return source_at(index, source);
    }

    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) noexcept override
    {
        return source_count(count);
    }

private:
    float amount_ = 0.0f;
};

struct SaturationEffect :
    GraphicsEffectBase,
    winrt::implements<
        SaturationEffect,
        effects::IGraphicsEffect,
        effects::IGraphicsEffectSource,
        abi_effects::IGraphicsEffectD2D1Interop>
{
    SaturationEffect(
        effects::IGraphicsEffectSource source,
        float saturation) :
        GraphicsEffectBase(std::move(source)),
        saturation_(std::clamp(saturation, 0.0f, 2.0f))
    {
        name_ = L"Saturation";
    }

    winrt::hstring Name() const { return GraphicsEffectBase::Name(); }
    void Name(const winrt::hstring& value) { GraphicsEffectBase::Name(value); }

    HRESULT STDMETHODCALLTYPE GetEffectId(GUID* id) noexcept override
    {
        if (!id)
        {
            return E_POINTER;
        }
        *id = CLSID_D2D1Saturation;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetNamedPropertyMapping(
        LPCWSTR name,
        UINT* index,
        abi_effects::GRAPHICS_EFFECT_PROPERTY_MAPPING* mapping) noexcept override
    {
        if (!name || !index || !mapping)
        {
            return E_POINTER;
        }
        if (std::wcscmp(name, L"Saturation") == 0)
        {
            *index = D2D1_SATURATION_PROP_SATURATION;
            *mapping = abi_effects::GRAPHICS_EFFECT_PROPERTY_MAPPING_DIRECT;
            return S_OK;
        }
        return E_INVALIDARG;
    }

    HRESULT STDMETHODCALLTYPE GetPropertyCount(UINT* count) noexcept override
    {
        if (!count)
        {
            return E_POINTER;
        }
        *count = 1u;
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetProperty(
        UINT index,
        ABI::Windows::Foundation::IPropertyValue** value) noexcept override
    {
        if (!value)
        {
            return E_POINTER;
        }
        *value = nullptr;
        if (index != D2D1_SATURATION_PROP_SATURATION)
        {
            return E_INVALIDARG;
        }
        auto property =
            winrt::Windows::Foundation::PropertyValue::CreateSingle(saturation_)
                .as<winrt::Windows::Foundation::IPropertyValue>();
        *value = reinterpret_cast<ABI::Windows::Foundation::IPropertyValue*>(
            winrt::detach_abi(property));
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE GetSource(
        UINT index,
        abi_effects::IGraphicsEffectSource** source) noexcept override
    {
        return source_at(index, source);
    }

    HRESULT STDMETHODCALLTYPE GetSourceCount(UINT* count) noexcept override
    {
        return source_count(count);
    }

private:
    float saturation_ = 1.0f;
};

class CompositionBridge
{
    struct LiquidSurfaceSample
    {
        composition::CompositionVisualSurface surface { nullptr };
        float localX = 0.0f;
        float localY = 0.0f;
        bool privateDesktop = false;
    };

    struct RegionVisualState
    {
        VibranceCompositionRegion descriptor {};
        composition::ContainerVisual container { nullptr };
        std::vector<LiquidSurfaceSample> liquidBackdropSamples;
    };

public:
    explicit CompositionBridge(const VibranceCompositionCreateInfo& info) :
        window_(static_cast<HWND>(info.window)),
        width_(info.width),
        height_(info.height),
        bufferCount_(info.bufferCount)
    {
        adapterLuid_.LowPart = info.adapterLuidLow;
        adapterLuid_.HighPart = info.adapterLuidHigh;
    }

    ~CompositionBridge()
    {
        reset_private_desktop();
        if (regions_)
        {
            regions_.Children().RemoveAll();
        }
        if (target_)
        {
            target_.Root(nullptr);
        }
        content_ = nullptr;
        regions_ = nullptr;
        root_ = nullptr;
        target_ = nullptr;
        compositor_ = nullptr;
        privateCompositorPartner_ = nullptr;
        compositionTarget_ = nullptr;
        compositionDevice_ = nullptr;
        d2dDevice_ = nullptr;
        d2dFactory_ = nullptr;
        dispatcherController_ = nullptr;

        if (hostBackdropEnabled_ && window_)
        {
            const BOOL disabled = FALSE;
            DwmSetWindowAttribute(
                window_,
                DWMWA_USE_HOSTBACKDROPBRUSH,
                &disabled,
                sizeof(disabled));
        }

        queries_.clear();
        sharedTextures_.clear();
        swapchain_ = nullptr;
        context_ = nullptr;
        device_ = nullptr;
        for (HANDLE handle : sharedHandles_)
        {
            if (handle)
            {
                CloseHandle(handle);
            }
        }
        sharedHandles_.clear();
        if (apartmentOwned_)
        {
            winrt::uninit_apartment();
        }
    }

    bool initialise(
        VibranceCompositionBuffer* outputBuffers,
        std::uint32_t outputCount)
    {
        lastCompositionError.clear();
        if (!window_ || width_ == 0u || height_ == 0u ||
            bufferCount_ < 2u || outputCount < bufferCount_)
        {
            lastCompositionError = "invalid creation parameters";
            return false;
        }

        try
        {
            initialise_dispatcher();
        }
        catch (const winrt::hresult_error& error)
        {
            set_last_error("DispatcherQueue initialisation", error);
            return false;
        }
        catch (const std::exception& error)
        {
            lastCompositionError = std::string("DispatcherQueue initialisation: ") +
                error.what();
            return false;
        }
        catch (...)
        {
            lastCompositionError = "DispatcherQueue initialisation: unknown error";
            return false;
        }
        if (!initialise_d3d())
        {
            if (lastCompositionError.empty())
            {
                lastCompositionError = "D3D11/DXGI initialisation failed";
            }
            return false;
        }
        try
        {
            if (!initialise_composition())
            {
                lastCompositionError = "Windows.UI.Composition initialisation failed";
                return false;
            }
        }
        catch (const winrt::hresult_error& error)
        {
            set_last_error("Windows.UI.Composition initialisation", error);
            return false;
        }
        catch (const std::exception& error)
        {
            lastCompositionError = std::string("Windows.UI.Composition initialisation: ") +
                error.what();
            return false;
        }
        catch (...)
        {
            lastCompositionError = "Windows.UI.Composition initialisation: unknown error";
            return false;
        }

        for (std::uint32_t index = 0u; index < bufferCount_; ++index)
        {
            outputBuffers[index].structSize = sizeof(VibranceCompositionBuffer);
            outputBuffers[index].index = index;
            outputBuffers[index].sharedHandle = sharedHandles_[index];
        }
        return true;
    }

    bool acquire(std::uint32_t index)
    {
        if (index >= queries_.size() || !queryPending_[index])
        {
            return index < queries_.size();
        }
        const HRESULT result = context_->GetData(
            queries_[index].get(),
            nullptr,
            0u,
            D3D11_ASYNC_GETDATA_DONOTFLUSH);
        if (result == S_OK)
        {
            queryPending_[index] = false;
            return true;
        }
        // D3D still owns this shared buffer. The Vulkan renderer can skip the
        // Composition copy for this frame instead of stalling its render loop.
        return false;
    }

    bool present(std::uint32_t index)
    {
        if (index >= sharedTextures_.size() || !swapchain_ || !context_)
        {
            return false;
        }

        winrt::com_ptr<IDXGISwapChain3> swapchain3;
        if (FAILED(swapchain_->QueryInterface(swapchain3.put())))
        {
            return false;
        }
        const std::uint32_t backBufferIndex =
            swapchain3->GetCurrentBackBufferIndex();
        winrt::com_ptr<ID3D11Texture2D> backBuffer;
        if (FAILED(swapchain_->GetBuffer(
                backBufferIndex,
                __uuidof(ID3D11Texture2D),
                backBuffer.put_void())))
        {
            return false;
        }

        context_->CopyResource(backBuffer.get(), sharedTextures_[index].get());
        context_->End(queries_[index].get());
        queryPending_[index] = true;
        const HRESULT presentResult = swapchain_->Present(
            0u,
            DXGI_PRESENT_DO_NOT_WAIT);
        // The compositor already has a newer queued frame. Dropping this copy
        // is preferable to blocking the Vulkan submission thread.
        if (presentResult == DXGI_ERROR_WAS_STILL_DRAWING)
        {
            // Present did not submit this command stream, so explicitly flush
            // the copy/query that releases the shared texture back to Vulkan.
            context_->Flush();
            return true;
        }
        return SUCCEEDED(presentResult);
    }

    bool set_regions(
        const VibranceCompositionRegion* regions,
        std::uint32_t count)
    {
        if (!regions_ || !compositor_)
        {
            return false;
        }
        try
        {
            bool canUpdateInPlace = regionVisuals_.size() == count;
            bool exactlyUnchanged = canUpdateInPlace;
            if (canUpdateInPlace)
            {
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    exactlyUnchanged = exactlyUnchanged &&
                        std::memcmp(
                            &regionVisuals_[index].descriptor,
                            &regions[index],
                            sizeof(regions[index])) == 0;
                    if (!same_region_except_position(
                            regionVisuals_[index].descriptor,
                            regions[index]))
                    {
                        canUpdateInPlace = false;
                        break;
                    }
                }
            }
            if (exactlyUnchanged)
            {
                return true;
            }
            if (canUpdateInPlace)
            {
                // A drag changes only offsets. Retaining the visual/effect
                // objects lets DWM interpolate one continuous composition
                // tree instead of flashing between destroyed trees.
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    update_region_position(regionVisuals_[index], regions[index]);
                }
                return true;
            }

            regions_.Children().RemoveAll();
            regionVisuals_.clear();
            const DWM_SYSTEMBACKDROP_TYPE disabled = DWMSBT_NONE;
            DwmSetWindowAttribute(
                window_,
                DWMWA_SYSTEMBACKDROP_TYPE,
                &disabled,
                sizeof(disabled));
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                if (!add_region(regions[index]))
                {
                    continue;
                }
            }
            return true;
        }
        catch (const winrt::hresult_error& error)
        {
            set_last_error("set backdrop regions", error);
            const std::wstring message =
                L"vibranceUI: Windows Composition rejected backdrop regions (HRESULT " +
                std::to_wstring(static_cast<std::int32_t>(error.code())) +
                L"): " + error.message().c_str() + L"\n";
            OutputDebugStringW(message.c_str());
            return false;
        }
        catch (...)
        {
            lastCompositionError = "set backdrop regions: unknown exception";
            return false;
        }
    }

private:
    static bool same_region_except_position(
        VibranceCompositionRegion left,
        VibranceCompositionRegion right) noexcept
    {
        // All bridge descriptors are zero-initialised before being populated,
        // including their padding. Position is the only value allowed to
        // change without rebuilding the shape/effect graph.
        left.x = 0.0f;
        left.y = 0.0f;
        right.x = 0.0f;
        right.y = 0.0f;
        return std::memcmp(&left, &right, sizeof(left)) == 0;
    }

    bool private_desktop_geometry_current() const noexcept
    {
        RECT current {};
        return window_ && GetWindowRect(window_, &current) &&
            EqualRect(&current, &privateDesktopWindowRect_) != FALSE;
    }

    winrt::Windows::Foundation::Numerics::float2 liquid_source_offset(
        const VibranceCompositionRegion& region) const noexcept
    {
        return {
            static_cast<float>(
                privateDesktopWindowRect_.left - privateDesktopSourceRect_.left) +
                region.x,
            static_cast<float>(
                privateDesktopWindowRect_.top - privateDesktopSourceRect_.top) +
                region.y
        };
    }

    void update_region_position(
        RegionVisualState& state,
        const VibranceCompositionRegion& region)
    {
        if (state.container)
        {
            state.container.Offset({ region.x, region.y, 0.0f });
        }
        if (privateDesktopSource_ && !private_desktop_geometry_current())
        {
            refresh_private_desktop();
        }
        for (LiquidSurfaceSample& sample : state.liquidBackdropSamples)
        {
            const auto origin = sample.privateDesktop ?
                liquid_source_offset(region) :
                winrt::Windows::Foundation::Numerics::float2 {
                    region.x,
                    region.y
                };
            sample.surface.SourceOffset({
                origin.x + sample.localX,
                origin.y + sample.localY
            });
        }
        state.descriptor = region;
    }

    void initialise_dispatcher()
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::single_threaded);
            apartmentOwned_ = true;
        }
        catch (const winrt::hresult_error& error)
        {
            if (error.code() != RPC_E_CHANGED_MODE)
            {
                throw;
            }
        }

        // A thread can own only one DispatcherQueue. Multiple vibranceUI
        // windows on the GLFW thread share that existing queue while retaining
        // separate Compositors and DesktopWindowTargets.
        if (winrt::Windows::System::DispatcherQueue::GetForCurrentThread())
        {
            return;
        }

        DispatcherQueueOptions options {
            sizeof(DispatcherQueueOptions),
            DQTYPE_THREAD_CURRENT,
            apartmentOwned_ ? DQTAT_COM_STA : DQTAT_COM_NONE
        };
        winrt::check_hresult(CreateDispatcherQueueController(
            options,
            reinterpret_cast<ABI::Windows::System::IDispatcherQueueController**>(
                winrt::put_abi(dispatcherController_))));
    }

    bool initialise_d3d()
    {
        winrt::com_ptr<IDXGIFactory4> factory;
        if (FAILED(CreateDXGIFactory2(0u, __uuidof(IDXGIFactory4), factory.put_void())))
        {
            lastCompositionError = "CreateDXGIFactory2 failed";
            return false;
        }

        winrt::com_ptr<IDXGIAdapter1> adapter;
        for (UINT index = 0u;
             factory->EnumAdapters1(index, adapter.put()) != DXGI_ERROR_NOT_FOUND;
             ++index)
        {
            DXGI_ADAPTER_DESC1 description {};
            if (SUCCEEDED(adapter->GetDesc1(&description)) &&
                description.AdapterLuid.LowPart == adapterLuid_.LowPart &&
                description.AdapterLuid.HighPart == adapterLuid_.HighPart)
            {
                break;
            }
            adapter = nullptr;
        }
        if (!adapter)
        {
            lastCompositionError = "DXGI adapter matching the Vulkan device LUID was not found";
            return false;
        }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
        flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
        D3D_FEATURE_LEVEL featureLevel {};
        winrt::com_ptr<ID3D11Device> baseDevice;
        winrt::com_ptr<ID3D11DeviceContext> baseContext;
        HRESULT result = D3D11CreateDevice(
            adapter.get(),
            D3D_DRIVER_TYPE_UNKNOWN,
            nullptr,
            flags,
            nullptr,
            0u,
            D3D11_SDK_VERSION,
            baseDevice.put(),
            &featureLevel,
            baseContext.put());
#if defined(_DEBUG)
        if (result == DXGI_ERROR_SDK_COMPONENT_MISSING)
        {
            flags &= ~D3D11_CREATE_DEVICE_DEBUG;
            result = D3D11CreateDevice(
                adapter.get(),
                D3D_DRIVER_TYPE_UNKNOWN,
                nullptr,
                flags,
                nullptr,
                0u,
                D3D11_SDK_VERSION,
                baseDevice.put(),
                &featureLevel,
                baseContext.put());
        }
#endif
        if (FAILED(result) || FAILED(baseDevice->QueryInterface(device_.put())) ||
            FAILED(baseContext->QueryInterface(context_.put())))
        {
            lastCompositionError = "D3D11CreateDevice or D3D11.4 interface query failed";
            return false;
        }

        DXGI_SWAP_CHAIN_DESC1 swapchainDescription {};
        swapchainDescription.Width = width_;
        swapchainDescription.Height = height_;
        swapchainDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        swapchainDescription.SampleDesc.Count = 1u;
        swapchainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
        swapchainDescription.BufferCount = 2u;
        swapchainDescription.Scaling = DXGI_SCALING_STRETCH;
        swapchainDescription.SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL;
        swapchainDescription.AlphaMode = DXGI_ALPHA_MODE_PREMULTIPLIED;
        if (FAILED(factory->CreateSwapChainForComposition(
                device_.get(),
                &swapchainDescription,
                nullptr,
                swapchain_.put())))
        {
            lastCompositionError = "IDXGIFactory2::CreateSwapChainForComposition failed";
            return false;
        }
        if (winrt::com_ptr<IDXGISwapChain2> lowLatencySwapchain;
            SUCCEEDED(swapchain_->QueryInterface(lowLatencySwapchain.put())))
        {
            // Keep only the freshest completed Vulkan frame queued for DWM.
            // This limits Composition latency without pacing the Vulkan loop.
            (void)lowLatencySwapchain->SetMaximumFrameLatency(1u);
        }

        sharedTextures_.resize(bufferCount_);
        sharedHandles_.resize(bufferCount_, nullptr);
        queries_.resize(bufferCount_);
        queryPending_.resize(bufferCount_, false);
        for (std::uint32_t index = 0u; index < bufferCount_; ++index)
        {
            D3D11_TEXTURE2D_DESC textureDescription {};
            textureDescription.Width = width_;
            textureDescription.Height = height_;
            textureDescription.MipLevels = 1u;
            textureDescription.ArraySize = 1u;
            textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            textureDescription.SampleDesc.Count = 1u;
            textureDescription.Usage = D3D11_USAGE_DEFAULT;
            textureDescription.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_RENDER_TARGET;
            textureDescription.MiscFlags = D3D11_RESOURCE_MISC_SHARED |
                D3D11_RESOURCE_MISC_SHARED_NTHANDLE;
            const HRESULT textureResult = device_->CreateTexture2D(
                &textureDescription,
                nullptr,
                sharedTextures_[index].put());
            if (FAILED(textureResult))
            {
                lastCompositionError =
                    "D3D11 shared texture creation failed (HRESULT " +
                    std::to_string(static_cast<std::int32_t>(textureResult)) + ")";
                return false;
            }

            winrt::com_ptr<IDXGIResource1> resource;
            if (FAILED(sharedTextures_[index]->QueryInterface(resource.put())) ||
                FAILED(resource->CreateSharedHandle(
                    nullptr,
                    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                    nullptr,
                    &sharedHandles_[index])))
            {
                lastCompositionError = "D3D11 NT shared-handle creation failed";
                return false;
            }

            D3D11_QUERY_DESC queryDescription { D3D11_QUERY_EVENT, 0u };
            if (FAILED(device_->CreateQuery(
                    &queryDescription,
                    queries_[index].put())))
            {
                lastCompositionError = "D3D11 completion-query creation failed";
                return false;
            }
        }
        return true;
    }

    bool initialise_private_compositor()
    {
        if (windows_build_number() < private_composition::minimumSupportedBuild)
        {
            return false;
        }

        try
        {
            winrt::com_ptr<IDXGIDevice> dxgiDevice;
            if (FAILED(device_->QueryInterface(dxgiDevice.put())))
            {
                return false;
            }

            D2D1_FACTORY_OPTIONS factoryOptions {};
            winrt::com_ptr<ID2D1Factory2> d2dFactory;
            if (FAILED(D2D1CreateFactory(
                    D2D1_FACTORY_TYPE_MULTI_THREADED,
                    __uuidof(ID2D1Factory2),
                    &factoryOptions,
                    d2dFactory.put_void())))
            {
                return false;
            }
            winrt::com_ptr<ID2D1Device> d2dDevice;
            if (FAILED(d2dFactory->CreateDevice(dxgiDevice.get(), d2dDevice.put())))
            {
                return false;
            }

            const auto factory = winrt::get_activation_factory<
                composition::Compositor,
                private_composition::IInteropCompositorFactoryPartner>();
            winrt::com_ptr<private_composition::IInteropCompositorPartner> partner;
            if (FAILED(factory->CreateInteropCompositor(
                    d2dDevice.get(),
                    nullptr,
                    __uuidof(private_composition::IInteropCompositorPartner),
                    partner.put_void())))
            {
                return false;
            }

            auto compositor = partner.as<composition::Compositor>();
            auto compositionDevice = partner.try_as<IDCompositionDesktopDevice>();
            if (!compositionDevice)
            {
                return false;
            }
            winrt::com_ptr<IDCompositionTarget> compositionTarget;
            if (FAILED(compositionDevice->CreateTargetForHwnd(
                    window_,
                    TRUE,
                    compositionTarget.put())))
            {
                return false;
            }
            auto target = compositionTarget.try_as<composition::CompositionTarget>();
            if (!target)
            {
                return false;
            }

            d2dFactory_ = std::move(d2dFactory);
            d2dDevice_ = std::move(d2dDevice);
            privateCompositorPartner_ = std::move(partner);
            compositionDevice_ = std::move(compositionDevice);
            compositionTarget_ = std::move(compositionTarget);
            compositor_ = std::move(compositor);
            target_ = std::move(target);
            privateInteropEnabled_ = true;
            OutputDebugStringW(
                L"vibranceUI: private Windows 19041 InteropCompositor enabled.\n");
            return true;
        }
        catch (const winrt::hresult_error& error)
        {
            const std::wstring message =
                L"vibranceUI: private InteropCompositor unavailable (HRESULT " +
                std::to_wstring(static_cast<std::int32_t>(error.code())) +
                L"); using the public Composition backend.\n";
            OutputDebugStringW(message.c_str());
            return false;
        }
        catch (...)
        {
            OutputDebugStringW(
                L"vibranceUI: private InteropCompositor unavailable; using the public Composition backend.\n");
            return false;
        }
    }

    bool initialise_composition()
    {
        // A desktop HWND must explicitly opt into HostBackdropBrush. Without
        // this attribute the brush can resolve to an opaque/black surface even
        // though the Composition visual and effect graph were created.
        const BOOL enabled = TRUE;
        hostBackdropEnabled_ = SUCCEEDED(DwmSetWindowAttribute(
            window_,
            DWMWA_USE_HOSTBACKDROPBRUSH,
            &enabled,
            sizeof(enabled)));

        if (!initialise_private_compositor())
        {
            compositor_ = composition::Compositor();
            const auto desktopInterop = compositor_.as<
                ABI::Windows::UI::Composition::Desktop::ICompositorDesktopInterop>();
            composition::Desktop::DesktopWindowTarget desktopTarget { nullptr };
            winrt::check_hresult(desktopInterop->CreateDesktopWindowTarget(
                window_,
                true,
                reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(
                    winrt::put_abi(desktopTarget))));
            target_ = desktopTarget.as<composition::CompositionTarget>();
        }

        winrt::com_ptr<ABI::Windows::UI::Composition::ICompositionSurface>
            compositionSurface;
        const auto compositorInterop =
            compositor_.as<ABI::Windows::UI::Composition::ICompositorInterop>();
        winrt::check_hresult(compositorInterop->CreateCompositionSurfaceForSwapChain(
            swapchain_.get(),
            compositionSurface.put()));

        auto surface = compositionSurface.as<composition::ICompositionSurface>();
        auto surfaceBrush = compositor_.CreateSurfaceBrush(surface);
        surfaceBrush.Stretch(composition::CompositionStretch::Fill);

        root_ = compositor_.CreateContainerVisual();
        root_.Size({ static_cast<float>(width_), static_cast<float>(height_) });
        regions_ = compositor_.CreateContainerVisual();
        regions_.Size(root_.Size());
        content_ = compositor_.CreateSpriteVisual();
        content_.Size(root_.Size());
        content_.Brush(surfaceBrush);
        root_.Children().InsertAtBottom(regions_);
        root_.Children().InsertAtTop(content_);
        target_.Root(root_);
        return true;
    }

    void reset_private_desktop() noexcept
    {
        if (privateDesktopWallpaperThumbnail_)
        {
            DwmUnregisterThumbnail(privateDesktopWallpaperThumbnail_);
            privateDesktopWallpaperThumbnail_ = nullptr;
        }
        privateDesktopWallpaperDwmVisual_ = nullptr;
        privateDesktopWallpaperVisual_ = nullptr;
        privateDesktopWallpaperSurface_ = nullptr;
        privateDesktopCombinedSource_ = nullptr;
        privateDesktopWindowsSource_ = nullptr;
        privateDesktopSource_ = nullptr;
        privateDesktopContainer_ = nullptr;
        privateDesktopVisual_ = nullptr;
        if (privateDesktopThumbnail_)
        {
            DwmUnregisterThumbnail(privateDesktopThumbnail_);
            privateDesktopThumbnail_ = nullptr;
        }
        createSharedDesktopVisual_ = nullptr;
        createSharedThumbnailVisual_ = nullptr;
        updateSharedDesktopVisual_ = nullptr;
        if (dwmPrivateModule_)
        {
            FreeLibrary(dwmPrivateModule_);
            dwmPrivateModule_ = nullptr;
        }
        if (privateDesktopWindowAttribute_ && window_)
        {
            if (const HMODULE user32 = GetModuleHandleW(L"user32.dll"))
            {
                const auto setWindowCompositionAttribute =
                    reinterpret_cast<private_composition::SetWindowCompositionAttributeFn>(
                        GetProcAddress(user32, "SetWindowCompositionAttribute"));
                if (setWindowCompositionAttribute)
                {
                    BOOL disabled = FALSE;
                    private_composition::WindowCompositionAttributeData data {
                        private_composition::WindowCompositionAttribute::ExcludedFromLivePreview,
                        &disabled,
                        sizeof(disabled)
                    };
                    setWindowCompositionAttribute(window_, &data);
                }
            }
        }
        privateDesktopWindowAttribute_ = false;
    }

    struct DesktopWallpaperDescriptor
    {
        std::wstring path;
        DESKTOP_WALLPAPER_POSITION position = DWPOS_FILL;
        COLORREF background = RGB(0, 0, 0);
        RECT monitorRect {};
        RECT virtualDesktopRect {};
    };

    DesktopWallpaperDescriptor query_desktop_wallpaper(
        const RECT& monitorRect) const noexcept
    {
        DesktopWallpaperDescriptor result;
        result.monitorRect = monitorRect;
        result.virtualDesktopRect = {
            GetSystemMetrics(SM_XVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN),
            GetSystemMetrics(SM_XVIRTUALSCREEN) +
                GetSystemMetrics(SM_CXVIRTUALSCREEN),
            GetSystemMetrics(SM_YVIRTUALSCREEN) +
                GetSystemMetrics(SM_CYVIRTUALSCREEN)
        };

        winrt::com_ptr<IDesktopWallpaper> wallpaper;
        if (FAILED(CoCreateInstance(
                CLSID_DesktopWallpaper,
                nullptr,
                CLSCTX_INPROC_SERVER,
                __uuidof(IDesktopWallpaper),
                wallpaper.put_void())))
        {
            return result;
        }

        wallpaper->GetPosition(&result.position);
        wallpaper->GetBackgroundColor(&result.background);

        std::wstring monitorId;
        UINT monitorCount = 0u;
        if (SUCCEEDED(wallpaper->GetMonitorDevicePathCount(&monitorCount)))
        {
            for (UINT index = 0u; index < monitorCount; ++index)
            {
                LPWSTR candidateId = nullptr;
                if (FAILED(wallpaper->GetMonitorDevicePathAt(
                        index,
                        &candidateId)) ||
                    !candidateId)
                {
                    continue;
                }

                RECT candidateRect {};
                const bool matches =
                    SUCCEEDED(wallpaper->GetMonitorRECT(
                        candidateId,
                        &candidateRect)) &&
                    EqualRect(&candidateRect, &monitorRect) != FALSE;
                if (matches)
                {
                    monitorId = candidateId;
                }
                CoTaskMemFree(candidateId);
                if (matches)
                {
                    break;
                }
            }
        }

        LPWSTR path = nullptr;
        HRESULT pathResult = wallpaper->GetWallpaper(
            monitorId.empty() ? nullptr : monitorId.c_str(),
            &path);
        if ((pathResult == S_OK || pathResult == S_FALSE) && path)
        {
            result.path = path;
        }
        if (path)
        {
            CoTaskMemFree(path);
        }
        return result;
    }

    bool ensure_wallpaper_graphics_device()
    {
        if (wallpaperGraphicsDevice_)
        {
            return true;
        }
        if (!compositor_ || !d2dDevice_)
        {
            return false;
        }

        const auto compositorInterop = compositor_.as<
            ABI::Windows::UI::Composition::ICompositorInterop>();
        return SUCCEEDED(compositorInterop->CreateGraphicsDevice(
            d2dDevice_.get(),
            reinterpret_cast<
                ABI::Windows::UI::Composition::ICompositionGraphicsDevice**>(
                    winrt::put_abi(wallpaperGraphicsDevice_))));
    }

    winrt::com_ptr<ID2D1Bitmap1> load_wallpaper_bitmap(
        ID2D1DeviceContext* context,
        const std::wstring& path,
        UINT& width,
        UINT& height) const noexcept
    {
        width = 0u;
        height = 0u;
        if (!context || path.empty())
        {
            return nullptr;
        }

        winrt::com_ptr<IWICImagingFactory2> factory;
        if (FAILED(CoCreateInstance(
                CLSID_WICImagingFactory2,
                nullptr,
                CLSCTX_INPROC_SERVER,
                __uuidof(IWICImagingFactory2),
                factory.put_void())))
        {
            return nullptr;
        }

        winrt::com_ptr<IWICBitmapDecoder> decoder;
        if (FAILED(factory->CreateDecoderFromFilename(
                path.c_str(),
                nullptr,
                GENERIC_READ,
                WICDecodeMetadataCacheOnLoad,
                decoder.put())))
        {
            return nullptr;
        }

        winrt::com_ptr<IWICBitmapFrameDecode> frame;
        if (FAILED(decoder->GetFrame(0u, frame.put())) ||
            FAILED(frame->GetSize(&width, &height)) ||
            width == 0u || height == 0u)
        {
            return nullptr;
        }

        winrt::com_ptr<IWICFormatConverter> converter;
        if (FAILED(factory->CreateFormatConverter(converter.put())) ||
            FAILED(converter->Initialize(
                frame.get(),
                GUID_WICPixelFormat32bppPBGRA,
                WICBitmapDitherTypeNone,
                nullptr,
                0.0,
                WICBitmapPaletteTypeCustom)))
        {
            return nullptr;
        }

        winrt::com_ptr<ID2D1Bitmap1> bitmap;
        if (FAILED(context->CreateBitmapFromWicBitmap(
                converter.get(),
                nullptr,
                bitmap.put())))
        {
            return nullptr;
        }
        return bitmap;
    }

    static D2D1_RECT_F scaled_wallpaper_destination(
        float sourceWidth,
        float sourceHeight,
        float targetWidth,
        float targetHeight,
        float scale)
    {
        const float width = sourceWidth * scale;
        const float height = sourceHeight * scale;
        const float left = (targetWidth - width) * 0.5f;
        const float top = (targetHeight - height) * 0.5f;
        return D2D1::RectF(left, top, left + width, top + height);
    }

    static void draw_wallpaper_bitmap(
        ID2D1DeviceContext* context,
        ID2D1Bitmap1* bitmap,
        UINT bitmapWidth,
        UINT bitmapHeight,
        const DesktopWallpaperDescriptor& wallpaper,
        float targetWidth,
        float targetHeight)
    {
        if (!context || !bitmap || bitmapWidth == 0u || bitmapHeight == 0u)
        {
            return;
        }

        const float imageWidth = static_cast<float>(bitmapWidth);
        const float imageHeight = static_cast<float>(bitmapHeight);
        const D2D1_RECT_F source = D2D1::RectF(
            0.0f,
            0.0f,
            imageWidth,
            imageHeight);
        D2D1_RECT_F destination = D2D1::RectF(
            0.0f,
            0.0f,
            targetWidth,
            targetHeight);

        if (wallpaper.position == DWPOS_TILE)
        {
            const float virtualOffsetX = static_cast<float>(
                wallpaper.monitorRect.left -
                wallpaper.virtualDesktopRect.left);
            const float virtualOffsetY = static_cast<float>(
                wallpaper.monitorRect.top -
                wallpaper.virtualDesktopRect.top);
            float firstX = -std::fmod(virtualOffsetX, imageWidth);
            float firstY = -std::fmod(virtualOffsetY, imageHeight);
            if (firstX > 0.0f)
            {
                firstX -= imageWidth;
            }
            if (firstY > 0.0f)
            {
                firstY -= imageHeight;
            }

            const std::uint64_t columns = static_cast<std::uint64_t>(
                std::ceil((targetWidth - firstX) / imageWidth));
            const std::uint64_t rows = static_cast<std::uint64_t>(
                std::ceil((targetHeight - firstY) / imageHeight));
            if (columns * rows <= 4096u)
            {
                for (float y = firstY; y < targetHeight; y += imageHeight)
                {
                    for (float x = firstX; x < targetWidth; x += imageWidth)
                    {
                        const D2D1_RECT_F tile = D2D1::RectF(
                            x,
                            y,
                            x + imageWidth,
                            y + imageHeight);
                        context->DrawBitmap(
                            bitmap,
                            &tile,
                            1.0f,
                            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
                            &source);
                    }
                }
                return;
            }
        }

        if (wallpaper.position == DWPOS_CENTER)
        {
            destination = scaled_wallpaper_destination(
                imageWidth,
                imageHeight,
                targetWidth,
                targetHeight,
                1.0f);
        }
        else if (wallpaper.position == DWPOS_FIT)
        {
            const float scale = std::min(
                targetWidth / imageWidth,
                targetHeight / imageHeight);
            destination = scaled_wallpaper_destination(
                imageWidth,
                imageHeight,
                targetWidth,
                targetHeight,
                scale);
        }
        else if (wallpaper.position == DWPOS_FILL ||
            wallpaper.position == DWPOS_TILE)
        {
            const float scale = std::max(
                targetWidth / imageWidth,
                targetHeight / imageHeight);
            destination = scaled_wallpaper_destination(
                imageWidth,
                imageHeight,
                targetWidth,
                targetHeight,
                scale);
        }
        else if (wallpaper.position == DWPOS_SPAN)
        {
            const float virtualWidth = static_cast<float>(
                wallpaper.virtualDesktopRect.right -
                wallpaper.virtualDesktopRect.left);
            const float virtualHeight = static_cast<float>(
                wallpaper.virtualDesktopRect.bottom -
                wallpaper.virtualDesktopRect.top);
            const float scale = std::max(
                virtualWidth / imageWidth,
                virtualHeight / imageHeight);
            const D2D1_RECT_F virtualDestination =
                scaled_wallpaper_destination(
                    imageWidth,
                    imageHeight,
                    virtualWidth,
                    virtualHeight,
                    scale);
            destination = virtualDestination;
            destination.left += static_cast<float>(
                wallpaper.virtualDesktopRect.left -
                wallpaper.monitorRect.left);
            destination.right += static_cast<float>(
                wallpaper.virtualDesktopRect.left -
                wallpaper.monitorRect.left);
            destination.top += static_cast<float>(
                wallpaper.virtualDesktopRect.top -
                wallpaper.monitorRect.top);
            destination.bottom += static_cast<float>(
                wallpaper.virtualDesktopRect.top -
                wallpaper.monitorRect.top);
        }

        context->DrawBitmap(
            bitmap,
            &destination,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            &source);
    }

    bool draw_desktop_wallpaper(
        const composition::CompositionDrawingSurface& surface,
        const DesktopWallpaperDescriptor& wallpaper,
        float width,
        float height)
    {
        const auto surfaceInterop = surface.as<
            ABI::Windows::UI::Composition::ICompositionDrawingSurfaceInterop>();
        winrt::com_ptr<ID2D1DeviceContext> context;
        POINT offset {};
        if (FAILED(surfaceInterop->BeginDraw(
                nullptr,
                __uuidof(ID2D1DeviceContext),
                context.put_void(),
                &offset)))
        {
            return false;
        }

        context->SetTransform(D2D1::Matrix3x2F::Translation(
            static_cast<float>(offset.x),
            static_cast<float>(offset.y)));
        context->Clear(D2D1::ColorF(
            static_cast<float>(GetRValue(wallpaper.background)) / 255.0f,
            static_cast<float>(GetGValue(wallpaper.background)) / 255.0f,
            static_cast<float>(GetBValue(wallpaper.background)) / 255.0f,
            1.0f));

        UINT bitmapWidth = 0u;
        UINT bitmapHeight = 0u;
        const auto bitmap = load_wallpaper_bitmap(
            context.get(),
            wallpaper.path,
            bitmapWidth,
            bitmapHeight);
        if (bitmap)
        {
            draw_wallpaper_bitmap(
                context.get(),
                bitmap.get(),
                bitmapWidth,
                bitmapHeight,
                wallpaper,
                width,
                height);
        }
        return SUCCEEDED(surfaceInterop->EndDraw());
    }

    bool rebuild_private_wallpaper()
    {
        if (!privateDesktopWindowsSource_)
        {
            return false;
        }

        const float width = static_cast<float>(
            privateDesktopSourceRect_.right -
            privateDesktopSourceRect_.left);
        const float height = static_cast<float>(
            privateDesktopSourceRect_.bottom -
            privateDesktopSourceRect_.top);
        if (width <= 0.0f || height <= 0.0f)
        {
            return false;
        }

        const DesktopWallpaperDescriptor wallpaper =
            query_desktop_wallpaper(privateDesktopSourceRect_);
        if (wallpaper.path.empty())
        {
            const composition::Visual liveWallpaper =
                create_private_wallpaper_host_visual(
                    privateDesktopSourceRect_,
                    static_cast<LONG>(width),
                    static_cast<LONG>(height));
            if (liveWallpaper)
            {
                return compose_private_wallpaper_visual(
                    liveWallpaper,
                    width,
                    height);
            }
        }

        if (!ensure_wallpaper_graphics_device())
        {
            return false;
        }
        auto surface = wallpaperGraphicsDevice_.CreateDrawingSurface(
            { width, height },
            composition_directx::DirectXPixelFormat::B8G8R8A8UIntNormalized,
            composition_directx::DirectXAlphaMode::Premultiplied);
        if (!draw_desktop_wallpaper(surface, wallpaper, width, height))
        {
            return false;
        }

        auto wallpaperBrush = compositor_.CreateSurfaceBrush(surface);
        wallpaperBrush.Stretch(composition::CompositionStretch::Fill);
        auto wallpaperVisual = compositor_.CreateSpriteVisual();
        wallpaperVisual.Size({ width, height });
        wallpaperVisual.Brush(wallpaperBrush);

        if (privateDesktopWallpaperThumbnail_)
        {
            DwmUnregisterThumbnail(privateDesktopWallpaperThumbnail_);
            privateDesktopWallpaperThumbnail_ = nullptr;
            privateDesktopWallpaperDwmVisual_ = nullptr;
        }
        if (!compose_private_wallpaper_visual(
                wallpaperVisual,
                width,
                height))
        {
            return false;
        }

        privateDesktopWallpaperSurface_ = std::move(surface);
        OutputDebugStringW(
            L"vibranceUI: decoded desktop wallpaper layered beneath the private DWM window source.\n");
        return true;
    }

    bool compose_private_wallpaper_visual(
        const composition::Visual& wallpaperVisual,
        float width,
        float height)
    {
        if (!wallpaperVisual || !privateDesktopWindowsSource_)
        {
            return false;
        }
        if (!privateDesktopCombinedSource_)
        {
            privateDesktopCombinedSource_ = compositor_.CreateContainerVisual();
        }
        privateDesktopCombinedSource_.Size({ width, height });
        privateDesktopCombinedSource_.Children().RemoveAll();
        privateDesktopCombinedSource_.Children().InsertAtBottom(wallpaperVisual);
        privateDesktopCombinedSource_.Children().InsertAtTop(
            privateDesktopWindowsSource_);
        privateDesktopWallpaperVisual_ = wallpaperVisual;
        privateDesktopSource_ = privateDesktopCombinedSource_;
        return true;
    }

    composition::Visual create_private_wallpaper_host_visual(
        const RECT& monitorRect,
        LONG width,
        LONG height)
    {
        if (!createSharedThumbnailVisual_ || !compositionDevice_ ||
            width <= 0 || height <= 0)
        {
            return nullptr;
        }

        const HWND programManager = FindWindowW(L"Progman", nullptr);
        if (!programManager)
        {
            return nullptr;
        }

        // Wallpaper Engine exposes its live surface as a Progman child. Try
        // that redirected visual first; Progman itself remains the generic
        // fallback for Windows Spotlight and other managed wallpaper hosts.
        std::array<HWND, 3> candidates {
            FindWindowExW(
                programManager,
                nullptr,
                L"WPEDesktopDX11Window",
                nullptr),
            FindWindowExW(
                programManager,
                nullptr,
                L"WPECloneView",
                nullptr),
            programManager
        };

        for (const HWND sourceWindow : candidates)
        {
            if (!sourceWindow)
            {
                continue;
            }

            RECT sourceWindowRect {};
            if (!GetWindowRect(sourceWindow, &sourceWindowRect))
            {
                continue;
            }
            DWM_THUMBNAIL_PROPERTIES properties {};
            properties.dwFlags =
                DWM_TNP_RECTDESTINATION |
                DWM_TNP_RECTSOURCE |
                DWM_TNP_VISIBLE |
                DWM_TNP_OPACITY |
                private_composition::thumbnailEnable3D;
            properties.rcDestination = { 0, 0, width, height };
            properties.rcSource = {
                monitorRect.left - sourceWindowRect.left,
                monitorRect.top - sourceWindowRect.top,
                monitorRect.right - sourceWindowRect.left,
                monitorRect.bottom - sourceWindowRect.top
            };
            properties.opacity = 255u;
            properties.fVisible = TRUE;
            properties.fSourceClientAreaOnly = FALSE;

            winrt::com_ptr<IDCompositionVisual2> visual;
            HTHUMBNAIL thumbnail = nullptr;
            const HRESULT result = createSharedThumbnailVisual_(
                window_,
                sourceWindow,
                2u,
                &properties,
                compositionDevice_.get(),
                visual.put_void(),
                &thumbnail);
            if (FAILED(result) || !visual || !thumbnail)
            {
                if (thumbnail)
                {
                    DwmUnregisterThumbnail(thumbnail);
                }
                continue;
            }

            auto source = visual.try_as<composition::Visual>();
            if (!source)
            {
                DwmUnregisterThumbnail(thumbnail);
                continue;
            }

            if (privateDesktopWallpaperThumbnail_)
            {
                DwmUnregisterThumbnail(privateDesktopWallpaperThumbnail_);
            }
            privateDesktopWallpaperThumbnail_ = thumbnail;
            privateDesktopWallpaperDwmVisual_ = std::move(visual);
            privateDesktopWallpaperSurface_ = nullptr;
            OutputDebugStringW(
                L"vibranceUI: live desktop wallpaper host layered beneath the private DWM window source.\n");
            return source;
        }
        return nullptr;
    }

    bool refresh_private_desktop()
    {
        if (!privateDesktopThumbnail_ || !updateSharedDesktopVisual_ ||
            !privateDesktopSource_)
        {
            return false;
        }

        RECT windowRectangle {};
        if (!GetWindowRect(window_, &windowRectangle))
        {
            return false;
        }
        MONITORINFO monitorInfo { sizeof(monitorInfo) };
        if (!GetMonitorInfoW(
                MonitorFromWindow(window_, MONITOR_DEFAULTTONEAREST),
                &monitorInfo))
        {
            return false;
        }

        RECT source = monitorInfo.rcMonitor;
        SIZE destination {
            source.right - source.left,
            source.bottom - source.top
        };
        if (destination.cx <= 0 || destination.cy <= 0)
        {
            return false;
        }

        // Exclude only the transparent renderer host. Excluding every HWND
        // owned by this process also removes an attached console and any
        // sibling tool windows from the private desktop visual. The host has
        // WCA_EXCLUDED_FROM_LIVEPREVIEW as a second guard against feedback.
        std::array<HWND, 1> excludedWindows { window_ };

        HRESULT result = E_NOTIMPL;
        if (windowsBuild_ >= private_composition::firstModernMultiWindowBuild)
        {
            const auto update = reinterpret_cast<
                private_composition::UpdateSharedMultiWindowVisualFn>(
                    updateSharedDesktopVisual_);
            result = update(
                privateDesktopThumbnail_,
                nullptr,
                0u,
                excludedWindows.data(),
                static_cast<DWORD>(excludedWindows.size()),
                &source,
                &destination,
                1u);
        }
        else
        {
            const auto update = reinterpret_cast<
                private_composition::UpdateSharedVirtualDesktopVisualFn>(
                    updateSharedDesktopVisual_);
            result = update(
                privateDesktopThumbnail_,
                nullptr,
                0u,
                excludedWindows.data(),
                static_cast<DWORD>(excludedWindows.size()),
                &source,
                &destination);
        }
        if (FAILED(result))
        {
            return false;
        }

        privateDesktopSourceRect_ = source;
        privateDesktopWindowRect_ = windowRectangle;
        privateDesktopWindowsSource_.Size({
            static_cast<float>(destination.cx),
            static_cast<float>(destination.cy)
        });
        if (!rebuild_private_wallpaper())
        {
            privateDesktopSource_ = privateDesktopWindowsSource_;
            OutputDebugStringW(
                L"vibranceUI: desktop wallpaper visual unavailable; retaining the private DWM window source.\n");
        }
        return true;
    }

    bool initialise_private_desktop()
    {
        if (!privateInteropEnabled_ || !compositionDevice_)
        {
            return false;
        }
        if (privateDesktopSource_)
        {
            return private_desktop_geometry_current() || refresh_private_desktop();
        }

        windowsBuild_ = windows_build_number();
        if (windowsBuild_ < private_composition::minimumSupportedBuild)
        {
            return false;
        }
        dwmPrivateModule_ = LoadLibraryExW(
            L"dwmapi.dll",
            nullptr,
            LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dwmPrivateModule_)
        {
            return false;
        }
        createSharedDesktopVisual_ = reinterpret_cast<
            private_composition::CreateSharedMultiWindowVisualFn>(
                GetProcAddress(
                    dwmPrivateModule_,
                    MAKEINTRESOURCEA(
                        private_composition::createSharedMultiWindowVisualOrdinal)));
        createSharedThumbnailVisual_ = reinterpret_cast<
            private_composition::CreateSharedThumbnailVisualFn>(
                GetProcAddress(
                    dwmPrivateModule_,
                    MAKEINTRESOURCEA(
                        private_composition::createSharedThumbnailVisualOrdinal)));
        updateSharedDesktopVisual_ = GetProcAddress(
            dwmPrivateModule_,
            MAKEINTRESOURCEA(
                private_composition::updateSharedMultiWindowVisualOrdinal));
        if (!createSharedDesktopVisual_ || !updateSharedDesktopVisual_)
        {
            reset_private_desktop();
            return false;
        }

        if (const HMODULE user32 = GetModuleHandleW(L"user32.dll"))
        {
            const auto setWindowCompositionAttribute =
                reinterpret_cast<private_composition::SetWindowCompositionAttributeFn>(
                    GetProcAddress(user32, "SetWindowCompositionAttribute"));
            if (setWindowCompositionAttribute)
            {
                BOOL enabled = TRUE;
                private_composition::WindowCompositionAttributeData data {
                    private_composition::WindowCompositionAttribute::ExcludedFromLivePreview,
                    &enabled,
                    sizeof(enabled)
                };
                privateDesktopWindowAttribute_ =
                    setWindowCompositionAttribute(window_, &data) != FALSE;
            }
        }

        winrt::com_ptr<IDCompositionVisual2> desktopVisual;
        HTHUMBNAIL desktopThumbnail = nullptr;
        const HRESULT createResult = createSharedDesktopVisual_(
            window_,
            compositionDevice_.get(),
            desktopVisual.put_void(),
            &desktopThumbnail);
        if (FAILED(createResult) || !desktopVisual || !desktopThumbnail)
        {
            reset_private_desktop();
            return false;
        }

        winrt::com_ptr<IDCompositionVisual2> desktopContainer;
        if (FAILED(compositionDevice_->CreateVisual(desktopContainer.put())) ||
            FAILED(desktopContainer->AddVisual(desktopVisual.get(), TRUE, nullptr)))
        {
            DwmUnregisterThumbnail(desktopThumbnail);
            reset_private_desktop();
            return false;
        }
        auto desktopSource = desktopContainer.try_as<composition::Visual>();
        if (!desktopSource)
        {
            DwmUnregisterThumbnail(desktopThumbnail);
            reset_private_desktop();
            return false;
        }

        privateDesktopVisual_ = std::move(desktopVisual);
        privateDesktopContainer_ = std::move(desktopContainer);
        privateDesktopThumbnail_ = desktopThumbnail;
        privateDesktopWindowsSource_ = std::move(desktopSource);
        privateDesktopSource_ = privateDesktopWindowsSource_;
        if (!refresh_private_desktop())
        {
            reset_private_desktop();
            return false;
        }
        OutputDebugStringW(
            L"vibranceUI: private DWM multi-window visual enabled for liquid material (no capture/readback surface).\n");
        return true;
    }

    composition::CompositionBrush make_private_liquid_source_brush(
        const VibranceCompositionRegion& region,
        composition::CompositionVisualSurface& visualSurface)
    {
        if (!initialise_private_desktop())
        {
            return nullptr;
        }

        visualSurface = compositor_.CreateVisualSurface();
        visualSurface.SourceVisual(privateDesktopSource_);
        visualSurface.SourceOffset(liquid_source_offset(region));
        visualSurface.SourceSize({ region.width, region.height });

        auto surfaceBrush = compositor_.CreateSurfaceBrush(visualSurface);
        surfaceBrush.Stretch(composition::CompositionStretch::Fill);
        surfaceBrush.CenterPoint({ region.width * 0.5f, region.height * 0.5f });
        // Keep the retained desktop visual's optical magnification subtle.
        // The SDF overlay supplies the stronger boundary response without
        // excessively enlarging the entire backdrop sample.
        const float scale = 1.0f +
            std::clamp(region.refraction, 0.0f, 0.20f) * 1.35f;
        surfaceBrush.Scale({ scale, scale });
        return surfaceBrush;
    }

    composition::CompositionBrush make_liquid_glaze()
    {
        auto gradient = compositor_.CreateRadialGradientBrush();
        gradient.EllipseCenter({ 0.42f, 0.38f });
        gradient.EllipseRadius({ 0.72f, 0.72f });
        gradient.GradientOriginOffset({ -0.12f, -0.16f });
        auto stops = gradient.ColorStops();
        stops.Append(compositor_.CreateColorGradientStop(
            0.0f,
            color(1.0f, 1.0f, 1.0f, 0.055f)));
        stops.Append(compositor_.CreateColorGradientStop(
            0.62f,
            color(0.82f, 0.94f, 1.0f, 0.012f)));
        stops.Append(compositor_.CreateColorGradientStop(
            0.86f,
            color(0.72f, 0.88f, 1.0f, 0.055f)));
        stops.Append(compositor_.CreateColorGradientStop(
            1.0f,
            color(1.0f, 1.0f, 1.0f, 0.16f)));
        return gradient;
    }

    composition::CompositionBrush make_effect_brush(
        const VibranceCompositionRegion& region,
        const composition::CompositionBrush& source = nullptr)
    {
        auto sourceParameter =
            composition::CompositionEffectSourceParameter(L"backdrop");
        auto blur = winrt::make<GaussianBlurEffect>(
            sourceParameter,
            std::max(region.blurRadius, 0.0f));
        effects::IGraphicsEffect graph = blur;
        if (std::abs(region.saturation - 1.0f) > 0.001f)
        {
            graph = winrt::make<SaturationEffect>(
                blur.as<effects::IGraphicsEffectSource>(),
                region.saturation);
        }
        auto factory = compositor_.CreateEffectFactory(graph);
        auto brush = factory.CreateBrush();
        brush.SetSourceParameter(
            L"backdrop",
            source ? source : compositor_.CreateHostBackdropBrush());
        return brush;
    }

    bool build_liquid_lens(
        const composition::ContainerVisual& container,
        const VibranceCompositionRegion& region,
        std::vector<LiquidSurfaceSample>& retainedSamples)
    {
        composition::CompositionVisualSurface backdropSurface { nullptr };
        auto lensSource = make_private_liquid_source_brush(
            region,
            backdropSurface);
        if (!lensSource || !backdropSurface)
        {
            return false;
        }

        auto lens = compositor_.CreateSpriteVisual();
        lens.Size({ region.width, region.height });
        lens.Brush(make_effect_brush(region, lensSource));
        container.Children().InsertAtBottom(lens);

        retainedSamples.clear();
        retainedSamples.push_back({
            std::move(backdropSurface),
            0.0f,
            0.0f,
            true
        });
        return true;
    }

    composition::CompositionClip make_clip(
        const VibranceCompositionRegion& region)
    {
        if (region.shape == 0u)
        {
            return nullptr;
        }
        if (region.shape == 2u)
        {
            auto geometry = compositor_.CreateEllipseGeometry();
            geometry.Center({ region.width * 0.5f, region.height * 0.5f });
            geometry.Radius({ region.width * 0.5f, region.height * 0.5f });
            return compositor_.CreateGeometricClip(geometry);
        }
        const float maximum = std::min(region.width, region.height) * 0.5f;
        const auto radius = [maximum](float requested, float fallback) {
            return std::clamp(requested > 0.0f ? requested : fallback, 0.0f, maximum);
        };
        const float topLeft = radius(region.topLeftRadius, region.cornerRadius);
        const float topRight = radius(region.topRightRadius, region.cornerRadius);
        const float bottomRight = radius(region.bottomRightRadius, region.cornerRadius);
        const float bottomLeft = radius(region.bottomLeftRadius, region.cornerRadius);
        if (region.shape == 3u || region.shape == 4u)
        {
            float clippedTopLeft = topLeft;
            float clippedTopRight = topRight;
            float amount = region.shape == 4u ?
                std::clamp(region.notchAmount, 0.0f, 1.0f) : 0.0f;
            if (region.shape == 4u)
            {
                constexpr float morphThreshold = 0.85f;
                const float topScale = std::clamp(
                    1.0f - amount / morphThreshold,
                    0.0f,
                    1.0f);
                clippedTopLeft *= topScale;
                clippedTopRight *= topScale;
            }
            auto source = winrt::make<SquircleGeometrySource>(
                region.width,
                region.height,
                clippedTopLeft,
                clippedTopRight,
                bottomRight,
                bottomLeft,
                std::max(region.squircleAmount, amount),
                region.squirclePower);
            auto path = composition::CompositionPath(source);
            return compositor_.CreateGeometricClip(
                compositor_.CreatePathGeometry(path));
        }
        try
        {
            return compositor_.CreateRectangleClip(
                0.0f,
                0.0f,
                region.width,
                region.height,
                { topLeft, topLeft },
                { topRight, topRight },
                { bottomRight, bottomRight },
                { bottomLeft, bottomLeft });
        }
        catch (...)
        {
            auto geometry = compositor_.CreateRoundedRectangleGeometry();
            geometry.Size({ region.width, region.height });
            const float uniform = std::max(
                std::max(topLeft, topRight),
                std::max(bottomRight, bottomLeft));
            geometry.CornerRadius({ uniform, uniform });
            return compositor_.CreateGeometricClip(geometry);
        }
    }

    bool add_region(const VibranceCompositionRegion& region)
    {
        if (region.material == 0u ||
            region.width <= 0.0f ||
            region.height <= 0.0f)
        {
            return false;
        }

        // Native Mica/Acrylic are window-level rectangles in this non-XAML
        // backend. Partial or non-rectangular requests are deliberately
        // rejected so an unbounded system backdrop can never leak outside the
        // requested region.
        if (region.provider != 0u)
        {
            const bool coversWindow =
                region.x <= 0.5f && region.y <= 0.5f &&
                region.width >= static_cast<float>(width_) - 1.0f &&
                region.height >= static_cast<float>(height_) - 1.0f;
            if (!coversWindow || region.shape != 0u ||
                region.verticalStart > 0.001f)
            {
                OutputDebugStringW(
                    L"vibranceUI: Windows Acrylic/Mica requires a full-window rectangle in the raw Win32 backend; request resolved to off.\n");
                return false;
            }
            const DWM_SYSTEMBACKDROP_TYPE type = region.provider == 1u ?
                DWMSBT_TRANSIENTWINDOW : DWMSBT_MAINWINDOW;
            if (FAILED(DwmSetWindowAttribute(
                    window_,
                    DWMWA_SYSTEMBACKDROP_TYPE,
                    &type,
                    sizeof(type))))
            {
                return false;
            }
            RegionVisualState state;
            state.descriptor = region;
            regionVisuals_.push_back(std::move(state));
            return true;
        }

        auto container = compositor_.CreateContainerVisual();
        container.Offset({ region.x, region.y, 0.0f });
        container.Size({ region.width, region.height });
        if (auto clip = make_clip(region))
        {
            container.Clip(clip);
        }

        RegionVisualState state;
        state.descriptor = region;
        state.container = container;

        const float materialTop =
            std::clamp(region.verticalStart, 0.0f, 0.98f) * region.height;
        VibranceCompositionRegion materialRegion = region;
        materialRegion.y += materialTop;
        materialRegion.height = std::max(region.height - materialTop, 1.0f);
        composition::ContainerVisual materialContainer = container;
        if (materialTop > 0.01f)
        {
            materialContainer = compositor_.CreateContainerVisual();
            materialContainer.Offset({ 0.0f, materialTop, 0.0f });
            materialContainer.Size({ region.width, materialRegion.height });
            container.Children().InsertAtTop(materialContainer);
        }

        if (region.material == 3u)
        {
            if (!build_liquid_lens(
                    materialContainer,
                    materialRegion,
                    state.liquidBackdropSamples))
            {
                OutputDebugStringW(
                    L"vibranceUI: liquid request resolved to off because the private DWM visual is unavailable.\n");
                return false;
            }
            for (LiquidSurfaceSample& sample : state.liquidBackdropSamples)
            {
                sample.localY += materialTop;
            }

            auto glaze = compositor_.CreateSpriteVisual();
            glaze.Size(materialContainer.Size());
            glaze.Brush(make_liquid_glaze());
            materialContainer.Children().InsertAtTop(glaze);
        }
        else
        {
            auto backdrop = compositor_.CreateSpriteVisual();
            backdrop.Size(materialContainer.Size());
            backdrop.Brush(make_effect_brush(materialRegion));
            materialContainer.Children().InsertAtBottom(backdrop);
        }

        if (region.material == 2u || region.material == 3u)
        {
            auto tint = compositor_.CreateSpriteVisual();
            tint.Size(materialContainer.Size());
            tint.Brush(compositor_.CreateColorBrush(color(
                region.tintRed,
                region.tintGreen,
                region.tintBlue,
                region.tintAlpha)));
            materialContainer.Children().InsertAtTop(tint);
        }

        regions_.Children().InsertAtTop(container);
        regionVisuals_.push_back(std::move(state));
        return true;
    }

    HWND window_ = nullptr;
    std::uint32_t width_ = 0u;
    std::uint32_t height_ = 0u;
    std::uint32_t bufferCount_ = 0u;
    LUID adapterLuid_ {};
    bool apartmentOwned_ = false;
    bool hostBackdropEnabled_ = false;
    bool privateInteropEnabled_ = false;
    bool privateDesktopWindowAttribute_ = false;
    DWORD windowsBuild_ = 0u;

    winrt::Windows::System::DispatcherQueueController dispatcherController_ { nullptr };
    winrt::com_ptr<ID3D11Device5> device_;
    winrt::com_ptr<ID3D11DeviceContext4> context_;
    winrt::com_ptr<IDXGISwapChain1> swapchain_;
    std::vector<winrt::com_ptr<ID3D11Texture2D>> sharedTextures_;
    std::vector<HANDLE> sharedHandles_;
    std::vector<winrt::com_ptr<ID3D11Query>> queries_;
    std::vector<bool> queryPending_;

    winrt::com_ptr<ID2D1Factory2> d2dFactory_;
    winrt::com_ptr<ID2D1Device> d2dDevice_;
    winrt::com_ptr<private_composition::IInteropCompositorPartner>
        privateCompositorPartner_;
    winrt::com_ptr<IDCompositionDesktopDevice> compositionDevice_;
    winrt::com_ptr<IDCompositionTarget> compositionTarget_;
    HMODULE dwmPrivateModule_ = nullptr;
    private_composition::CreateSharedMultiWindowVisualFn
        createSharedDesktopVisual_ = nullptr;
    private_composition::CreateSharedThumbnailVisualFn
        createSharedThumbnailVisual_ = nullptr;
    FARPROC updateSharedDesktopVisual_ = nullptr;
    HTHUMBNAIL privateDesktopThumbnail_ = nullptr;
    HTHUMBNAIL privateDesktopWallpaperThumbnail_ = nullptr;
    winrt::com_ptr<IDCompositionVisual2> privateDesktopVisual_;
    winrt::com_ptr<IDCompositionVisual2> privateDesktopContainer_;
    winrt::com_ptr<IDCompositionVisual2> privateDesktopWallpaperDwmVisual_;
    composition::CompositionGraphicsDevice wallpaperGraphicsDevice_ { nullptr };
    composition::CompositionDrawingSurface privateDesktopWallpaperSurface_ { nullptr };
    composition::Visual privateDesktopWallpaperVisual_ { nullptr };
    composition::Visual privateDesktopWindowsSource_ { nullptr };
    composition::ContainerVisual privateDesktopCombinedSource_ { nullptr };
    composition::Visual privateDesktopSource_ { nullptr };
    RECT privateDesktopSourceRect_ {};
    RECT privateDesktopWindowRect_ {};

    composition::Compositor compositor_ { nullptr };
    composition::CompositionTarget target_ { nullptr };
    composition::ContainerVisual root_ { nullptr };
    composition::ContainerVisual regions_ { nullptr };
    composition::SpriteVisual content_ { nullptr };
    std::vector<RegionVisualState> regionVisuals_;
};
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_composition_abi_version()
{
    return VIBRANCE_COMPOSITION_ABI_VERSION;
}

extern "C" __declspec(dllexport) const char* __cdecl
vibrance_composition_last_error()
{
    return lastCompositionError.c_str();
}

extern "C" __declspec(dllexport) VibranceCompositionHandle __cdecl
vibrance_composition_create(
    const VibranceCompositionCreateInfo* createInfo,
    VibranceCompositionBuffer* buffers,
    std::uint32_t bufferCount)
{
    if (!createInfo || createInfo->structSize < sizeof(*createInfo) ||
        !buffers || bufferCount == 0u)
    {
        return nullptr;
    }
    try
    {
        auto bridge = std::make_unique<CompositionBridge>(*createInfo);
        if (!bridge->initialise(buffers, bufferCount))
        {
            return nullptr;
        }
        return bridge.release();
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" __declspec(dllexport) void __cdecl
vibrance_composition_destroy(VibranceCompositionHandle handle)
{
    delete static_cast<CompositionBridge*>(handle);
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_composition_acquire(
    VibranceCompositionHandle handle,
    std::uint32_t index)
{
    auto* bridge = static_cast<CompositionBridge*>(handle);
    return bridge && bridge->acquire(index) ? 1u : 0u;
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_composition_present(
    VibranceCompositionHandle handle,
    std::uint32_t index)
{
    auto* bridge = static_cast<CompositionBridge*>(handle);
    return bridge && bridge->present(index) ? 1u : 0u;
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_composition_set_regions(
    VibranceCompositionHandle handle,
    const VibranceCompositionRegion* regions,
    std::uint32_t count)
{
    auto* bridge = static_cast<CompositionBridge*>(handle);
    if (!bridge || (count > 0u && !regions))
    {
        return 0u;
    }
    return bridge->set_regions(regions, count) ? 1u : 0u;
}

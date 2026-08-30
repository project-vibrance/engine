#if defined(_WIN32)

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
#include <windows.graphics.effects.interop.h>
#include <windows.graphics.interop.h>
#include <windows.ui.composition.interop.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Graphics.Effects.h>
#include <winrt/Windows.Graphics.h>
#include <winrt/Windows.System.h>
#include <winrt/Windows.UI.h>
#include <winrt/Windows.UI.Composition.Desktop.h>
#include <winrt/Windows.UI.Composition.h>
#include <winrt/base.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>
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

constexpr HRESULT windowAlreadyComposed =
    static_cast<HRESULT>(0x88980800u);

std::mutex activeCompositionWindowsMutex;
std::unordered_set<HWND> activeCompositionWindows;

bool claim_composition_window(HWND window)
{
    std::lock_guard<std::mutex> lock(activeCompositionWindowsMutex);
    return window && activeCompositionWindows.insert(window).second;
}

void release_composition_window(HWND window) noexcept
{
    std::lock_guard<std::mutex> lock(activeCompositionWindowsMutex);
    activeCompositionWindows.erase(window);
}

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
    struct RegionVisualState
    {
        VibranceCompositionRegion descriptor {};
        composition::ContainerVisual container { nullptr };
        composition::ContainerVisual materialContainer { nullptr };
        composition::SpriteVisual materialVisual { nullptr };
        composition::SpriteVisual tintVisual { nullptr };
        bool materialInset = false;
    };

public:
    explicit CompositionBridge(const VibranceCompositionCreateInfo& info) :
        window_(static_cast<HWND>(info.window)),
        width_(info.width),
        height_(info.height),
        bufferCount_(info.bufferCount),
        windowClaimed_(claim_composition_window(window_))
    {
        adapterLuid_.LowPart = info.adapterLuidLow;
        adapterLuid_.HighPart = info.adapterLuidHigh;
    }

    ~CompositionBridge()
    {
        release_composition_target();
        content_ = nullptr;
        regions_ = nullptr;
        root_ = nullptr;
        compositor_ = nullptr;
        privateCompositorPartner_ = nullptr;
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
        backBuffers_.clear();
        backBufferInitialised_.clear();
        backBufferContentRects_.clear();
        backBufferPendingDamageRects_.clear();
        swapchain3_ = nullptr;
        swapchain_ = nullptr;
        if (frameLatencyWaitableObject_)
        {
            CloseHandle(frameLatencyWaitableObject_);
            frameLatencyWaitableObject_ = nullptr;
        }
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
        if (windowClaimed_)
        {
            release_composition_window(window_);
            windowClaimed_ = false;
        }
    }

    bool initialise(
        VibranceCompositionBuffer* outputBuffers,
        std::uint32_t outputCount)
    {
        lastCompositionError.clear();
        if (!windowClaimed_)
        {
            lastCompositionError =
                "another vibranceUI Composition presenter already owns this HWND";
            return false;
        }
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
                if (lastCompositionError.empty())
                {
                    lastCompositionError =
                        "Windows.UI.Composition initialisation failed";
                }
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

    std::uint32_t present(
        std::uint32_t index,
        const VibranceCompositionPresentInfo* presentInfo)
    {
        if (index >= sharedTextures_.size() || !swapchain_ || !swapchain3_ ||
            !context_ || backBuffers_.empty() || !presentInfo ||
            presentInfo->structSize < sizeof(VibranceCompositionPresentInfo))
        {
            return VIBRANCE_COMPOSITION_PRESENT_FAILED;
        }

        const bool synchronize =
            (presentInfo->flags & VIBRANCE_COMPOSITION_PRESENT_SYNCHRONIZE) != 0u;
        // Avoid recording a full-size D3D copy when DWM has not consumed the
        // previous frame. The newest completed Vulkan frame will be offered on
        // the next display opportunity instead.
        if (frameLatencyWaitableObject_)
        {
            const DWORD waitResult = WaitForSingleObject(
                frameLatencyWaitableObject_,
                synchronize ? 100u : 0u);
            if (waitResult == WAIT_TIMEOUT)
            {
                return VIBRANCE_COMPOSITION_PRESENT_DEFERRED;
            }
            if (waitResult == WAIT_FAILED)
            {
                return VIBRANCE_COMPOSITION_PRESENT_FAILED;
            }
        }
        const std::uint32_t backBufferIndex =
            swapchain3_->GetCurrentBackBufferIndex();
        if (backBufferIndex >= backBuffers_.size() ||
            backBufferIndex >= backBufferInitialised_.size() ||
            backBufferIndex >= backBufferContentRects_.size() ||
            backBufferIndex >= backBufferPendingDamageRects_.size())
        {
            return VIBRANCE_COMPOSITION_PRESENT_FAILED;
        }

        const LONG contentLeft = static_cast<LONG>(std::min(
            presentInfo->contentX,
            width_));
        const LONG contentTop = static_cast<LONG>(std::min(
            presentInfo->contentY,
            height_));
        const LONG contentRight = static_cast<LONG>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(presentInfo->contentX) +
                presentInfo->contentWidth,
            width_));
        const LONG contentBottom = static_cast<LONG>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(presentInfo->contentY) +
                presentInfo->contentHeight,
            height_));
        RECT contentRect {
            contentLeft,
            contentTop,
            std::max(contentRight, contentLeft),
            std::max(contentBottom, contentTop)
        };
        const LONG damageLeft = static_cast<LONG>(std::min(
            presentInfo->damageX,
            width_));
        const LONG damageTop = static_cast<LONG>(std::min(
            presentInfo->damageY,
            height_));
        const LONG damageRight = static_cast<LONG>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(presentInfo->damageX) +
                presentInfo->damageWidth,
            width_));
        const LONG damageBottom = static_cast<LONG>(std::min<std::uint64_t>(
            static_cast<std::uint64_t>(presentInfo->damageY) +
                presentInfo->damageHeight,
            height_));
        RECT damageRect {
            damageLeft,
            damageTop,
            std::max(damageRight, damageLeft),
            std::max(damageBottom, damageTop)
        };
        auto has_area = [](const RECT& rect) {
            return rect.right > rect.left && rect.bottom > rect.top;
        };
        auto union_rect = [&](const RECT& left, const RECT& right) {
            if (!has_area(left))
            {
                return right;
            }
            if (!has_area(right))
            {
                return left;
            }
            return RECT {
                std::min(left.left, right.left),
                std::min(left.top, right.top),
                std::max(left.right, right.right),
                std::max(left.bottom, right.bottom)
            };
        };

        for (RECT& pendingDamage : backBufferPendingDamageRects_)
        {
            pendingDamage = union_rect(pendingDamage, damageRect);
        }
        RECT copyRect = backBufferPendingDamageRects_[backBufferIndex];
        if (!backBufferInitialised_[backBufferIndex])
        {
            copyRect = {
                0,
                0,
                static_cast<LONG>(width_),
                static_cast<LONG>(height_)
            };
            context_->CopyResource(
                backBuffers_[backBufferIndex].get(),
                sharedTextures_[index].get());
            backBufferInitialised_[backBufferIndex] = true;
        }
        else if (has_area(copyRect))
        {
            D3D11_BOX sourceBox {};
            sourceBox.left = static_cast<UINT>(copyRect.left);
            sourceBox.top = static_cast<UINT>(copyRect.top);
            sourceBox.front = 0u;
            sourceBox.right = static_cast<UINT>(copyRect.right);
            sourceBox.bottom = static_cast<UINT>(copyRect.bottom);
            sourceBox.back = 1u;
            context_->CopySubresourceRegion(
                backBuffers_[backBufferIndex].get(),
                0u,
                sourceBox.left,
                sourceBox.top,
                0u,
                sharedTextures_[index].get(),
                0u,
                &sourceBox);
        }
        backBufferContentRects_[backBufferIndex] = contentRect;
        context_->End(queries_[index].get());
        queryPending_[index] = true;
        DXGI_PRESENT_PARAMETERS parameters {};
        parameters.DirtyRectsCount = has_area(copyRect) ? 1u : 0u;
        parameters.pDirtyRects = has_area(copyRect) ? &copyRect : nullptr;
        const HRESULT presentResult = swapchain_->Present1(
            0u,
            synchronize ? 0u : DXGI_PRESENT_DO_NOT_WAIT,
            &parameters);
        // The compositor already has a newer queued frame. Dropping this copy
        // is preferable to blocking the Vulkan submission thread.
        if (presentResult == DXGI_ERROR_WAS_STILL_DRAWING)
        {
            // Present did not submit this command stream, so explicitly flush
            // the copy/query that releases the shared texture back to Vulkan.
            context_->Flush();
            return VIBRANCE_COMPOSITION_PRESENT_DEFERRED;
        }
        if (SUCCEEDED(presentResult))
        {
            backBufferPendingDamageRects_[backBufferIndex] = RECT {};
        }
        return SUCCEEDED(presentResult) ?
            VIBRANCE_COMPOSITION_PRESENTED :
            VIBRANCE_COMPOSITION_PRESENT_FAILED;
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
            bool canUpdateGeometry = regionVisuals_.size() == count;
            bool exactlyUnchanged = canUpdateGeometry;
            if (canUpdateGeometry)
            {
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    exactlyUnchanged = exactlyUnchanged &&
                        std::memcmp(
                            &regionVisuals_[index].descriptor,
                            &regions[index],
                            sizeof(regions[index])) == 0;
                    if (!same_region_effect_graph(
                            regionVisuals_[index].descriptor,
                            regions[index]))
                    {
                        canUpdateGeometry = false;
                        break;
                    }
                }
            }
            if (exactlyUnchanged)
            {
                return true;
            }
            if (canUpdateGeometry)
            {
                // Position, size, radii, squircle and notch changes only alter
                // visual geometry. Retain the HostBackdrop effect and glaze
                // graph while an island morphs; rebuilding every region
                // on every spring sample creates a large transient GPU/resource
                // backlog in DWM that can remain visible after hover ends.
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    update_region_geometry(regionVisuals_[index], regions[index]);
                }
                return commit_composition_changes();
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
            return commit_composition_changes();
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
    bool commit_composition_changes()
    {
        // The private Win32 interop compositor uses IDComposition and does not
        // auto-commit Windows.UI.Composition property changes. Without this
        // transaction, an island can collapse in the Vulkan surface while its
        // old expanded HostBackdrop geometry remains cached by DWM.
        if (!compositionDevice_)
        {
            return true;
        }
        const HRESULT result = compositionDevice_->Commit();
        if (FAILED(result))
        {
            lastCompositionError =
                "commit backdrop geometry: HRESULT " +
                std::to_string(static_cast<std::int32_t>(result));
            return false;
        }
        return true;
    }

    static bool same_region_effect_graph(
        VibranceCompositionRegion left,
        VibranceCompositionRegion right) noexcept
    {
        // These fields affect only visual geometry. Material/provider, blur,
        // saturation, tint and vertical split still require a new effect graph.
        left.shape = 0u;
        right.shape = 0u;
        left.x = 0.0f;
        left.y = 0.0f;
        right.x = 0.0f;
        right.y = 0.0f;
        left.width = 0.0f;
        left.height = 0.0f;
        right.width = 0.0f;
        right.height = 0.0f;
        left.cornerRadius = 0.0f;
        left.topLeftRadius = 0.0f;
        left.topRightRadius = 0.0f;
        left.bottomRightRadius = 0.0f;
        left.bottomLeftRadius = 0.0f;
        right.cornerRadius = 0.0f;
        right.topLeftRadius = 0.0f;
        right.topRightRadius = 0.0f;
        right.bottomRightRadius = 0.0f;
        right.bottomLeftRadius = 0.0f;
        left.squircleAmount = 0.0f;
        left.squirclePower = 0.0f;
        left.notchAmount = 0.0f;
        left.notchDepth = 0.0f;
        right.squircleAmount = 0.0f;
        right.squirclePower = 0.0f;
        right.notchAmount = 0.0f;
        right.notchDepth = 0.0f;
        return std::memcmp(&left, &right, sizeof(left)) == 0;
    }

    void update_region_geometry(
        RegionVisualState& state,
        const VibranceCompositionRegion& region)
    {
        const VibranceCompositionRegion& previous = state.descriptor;
        const bool positionChanged =
            previous.x != region.x ||
            previous.y != region.y;
        const bool sizeChanged =
            previous.width != region.width ||
            previous.height != region.height;
        const bool materialGeometryChanged =
            sizeChanged ||
            previous.verticalStart != region.verticalStart;
        const bool clipChanged =
            sizeChanged ||
            previous.shape != region.shape ||
            previous.cornerRadius != region.cornerRadius ||
            previous.topLeftRadius != region.topLeftRadius ||
            previous.topRightRadius != region.topRightRadius ||
            previous.bottomRightRadius != region.bottomRightRadius ||
            previous.bottomLeftRadius != region.bottomLeftRadius ||
            previous.squircleAmount != region.squircleAmount ||
            previous.squirclePower != region.squirclePower ||
            previous.notchAmount != region.notchAmount ||
            previous.notchDepth != region.notchDepth;

        if (state.container)
        {
            if (positionChanged)
            {
                state.container.Offset({ region.x, region.y, 0.0f });
            }
            if (sizeChanged)
            {
                state.container.Size({ region.width, region.height });
            }
            if (clipChanged)
            {
                // Composition clips are immutable geometry objects. Replacing
                // one for a pure translation allocated compositor resources
                // every pointer sample and was the main drag-time GPU spike.
                state.container.Clip(make_clip(region));
            }
        }

        const float materialTop =
            std::clamp(region.verticalStart, 0.0f, 0.98f) * region.height;
        const float materialHeight = std::max(region.height - materialTop, 1.0f);
        if (materialGeometryChanged && state.materialContainer)
        {
            if (state.materialInset)
            {
                state.materialContainer.Offset({ 0.0f, materialTop, 0.0f });
            }
            state.materialContainer.Size({ region.width, materialHeight });
        }
        if (materialGeometryChanged && state.materialVisual)
        {
            state.materialVisual.Size({ region.width, materialHeight });
        }
        if (materialGeometryChanged && state.tintVisual)
        {
            state.tintVisual.Size({ region.width, materialHeight });
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
        swapchainDescription.Flags =
            DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT;
        HRESULT swapchainResult = factory->CreateSwapChainForComposition(
                device_.get(),
                &swapchainDescription,
                nullptr,
                swapchain_.put());
        if (FAILED(swapchainResult))
        {
            // Some older DXGI implementations reject the latency flag for a
            // Composition swapchain. Retain a fully supported fallback.
            swapchainDescription.Flags = 0u;
            swapchainResult = factory->CreateSwapChainForComposition(
                device_.get(),
                &swapchainDescription,
                nullptr,
                swapchain_.put());
        }
        if (FAILED(swapchainResult))
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
            frameLatencyWaitableObject_ =
                lowLatencySwapchain->GetFrameLatencyWaitableObject();
        }
        if (FAILED(swapchain_->QueryInterface(swapchain3_.put())))
        {
            lastCompositionError = "IDXGISwapChain3 interface query failed";
            return false;
        }
        backBuffers_.resize(swapchainDescription.BufferCount);
        backBufferInitialised_.assign(swapchainDescription.BufferCount, false);
        backBufferContentRects_.assign(swapchainDescription.BufferCount, RECT {});
        backBufferPendingDamageRects_.assign(swapchainDescription.BufferCount, RECT {});
        for (std::uint32_t index = 0u;
            index < swapchainDescription.BufferCount; ++index)
        {
            if (FAILED(swapchain_->GetBuffer(
                    index,
                    __uuidof(ID3D11Texture2D),
                    backBuffers_[index].put_void())))
            {
                lastCompositionError = "D3D11 swapchain back-buffer query failed";
                return false;
            }
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
            HRESULT targetResult = compositionDevice->CreateTargetForHwnd(
                window_,
                TRUE,
                compositionTarget.put());
            if (targetResult == windowAlreadyComposed)
            {
                // An existing framework/native presenter may already own the
                // topmost layer. Windows supports one target on each HWND
                // layer, so use the non-topmost layer before giving up.
                targetResult = compositionDevice->CreateTargetForHwnd(
                    window_,
                    FALSE,
                    compositionTarget.put());
            }
            if (FAILED(targetResult))
            {
                return false;
            }
            auto target = compositionTarget.try_as<composition::CompositionTarget>();
            if (!target)
            {
                // CreateTargetForHwnd can bind the HWND before the private
                // target is proven compatible with this Windows runtime. Undo
                // that binding and wait for DWM before the public fallback.
                (void)compositionTarget->SetRoot(nullptr);
                (void)compositionDevice->Commit();
                (void)compositionDevice->WaitForCommitCompletion();
                compositionTarget = nullptr;
                DwmFlush();
                return false;
            }

            d2dFactory_ = std::move(d2dFactory);
            d2dDevice_ = std::move(d2dDevice);
            privateCompositorPartner_ = std::move(partner);
            compositionDevice_ = std::move(compositionDevice);
            compositionTarget_ = std::move(compositionTarget);
            compositor_ = std::move(compositor);
            target_ = std::move(target);
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
            HRESULT targetResult = desktopInterop->CreateDesktopWindowTarget(
                window_,
                true,
                reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(
                    winrt::put_abi(desktopTarget)));
            if (targetResult == windowAlreadyComposed)
            {
                desktopTarget = nullptr;
                targetResult = desktopInterop->CreateDesktopWindowTarget(
                    window_,
                    false,
                    reinterpret_cast<ABI::Windows::UI::Composition::Desktop::IDesktopWindowTarget**>(
                        winrt::put_abi(desktopTarget)));
            }
            winrt::check_hresult(targetResult);
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

    void release_composition_target() noexcept
    {
        try
        {
            if (regions_)
            {
                regions_.Children().RemoveAll();
            }
            if (target_)
            {
                target_.Root(nullptr);
            }
        }
        catch (...)
        {
        }
        if (compositionTarget_)
        {
            (void)compositionTarget_->SetRoot(nullptr);
        }
        if (compositionDevice_)
        {
            (void)compositionDevice_->Commit();
            (void)compositionDevice_->WaitForCommitCompletion();
        }
        target_ = nullptr;
        compositionTarget_ = nullptr;
        compositionDevice_ = nullptr;
        // Target release is asynchronous with DWM. Flushing here prevents a
        // resize/rebuild from racing the old target on the same HWND layer.
        if (window_)
        {
            DwmFlush();
        }
    }

    composition::CompositionBrush make_effect_brush(
        const VibranceCompositionRegion& region)
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
            compositor_.CreateHostBackdropBrush());
        return brush;
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
        // ABI material 1 is eSystemGlass. No other material is allowed into
        // the native compositor bridge.
        if (region.material != 1u ||
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
            state.materialInset = true;
        }
        state.materialContainer = materialContainer;

        auto backdrop = compositor_.CreateSpriteVisual();
        backdrop.Size(materialContainer.Size());
        backdrop.Brush(make_effect_brush(materialRegion));
        materialContainer.Children().InsertAtBottom(backdrop);
        state.materialVisual = backdrop;

        if (region.tintAlpha > 0.0001f)
        {
            auto tint = compositor_.CreateSpriteVisual();
            tint.Size(materialContainer.Size());
            tint.Brush(compositor_.CreateColorBrush(color(
                region.tintRed,
                region.tintGreen,
                region.tintBlue,
                region.tintAlpha)));
            materialContainer.Children().InsertAtTop(tint);
            state.tintVisual = tint;
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
    bool windowClaimed_ = false;

    winrt::Windows::System::DispatcherQueueController dispatcherController_ { nullptr };
    winrt::com_ptr<ID3D11Device5> device_;
    winrt::com_ptr<ID3D11DeviceContext4> context_;
    winrt::com_ptr<IDXGISwapChain1> swapchain_;
    winrt::com_ptr<IDXGISwapChain3> swapchain3_;
    std::vector<winrt::com_ptr<ID3D11Texture2D>> backBuffers_;
    std::vector<bool> backBufferInitialised_;
    std::vector<RECT> backBufferContentRects_;
    std::vector<RECT> backBufferPendingDamageRects_;
    HANDLE frameLatencyWaitableObject_ = nullptr;
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
    std::uint32_t index,
    const VibranceCompositionPresentInfo* presentInfo)
{
    auto* bridge = static_cast<CompositionBridge*>(handle);
    return bridge ?
        bridge->present(index, presentInfo) :
        VIBRANCE_COMPOSITION_PRESENT_FAILED;
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

#endif // defined(_WIN32)

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <initguid.h>
#include <shellapi.h>

#include "composition_bridge_abi.h"
#include "composition_region_effects.h"
#include "composition_raw_backdrop_clip.h"
#include "composition_present_retry.h"
#include "composition_upload.h"
#include "composition_damage.h"
#include "composition_copy_diagnostics.h"
#include "liquid_glass_d3d11.h"
#include "liquid_glass_dwm_protocol.h"
#include "liquid_glass_dwm_security.h"
#include "liquid_glass_dwm_startup.h"
#include "liquid_glass_dwm_windows10_layout.h"
#include "private_composition_19041.h"
#include "system_backdrop_policy.h"

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
#include <array>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
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
    constexpr UINT kSharedTextureMiscFlags =
        D3D11_RESOURCE_MISC_SHARED_NTHANDLE |
        D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX;
    static_assert(
        (kSharedTextureMiscFlags & D3D11_RESOURCE_MISC_SHARED_NTHANDLE) != 0u);
    static_assert(
        (kSharedTextureMiscFlags & D3D11_RESOURCE_MISC_SHARED_KEYEDMUTEX) != 0u);
    static_assert(
        (kSharedTextureMiscFlags & D3D11_RESOURCE_MISC_SHARED) == 0u);

thread_local std::string lastCompositionError;

constexpr HRESULT windowAlreadyComposed =
    static_cast<HRESULT>(0x88980800u);

std::mutex activeCompositionWindowsMutex;
std::unordered_set<HWND> activeCompositionWindows;
std::once_flag liquidExtensionLaunch;
DWORD windows_build_number() noexcept;

bool liquid_extension_disabled()
{
    const auto build = windows_build_number();
#if defined(_M_ARM64)
    constexpr std::uint32_t machine = IMAGE_FILE_MACHINE_ARM64;
#else
    constexpr std::uint32_t machine = IMAGE_FILE_MACHINE_AMD64;
#endif
    USHORT processMachine = 0, nativeMachine = 0;
    if (!IsWow64Process2(GetCurrentProcess(), &processMachine, &nativeMachine) ||
        nativeMachine != machine ||
        !vibrance::win32::liquid_dwm_native_hooks_allowed(build, machine)) return true;
    // Session transitions are guarded by the native observer's readiness
    // event. A stable RDP session must still be allowed to start the broker.
    wchar_t value[16] {};
    GetEnvironmentVariableW(L"VIBRANCE_DISABLE_LIQUID_DWM_EXTENSION", value,
        ARRAYSIZE(value));
    return _wcsicmp(value, L"1") == 0 || _wcsicmp(value, L"true") == 0;
}

void launch_liquid_extension_once()
{
    std::call_once(liquidExtensionLaunch, [] {
        if (liquid_extension_disabled()) return;
        std::thread([] {
        wchar_t executable[MAX_PATH] {};
        const DWORD pathLength = GetModuleFileNameW(nullptr, executable,
            ARRAYSIZE(executable));
        if (!pathLength || pathLength >= ARRAYSIZE(executable))
        {
            return;
        }
        std::wstring directory(executable, pathLength);
        const auto separator = directory.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            return;
        }
        directory.resize(separator + 1);
        const std::wstring broker = directory +
            L"liquid_glass_dwm_broker.exe";
        const std::wstring extension = directory +
            L"vibrance_liquid_dwm_extension.dll";
        if (GetFileAttributesW(broker.c_str()) ==
                INVALID_FILE_ATTRIBUTES ||
            GetFileAttributesW(extension.c_str()) ==
                INVALID_FILE_ATTRIBUTES)
        {
            return;
        }
        namespace startup = vibrance::win32::liquid_startup;
        const auto local = startup::known_folder(FOLDERID_LocalAppData) / L"VibranceDwmSymbols";
        std::error_code error;
        std::filesystem::create_directories(local, error);
        const auto report = [&](const std::string& message) {
            std::ofstream log(local / L"launcher.log", std::ios::trunc);
            log << message << '\n';
        };
        const auto key = startup::bundle_key(directory);
        if (key.empty())
        {
            report("Liquid Glass bundle is incomplete. Reinstall the native architecture build including dbghelp.dll and symsrv.dll.");
            return;
        }
        const auto installed = startup::installed_directory(key);
        if (SUCCEEDED(startup::run_task(installed)))
        {
            report("Automatic Liquid Glass helper started without elevation.");
            return;
        }
        // A first-time consent prompt switches to the secure desktop. Defer it
        // until a game has yielded and display-mode transitions have settled.
        unsigned desktopSamples = 0;
        while (desktopSamples < 4)
        {
            desktopSamples = startup::fullscreen_active() ? 0 : desktopSamples + 1;
            Sleep(500);
        }
        const std::wstring parameters = L"--install-auto-run " + startup::user_sid();
        SHELLEXECUTEINFOW launch { sizeof(launch) };
        launch.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_FLAG_NO_UI;
        launch.lpVerb = L"runas";
        launch.lpFile = broker.c_str();
        launch.lpParameters = parameters.c_str();
        launch.nShow = SW_HIDE;
        if (!ShellExecuteExW(&launch))
        {
            report("Automatic helper setup was not approved or could not launch (Win32 error " +
                std::to_string(GetLastError()) + "). Liquid Glass retains its Composition fallback.");
        }
        else if (launch.hProcess)
        {
            WaitForSingleObject(launch.hProcess, INFINITE);
            DWORD code = 0;
            GetExitCodeProcess(launch.hProcess, &code);
            report("Automatic helper setup returned exit code " + std::to_string(code) + ".");
            CloseHandle(launch.hProcess);
        }
        }).detach();
    });
}

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
// WinRT geometry-source contract so Composition can clip the backdrop to the
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
        float power,
        ID2D1Factory* factory = nullptr) :
        width_(std::max(width, 0.0f)),
        height_(std::max(height, 0.0f)),
        topLeftRadius_(topLeftRadius),
        topRightRadius_(topRightRadius),
        bottomRightRadius_(bottomRightRadius),
        bottomLeftRadius_(bottomLeftRadius),
        amount_(std::clamp(amount, 0.0f, 1.0f)),
        power_(std::clamp(power, 2.0f, 5.0f))
    {
        if (factory) factory_.copy_from(factory);
        else winrt::check_hresult(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_MULTI_THREADED, factory_.put()));
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

// Only the identity backdrop dependency needs this silhouette. The marker
// stays unclipped so DWM can identify it on the original composition target.
// Geometry is retained with its visual, and is never rebuilt for translation.
class RawBackdropGeometrySource :
    public winrt::implements<RawBackdropGeometrySource,
        winrt::Windows::Graphics::IGeometrySource2D,
        ABI::Windows::Graphics::IGeometrySource2DInterop>
{
public:
    RawBackdropGeometrySource(const VibranceCompositionRegion& region,
        float horizontalPadding) :
        region_(vibrance::win32::composition_raw_backdrop_source_region(region)),
        horizontalPadding_(horizontalPadding)
    {
        // All bridge updates use its composition thread. Share one factory
        // across retained clips rather than allocating a factory per lens.
        static thread_local winrt::com_ptr<ID2D1Factory> sharedFactory;
        if (!sharedFactory) winrt::check_hresult(D2D1CreateFactory(
            D2D1_FACTORY_TYPE_MULTI_THREADED, sharedFactory.put()));
        factory_ = sharedFactory;
        geometry_ = create_geometry(factory_.get());
    }

    HRESULT STDMETHODCALLTYPE GetGeometry(ID2D1Geometry** value) noexcept override
    {
        if (!value) return E_POINTER;
        geometry_.copy_to(value);
        return S_OK;
    }

    HRESULT STDMETHODCALLTYPE TryGetGeometryUsingFactory(
        ID2D1Factory* factory, ID2D1Geometry** value) noexcept override
    {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (!factory) return E_INVALIDARG;
        try
        {
            if (factory == factory_.get()) geometry_.copy_to(value);
            else create_geometry(factory).copy_to(value);
            return S_OK;
        }
        catch (...) { return winrt::to_hresult(); }
    }

private:
    static winrt::com_ptr<ID2D1Geometry> combine(ID2D1Factory* factory,
        ID2D1Geometry* left, ID2D1Geometry* right, D2D1_COMBINE_MODE mode)
    {
        winrt::com_ptr<ID2D1PathGeometry> path;
        winrt::check_hresult(factory->CreatePathGeometry(path.put()));
        winrt::com_ptr<ID2D1GeometrySink> sink;
        winrt::check_hresult(path->Open(sink.put()));
        winrt::check_hresult(left->CombineWithGeometry(right, mode, nullptr, 0.05f, sink.get()));
        winrt::check_hresult(sink->Close());
        return path.as<ID2D1Geometry>();
    }

    static winrt::com_ptr<ID2D1Geometry> leaf(ID2D1Factory* factory,
        const VibranceCompositionContour& contour)
    {
        const float width = std::max(contour.width, 0.0f);
        const float height = std::max(contour.height, 0.0f);
        if (contour.shape == 0u)
        {
            winrt::com_ptr<ID2D1RectangleGeometry> rectangle;
            winrt::check_hresult(factory->CreateRectangleGeometry(
                D2D1::RectF(0, 0, width, height), rectangle.put()));
            return rectangle.as<ID2D1Geometry>();
        }
        if (contour.shape == 2u)
        {
            winrt::com_ptr<ID2D1EllipseGeometry> ellipse;
            winrt::check_hresult(factory->CreateEllipseGeometry(D2D1::Ellipse(
                D2D1::Point2F(width * 0.5f, height * 0.5f),
                width * 0.5f, height * 0.5f), ellipse.put()));
            return ellipse.as<ID2D1Geometry>();
        }
        if (contour.shape == 5u)
        {
            winrt::com_ptr<ID2D1PathGeometry> path;
            winrt::check_hresult(factory->CreatePathGeometry(path.put()));
            winrt::com_ptr<ID2D1GeometrySink> sink;
            winrt::check_hresult(path->Open(sink.put()));
            sink->SetFillMode(D2D1_FILL_MODE_WINDING);
            for (const auto& profile : vibrance::win32::composition_bridge_clip_profiles(
                width, height, contour.notchAmount))
            {
                if (profile.size() < 2u) continue;
                sink->BeginFigure(D2D1::Point2F(profile.front()[0],
                    height * 0.5f - profile.front()[1]), D2D1_FIGURE_BEGIN_FILLED);
                for (const auto& point : profile) sink->AddLine(
                    D2D1::Point2F(point[0], height * 0.5f - point[1]));
                for (auto point = profile.rbegin(); point != profile.rend(); ++point)
                    sink->AddLine(D2D1::Point2F((*point)[0], height * 0.5f + (*point)[1]));
                sink->EndFigure(D2D1_FIGURE_END_CLOSED);
            }
            winrt::check_hresult(sink->Close());
            return path.as<ID2D1Geometry>();
        }
        const float maximum = std::min(width, height) * 0.5f;
        const auto radius = [maximum, &contour](float value) {
            return std::clamp(value > 0.0f ? value : contour.cornerRadius, 0.0f, maximum);
        };
        const float notch = contour.shape == 4u && contour.notchAmount > 0.01f ?
            std::clamp(contour.notchAmount, 0.0f, 1.0f) : 0.0f;
        const float topScale = 1.0f - std::clamp(notch / 0.85f, 0.0f, 1.0f);
        const float amount = contour.shape >= 3u ? std::max(contour.squircleAmount, notch) : 0.0f;
        const float uniform = radius(contour.cornerRadius);
        auto source = winrt::make_self<SquircleGeometrySource>(width, height,
            (notch > 0 ? uniform : radius(contour.topLeftRadius)) * topScale,
            (notch > 0 ? uniform : radius(contour.topRightRadius)) * topScale,
            notch > 0 ? uniform : radius(contour.bottomRightRadius),
            notch > 0 ? uniform : radius(contour.bottomLeftRadius),
            amount, contour.squirclePower, factory);
        winrt::com_ptr<ID2D1Geometry> body;
        winrt::check_hresult(source->GetGeometry(body.put()));
        // A fully formed notch flares beyond its body at the screen edge.
        // Use the same circular cut-outs as the optical shader, retaining
        // those corners instead of clipping them to the body's rectangle.
        if (notch > 0.85f)
        {
            const float depth = contour.notchDepth > 0.5f ? contour.notchDepth :
                std::clamp(height * 0.62f, 0.0f, 92.0f);
            const float target = std::clamp(depth * 0.18f, 0.0f, height * 0.5f);
            const float progress = std::clamp((notch - 0.85f) / 0.15f, 0.0f, 1.0f);
            const float factor = std::clamp(height * 0.5f / std::max(target, 0.001f), 0.0f, 1.0f) * progress;
            const float flare = target * factor * factor;
            if (flare > 0.05f)
            {
                winrt::com_ptr<ID2D1RectangleGeometry> bar;
                winrt::check_hresult(factory->CreateRectangleGeometry(
                    D2D1::RectF(-flare, 0, width + flare, flare), bar.put()));
                winrt::com_ptr<ID2D1Geometry> cap = bar.as<ID2D1Geometry>();
                for (float x : { -flare, width + flare })
                {
                    winrt::com_ptr<ID2D1EllipseGeometry> cutout;
                    winrt::check_hresult(factory->CreateEllipseGeometry(D2D1::Ellipse(
                        D2D1::Point2F(x, flare), flare, flare), cutout.put()));
                    cap = combine(factory, cap.get(), cutout.get(), D2D1_COMBINE_MODE_EXCLUDE);
                }
                body = combine(factory, body.get(), cap.get(), D2D1_COMBINE_MODE_UNION);
            }
        }
        return body;
    }

    static winrt::com_ptr<ID2D1Geometry> placed_leaf(ID2D1Factory* factory,
        const VibranceCompositionContour& contour)
    {
        auto geometry = leaf(factory, contour);
        if (contour.x == 0.0f && contour.y == 0.0f) return geometry;
        winrt::com_ptr<ID2D1TransformedGeometry> translated;
        winrt::check_hresult(factory->CreateTransformedGeometry(geometry.get(),
            D2D1::Matrix3x2F::Translation(contour.x, contour.y), translated.put()));
        return translated.as<ID2D1Geometry>();
    }

    winrt::com_ptr<ID2D1Geometry> create_geometry(ID2D1Factory* factory) const
    {
        std::array<winrt::com_ptr<ID2D1Geometry>, 3> leaves;
        std::array<ID2D1Geometry*, 3> pointers {};
        const unsigned count = std::clamp(region_.contourCount, 1u, 3u);
        if (region_.contourCount == 0u)
        {
            VibranceCompositionContour contour {};
            contour.width = region_.width; contour.height = region_.height;
            contour.cornerRadius = region_.cornerRadius;
            contour.topLeftRadius = region_.topLeftRadius; contour.topRightRadius = region_.topRightRadius;
            contour.bottomRightRadius = region_.bottomRightRadius; contour.bottomLeftRadius = region_.bottomLeftRadius;
            contour.squircleAmount = region_.squircleAmount; contour.squirclePower = region_.squirclePower;
            contour.notchAmount = region_.notchAmount; contour.notchDepth = region_.notchDepth;
            contour.shape = region_.shape;
            leaves[0] = leaf(factory, contour);
        }
        else for (unsigned index = 0; index < count; ++index)
            leaves[index] = placed_leaf(factory, region_.contours[index]);
        for (unsigned index = 0; index < count; ++index) pointers[index] = leaves[index].get();
        winrt::com_ptr<ID2D1GeometryGroup> group;
        winrt::check_hresult(factory->CreateGeometryGroup(D2D1_FILL_MODE_WINDING,
            pointers.data(), count, group.put()));
        winrt::com_ptr<ID2D1Geometry> result = group.as<ID2D1Geometry>();
        if (region_.contourCount != 0u)
        {
            // Contours keep their full body geometry when a material starts
            // part-way down the island. Match the marker's actual draw bounds
            // rather than letting that body paint above the glass slice.
            winrt::com_ptr<ID2D1RectangleGeometry> bounds;
            winrt::check_hresult(factory->CreateRectangleGeometry(D2D1::RectF(
                -horizontalPadding_, 0, region_.width + horizontalPadding_,
                region_.height), bounds.put()));
            result = combine(factory, result.get(), bounds.get(), D2D1_COMBINE_MODE_INTERSECT);
        }
        return result;
    }

    VibranceCompositionRegion region_ {};
    float horizontalPadding_ = 0.0f;
    winrt::com_ptr<ID2D1Factory> factory_;
    winrt::com_ptr<ID2D1Geometry> geometry_;
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
    struct LiquidTile
    {
        composition::CompositionSurfaceBrush brush { nullptr };
        composition::SpriteVisual visual { nullptr };
    };

    struct RegionVisualState
    {
        VibranceCompositionRegion descriptor {};
        composition::ContainerVisual container { nullptr };
        composition::ContainerVisual materialContainer { nullptr };
        composition::SpriteVisual materialVisual { nullptr };
        composition::SpriteVisual rawBackdropVisual { nullptr };
        composition::CompositionClip rawBackdropClip { nullptr };
        composition::ContainerVisual rawBackdropContainer { nullptr };
        composition::SpriteVisual liquidSourceVisual { nullptr };
        composition::CompositionVisualSurface liquidSurface { nullptr };
        std::vector<LiquidTile> liquidTiles;
        composition::SpriteVisual tintVisual { nullptr };
        composition::CompositionLinearGradientBrush viewportMaskBrush { nullptr };
        bool materialInset = false;
        bool shaderOnly = false;
        bool rimClockRunning = false;
        float stoppedClockOpacity = -1.0f;
        std::uint32_t shaderSlot = vibrance::win32::liquidDwmRegionSlots;
    };

    struct LiquidRegionBinding
    {
        std::uint64_t identity = 0u;
        std::uint64_t regionId = 0u;
        vibrance::win32::LiquidDwmRegion* view = nullptr;
        std::uint32_t slot = vibrance::win32::liquidDwmRegionSlots;
    };

public:
    explicit CompositionBridge(const VibranceCompositionCreateInfo& info) :
        window_(static_cast<HWND>(info.window)),
        width_(info.width),
        height_(info.height),
        bufferCount_(info.bufferCount),
        transparentFramebuffer_(
            (info.flags &
                VIBRANCE_COMPOSITION_CREATE_TRANSPARENT_FRAMEBUFFER) != 0u),
        cpuUpload_(
            (info.flags & VIBRANCE_COMPOSITION_CREATE_CPU_UPLOAD) != 0u),
        windowClaimed_(claim_composition_window(window_))
    {
        adapterLuid_.LowPart = info.adapterLuidLow;
        adapterLuid_.HighPart = info.adapterLuidHigh;
    }

    ~CompositionBridge()
    {
        for (const auto& binding : liquidRegionBindings_)
            release_liquid_region(binding);
        liquidRegionBindings_.clear();
        refresh_liquid_region_alias();
        if (liquidRegionTable_)
        {
            UnmapViewOfFile(liquidRegionTable_);
        }
        if (liquidRegionMapping_)
        {
            CloseHandle(liquidRegionMapping_);
        }
        if (uiCpuView_) UnmapViewOfFile(uiCpuView_);
        if (uiCpuMapping_) CloseHandle(uiCpuMapping_);
        release_composition_target();
        content_ = nullptr;
        regions_ = nullptr;
        root_ = nullptr;
        rawBackdropEffectFactory_ = nullptr;
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
        for (HANDLE handle : uiMirrorHandles_)
        {
            if (handle) CloseHandle(handle);
        }
        uiMirrorMutexes_ = {};
        uiMirrorTextures_ = {};
        sharedMutexes_.clear();
        sharedTextures_.clear();
        backBuffers_.clear();
        backBufferInitialised_.clear();
        backBufferContentRects_.clear();
        presentDamage_.reset();
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
            bufferCount_ < 2u || bufferCount_ > VIBRANCE_COMPOSITION_MAX_BUFFERS ||
            outputCount < bufferCount_)
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
        if (!validate_window_redirection_for_transparency())
        {
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
        if (index >= queries_.size())
        {
            return false;
        }
        if (queryPending_[index])
        {
            const HRESULT result = context_->GetData(queries_[index].get(),
                nullptr, 0u, D3D11_ASYNC_GETDATA_DONOTFLUSH);
            if (result != S_OK) return false;
            queryPending_[index] = false;
        }
        if (cpuUpload_) return true;
        if (index >= sharedMutexes_.size() || !sharedMutexes_[index]) return false;
        // A completed D3D query alone does not prove that a hybrid driver's
        // keyed-mutex hand-off is visible to Vulkan. Probe the exact key and
        // return it immediately so Vulkan's bounded acquire cannot wait
        // behind another display. The renderer reserves this buffer until its
        // next hand-off; no producer competes for it between these operations.
        if (sharedMutexes_[index]->AcquireSync(
                VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_KEY, 0u) != S_OK)
            return false;
        return SUCCEEDED(sharedMutexes_[index]->ReleaseSync(
            VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_KEY));
    }

    bool upload(
        std::uint32_t index,
        const VibranceCompositionUploadInfo* uploadInfo)
    {
        lastCompositionError.clear();
        if (!cpuUpload_ || index >= sharedTextures_.size() || !context_ ||
            !uploadInfo ||
            uploadInfo->structSize < VIBRANCE_COMPOSITION_UPLOAD_BASE_SIZE ||
            !uploadInfo->pixels || uploadInfo->width == 0u ||
            uploadInfo->height == 0u ||
            static_cast<std::uint64_t>(uploadInfo->rowPitch) <
                static_cast<std::uint64_t>(uploadInfo->width) * 4u)
        {
            lastCompositionError = "invalid CPU Composition upload parameters";
            return false;
        }

        const std::size_t destinationRowPitch =
            static_cast<std::size_t>(width_) * 4u;
        cpuUploadPixels_.resize(
            destinationRowPitch * static_cast<std::size_t>(height_));
        const auto* source = static_cast<const std::uint8_t*>(
            uploadInfo->pixels);

        // Vulkan's persistent portable target is RGBA8; only convert and upload
        // the accumulated changes for this rotating BGRA8 D3D source texture.
        // A legacy upload without the optional damage fields is still full.
        vibrance::win32::CompositionUploadRect sourceDamage {};
        if (uploadInfo->structSize >= sizeof(VibranceCompositionUploadInfo))
            sourceDamage = {uploadInfo->damageX, uploadInfo->damageY,
                uploadInfo->damageWidth, uploadInfo->damageHeight};
        const auto damage = cpuUploadDamage_.prepare(index,
            uploadInfo->width, uploadInfo->height, width_, height_, sourceDamage);
        if (damage.empty()) return true;
        cpuUploadConverter_.convert(cpuUploadPixels_.data(), destinationRowPitch,
            width_, height_, source, uploadInfo->rowPitch,
            uploadInfo->width, uploadInfo->height, damage);

        D3D11_BOX box {};
        box.left = damage.x;
        box.top = damage.y;
        box.right = damage.x + damage.width;
        box.bottom = damage.y + damage.height;
        box.back = 1u;

        context_->UpdateSubresource(
            sharedTextures_[index].get(),
            0u,
            &box,
            cpuUploadPixels_.data() +
                static_cast<std::size_t>(damage.y) * destinationRowPitch +
                static_cast<std::size_t>(damage.x) * 4u,
            static_cast<UINT>(destinationRowPitch),
            0u);
        cpuUploadDamage_.commit(index);
        copyDiagnostics_.uploaded(static_cast<std::uint64_t>(damage.width) * damage.height * 4u);
        return true;
    }

    std::uint32_t present(
        std::uint32_t index,
        const VibranceCompositionPresentInfo* presentInfo)
    {
        lastCompositionError.clear();
        if (index >= sharedTextures_.size() || !swapchain_ || !swapchain3_ ||
            !context_ || backBuffers_.empty() || !presentInfo ||
            presentInfo->structSize < sizeof(VibranceCompositionPresentInfo))
        {
            lastCompositionError = "invalid Composition present parameters";
            return VIBRANCE_COMPOSITION_PRESENT_FAILED;
        }

        const auto result = presentRetry_.deliver(index,
            [&] { return prepare_present(index, presentInfo); },
            [&] { return finish_present(); });
        if (presentStall_.needs_recovery(result,
                IsWindowVisible(window_) && !IsIconic(window_), GetTickCount64()))
        {
            lastCompositionError =
                "visible Windows Composition target did not accept a frame for 1000 ms";
            return VIBRANCE_COMPOSITION_PRESENT_FAILED;
        }
        return result;
    }

    std::uint32_t prepare_present(
        std::uint32_t index,
        const VibranceCompositionPresentInfo* presentInfo)
    {
        // Avoid recording a full-size D3D copy when DWM has not consumed the
        // previous frame. The newest completed Vulkan frame will be offered on
        // the next display opportunity instead.
        if (frameLatencyWaitableObject_ && !frameLatencyReady_)
        {
            const DWORD waitResult = WaitForSingleObject(
                frameLatencyWaitableObject_,
                0u);
            if (waitResult == WAIT_FAILED)
            {
                lastCompositionError =
                    "Composition frame-latency wait failed";
                return VIBRANCE_COMPOSITION_PRESENT_FAILED;
            }
            if (!latencyHint_.may_prepare(waitResult == WAIT_OBJECT_0, GetTickCount64()))
            {
                return VIBRANCE_COMPOSITION_PRESENT_DEFERRED;
            }
            // A deferred keyed-mutex acquire must retain this opportunity;
            // another wait could consume a second slot without presenting.
            frameLatencyReady_ = true;
        }
        const std::uint32_t backBufferIndex =
            swapchain3_->GetCurrentBackBufferIndex();
        if (backBufferIndex >= backBuffers_.size() ||
            backBufferIndex >= backBufferInitialised_.size() ||
            backBufferIndex >= backBufferContentRects_.size() ||
            backBufferIndex >= VIBRANCE_COMPOSITION_MAX_BUFFERS)
        {
            lastCompositionError =
                "Composition swapchain returned an invalid back-buffer index";
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

        RECT opticalDamageRect {};
        for (const auto& binding : liquidRegionBindings_)
        {
            if (!binding.view || !binding.view->active) continue;
            // The native shader samples beyond the UI's changed pixels.
            // Announce that entire source footprint to DWM so retained UI or
            // an earlier lens cannot be sampled as the next frame's backdrop.
            // Keep this invalidation separate from sharp UI copy damage. DWM
            // paints the optical result into its own target; app-owned source
            // textures retain the sharp pixels outside their actual UI changes.
            const auto& lens = *binding.view;
            const float reach = std::max({ lens.blurRadius * 5.0f,
                lens.refractionStrength + lens.dispersionStrength + 2.0f,
                4.0f });
            const auto margin = static_cast<std::int64_t>(std::ceil(reach));
            const auto horizontalMargin = margin +
                static_cast<std::int64_t>(lens.markerInsetX);
            RECT opticalDamage {
                static_cast<LONG>(std::max<std::int64_t>(0, lens.x - horizontalMargin)),
                static_cast<LONG>(std::max<std::int64_t>(0, lens.y - margin)),
                static_cast<LONG>(std::min<std::int64_t>(width_,
                    static_cast<std::int64_t>(lens.x) + lens.width + horizontalMargin)),
                static_cast<LONG>(std::min<std::int64_t>(height_,
                    static_cast<std::int64_t>(lens.y) + lens.height + margin))
            };
            opticalDamageRect = union_rect(opticalDamageRect, opticalDamage);
        }

        const auto trackedDamage = presentDamage_.prepare(backBufferIndex, width_, height_,
            {damageRect.left, damageRect.top, damageRect.right, damageRect.bottom},
            {opticalDamageRect.left, opticalDamageRect.top,
                opticalDamageRect.right, opticalDamageRect.bottom});
        RECT copyRect {trackedDamage.copy.left, trackedDamage.copy.top,
            trackedDamage.copy.right, trackedDamage.copy.bottom};
        const RECT presentRect {trackedDamage.present.left, trackedDamage.present.top,
            trackedDamage.present.right, trackedDamage.present.bottom};
        if (!cpuUpload_ &&
            (index >= sharedMutexes_.size() || !sharedMutexes_[index]))
        {
            lastCompositionError =
                "Composition buffer has no D3D11 keyed mutex";
            return VIBRANCE_COMPOSITION_PRESENT_FAILED;
        }
        if (!cpuUpload_)
        {
            const HRESULT acquireResult = sharedMutexes_[index]->AcquireSync(
                VIBRANCE_COMPOSITION_VULKAN_RELEASE_KEY,
                0u);
            if (acquireResult == WAIT_TIMEOUT)
            {
                return VIBRANCE_COMPOSITION_PRESENT_DEFERRED;
            }
            if (acquireResult != S_OK)
            {
                lastCompositionError =
                    "D3D11 keyed-mutex acquire failed (HRESULT " +
                    std::to_string(static_cast<std::int32_t>(acquireResult)) + ")";
                return VIBRANCE_COMPOSITION_PRESENT_FAILED;
            }
        }

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
        if (has_area(copyRect))
            copyDiagnostics_.copied(static_cast<std::uint64_t>(copyRect.right - copyRect.left) *
                (copyRect.bottom - copyRect.top) * 4u);
        backBufferContentRects_[backBufferIndex] = contentRect;
        context_->End(queries_[index].get());
        queryPending_[index] = true;
        if (!cpuUpload_)
        {
            const HRESULT releaseResult = sharedMutexes_[index]->ReleaseSync(
                VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_KEY);
            if (FAILED(releaseResult))
            {
                lastCompositionError =
                    "D3D11 keyed-mutex release failed (HRESULT " +
                    std::to_string(static_cast<std::int32_t>(releaseResult)) + ")";
                return VIBRANCE_COMPOSITION_PRESENT_FAILED;
            }
        }
        pendingPresentBackBuffer_ = backBufferIndex;
        pendingPresentDamage_ = union_rect(copyRect, presentRect);
        return VIBRANCE_COMPOSITION_PRESENTED;
    }

    bool set_regions(
        const VibranceCompositionRegion* regions,
        std::uint32_t count)
    {
        if (!regions_ || !compositor_)
        {
            return false;
        }
        std::vector<std::uint32_t> shaderSlots(count,
            vibrance::win32::liquidDwmRegionSlots);
        std::unordered_set<std::uint64_t> nativeIdentities;
        bool shaderCandidateSeen = false;
        const bool extensionDisabled = liquid_extension_disabled();
        for (std::uint32_t index = 0u; index < count; ++index)
        {
            if (!extensionDisabled && regions[index].material == 2u &&
                regions[index].liquidBackend == 2u &&
                regions[index].width > 0.0f && regions[index].height > 0.0f)
            {
                shaderCandidateSeen = true;
                if (regions[index].identity != 0u)
                    nativeIdentities.insert(regions[index].identity);
            }
        }
        const bool shaderReady = shaderCandidateSeen &&
            ensure_liquid_region_mapping() && liquid_extension_ready();
        // Validate the desired helper bundle once even if an older pinned
        // extension has already signalled the protocol's readiness event.
        // The asynchronous launch performs hashes and deployment off this path.
        if (shaderCandidateSeen) launch_liquid_extension_once();
        if (shaderCandidateSeen && !shaderReady)
        {
            lastCompositionError = liquidRegionTable_ ?
                "Liquid Glass DWM extension is starting or unavailable" :
                "Liquid Glass region table is unavailable";
        }
        // Bindings follow stable entities rather than the sorted input index.
        // Removed cards release their claim before replacement cards allocate.
        for (auto iterator = liquidRegionBindings_.begin();
             iterator != liquidRegionBindings_.end();)
        {
            if (!shaderReady || nativeIdentities.count(iterator->identity) == 0u)
            {
                release_liquid_region(*iterator);
                iterator = liquidRegionBindings_.erase(iterator);
            }
            else ++iterator;
        }
        bool hasShaderRegion = false;
        if (shaderReady)
        {
            std::unordered_set<std::uint64_t> publishedIdentities;
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                const auto& region = regions[index];
                if (region.material != 2u || region.liquidBackend != 2u ||
                    region.width <= 0.0f || region.height <= 0.0f) continue;
                if (region.identity == 0u ||
                    !publishedIdentities.insert(region.identity).second)
                {
                    lastCompositionError =
                        "Liquid Glass regions require unique non-zero identities";
                    continue;
                }
                auto* binding = find_liquid_region_identity(region.identity);
                if (!binding) binding = claim_liquid_region(region.identity);
                if (!binding)
                {
                    lastCompositionError = "Liquid Glass region table is full";
                    continue;
                }
                shaderSlots[index] = binding->slot;
                publish_liquid_region(*binding, &region);
                hasShaderRegion = true;
            }
        }
        refresh_liquid_region_alias();
        // Keep background pixels under artwork available to the optical
        // shader. An exactly opaque UI lets DWM cull those pixels before the
        // lens bends into them; this sub-pixel alpha difference prevents that.
        try
        {
            if (contentHasShaderRegion_ != hasShaderRegion)
            {
                content_.Opacity(hasShaderRegion ? 0.999f : 1.0f);
                contentHasShaderRegion_ = hasShaderRegion;
            }
            bool canUpdateGeometry = regionVisuals_.size() == count;
            bool exactlyUnchanged = canUpdateGeometry;
            if (canUpdateGeometry)
            {
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    const bool shaderOnly =
                        shaderSlots[index] < vibrance::win32::liquidDwmRegionSlots;
                    if (regionVisuals_[index].shaderOnly != shaderOnly ||
                        regionVisuals_[index].shaderSlot != shaderSlots[index] ||
                        regionVisuals_[index].descriptor.identity !=
                            regions[index].identity)
                    {
                        canUpdateGeometry = false;
                        exactlyUnchanged = false;
                        break;
                    }
                    exactlyUnchanged = exactlyUnchanged &&
                        std::memcmp(
                            &regionVisuals_[index].descriptor,
                            &regions[index],
                            sizeof(regions[index])) == 0;
                    if (!same_region_effect_graph(
                            regionVisuals_[index].descriptor,
                            regions[index], shaderOnly))
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
                // visual geometry. Retain the backdrop effect and glaze
                // graph while an island morphs; rebuilding every region
                // on every spring sample creates a large transient GPU/resource
                // backlog in DWM that can remain visible after hover ends.
                for (std::uint32_t index = 0u; index < count; ++index)
                {
                    if (std::memcmp(&regionVisuals_[index].descriptor,
                            &regions[index], sizeof(regions[index])) != 0)
                    {
                        update_region_geometry(regionVisuals_[index], regions[index]);
                    }
                }
                return commit_composition_changes();
            }

            regions_.Children().RemoveAll();
            // Scrolling changes which cards intersect the viewport. Retain the
            // other native graphs instead of rebuilding the whole window's
            // glass whenever a card crosses an edge.
            auto retainedVisuals = std::move(regionVisuals_);
            regionVisuals_.clear();
            regionVisuals_.reserve(count);
            if (windows_build_number() >= 22621u)
            {
                const DWM_SYSTEMBACKDROP_TYPE disabled = DWMSBT_NONE;
                DwmSetWindowAttribute(window_, DWMWA_SYSTEMBACKDROP_TYPE,
                    &disabled, sizeof(disabled));
            }
            for (std::uint32_t index = 0u; index < count; ++index)
            {
                const bool shaderOnly = shaderSlots[index] < vibrance::win32::liquidDwmRegionSlots;
                const auto retained = std::find_if(retainedVisuals.begin(), retainedVisuals.end(),
                    [&](const auto& state) {
                        return state.container && state.descriptor.identity == regions[index].identity &&
                            state.shaderOnly == shaderOnly && state.shaderSlot == shaderSlots[index] &&
                            same_region_effect_graph(state.descriptor, regions[index], shaderOnly);
                    });
                if (retained != retainedVisuals.end())
                {
                    if (std::memcmp(&retained->descriptor, &regions[index], sizeof(regions[index])) != 0)
                        update_region_geometry(*retained, regions[index]);
                    regions_.Children().InsertAtTop(retained->container);
                    regionVisuals_.push_back(std::move(*retained));
                    continue;
                }
                if (!add_region(regions[index],
                        shaderSlots[index] < vibrance::win32::liquidDwmRegionSlots,
                        shaderSlots[index]))
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
    std::uint32_t finish_present()
    {
        const bool hasDamage = pendingPresentDamage_.right >
                pendingPresentDamage_.left &&
            pendingPresentDamage_.bottom > pendingPresentDamage_.top;
        DXGI_PRESENT_PARAMETERS parameters {};
        parameters.DirtyRectsCount = hasDamage ? 1u : 0u;
        parameters.pDirtyRects = hasDamage ? &pendingPresentDamage_ : nullptr;
        const HRESULT result = swapchain_->Present1(0u,
            DXGI_PRESENT_DO_NOT_WAIT, &parameters);
        if (result == DXGI_ERROR_WAS_STILL_DRAWING)
        {
            // Submit the copy/query even though DXGI did not queue a present.
            // Keep the dirty rectangle and back buffer for the next retry.
            context_->Flush();
            return VIBRANCE_COMPOSITION_PRESENT_DEFERRED;
        }
        frameLatencyReady_ = false;
        if (SUCCEEDED(result))
        {
            presentDamage_.commit(pendingPresentBackBuffer_);
            copyDiagnostics_.presented(window_);
            return VIBRANCE_COMPOSITION_PRESENTED;
        }
        lastCompositionError =
            "IDXGISwapChain1::Present1 failed (HRESULT " +
            std::to_string(static_cast<std::int32_t>(result)) + ")";
        context_->Flush();
        return VIBRANCE_COMPOSITION_PRESENT_FAILED;
    }

    bool ensure_liquid_cpu_ui()
    {
        if (uiCpuView_) return true;
        const std::uint64_t pixels = static_cast<std::uint64_t>(width_) *
            height_ * 4u;
        if (!pixels || pixels > 64u * 1024u * 1024u) return false;
        D3D11_TEXTURE2D_DESC staging {};
        staging.Width = width_;
        staging.Height = height_;
        staging.MipLevels = 1;
        staging.ArraySize = 1;
        staging.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        staging.SampleDesc.Count = 1;
        staging.Usage = D3D11_USAGE_STAGING;
        staging.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        for (auto& texture : uiCpuStaging_)
        {
            if (FAILED(device_->CreateTexture2D(&staging, nullptr,
                    texture.put()))) return false;
        }
        vibrance::win32::PublicReadSecurity security;
        const std::wstring name = vibrance::win32::liquid_dwm_cpu_ui_name(
            GetCurrentProcessId(), reinterpret_cast<std::uint64_t>(window_),
            width_, height_);
        const std::uint64_t size =
            sizeof(vibrance::win32::LiquidDwmCpuUiHeader) + pixels;
        uiCpuMapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE,
            security.get(), PAGE_READWRITE,
            static_cast<DWORD>(size >> 32), static_cast<DWORD>(size),
            name.c_str());
        if (!uiCpuMapping_) return false;
        uiCpuView_ = static_cast<vibrance::win32::LiquidDwmCpuUiHeader*>(
            MapViewOfFile(uiCpuMapping_, FILE_MAP_WRITE, 0, 0,
                static_cast<SIZE_T>(size)));
        if (!uiCpuView_) return false;
        uiCpuView_->version = vibrance::win32::liquidDwmProtocolVersion;
        uiCpuView_->width = width_;
        uiCpuView_->height = height_;
        uiCpuView_->rowPitch = width_ * 4u;
        uiCpuView_->ownerPid = GetCurrentProcessId();
        uiCpuView_->hwnd = reinterpret_cast<std::uint64_t>(window_);
        return true;
    }

    bool ensure_liquid_ui_transport()
    {
        if (!ensure_liquid_cpu_ui()) return false;
        if (!uiMirrorAttempted_)
        {
            uiMirrorAttempted_ = true;
            (void)ensure_liquid_ui_mirrors();
        }
        return true;
    }

    void publish_cpu_ui(ID3D11Texture2D* source) noexcept
    {
        if (!source || !uiCpuView_) return;
        for (std::size_t index = 0; index < uiCpuStaging_.size(); ++index)
        {
            if (!uiCpuPending_[index]) continue;
            D3D11_MAPPED_SUBRESOURCE mapped {};
            if (context_->Map(uiCpuStaging_[index].get(), 0,
                    D3D11_MAP_READ, D3D11_MAP_FLAG_DO_NOT_WAIT,
                    &mapped) != S_OK) continue;
            if (uiCpuSourceFrame_[index] > uiCpuLastPublishedSource_)
            {
                auto* sequence = reinterpret_cast<volatile LONG*>(
                    &uiCpuView_->sequence);
                InterlockedIncrement(sequence);
                auto* pixels = reinterpret_cast<std::uint8_t*>(
                    uiCpuView_ + 1);
                const auto* sourceBytes = static_cast<const std::uint8_t*>(
                    mapped.pData);
                for (std::uint32_t row = 0; row < height_; ++row)
                {
                    std::memcpy(pixels + static_cast<std::size_t>(row) *
                            width_ * 4u,
                        sourceBytes + static_cast<std::size_t>(row) *
                            mapped.RowPitch,
                        static_cast<std::size_t>(width_) * 4u);
                }
                uiCpuView_->frame = ++uiCpuFrame_;
                MemoryBarrier();
                InterlockedIncrement(sequence);
                uiCpuLastPublishedSource_ = uiCpuSourceFrame_[index];
            }
            context_->Unmap(uiCpuStaging_[index].get(), 0);
            uiCpuPending_[index] = false;
        }
        const ULONGLONG now = GetTickCount64();
        if (now - uiCpuLastScheduleTick_ < 33u) return;
        for (std::size_t index = 0; index < uiCpuStaging_.size(); ++index)
        {
            if (uiCpuPending_[index]) continue;
            context_->CopyResource(uiCpuStaging_[index].get(), source);
            uiCpuPending_[index] = true;
            uiCpuSourceFrame_[index] = ++uiCpuScheduleSerial_;
            uiCpuLastScheduleTick_ = now;
            return;
        }
    }

    bool ensure_liquid_ui_mirrors()
    {
        if (uiMirrorHandles_[0] && uiMirrorHandles_[1]) return true;
        vibrance::win32::PublicReadSecurity security;
        for (std::uint32_t index = 0;
             index < vibrance::win32::liquidDwmUiBuffers; ++index)
        {
            if (uiMirrorHandles_[index]) continue;
            D3D11_TEXTURE2D_DESC description {};
            description.Width = width_;
            description.Height = height_;
            description.MipLevels = 1;
            description.ArraySize = 1;
            description.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
            description.SampleDesc.Count = 1;
            description.Usage = D3D11_USAGE_DEFAULT;
            description.BindFlags = D3D11_BIND_SHADER_RESOURCE |
                D3D11_BIND_RENDER_TARGET;
            description.MiscFlags = kSharedTextureMiscFlags;
            if (FAILED(device_->CreateTexture2D(&description, nullptr,
                    uiMirrorTextures_[index].put())) ||
                FAILED(uiMirrorTextures_[index]->QueryInterface(
                    uiMirrorMutexes_[index].put())))
            {
                return false;
            }
            winrt::com_ptr<IDXGIResource1> resource;
            const std::wstring name = vibrance::win32::liquid_dwm_ui_name(
                GetCurrentProcessId(),
                reinterpret_cast<std::uint64_t>(window_), index);
            if (FAILED(uiMirrorTextures_[index]->QueryInterface(resource.put())) ||
                FAILED(resource->CreateSharedHandle(security.get(),
                    DXGI_SHARED_RESOURCE_READ | DXGI_SHARED_RESOURCE_WRITE,
                    name.c_str(), &uiMirrorHandles_[index])))
            {
                return false;
            }
        }
        return true;
    }

    void mirror_ui_frame(ID3D11Texture2D* source) noexcept
    {
        if (!source || !liquidRegionView_->active) return;
        for (std::uint32_t offset = 0;
             offset < vibrance::win32::liquidDwmUiBuffers; ++offset)
        {
            const std::uint32_t index =
                (uiFrame_ + offset) % vibrance::win32::liquidDwmUiBuffers;
            if (!uiMirrorMutexes_[index] ||
                uiMirrorMutexes_[index]->AcquireSync(0, 0) != S_OK)
            {
                continue;
            }
            context_->CopyResource(uiMirrorTextures_[index].get(), source);
            if (FAILED(uiMirrorMutexes_[index]->ReleaseSync(1)))
            {
                return;
            }
            std::lock_guard<std::mutex> lock(liquidRegionWriteMutex_);
            auto* sequence = reinterpret_cast<volatile LONG*>(
                &liquidRegionView_->sequence);
            InterlockedIncrement(sequence);
            liquidRegionView_->uiBufferIndex = index;
            liquidRegionView_->uiFrame = ++uiFrame_;
            MemoryBarrier();
            InterlockedIncrement(sequence);
            return;
        }
    }

    bool liquid_extension_ready() const noexcept
    {
        const HANDLE ready = OpenEventW(SYNCHRONIZE, FALSE,
            vibrance::win32::liquidDwmReadyName);
        if (!ready)
        {
            return false;
        }
        const bool signalled = WaitForSingleObject(ready, 0) == WAIT_OBJECT_0;
        CloseHandle(ready);
        return signalled;
    }

    bool ensure_liquid_region_mapping()
    {
        if (liquidRegionTable_)
        {
            return true;
        }
        vibrance::win32::PublicReadSecurity security;
        liquidRegionMapping_ = CreateFileMappingW(INVALID_HANDLE_VALUE,
            security.get(), PAGE_READWRITE, 0,
            sizeof(vibrance::win32::LiquidDwmRegionTable),
            vibrance::win32::liquidDwmRegionName);
        if (!liquidRegionMapping_)
        {
            return false;
        }
        liquidRegionTable_ = static_cast<vibrance::win32::LiquidDwmRegionTable*>(
            MapViewOfFile(liquidRegionMapping_, FILE_MAP_WRITE, 0, 0,
                sizeof(vibrance::win32::LiquidDwmRegionTable)));
        if (!liquidRegionTable_)
        {
            CloseHandle(liquidRegionMapping_);
            liquidRegionMapping_ = nullptr;
            return false;
        }
        return true;
    }

    LiquidRegionBinding* find_liquid_region_identity(std::uint64_t identity) noexcept
    {
        const auto found = std::find_if(liquidRegionBindings_.begin(),
            liquidRegionBindings_.end(), [identity](const auto& binding) {
                return binding.identity == identity;
            });
        return found == liquidRegionBindings_.end() ? nullptr : &*found;
    }

    LiquidRegionBinding* find_liquid_region_slot(std::uint32_t slot) noexcept
    {
        const auto found = std::find_if(liquidRegionBindings_.begin(),
            liquidRegionBindings_.end(), [slot](const auto& binding) {
                return binding.slot == slot;
            });
        return found == liquidRegionBindings_.end() ? nullptr : &*found;
    }

    void refresh_liquid_region_alias() noexcept
    {
        // Reserved UI transport helpers still refer to this first-view alias;
        // native rendering never uses those helpers or their UI copies.
        liquidRegionView_ = liquidRegionBindings_.empty() ? nullptr :
            liquidRegionBindings_.front().view;
    }

    LiquidRegionBinding* claim_liquid_region(std::uint64_t identity)
    {
        if (!identity || !ensure_liquid_region_mapping()) return nullptr;
        const auto hwnd = static_cast<LONG64>(
            reinterpret_cast<std::uintptr_t>(window_));
        for (std::uint32_t index = 0;
             index < vibrance::win32::liquidDwmRegionSlots; ++index)
        {
            if (find_liquid_region_slot(index)) continue;
            auto& slot = liquidRegionTable_->regions[index];
            LONG64 previous = InterlockedCompareExchange64(
                reinterpret_cast<volatile LONG64*>(&slot.hwnd), hwnd, 0);
            if (previous != 0 && previous != hwnd)
            {
                DWORD windowOwner = 0;
                if (IsWindow(reinterpret_cast<HWND>(previous)))
                    GetWindowThreadProcessId(
                        reinterpret_cast<HWND>(previous), &windowOwner);
                if (windowOwner == 0 ||
                    (slot.ownerPid != 0 && slot.ownerPid != windowOwner))
                {
                    if (InterlockedCompareExchange64(
                            reinterpret_cast<volatile LONG64*>(&slot.hwnd),
                            hwnd, previous) == previous)
                        previous = 0;
                }
            }
            if (previous == 0 || previous == hwnd)
            {
                // A new allocation gets a distinct lifetime token even if a
                // removed entity or the same HWND later reuses this slot.
                static volatile LONG64 allocationSerial =
                    static_cast<LONG64>(GetTickCount64());
                std::uint64_t regionId = static_cast<std::uint64_t>(
                    InterlockedIncrement64(&allocationSerial));
                if (!regionId) regionId = static_cast<std::uint64_t>(
                    InterlockedIncrement64(&allocationSerial));
                try
                {
                    liquidRegionBindings_.push_back({ identity, regionId, &slot, index });
                }
                catch (...)
                {
                    InterlockedCompareExchange64(
                        reinterpret_cast<volatile LONG64*>(&slot.hwnd), 0, hwnd);
                    return nullptr;
                }
                auto* binding = &liquidRegionBindings_.back();
                publish_liquid_region(*binding, nullptr);
                return binding;
            }
        }
        return nullptr;
    }

    void release_liquid_region(const LiquidRegionBinding& binding) noexcept
    {
        if (!binding.view ||
            binding.view->hwnd != reinterpret_cast<std::uint64_t>(window_) ||
            binding.view->ownerPid != GetCurrentProcessId() ||
            binding.view->regionId != binding.regionId) return;
        publish_liquid_region(binding, nullptr);
        InterlockedCompareExchange64(reinterpret_cast<volatile LONG64*>(
            &binding.view->hwnd), 0,
            static_cast<LONG64>(reinterpret_cast<std::uintptr_t>(window_)));
    }

    static float liquid_marker_padding(
        const VibranceCompositionRegion& region) noexcept
    {
        if (region.contourCount != 0u)
        {
            // Union regions already include their notch flares in the supplied
            // bounds. Add padding only if an exposed contour exceeds them.
            float padding = 0.0f;
            for (std::uint32_t index = 0u;
                index < std::min(region.contourCount, 3u); ++index)
            {
                const auto& contour = region.contours[index];
                VibranceCompositionRegion shape {};
                shape.shape = contour.shape;
                shape.height = contour.height;
                shape.notchAmount = contour.notchAmount;
                shape.notchDepth = contour.notchDepth;
                const float flare = liquid_marker_padding(shape);
                padding = std::max({ padding, flare - contour.x,
                    contour.x + contour.width + flare - region.width });
            }
            // Bounding-box subtraction may leave a fractional-ulp excess.
            return padding > 0.01f ? std::ceil(padding) : 0.0f;
        }
        if (region.shape != 4u) return 0.0f;
        // Match notch_flare_size in shape_2d.comp, including the late morph
        // threshold. The island body remains centred inside this wider marker.
        constexpr float morphThreshold = 0.85f;
        const float height = std::max(region.height, 1.0f);
        const float progress = std::clamp(
            (std::clamp(region.notchAmount, 0.0f, 1.0f) - morphThreshold) /
                (1.0f - morphThreshold),
            0.0f, 1.0f);
        const float depth = region.notchDepth > 0.5f ? region.notchDepth :
            std::clamp(height * 0.62f, 0.0f, 92.0f);
        const float flareTarget = std::clamp(depth * 0.18f,
            0.0f, height * 0.5f);
        const float sizeRatio = std::clamp(height * 0.5f /
            std::max(flareTarget, 0.001f), 0.0f, 1.0f);
        const float flareT = sizeRatio * progress;
        return std::ceil(flareTarget * flareT * flareT);
    }

    static float liquid_refraction(const VibranceCompositionRegion& region) noexcept
    {
        const float size = std::clamp(region.width / 500.0f, 0.35f, 2.0f);
        return std::min(std::clamp(region.liquidRefraction, 0.0f, 1.0f) * 56.0f * size,
            std::min(region.width, region.height) * 0.38f);
    }

    static float liquid_blur(const VibranceCompositionRegion& region) noexcept
    {
        return std::clamp(region.liquidBlur, 0.0f, 1.0f) * 30.0f;
    }

    static float liquid_dispersion(const VibranceCompositionRegion& region) noexcept
    {
        return std::clamp(region.liquidDispersion, 0.0f, 1.0f) * 3.0f;
    }

    static float liquid_source_margin(const VibranceCompositionRegion& region) noexcept
    {
        return std::ceil(std::max({ liquid_blur(region) * 5.0f,
            region.liquidRim > 0.0f ? std::min(std::min(region.width, region.height) * 1.6f, 120.0f) + 2.0f : 0.0f,
            liquid_refraction(region) + liquid_dispersion(region) + 2.0f, 4.0f }));
    }

    void publish_liquid_region(const LiquidRegionBinding& binding,
        const VibranceCompositionRegion* region) noexcept
    {
        if (!binding.view)
        {
            return;
        }
        vibrance::win32::LiquidDwmRegion next {};
        next.hwnd = reinterpret_cast<std::uint64_t>(window_);
        next.ownerPid = GetCurrentProcessId();
        next.regionId = binding.regionId;
        if (region && region->width > 0.0f && region->height > 0.0f)
        {
            const float top = std::clamp(region->verticalStart,
                0.0f, 0.98f) * region->height;
            next.x = static_cast<std::int32_t>(std::lround(region->x));
            next.y = static_cast<std::int32_t>(std::lround(region->y + top));
            next.width = static_cast<std::uint32_t>(std::lround(region->width));
            next.height = static_cast<std::uint32_t>(std::lround(
                region->height - top));
            next.cornerRadius = region->cornerRadius;
            next.shape = region->shape;
            next.squircleAmount = std::clamp(region->squircleAmount, 0.0f, 1.0f);
            next.notchAmount = std::clamp(region->notchAmount, 0.0f, 1.0f);
            next.notchDepth = std::max(region->notchDepth, 0.0f);
            const float maximumRadius = std::min(region->width,
                region->height) * 0.5f;
            const auto radius = [maximumRadius](float requested, float fallback) {
                return std::clamp(requested > 0.0f ? requested : fallback,
                    0.0f, maximumRadius);
            };
            next.topLeftRadius = radius(region->topLeftRadius, region->cornerRadius);
            next.topRightRadius = radius(region->topRightRadius, region->cornerRadius);
            next.bottomRightRadius = radius(region->bottomRightRadius, region->cornerRadius);
            next.bottomLeftRadius = radius(region->bottomLeftRadius, region->cornerRadius);
            next.markerInsetX = liquid_marker_padding(*region);
            next.edgeThickness = std::clamp(std::min(region->width, region->height) * 0.16f,
                4.0f, 44.0f);
            next.refractionStrength = liquid_refraction(*region);
            next.dispersionStrength = liquid_dispersion(*region);
            next.blurRadius = liquid_blur(*region);
            next.rimStrength = std::clamp(region->liquidRim, 0.0f, 1.0f) * 0.55f;
            next.rimStatic = (region->flags & 2u) != 0u ? 1.0f : 0.0f;
            next.opacity = std::clamp(region->opacity, 0.0f, 1.0f);
            next.tintRed = std::clamp(region->liquidTintRed, 0.0f, 1.0f);
            next.tintGreen = std::clamp(region->liquidTintGreen, 0.0f, 1.0f);
            next.tintBlue = std::clamp(region->liquidTintBlue, 0.0f, 1.0f);
            next.tintAlpha = std::clamp(region->liquidTintAlpha, 0.0f, 1.0f);
            next.cornerExponent = std::clamp(region->squirclePower,
                2.0f, 5.0f);
            static_assert(sizeof(VibranceCompositionContour) ==
                sizeof(vibrance::win32::LiquidDwmContour));
            next.contourCount = std::min(region->contourCount, 3u);
            next.hasExclusion = region->hasExclusion != 0u ? 1u : 0u;
            next.paintCapGradient = region->paintCapGradient;
            std::copy_n(region->capGradient, 4u, next.capGradient);
            std::copy_n(region->viewportRect, 4u, next.viewportRect);
            std::copy_n(region->viewportFade, 4u, next.viewportFade);
            next.viewportRect[0] -= static_cast<float>(next.x);
            next.viewportRect[1] -= static_cast<float>(next.y);
            std::memcpy(next.contours, region->contours, sizeof(next.contours));
            std::memcpy(&next.exclusion, &region->exclusion, sizeof(next.exclusion));
            // The native marker starts at materialTop. Keep all contour sizes,
            // radii and flare depths in the caller's framebuffer units while
            // translating their body-relative origin to that marker.
            for (std::uint32_t index = 0u; index < next.contourCount; ++index)
                next.contours[index].y -= top;
            if (next.hasExclusion != 0u) next.exclusion.y -= top;
            POINT origin {};
            MONITORINFO monitor { sizeof(monitor) };
            if (ClientToScreen(window_, &origin) &&
                GetMonitorInfoW(MonitorFromWindow(window_,
                    MONITOR_DEFAULTTONEAREST), &monitor))
            {
                next.screenX = origin.x;
                next.screenY = origin.y;
                next.monitorLeft = monitor.rcMonitor.left;
                next.monitorTop = monitor.rcMonitor.top;
                next.monitorWidth = static_cast<std::uint32_t>(
                    monitor.rcMonitor.right - monitor.rcMonitor.left);
                next.monitorHeight = static_cast<std::uint32_t>(
                    monitor.rcMonitor.bottom - monitor.rcMonitor.top);
                next.clientWidth = width_;
                next.clientHeight = height_;
                next.active = 1;
            }
        }
        std::lock_guard<std::mutex> lock(liquidRegionWriteMutex_);
        if (next.active)
        {
            next.uiBufferIndex = binding.view->uiBufferIndex;
            next.uiFrame = binding.view->uiFrame;
        }
        // Other regions can change while this lens's published data stays the
        // same. Leave its seqlock stable so DWM retains its cached snapshot.
        const auto* publishedPayload = reinterpret_cast<const char*>(
            binding.view) + sizeof(binding.view->sequence);
        const auto* nextPayload = reinterpret_cast<const char*>(&next) +
            sizeof(next.sequence);
        const std::size_t payloadSize = sizeof(next) - sizeof(next.sequence);
        if (std::memcmp(publishedPayload, nextPayload, payloadSize) == 0)
        {
            return;
        }
        auto* sequence = reinterpret_cast<volatile LONG*>(
            &binding.view->sequence);
        InterlockedIncrement(sequence);
        std::memcpy(reinterpret_cast<char*>(binding.view) +
            sizeof(binding.view->sequence), nextPayload, payloadSize);
        MemoryBarrier();
        InterlockedIncrement(sequence);
    }

    bool validate_window_redirection_for_transparency()
    {
        if (!transparentFramebuffer_)
        {
            return true;
        }

        SetLastError(ERROR_SUCCESS);
        const LONG_PTR extendedStyle = GetWindowLongPtrW(window_, GWL_EXSTYLE);
        const DWORD styleReadError = GetLastError();
        if (extendedStyle == 0 && styleReadError != ERROR_SUCCESS)
        {
            lastCompositionError =
                "could not read the HWND extended style before enabling transparent Composition";
            return false;
        }

        if ((extendedStyle & WS_EX_NOREDIRECTIONBITMAP) != 0)
        {
            return true;
        }
        // DWM decides whether to allocate the opaque redirection bitmap while
        // CreateWindowEx is running. Retrofitting this style after GLFW has
        // created the HWND is unreliable and leaves a black client surface on
        // some Windows/ARM64 drivers.
        lastCompositionError =
            "transparent Composition HWND was not created with WS_EX_NOREDIRECTIONBITMAP";
        return false;
    }

    bool commit_composition_changes()
    {
        // The private Win32 interop compositor uses IDComposition and does not
        // auto-commit Windows.UI.Composition property changes. Without this
        // transaction, an island can collapse in the Vulkan surface while its
        // old expanded backdrop geometry remains cached by DWM.
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
        VibranceCompositionRegion right,
        bool shaderOnly) noexcept
    {
        return vibrance::win32::same_composition_region_effect_graph(left, right, shaderOnly);
    }

    bool initialise_liquid_pipeline()
    {
        if (liquidShaderProbeDone_)
        {
            return liquidShaderProbeResult_;
        }
        liquidShaderProbeDone_ = true;
        wchar_t executablePath[32768] {};
        const DWORD pathLength = GetModuleFileNameW(
            nullptr, executablePath, ARRAYSIZE(executablePath));
        if (pathLength == 0u || pathLength >= ARRAYSIZE(executablePath))
        {
            return false;
        }
        std::wstring directory(executablePath, pathLength);
        const auto separator = directory.find_last_of(L"\\/");
        if (separator == std::wstring::npos)
        {
            return false;
        }
        directory.resize(separator + 1u);

        liquidShaderProbeResult_ = liquidRenderer_.initialise(device_.get(), directory);
        return liquidShaderProbeResult_;
    }

    static float liquid_sample_coordinate(float coordinate, float extent) noexcept
    {
        const float half = std::max(extent * 0.5f, 1.0f);
        const float u = std::clamp((coordinate - half) / half, -1.0f, 1.0f);
        const float edge = std::pow(std::abs(u), 6.0f);
        // Magnify the whole surface gently, then concentrate the additional
        // bend at the perimeter. Both terms remain monotone at any size.
        const float depth = std::min(extent * 0.125f, 34.0f);
        return coordinate - depth * u * (0.55f + 0.45f * edge);
    }

    static void update_liquid_tiles(RegionVisualState& state,
                                    float width,
                                    float height)
    {
        constexpr std::size_t columns = 20u;
        constexpr std::size_t rows = 8u;
        if (state.liquidTiles.size() != columns * rows)
        {
            return;
        }
        for (std::size_t row = 0u; row < rows; ++row)
        {
            const float top = height * static_cast<float>(row) / static_cast<float>(rows);
            const float bottom = height * static_cast<float>(row + 1u) / static_cast<float>(rows);
            const float sourceTop = liquid_sample_coordinate(top, height);
            const float sourceBottom = liquid_sample_coordinate(bottom, height);
            for (std::size_t column = 0u; column < columns; ++column)
            {
                const float left = width * static_cast<float>(column) / static_cast<float>(columns);
                const float right = width * static_cast<float>(column + 1u) / static_cast<float>(columns);
                const float sourceLeft = liquid_sample_coordinate(left, width);
                const float sourceRight = liquid_sample_coordinate(right, width);
                const float xScale = (right - left) /
                    std::max(sourceRight - sourceLeft, 0.01f);
                const float yScale = (bottom - top) /
                    std::max(sourceBottom - sourceTop, 0.01f);
                auto& tile = state.liquidTiles[row * columns + column];
                tile.visual.Offset({ left, top, 0.0f });
                tile.visual.Size({ right - left, bottom - top });
                tile.brush.Scale({ xScale, yScale });
                tile.brush.Offset({ -sourceLeft * xScale, -sourceTop * yScale });
            }
        }
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
        const bool opacityChanged = previous.opacity != region.opacity;
        const bool materialGeometryChanged =
            sizeChanged ||
            previous.verticalStart != region.verticalStart;
        const bool maskGeometryChanged =
            previous.contourCount != region.contourCount ||
            previous.hasExclusion != region.hasExclusion ||
            std::memcmp(previous.contours, region.contours,
                sizeof(region.contours)) != 0 ||
            std::memcmp(&previous.exclusion, &region.exclusion,
                sizeof(region.exclusion)) != 0;
        const bool rawContourGeometryChanged =
            previous.contourCount != region.contourCount ||
            std::memcmp(previous.contours, region.contours, sizeof(region.contours)) != 0;
        const bool markerGeometryChanged = materialGeometryChanged ||
            (state.shaderOnly &&
                (maskGeometryChanged || previous.shape != region.shape ||
                    previous.notchAmount != region.notchAmount ||
                    previous.notchDepth != region.notchDepth));
        const bool sourceFootprintChanged = markerGeometryChanged ||
            previous.liquidRim != region.liquidRim ||
            previous.liquidRefraction != region.liquidRefraction ||
            previous.liquidBlur != region.liquidBlur ||
            previous.liquidDispersion != region.liquidDispersion;
        const bool rawBackdropVisibilityChanged =
            vibrance::win32::composition_raw_backdrop_required(previous) !=
            vibrance::win32::composition_raw_backdrop_required(region);
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
            previous.notchDepth != region.notchDepth || previous.flags != region.flags;

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
            if (opacityChanged)
            {
                state.container.Opacity(std::clamp(region.opacity, 0.0f, 1.0f));
            }
            if (clipChanged && !state.shaderOnly)
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
        if (materialGeometryChanged && state.rawBackdropContainer)
            state.rawBackdropContainer.Size({ region.width, materialHeight });
        const bool viewportChanged = positionChanged || materialGeometryChanged ||
            std::memcmp(previous.viewportRect, region.viewportRect, sizeof(region.viewportRect)) != 0 ||
            std::memcmp(previous.viewportFade, region.viewportFade, sizeof(region.viewportFade)) != 0;
        if (viewportChanged) sync_viewport_mask(state, region);
        if (markerGeometryChanged && state.materialVisual)
        {
            const float padding = state.shaderOnly ?
                liquid_marker_padding(region) : 0.0f;
            state.materialVisual.Offset({ -padding, 0.0f, 0.0f });
            state.materialVisual.Size({ region.width + padding * 2.0f,
                materialHeight });
        }
        if ((sourceFootprintChanged || clipChanged || rawBackdropVisibilityChanged) && state.rawBackdropVisual)
        {
            // The source footprint includes the shader's bounded sampling
            // margin, while the marker retains the exact optical bounds.
            const float margin = liquid_source_margin(region);
            const float horizontalMargin = margin + liquid_marker_padding(region);
            state.rawBackdropVisual.Offset({ -horizontalMargin, -margin, 0.0f });
            state.rawBackdropVisual.Size({ region.width + horizontalMargin * 2.0f,
                materialHeight + margin * 2.0f });
            sync_raw_backdrop_clip(state, region,
                materialGeometryChanged || rawContourGeometryChanged || clipChanged);
        }
        if (materialGeometryChanged && state.liquidSourceVisual)
        {
            state.liquidSourceVisual.Size({ region.width, materialHeight });
        }
        if (materialGeometryChanged && state.liquidSurface)
        {
            state.liquidSurface.SourceSize({ region.width, materialHeight });
            update_liquid_tiles(state, region.width, materialHeight);
        }
        if (materialGeometryChanged && state.tintVisual)
        {
            state.tintVisual.Size({ region.width, materialHeight });
        }
        sync_liquid_rim_clock(state, region);
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
            // CPU uploads do not share allocations with Vulkan, so an exact
            // adapter match is unnecessary. Prefer DXGI's first hardware
            // adapter when a native Vulkan ICD did not publish a usable LUID.
            if (!cpuUpload_ ||
                factory->EnumAdapters1(0u, adapter.put()) ==
                    DXGI_ERROR_NOT_FOUND ||
                !adapter)
            {
                lastCompositionError = "DXGI adapter matching the Vulkan device LUID was not found";
                return false;
            }
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
        presentDamage_.reset();
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
        sharedMutexes_.resize(bufferCount_);
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
            // IDXGIResource1::CreateSharedHandle requires the documented
            // NT-handle + keyed-mutex pair. SHARED_NTHANDLE by itself is
            // rejected with E_INVALIDARG by conforming D3D11 drivers, while
            // the legacy SHARED flag belongs to IDXGIResource::GetSharedHandle
            // and must not be mixed into this path.
            textureDescription.MiscFlags = cpuUpload_ ?
                0u : kSharedTextureMiscFlags;
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

            if (!cpuUpload_ &&
                FAILED(sharedTextures_[index]->QueryInterface(
                    sharedMutexes_[index].put())))
            {
                lastCompositionError =
                    "D3D11 shared texture does not expose IDXGIKeyedMutex";
                return false;
            }

            // D3D11 resources created without initial data contain undefined
            // pixels. Initialise the complete allocation on its owning API.
            // The shared path first claims key 0; CPU-upload textures require
            // no cross-API ownership transition.
            HRESULT initialiseAcquire = S_OK;
            if (!cpuUpload_)
            {
                initialiseAcquire = sharedMutexes_[index]->AcquireSync(
                    VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_KEY,
                    VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_TIMEOUT_MS);
                if (initialiseAcquire != S_OK)
                {
                    lastCompositionError =
                        "D3D11 shared texture initial acquire failed (HRESULT " +
                        std::to_string(
                            static_cast<std::int32_t>(initialiseAcquire)) + ")";
                    return false;
                }
            }
            winrt::com_ptr<ID3D11RenderTargetView> initialTarget;
            const HRESULT targetResult = device_->CreateRenderTargetView(
                sharedTextures_[index].get(),
                nullptr,
                initialTarget.put());
            if (SUCCEEDED(targetResult))
            {
                constexpr float transparent[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
                context_->ClearRenderTargetView(initialTarget.get(), transparent);
                context_->Flush();
            }
            const HRESULT initialiseRelease = cpuUpload_ ? S_OK :
                sharedMutexes_[index]->ReleaseSync(
                    VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_KEY);
            if (FAILED(targetResult) || FAILED(initialiseRelease))
            {
                lastCompositionError = FAILED(targetResult) ?
                    "D3D11 shared texture transparent initialisation failed" :
                    "D3D11 shared texture initial release failed";
                return false;
            }

            if (!cpuUpload_)
            {
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
        hostBackdropEnabled_ = vibrance::win32::system_backdrop_uses_host_brush(windows_build_number()) &&
            SUCCEEDED(DwmSetWindowAttribute(
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

    composition::CompositionEffectBrush make_effect_brush(
        const VibranceCompositionRegion& region)
    {
        auto sourceParameter =
            composition::CompositionEffectSourceParameter(L"backdrop");
        // Liquid glass needs a legible backdrop for the lens to refract.
        // The system material retains its broader frosted blur.
        const float blurRadius = region.material == 2u ?
            std::min(std::max(region.blurRadius, 0.0f), 3.0f) :
            std::max(region.blurRadius, 0.0f);
        auto blur = winrt::make<GaussianBlurEffect>(
            sourceParameter, blurRadius);
        effects::IGraphicsEffect graph = blur;
        if (std::abs(region.saturation - 1.0f) > 0.001f)
        {
            graph = winrt::make<SaturationEffect>(
                graph.as<effects::IGraphicsEffectSource>(),
                region.saturation);
        }
        auto factory = compositor_.CreateEffectFactory(graph);
        auto brush = factory.CreateBrush();
        const bool hostBrush = region.material != 2u && hostBackdropEnabled_ &&
            vibrance::win32::system_backdrop_uses_host_brush(windows_build_number());
        brush.SetSourceParameter(L"backdrop", hostBrush ?
            compositor_.CreateHostBackdropBrush() : compositor_.CreateBackdropBrush());
        return brush;
    }

    void sync_raw_backdrop_clip(RegionVisualState& state,
        const VibranceCompositionRegion& region, bool geometryChanged = true)
    {
        if (!state.rawBackdropVisual) return;
        if (geometryChanged) state.rawBackdropClip = nullptr;
        if ((region.flags & 1u) == 0u ||
            !vibrance::win32::composition_raw_backdrop_required(region))
        {
            state.rawBackdropVisual.Clip(nullptr);
            return;
        }
        if (!state.rawBackdropClip)
        {
            auto local = vibrance::win32::composition_raw_backdrop_source_region(region);
            const float top = std::clamp(region.verticalStart, 0.0f, 0.98f) * region.height;
            local.height = std::max(region.height - top, 1.0f);
            for (unsigned index = 0; index < std::min(local.contourCount, 3u); ++index)
                local.contours[index].y -= top;
            const bool complex = local.contourCount != 0u ||
                local.shape == 5u || (local.shape == 4u && local.notchAmount > 0.85f);
            if (complex)
            {
                auto source = winrt::make<RawBackdropGeometrySource>(local, liquid_marker_padding(region));
                state.rawBackdropClip = compositor_.CreateGeometricClip(
                    compositor_.CreatePathGeometry(composition::CompositionPath(source)));
            }
            else if (local.shape == 0u)
            {
                auto rectangle = compositor_.CreateRectangleGeometry();
                rectangle.Size({ local.width, local.height });
                state.rawBackdropClip = compositor_.CreateGeometricClip(rectangle);
            }
            else state.rawBackdropClip = make_clip(local);
        }
        auto clip = state.rawBackdropClip;
        if (clip)
        {
            const float margin = liquid_source_margin(region);
            const float horizontal = margin + liquid_marker_padding(region);
            clip.TransformMatrix({ 1.0f, 0.0f, 0.0f, 1.0f, horizontal, margin });
        }
        state.rawBackdropVisual.Clip(clip);
    }

    void sync_viewport_mask(RegionVisualState& state, const VibranceCompositionRegion& region)
    {
        const bool enabled = region.viewportFade[3] != 0.0f;
        const float materialTop = std::clamp(region.verticalStart, 0.0f, 0.98f) * region.height;
        const float originY = region.y + materialTop;
        // The optical shader clips and fades its own output. Clipping the
        // marker's ancestor as well lets DWM crop/cache the sentinel into an
        // intermediate surface as soon as an inbox overflows. Keep the marker
        // on the original target; only its raw backdrop dependency needs a
        // native viewport clip. Fallback materials still use their own mask.
        auto clipped = state.shaderOnly ? state.rawBackdropContainer : state.materialContainer;
        if (clipped && (state.shaderOnly || state.materialInset))
        {
            if (enabled)
            {
                clipped.Clip(compositor_.CreateInsetClip(
                    std::max(region.viewportRect[0] - region.x, 0.0f),
                    std::max(region.viewportRect[1] - originY, 0.0f),
                    std::max(region.x + region.width - region.viewportRect[0] - region.viewportRect[2], 0.0f),
                    std::max(originY + region.height - materialTop -
                        region.viewportRect[1] - region.viewportRect[3], 0.0f)));
            }
            else clipped.Clip(nullptr);
        }
        if (!state.viewportMaskBrush) return;
        auto stops = state.viewportMaskBrush.ColorStops();
        stops.Clear();
        const float height = std::max(region.height - materialTop, 1.0f);
        const auto add = [&](float absoluteY, float alpha) {
            stops.Append(compositor_.CreateColorGradientStop(
                std::clamp((absoluteY - originY) / height, 0.0f, 1.0f),
                color(1.0f, 1.0f, 1.0f, alpha)));
        };
        const float top = region.viewportRect[1], bottom = top + region.viewportRect[3];
        const auto opacity = [&](float y) {
            if (!enabled) return 1.0f;
            if (y < top || y >= bottom) return 0.0f;
            const auto smooth = [](float distance, float extent) {
                if (extent <= 0.0f) return 1.0f;
                const float t = std::clamp(distance / extent, 0.0f, 1.0f);
                return t * t * (3.0f - 2.0f * t);
            };
            return region.viewportFade[2] + (1.0f - region.viewportFade[2]) *
                std::min(smooth(y - top, region.viewportFade[0]), smooth(bottom - y, region.viewportFade[1]));
        };
        std::vector<float> positions { originY, originY + height };
        // Stops outside this lens are omitted, retaining one continuous brush.
        for (unsigned index = 0; index <= 16; ++index)
            for (float y : { top + region.viewportFade[0] * index / 16.0f,
                bottom - region.viewportFade[1] * index / 16.0f })
                if (y > originY && y < originY + height) positions.push_back(y);
        std::sort(positions.begin(), positions.end());
        positions.erase(std::unique(positions.begin(), positions.end()), positions.end());
        for (float y : positions) add(y, opacity(y));
    }

    composition::CompositionBrush mask_viewport_brush(RegionVisualState& state,
        composition::CompositionBrush source)
    {
        if (!state.viewportMaskBrush) return source;
        auto masked = compositor_.CreateMaskBrush();
        masked.Source(source); masked.Mask(state.viewportMaskBrush);
        return masked;
    }

    composition::CompositionClip make_clip(
        const VibranceCompositionRegion& region)
    {
        if (region.shape == 0u)
        {
            if (region.contourCount != 0u)
            {
                auto geometry = compositor_.CreateRectangleGeometry();
                geometry.Size({ region.width, region.height });
                return compositor_.CreateGeometricClip(geometry);
            }
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

    void sync_liquid_rim_clock(RegionVisualState& state,
        const VibranceCompositionRegion& region)
    {
        const bool rawDependency = vibrance::win32::liquid_dwm_uses_raw_backdrop_dependency(windows_build_number());
        auto clockVisual = rawDependency ? state.rawBackdropVisual : state.materialVisual;
        const bool animate = state.shaderOnly && clockVisual &&
            vibrance::win32::composition_liquid_rim_clock_required(region);
        const float stoppedOpacity = rawDependency ?
            (vibrance::win32::composition_raw_backdrop_required(region) ? 0.99f : 0.0f) : 0.9999f;
        if (animate == state.rimClockRunning &&
            (animate || state.stoppedClockOpacity == stoppedOpacity)) return;
        if (animate)
        {
            // Composition advances this clock at display cadence, even when
            // the HWND is unfocused and its sharp UI buffer is retained. The
            // identity backdrop is blended over the identical source, so its
            // changing opacity does not change the visible background. It
            // invalidates the lens dependency for the native animated rim.
            auto clock = compositor_.CreateScalarKeyFrameAnimation();
            auto linear = compositor_.CreateLinearEasingFunction();
            // Keep every frame translucent, including the loop boundary.
            // At unity opacity DWM can promote the raw backdrop to an opaque
            // draw and discard the native lens dependency until it changes
            // back, making the glass disappear once per clock iteration.
            // Windows 10 invalidates the translucent marker itself. Its brush
            // colour remains unchanged, and it has no desktop backdrop brush
            // that could introduce an opaque intermediate surface.
            const float high = rawDependency ? 0.99f : 0.9999f;
            const float low = rawDependency ? 0.98f : 0.9998f;
            clock.InsertKeyFrame(0.0f, high, linear);
            clock.InsertKeyFrame(0.5f, low, linear);
            clock.InsertKeyFrame(1.0f, high, linear);
            clock.Duration(std::chrono::seconds(4));
            clock.IterationBehavior(composition::AnimationIterationBehavior::Forever);
            clockVisual.StartAnimation(L"Opacity", clock);
            state.stoppedClockOpacity = -1.0f;
        }
        else if (clockVisual)
        {
            clockVisual.StopAnimation(L"Opacity");
            // Retain a translucent marker without a clock. Unity opacity can
            // let DWM promote away the dependency that owns the liquid effect.
            // A fully opaque cap returns before any backdrop taps in HLSL.
            // Hide its unused identity dependency as well; the marker remains
            // available to draw the opaque cap and its merging silhouette.
            clockVisual.Opacity(stoppedOpacity);
            state.stoppedClockOpacity = stoppedOpacity;
        }
        state.rimClockRunning = animate;
    }

    bool add_region(const VibranceCompositionRegion& region, bool shaderOnly,
        std::uint32_t shaderSlot)
    {
        // ABI material 1 is system glass; 2 is the shaped liquid underlay.
        if ((region.material != 1u && region.material != 2u) ||
            region.width <= 0.0f ||
            region.height <= 0.0f)
        {
            return false;
        }

        if (shaderOnly)
        {
            // Replace this translucent native brush at its composition draw.
            // It keeps the background visible and the UI in normal window order.
            RegionVisualState state;
            state.descriptor = region;
            state.shaderOnly = true;
            state.shaderSlot = shaderSlot;
            state.container = compositor_.CreateContainerVisual();
            state.container.Offset({ region.x, region.y, 0.0f });
            state.container.Size({ region.width, region.height });
            state.container.Opacity(std::clamp(region.opacity, 0.0f, 1.0f));
            const float top = std::clamp(region.verticalStart, 0.0f, 0.98f) * region.height;
            state.materialContainer = compositor_.CreateContainerVisual();
            state.materialContainer.Offset({ 0.0f, top, 0.0f });
            state.materialContainer.Size({ region.width, region.height - top });
            state.materialInset = true;
            // Declare a native dependency on the unmodified pixels behind the
            // lens. The identity effect keeps independently presented desktop
            // layers available to DWM without applying a system glass material.
            // HLSL owns the blur, refraction and rim above this raw source.
            // On Windows 10 this desktop dependency can resolve to an opaque
            // black intermediate surface, including its padded sampling halo.
            // The legacy emitter samples the live target directly instead.
            if (vibrance::win32::liquid_dwm_uses_raw_backdrop_dependency(windows_build_number()))
            {
                if (!rawBackdropEffectFactory_)
                {
                    auto rawSource = composition::CompositionEffectSourceParameter(
                        L"rawBackdrop");
                    auto rawEffect = winrt::make<SaturationEffect>(rawSource, 1.0f);
                    rawBackdropEffectFactory_ = compositor_.CreateEffectFactory(rawEffect);
                }
                auto rawBrush = rawBackdropEffectFactory_.CreateBrush();
                rawBrush.SetSourceParameter(L"rawBackdrop",
                    compositor_.CreateBackdropBrush());
                state.rawBackdropVisual = compositor_.CreateSpriteVisual();
                state.rawBackdropVisual.Opacity(
                    vibrance::win32::composition_raw_backdrop_required(region) ? 0.99f : 0.0f);
                const float margin = liquid_source_margin(region);
                const float horizontalMargin = margin + liquid_marker_padding(region);
                state.rawBackdropVisual.Offset({ -horizontalMargin, -margin, 0.0f });
                state.rawBackdropVisual.Size({ region.width + horizontalMargin * 2.0f,
                    region.height - top + margin * 2.0f });
                state.rawBackdropVisual.Brush(rawBrush);
                sync_raw_backdrop_clip(state, region);
                state.rawBackdropContainer = compositor_.CreateContainerVisual();
                state.rawBackdropContainer.Size(state.materialContainer.Size());
                state.rawBackdropContainer.Children().InsertAtBottom(state.rawBackdropVisual);
                state.materialContainer.Children().InsertAtBottom(state.rawBackdropContainer);
            }
            sync_viewport_mask(state, region);
            const float markerPadding = liquid_marker_padding(region);
            state.materialVisual = compositor_.CreateSpriteVisual();
            state.materialVisual.Opacity(0.9999f);
            state.materialVisual.Offset({ -markerPadding, 0.0f, 0.0f });
            state.materialVisual.Size({ region.width + markerPadding * 2.0f,
                region.height - top });
            state.materialVisual.Brush(compositor_.CreateColorBrush(
                winrt::Windows::UI::Color { vibrance::win32::liquidMarkerAlpha,
                    static_cast<std::uint8_t>(vibrance::win32::liquidMarkerRed + shaderSlot), vibrance::win32::liquidMarkerGreen,
                    vibrance::win32::liquidMarkerBlue }));
            state.materialContainer.Children().InsertAtTop(state.materialVisual);
            sync_liquid_rim_clock(state, region);
            state.container.Children().InsertAtBottom(state.materialContainer);
            regions_.Children().InsertAtTop(state.container);
            regionVisuals_.push_back(std::move(state));
            return true;
        }

        // Native Mica/Acrylic are window-level rectangles in this non-XAML
        // backend. Partial or non-rectangular requests are deliberately
        // rejected so an unbounded system backdrop can never leak outside the
        // requested region.
        const auto implementation = vibrance::win32::system_backdrop_implementation(
            region.provider, windows_build_number());
        if (implementation != vibrance::win32::SystemBackdropImplementation::eCompositionAcrylic)
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
            const DWM_SYSTEMBACKDROP_TYPE type =
                implementation == vibrance::win32::SystemBackdropImplementation::eDwmAcrylic ?
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
        container.Opacity(std::clamp(region.opacity, 0.0f, 1.0f));
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
        if (materialTop > 0.01f || region.viewportFade[3] != 0.0f)
        {
            materialContainer = compositor_.CreateContainerVisual();
            materialContainer.Offset({ 0.0f, materialTop, 0.0f });
            materialContainer.Size({ region.width, materialRegion.height });
            container.Children().InsertAtTop(materialContainer);
            state.materialInset = true;

        }
        state.materialContainer = materialContainer;
        if (region.viewportFade[3] != 0.0f)
        {
            state.viewportMaskBrush = compositor_.CreateLinearGradientBrush();
            state.viewportMaskBrush.StartPoint({ 0.0f, 0.0f });
            state.viewportMaskBrush.EndPoint({ 0.0f, 1.0f });
        }
        sync_viewport_mask(state, region);

        auto backdrop = compositor_.CreateSpriteVisual();
        backdrop.Size(materialContainer.Size());
        backdrop.Brush(mask_viewport_brush(state, make_effect_brush(materialRegion)));
        materialContainer.Children().InsertAtBottom(backdrop);
        state.materialVisual = backdrop;
        if (region.material == 2u && region.liquidBackend != 2u && region.viewportFade[3] == 0.0f)
        {
            try
            {
                // The backdrop cannot be sampled by the app renderer. A live
                // visual surface lets Composition resample it in clipped
                // affine tiles, giving the island an edge-dependent lens.
                auto lensSource = compositor_.CreateSpriteVisual();
                lensSource.Size(materialContainer.Size());
                VibranceCompositionRegion lensRegion = materialRegion;
                lensRegion.blurRadius = 0.75f;
                lensSource.Brush(make_effect_brush(lensRegion));
                materialContainer.Children().InsertAtBottom(lensSource);
                auto surface = compositor_.CreateVisualSurface();
                surface.SourceVisual(lensSource);
                surface.SourceSize({ region.width, materialRegion.height });
                state.liquidSourceVisual = lensSource;
                state.liquidSurface = surface;
                constexpr std::size_t tileCount = 20u * 8u;
                state.liquidTiles.reserve(tileCount);
                for (std::size_t index = 0u; index < tileCount; ++index)
                {
                    auto brush = compositor_.CreateSurfaceBrush(surface);
                    brush.Stretch(composition::CompositionStretch::None);
                    brush.HorizontalAlignmentRatio(0.0f);
                    brush.VerticalAlignmentRatio(0.0f);
                    auto visual = compositor_.CreateSpriteVisual();
                    visual.Brush(brush);
                    visual.Opacity(0.92f);
                    materialContainer.Children().InsertAtTop(visual);
                    state.liquidTiles.push_back({ brush, visual });
                }
                update_liquid_tiles(state, region.width, materialRegion.height);
            }
            catch (const winrt::hresult_error&)
            {
                OutputDebugStringW(
                    L"vibranceUI: liquid refraction unavailable; retaining shaped backdrop blur.\n");
            }
        }

        const auto fallbackTint = vibrance::win32::composition_fallback_tint(region);
        if (fallbackTint[3] > 0.0001f)
        {
            auto tint = compositor_.CreateSpriteVisual();
            tint.Size(materialContainer.Size());
            tint.Brush(mask_viewport_brush(state, compositor_.CreateColorBrush(color(
                fallbackTint[0], fallbackTint[1], fallbackTint[2], fallbackTint[3]))));
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
    bool transparentFramebuffer_ = false;
    bool cpuUpload_ = false;
    bool windowClaimed_ = false;

    winrt::Windows::System::DispatcherQueueController dispatcherController_ { nullptr };
    winrt::com_ptr<ID3D11Device5> device_;
    vibrance::win32::LiquidGlassD3D11 liquidRenderer_;
    bool liquidShaderProbeDone_ = false;
    bool liquidShaderProbeResult_ = false;
    HANDLE liquidRegionMapping_ = nullptr;
    vibrance::win32::LiquidDwmRegionTable* liquidRegionTable_ = nullptr;
    std::vector<LiquidRegionBinding> liquidRegionBindings_;
    vibrance::win32::LiquidDwmRegion* liquidRegionView_ = nullptr;
    std::mutex liquidRegionWriteMutex_;
    std::uint32_t uiFrame_ = 0;
    std::array<winrt::com_ptr<ID3D11Texture2D>,
        vibrance::win32::liquidDwmUiBuffers> uiMirrorTextures_;
    std::array<winrt::com_ptr<IDXGIKeyedMutex>,
        vibrance::win32::liquidDwmUiBuffers> uiMirrorMutexes_;
    std::array<HANDLE, vibrance::win32::liquidDwmUiBuffers>
        uiMirrorHandles_ {};
    bool uiMirrorAttempted_ = false;
    HANDLE uiCpuMapping_ = nullptr;
    vibrance::win32::LiquidDwmCpuUiHeader* uiCpuView_ = nullptr;
    std::array<winrt::com_ptr<ID3D11Texture2D>, 3> uiCpuStaging_;
    std::array<bool, 3> uiCpuPending_ {};
    std::array<std::uint32_t, 3> uiCpuSourceFrame_ {};
    std::uint32_t uiCpuScheduleSerial_ = 0;
    std::uint32_t uiCpuLastPublishedSource_ = 0;
    std::uint32_t uiCpuFrame_ = 0;
    ULONGLONG uiCpuLastScheduleTick_ = 0;
    winrt::com_ptr<ID3D11DeviceContext4> context_;
    winrt::com_ptr<IDXGISwapChain1> swapchain_;
    winrt::com_ptr<IDXGISwapChain3> swapchain3_;
    std::vector<winrt::com_ptr<ID3D11Texture2D>> backBuffers_;
    std::vector<bool> backBufferInitialised_;
    std::vector<RECT> backBufferContentRects_;
    vibrance::win32::CompositionPresentDamageHistory presentDamage_;
    vibrance::win32::CompositionCopyDiagnostics copyDiagnostics_;
    vibrance::win32::CompositionPresentRetry presentRetry_;
    vibrance::win32::CompositionPresentStall presentStall_;
    std::uint32_t pendingPresentBackBuffer_ = 0u;
    RECT pendingPresentDamage_ {};
    HANDLE frameLatencyWaitableObject_ = nullptr;
    bool frameLatencyReady_ = false;
    vibrance::win32::CompositionLatencyHint latencyHint_;
    std::vector<winrt::com_ptr<ID3D11Texture2D>> sharedTextures_;
    std::vector<winrt::com_ptr<IDXGIKeyedMutex>> sharedMutexes_;
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
    composition::CompositionEffectFactory rawBackdropEffectFactory_ { nullptr };
    bool contentHasShaderRegion_ = false;
    std::vector<RegionVisualState> regionVisuals_;
    std::vector<std::uint8_t> cpuUploadPixels_;
    vibrance::win32::CompositionUploadDamage cpuUploadDamage_;
    vibrance::win32::CompositionUploadPixels cpuUploadConverter_;
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
vibrance_composition_upload(
    VibranceCompositionHandle handle,
    std::uint32_t index,
    const VibranceCompositionUploadInfo* uploadInfo)
{
    auto* bridge = static_cast<CompositionBridge*>(handle);
    return bridge && bridge->upload(index, uploadInfo) ? 1u : 0u;
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

#pragma once

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "liquid_glass_shader_abi.h"

#include <d3d11.h>
#include <d3d11_1.h>
#include <wrl/client.h>

#include <cstdint>
#include <array>
#include <string>
#include <vector>

namespace vibrance::win32
{
// Coordinates are physical pixels in the compositor render target. The
// source must contain the live, unmodified desktop below the island.
struct LiquidGlassRenderRegion
{
    std::int32_t x = 0;
    std::int32_t y = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    float cornerRadius = 28.0f;
    float edgeThickness = 22.0f;
    float refractionStrength = 20.0f;
    float dispersionStrength = 2.0f;
    float blurRadius = 7.0f;
    float rimStrength = 0.25f;
    float lightPhase = 0.0f;
    float opacity = 1.0f;
    float cornerExponent = 4.0f;
    std::uint32_t shape = 3u;
    float squircleAmount = 1.0f;
    float notchAmount = 0.0f;
    float notchDepth = 0.0f;
    float topLeftRadius = -1.0f;
    float topRightRadius = -1.0f;
    float bottomRightRadius = -1.0f;
    float bottomLeftRadius = -1.0f;
    float markerInsetX = 0.0f;
    float tintRed = 213.0f / 255.0f;
    float tintGreen = 231.0f / 255.0f;
    float tintBlue = 1.0f;
    float tintAlpha = 0.08f;
    struct Contour
    {
        float x = 0.0f, y = 0.0f, width = 0.0f, height = 0.0f;
        float cornerRadius = 0.0f;
        float topLeftRadius = 0.0f, topRightRadius = 0.0f;
        float bottomRightRadius = 0.0f, bottomLeftRadius = 0.0f;
        float squircleAmount = 1.0f, squirclePower = 4.0f;
        float notchAmount = 0.0f, notchDepth = 0.0f;
        std::uint32_t shape = 0u;
    };
    std::array<Contour, 3> contours {};
    std::uint32_t contourCount = 0u;
    Contour exclusion {};
    bool hasExclusion = false;
    bool paintCapGradient = false;
    std::array<float, 3> capColour { 0.0f, 0.0f, 0.0f };
    std::array<float, 4> capGradient { 0.98f, 0.015f, 0.0f, 1.0f };
    std::array<float, 4> viewportRect {};
    std::array<float, 4> viewportFade {};
};
static_assert(sizeof(LiquidGlassRenderRegion::Contour) == 56u);

// GPU-only DX11 pass. It copies just the island and its sampling margin from
// the compositor target, blurs that copy twice, then refracts the sharp copy.
// Fully opaque cap paint retains only the small snapshot needed by the common
// draw path and skips invisible optical sampling margins and Gaussian work.
// The caller owns and restores its D3D11 context state around render().
class LiquidGlassD3D11 final
{
public:
    bool initialise(ID3D11Device* device, const std::wstring& shaderDirectory);
    int initialisation_stage() const noexcept { return initialisationStage_; }
    std::uint64_t scratch_allocation_count() const noexcept { return scratchAllocationCount_; }
    std::uint64_t blur_reuse_count() const noexcept { return blurReuseCount_; }
    std::uint64_t blur_change_count() const noexcept { return blurChangeCount_; }
    std::uint64_t blur_comparison_count() const noexcept { return blurComparisonCount_; }
    // Supply the typed RTV format when liveBackdrop is a typeless resource.
    bool render(ID3D11DeviceContext* context,
                ID3D11Texture2D* liveBackdrop,
                ID3D11RenderTargetView* target,
                const LiquidGlassRenderRegion& region,
                std::int32_t sourceOriginX = 0,
                std::int32_t sourceOriginY = 0,
                const D3D11_RECT* clip = nullptr,
                DXGI_FORMAT backdropFormat = DXGI_FORMAT_UNKNOWN);
    bool composite_ui(ID3D11DeviceContext* context,
                      ID3D11ShaderResourceView* ui,
                      ID3D11RenderTargetView* target,
                      std::int32_t x, std::int32_t y,
                      std::uint32_t width, std::uint32_t height);

private:
    friend struct LiquidGlassD3D11TestAccess;
    struct BlurComparison
    {
        Microsoft::WRL::ComPtr<ID3D11Predicate> predicate;
        bool pending = false;
    };
    struct BlurHistory
    {
        D3D11_RECT snapshotBounds {};
        D3D11_RECT blurBounds {};
        float radius = -1.0f;
        std::array<BlurComparison, 3> comparisons {};
        std::size_t nextComparison = 0u;
        std::uint8_t changedStreak = 0u;
        std::uint8_t backoff = 0u;
    };
    struct ScratchSurfaces
    {
        Microsoft::WRL::ComPtr<ID3D11Texture2D> snapshot, blurX, blurY;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> snapshotView, blurXView, blurYView;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> blurXTarget, blurYTarget;
        BlurHistory history;
        DXGI_FORMAT format = DXGI_FORMAT_UNKNOWN;
        std::uint32_t width = 0u;
        std::uint32_t height = 0u;
        std::uint64_t lastUsed = 0u;
    };
    bool resize_scratch(DXGI_FORMAT format, std::uint32_t width,
                        std::uint32_t height);
    void swap_scratch(ScratchSurfaces& surfaces) noexcept;
    void trim_scratch_cache() noexcept;
    BlurComparison* available_blur_comparison() noexcept
    {
        for (std::size_t offset = 0u; offset < blurHistory_.comparisons.size(); ++offset)
        {
            const auto index = (blurHistory_.nextComparison + offset) % blurHistory_.comparisons.size();
            auto& comparison = blurHistory_.comparisons[index];
            if (comparison.predicate && !comparison.pending)
            {
                blurHistory_.nextComparison = (index + 1u) % blurHistory_.comparisons.size();
                return &comparison;
            }
        }
        return nullptr;
    }
    template<typename Poll>
    void collect_blur_comparisons(Poll poll)
    {
        // Poll in ring order, oldest first. CPU query availability must never
        // force an unconditional blur while another exact predicate is free.
        for (std::size_t offset = 0u; offset < blurHistory_.comparisons.size(); ++offset)
        {
            auto& comparison = blurHistory_.comparisons[
                (blurHistory_.nextComparison + offset) % blurHistory_.comparisons.size()];
            if (!comparison.pending) continue;
            BOOL changed = FALSE;
            const HRESULT result = poll(comparison.predicate.Get(), changed);
            if (result == S_FALSE) continue;
            comparison.pending = false;
            if (FAILED(result))
            {
                // A failed query disables only this slot. Full filtering is
                // still the exact fallback if the remaining pool is busy.
                comparison.predicate.Reset();
                continue;
            }
            if (changed)
            {
                ++blurChangeCount_;
                if (++blurHistory_.changedStreak >= 2u)
                {
                    blurHistory_.backoff = 32u;
                    blurHistory_.changedStreak = 0u;
                }
            }
            else
            {
                ++blurReuseCount_;
                blurHistory_.changedStreak = 0u;
            }
        }
    }

    Microsoft::WRL::ComPtr<ID3D11Device> device_;
    Microsoft::WRL::ComPtr<ID3D11VertexShader> fullscreenVertex_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> blurHorizontal_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> blurVertical_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> optical_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> compare_;
    Microsoft::WRL::ComPtr<ID3D11PixelShader> uiPixel_;
    Microsoft::WRL::ComPtr<ID3D11SamplerState> sampler_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> blend_;
    Microsoft::WRL::ComPtr<ID3D11BlendState> uiBlend_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> rasteriser_;
    Microsoft::WRL::ComPtr<ID3D11RasterizerState> clippedRasteriser_;
    Microsoft::WRL::ComPtr<ID3D11DepthStencilState> depthState_;
    Microsoft::WRL::ComPtr<ID3DDeviceContextState> contextState_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> blurParameters_;
    Microsoft::WRL::ComPtr<ID3D11Buffer> opticalParameters_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> snapshot_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> blurX_;
    Microsoft::WRL::ComPtr<ID3D11Texture2D> blurY_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> snapshotView_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> blurXView_;
    Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> blurYView_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> blurXTarget_;
    Microsoft::WRL::ComPtr<ID3D11RenderTargetView> blurYTarget_;
    BlurHistory blurHistory_;
    DXGI_FORMAT scratchFormat_ = DXGI_FORMAT_UNKNOWN;
    std::uint32_t scratchWidth_ = 0;
    std::uint32_t scratchHeight_ = 0;
    // Grow the metadata for small lenses instead of imposing an eight-size
    // allocation cliff. GPU memory remains bounded by the existing allowance.
    std::vector<ScratchSurfaces> scratchCache_;
    std::uint64_t scratchLastUsed_ = 0u;
    std::uint64_t scratchCacheClock_ = 0u;
    std::uint64_t scratchCacheBudget_ = 32u * 1024u * 1024u;
    std::uint64_t scratchAllocationCount_ = 0u;
    std::uint64_t blurReuseCount_ = 0u;
    std::uint64_t blurChangeCount_ = 0u;
    std::uint64_t blurComparisonCount_ = 0u;
    LiquidGlassBlurParameters blurKernel_ {};
    float cachedBlurRadius_ = -1.0f;
    std::uint32_t cachedBlurWidth_ = 0u;
    std::uint32_t cachedBlurHeight_ = 0u;
    int initialisationStage_ = 0;
};
}

#endif

#include "liquid_glass_d3d11.h"

#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

namespace vibrance::win32
{
namespace
{
using Microsoft::WRL::ComPtr;

bool load_shader(const std::wstring& directory, const wchar_t* filename,
                 std::vector<std::uint8_t>& bytes)
{
    const std::wstring path = directory + L"\\" + filename;
    const HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ,
                                    nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL,
                                    nullptr);
    if (file == INVALID_HANDLE_VALUE)
    {
        return false;
    }
    LARGE_INTEGER size {};
    const bool valid = GetFileSizeEx(file, &size) && size.QuadPart > 0 &&
        size.QuadPart <= 1024 * 1024;
    if (!valid)
    {
        CloseHandle(file);
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size.QuadPart));
    DWORD read = 0;
    const bool success = ReadFile(file, bytes.data(),
                                  static_cast<DWORD>(bytes.size()), &read,
                                  nullptr) && read == bytes.size();
    CloseHandle(file);
    return success;
}

bool make_surface(ID3D11Device* device, DXGI_FORMAT format,
                  std::uint32_t width, std::uint32_t height,
                  ComPtr<ID3D11Texture2D>& texture,
                  ComPtr<ID3D11ShaderResourceView>& view,
                  ComPtr<ID3D11RenderTargetView>* target)
{
    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = format;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE |
        (target ? D3D11_BIND_RENDER_TARGET : 0u);
    if (FAILED(device->CreateTexture2D(&desc, nullptr, texture.ReleaseAndGetAddressOf())) ||
        FAILED(device->CreateShaderResourceView(texture.Get(), nullptr,
                                                view.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    return !target || SUCCEEDED(device->CreateRenderTargetView(
        texture.Get(), nullptr, target->ReleaseAndGetAddressOf()));
}

struct ContextRestore
{
    ID3D11DeviceContext1* context = nullptr;
    ID3DDeviceContextState* previous = nullptr;

    ~ContextRestore()
    {
        if (context)
        {
            context->SwapDeviceContextState(previous, nullptr);
        }
        if (previous)
        {
            previous->Release();
        }
    }
};

void prepare_blur_kernel(float radius, LiquidGlassBlurParameters& kernel)
{
    kernel = {};
    const double sigma = std::clamp(static_cast<double>(radius) / 3.0, 0.35, 10.0);
    const double inverseVariance = 0.5 / (sigma * sigma);
    std::array<double, 15> weights {};
    std::array<double, 15> offsets {};
    double totalWeight = 1.0;
    for (std::size_t pair = 0u; pair < weights.size(); ++pair)
    {
        const double firstOffset = static_cast<double>(pair * 2u + 1u);
        const double secondOffset = firstOffset + 1.0;
        const double firstWeight = std::exp(-firstOffset * firstOffset * inverseVariance);
        const double secondWeight = std::exp(-secondOffset * secondOffset * inverseVariance);
        weights[pair] = firstWeight + secondWeight;
        offsets[pair] = firstOffset + secondWeight / std::max(weights[pair], 1e-30);
        totalWeight += 2.0 * weights[pair];
    }
    // Bound the complete omitted mass, rather than each individual tap.
    // Retaining and renormalising the remainder changes a unit-range channel
    // by at most 1e-7 per axis, well below one UNORM8 quantisation step.
    std::size_t count = weights.size();
    double omittedWeight = 0.0;
    while (count > 0u &&
        (omittedWeight + 2.0 * weights[count - 1u]) <= totalWeight * 1e-7)
    {
        omittedWeight += 2.0 * weights[--count];
    }
    const double retainedWeight = totalWeight - omittedWeight;
    kernel.centreWeight = static_cast<float>(1.0 / retainedWeight);
    kernel.pairCount = static_cast<std::uint32_t>(count);
    for (std::size_t pair = 0u; pair < count; ++pair)
    {
        kernel.pairs[pair][0] = static_cast<float>(offsets[pair]);
        kernel.pairs[pair][1] = static_cast<float>(weights[pair] / retainedWeight);
    }
}
}

bool LiquidGlassD3D11::initialise(ID3D11Device* device,
                                  const std::wstring& shaderDirectory)
{
    initialisationStage_ = 0;
    if (!device)
    {
        return false;
    }
    device_ = device;
    // A recreated constant buffer has no kernel contents, even when the next
    // lens has the same radius and dimensions as the previous device's lens.
    cachedBlurRadius_ = -1.0f;
    cachedBlurWidth_ = 0u;
    cachedBlurHeight_ = 0u;
    ScratchSurfaces previousScratch;
    swap_scratch(previousScratch);
    scratchCache_ = {};
    scratchCacheInsertion_ = 0u;
    std::vector<std::uint8_t> bytecode;
    if (!load_shader(shaderDirectory, L"liquid_glass_fullscreen.cso", bytecode) ||
        FAILED(device->CreateVertexShader(bytecode.data(), bytecode.size(),
                                          nullptr, fullscreenVertex_.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    initialisationStage_ = 1;
    const std::array<std::pair<const wchar_t*, ComPtr<ID3D11PixelShader>*>, 4>
        pixelShaders {{
            { L"liquid_glass_blur_horizontal.cso", &blurHorizontal_ },
            { L"liquid_glass_blur_vertical.cso", &blurVertical_ },
            { L"liquid_glass_win32.cso", &optical_ },
            { L"liquid_glass_ui_win32.cso", &uiPixel_ }
        }};
    for (const auto& [filename, shader] : pixelShaders)
    {
        if (!load_shader(shaderDirectory, filename, bytecode) ||
            FAILED(device->CreatePixelShader(bytecode.data(), bytecode.size(),
                                             nullptr, shader->ReleaseAndGetAddressOf())))
        {
            return false;
        }
        ++initialisationStage_;
    }

    D3D11_SAMPLER_DESC sampler {};
    sampler.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler.MaxLOD = D3D11_FLOAT32_MAX;
    if (FAILED(device->CreateSamplerState(&sampler, sampler_.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    initialisationStage_ = 6;

    D3D11_BLEND_DESC blend {};
    blend.RenderTarget[0].BlendEnable = TRUE;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
    blend.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
    blend.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    if (FAILED(device->CreateBlendState(&blend, blend_.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    initialisationStage_ = 7;
    blend.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    if (FAILED(device->CreateBlendState(&blend, uiBlend_.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    initialisationStage_ = 8;

    D3D11_RASTERIZER_DESC rasteriser {};
    rasteriser.FillMode = D3D11_FILL_SOLID;
    rasteriser.CullMode = D3D11_CULL_NONE;
    rasteriser.DepthClipEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasteriser,
                                             rasteriser_.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    initialisationStage_ = 9;
    rasteriser.ScissorEnable = TRUE;
    if (FAILED(device->CreateRasterizerState(&rasteriser,
            clippedRasteriser_.ReleaseAndGetAddressOf()))) return false;
    D3D11_DEPTH_STENCIL_DESC depth {};
    depth.DepthEnable = FALSE;
    depth.StencilEnable = FALSE;
    if (FAILED(device->CreateDepthStencilState(&depth, depthState_.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    initialisationStage_ = 10;

    D3D11_BUFFER_DESC constantBuffer {};
    constantBuffer.Usage = D3D11_USAGE_DEFAULT;
    constantBuffer.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    constantBuffer.ByteWidth = sizeof(LiquidGlassBlurParameters);
    if (FAILED(device->CreateBuffer(&constantBuffer, nullptr,
                                    blurParameters_.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    initialisationStage_ = 11;
    constantBuffer.ByteWidth = sizeof(LiquidGlassShaderParameters);
    if (FAILED(device->CreateBuffer(&constantBuffer, nullptr,
                                    opticalParameters_.ReleaseAndGetAddressOf())))
    {
        return false;
    }
    initialisationStage_ = 12;

    ComPtr<ID3D11Device1> device1;
    if (FAILED(device->QueryInterface(IID_PPV_ARGS(&device1))))
    {
        return false;
    }
    initialisationStage_ = 13;
    const D3D_FEATURE_LEVEL levels[] = { device->GetFeatureLevel() };
    const UINT contextFlags = (device->GetCreationFlags() &
        D3D11_CREATE_DEVICE_SINGLETHREADED) ?
        D3D11_1_CREATE_DEVICE_CONTEXT_STATE_SINGLETHREADED : 0u;
    const bool ready = SUCCEEDED(device1->CreateDeviceContextState(
        contextFlags, levels, 1u, D3D11_SDK_VERSION,
        __uuidof(ID3D11Device1), nullptr,
        contextState_.ReleaseAndGetAddressOf()));
    if (ready) initialisationStage_ = 14;
    return ready;
}

bool LiquidGlassD3D11::resize_scratch(DXGI_FORMAT format,
                                      std::uint32_t width,
                                      std::uint32_t height)
{
    if (snapshot_ && scratchFormat_ == format &&
        scratchWidth_ == width && scratchHeight_ == height)
    {
        return true;
    }
    for (auto& cached : scratchCache_)
    {
        if (cached.snapshot && cached.format == format &&
            cached.width == width && cached.height == height)
        {
            swap_scratch(cached);
            trim_scratch_cache();
            return true;
        }
    }
    ScratchSurfaces next;
    if (!make_surface(device_.Get(), format, width, height,
                      next.snapshot, next.snapshotView, nullptr) ||
        !make_surface(device_.Get(), format, width, height,
                      next.blurX, next.blurXView, &next.blurXTarget) ||
        !make_surface(device_.Get(), format, width, height,
                      next.blurY, next.blurYView, &next.blurYTarget))
    {
        return false;
    }
    next.format = format;
    next.width = width;
    next.height = height;
    swap_scratch(next);
    if (next.snapshot)
    {
        scratchCache_[scratchCacheInsertion_] = std::move(next);
        scratchCacheInsertion_ = (scratchCacheInsertion_ + 1u) % scratchCache_.size();
        trim_scratch_cache();
    }
    return true;
}

void LiquidGlassD3D11::swap_scratch(ScratchSurfaces& surfaces) noexcept
{
    snapshot_.Swap(surfaces.snapshot);
    blurX_.Swap(surfaces.blurX);
    blurY_.Swap(surfaces.blurY);
    snapshotView_.Swap(surfaces.snapshotView);
    blurXView_.Swap(surfaces.blurXView);
    blurYView_.Swap(surfaces.blurYView);
    blurXTarget_.Swap(surfaces.blurXTarget);
    blurYTarget_.Swap(surfaces.blurYTarget);
    std::swap(scratchFormat_, surfaces.format);
    std::swap(scratchWidth_, surfaces.width);
    std::swap(scratchHeight_, surfaces.height);
}

void LiquidGlassD3D11::trim_scratch_cache() noexcept
{
    // Reserve at most 16 MiB for retired sets. Account for the common DWM
    // formats exactly; sixteen bytes conservatively covers all other formats.
    const auto bytes = [](const ScratchSurfaces& surfaces) {
        std::uint32_t texelBytes = 16u;
        switch (surfaces.format)
        {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_R8G8B8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM_SRGB:
        case DXGI_FORMAT_B8G8R8X8_UNORM:
        case DXGI_FORMAT_B8G8R8X8_UNORM_SRGB:
        case DXGI_FORMAT_R10G10B10A2_UNORM:
        case DXGI_FORMAT_R11G11B10_FLOAT:
            texelBytes = 4u;
            break;
        case DXGI_FORMAT_R16G16B16A16_FLOAT:
        case DXGI_FORMAT_R16G16B16A16_UNORM:
            texelBytes = 8u;
            break;
        default:
            break;
        }
        return static_cast<std::uint64_t>(surfaces.width) * surfaces.height * texelBytes * 3u;
    };
    std::uint64_t retainedBytes = 0u;
    for (const auto& surfaces : scratchCache_) retainedBytes += bytes(surfaces);
    constexpr std::uint64_t maximumRetainedBytes = 16u * 1024u * 1024u;
    for (std::size_t age = 0u;
        retainedBytes > maximumRetainedBytes && age < scratchCache_.size(); ++age)
    {
        auto& oldest = scratchCache_[(scratchCacheInsertion_ + age) % scratchCache_.size()];
        retainedBytes -= bytes(oldest);
        oldest = {};
    }
}

bool LiquidGlassD3D11::render(ID3D11DeviceContext* context,
                              ID3D11Texture2D* liveBackdrop,
                              ID3D11RenderTargetView* target,
                              const LiquidGlassRenderRegion& region,
                              std::int32_t sourceOriginX,
                              std::int32_t sourceOriginY,
                              const D3D11_RECT* clip)
{
    if (!context || !liveBackdrop || !target || !contextState_ ||
        !std::isfinite(region.blurRadius) ||
        !std::isfinite(region.refractionStrength) ||
        !std::isfinite(region.dispersionStrength) ||
        !std::isfinite(region.cornerRadius) ||
        !std::isfinite(region.edgeThickness) ||
        !std::isfinite(region.rimStrength) ||
        !std::isfinite(region.lightPhase) ||
        !std::isfinite(region.opacity) ||
        !std::isfinite(region.cornerExponent) ||
        !std::isfinite(region.squircleAmount) ||
        !std::isfinite(region.notchAmount) ||
        !std::isfinite(region.notchDepth) ||
        !std::isfinite(region.topLeftRadius) ||
        !std::isfinite(region.topRightRadius) ||
        !std::isfinite(region.bottomRightRadius) ||
        !std::isfinite(region.bottomLeftRadius) ||
        !std::isfinite(region.markerInsetX) ||
        !std::isfinite(region.tintRed) ||
        !std::isfinite(region.tintGreen) ||
        !std::isfinite(region.tintBlue) ||
        !std::isfinite(region.tintAlpha) ||
        region.markerInsetX < 0.0f ||
        region.markerInsetX * 2.0f >= region.width ||
        region.shape > 5u ||
        region.contourCount > region.contours.size() ||
        region.width == 0 || region.height == 0 ||
        region.width > 32768u || region.height > 32768u)
    {
        return false;
    }
    const auto validContour = [](const LiquidGlassRenderRegion::Contour& contour) {
        const float values[] = { contour.x, contour.y, contour.width,
            contour.height, contour.cornerRadius, contour.topLeftRadius,
            contour.topRightRadius, contour.bottomRightRadius,
            contour.bottomLeftRadius, contour.squircleAmount,
            contour.squirclePower, contour.notchAmount, contour.notchDepth };
        for (const float value : values)
            if (!std::isfinite(value)) return false;
        return contour.shape <= 5u && contour.width > 0.0f &&
            contour.height > 0.0f && contour.width <= 32768.0f &&
            contour.height <= 32768.0f && std::abs(contour.x) <= 65536.0f &&
            std::abs(contour.y) <= 65536.0f;
    };
    for (std::uint32_t index = 0u; index < region.contourCount; ++index)
        if (!validContour(region.contours[index])) return false;
    if (region.hasExclusion && !validContour(region.exclusion)) return false;
    if (region.paintCapGradient)
    {
        for (const float value : region.capGradient)
            if (!std::isfinite(value)) return false;
        for (const float value : region.capColour)
            if (!std::isfinite(value)) return false;
    }
    if (region.viewportFade[3] != 0.0f)
    {
        for (const float value : region.viewportRect) if (!std::isfinite(value)) return false;
        for (const float value : region.viewportFade) if (!std::isfinite(value)) return false;
        if (region.viewportRect[2] <= 0.0f || region.viewportRect[3] <= 0.0f ||
            region.viewportFade[0] < 0.0f || region.viewportFade[1] < 0.0f) return false;
    }
    ComPtr<ID3D11Resource> targetResource;
    target->GetResource(&targetResource);
    ComPtr<ID3D11Texture2D> targetTexture;
    if (!targetResource || FAILED(targetResource.As(&targetTexture))) return false;
    D3D11_TEXTURE2D_DESC targetDescription {};
    targetTexture->GetDesc(&targetDescription);
    if (!targetDescription.Width || !targetDescription.Height ||
        targetDescription.Width > 32768u || targetDescription.Height > 32768u ||
        targetDescription.ArraySize != 1u || targetDescription.SampleDesc.Count != 1u)
        return false;
    const auto visibleLeft = std::max<std::int64_t>(0, region.x);
    const auto visibleTop = std::max<std::int64_t>(0, region.y);
    const auto visibleRight = std::min<std::int64_t>(targetDescription.Width,
        static_cast<std::int64_t>(region.x) + region.width);
    const auto visibleBottom = std::min<std::int64_t>(targetDescription.Height,
        static_cast<std::int64_t>(region.y) + region.height);
    if (visibleRight <= visibleLeft || visibleBottom <= visibleTop) return true;
    const auto visibleWidth = static_cast<std::uint32_t>(visibleRight - visibleLeft);
    const auto visibleHeight = static_cast<std::uint32_t>(visibleBottom - visibleTop);
    const auto offsetX = visibleLeft - region.x;
    const auto offsetY = visibleTop - region.y;
    D3D11_RECT visibleClip {};
    if (clip)
    {
        visibleClip = {
            std::max<LONG>(static_cast<LONG>(visibleLeft), clip->left),
            std::max<LONG>(static_cast<LONG>(visibleTop), clip->top),
            std::min<LONG>(static_cast<LONG>(visibleRight), clip->right),
            std::min<LONG>(static_cast<LONG>(visibleBottom), clip->bottom)
        };
        if (visibleClip.right <= visibleClip.left ||
            visibleClip.bottom <= visibleClip.top) return true;
    }

    D3D11_TEXTURE2D_DESC source {};
    liveBackdrop->GetDesc(&source);
    const std::int64_t localX = static_cast<std::int64_t>(region.x) -
        sourceOriginX;
    const std::int64_t localY = static_cast<std::int64_t>(region.y) -
        sourceOriginY;
    const std::int64_t right = localX + region.width;
    const std::int64_t bottom = localY + region.height;
    if (source.SampleDesc.Count != 1 || source.ArraySize != 1 ||
        source.Width == 0u || source.Height == 0u ||
        right <= 0 || bottom <= 0 || localX >= source.Width || localY >= source.Height)
    {
        return false;
    }

    const float reach = std::max({ region.blurRadius * 5.0f,
        region.refractionStrength + region.dispersionStrength + 2.0f,
        4.0f });
    if (!std::isfinite(reach) || reach > 2048.0f)
    {
        return false;
    }
    const std::int64_t margin = static_cast<std::int64_t>(std::ceil(reach));
    const std::int64_t left = std::max<std::int64_t>(0, localX - margin);
    const std::int64_t top = std::max<std::int64_t>(0, localY - margin);
    const std::int64_t cropRight = std::min<std::int64_t>(source.Width,
                                                          right + margin);
    const std::int64_t cropBottom = std::min<std::int64_t>(source.Height,
                                                           bottom + margin);
    if (cropRight <= left || cropBottom <= top) return false;
    const auto width = static_cast<std::uint32_t>(cropRight - left);
    const auto height = static_cast<std::uint32_t>(cropBottom - top);
    if (!resize_scratch(source.Format, width, height))
    {
        return false;
    }

    ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1))))
    {
        return false;
    }
    ContextRestore restore { context1.Get(), nullptr };
    context1->SwapDeviceContextState(contextState_.Get(), &restore.previous);

    D3D11_BOX box {};
    box.left = static_cast<UINT>(left);
    box.top = static_cast<UINT>(top);
    box.right = static_cast<UINT>(cropRight);
    box.bottom = static_cast<UINT>(cropBottom);
    box.back = 1;
    context->CopySubresourceRegion(snapshot_.Get(), 0, 0, 0, 0,
                                   liveBackdrop, 0, &box);

    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(fullscreenVertex_.Get(), nullptr, 0);
    context->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context->RSSetState(rasteriser_.Get());
    context->OMSetDepthStencilState(depthState_.Get(), 0);
    const FLOAT blendFactor[4] = { 0, 0, 0, 0 };
    D3D11_VIEWPORT viewport {};
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);

    const float blurRadius = std::clamp(region.blurRadius, 0.0f, 30.0f);
    ID3D11ShaderResourceView* blurredView = snapshotView_.Get();
    if (blurRadius > 0.0f)
    {
        if (cachedBlurRadius_ != blurRadius)
        {
            prepare_blur_kernel(blurRadius, blurKernel_);
            cachedBlurRadius_ = blurRadius;
            cachedBlurWidth_ = 0u;
        }
        if (cachedBlurWidth_ != width || cachedBlurHeight_ != height)
        {
            blurKernel_.texelSize[0] = 1.0f / width;
            blurKernel_.texelSize[1] = 1.0f / height;
            context->UpdateSubresource(blurParameters_.Get(), 0, nullptr, &blurKernel_, 0, 0);
            cachedBlurWidth_ = width;
            cachedBlurHeight_ = height;
        }
        context->PSSetConstantBuffers(0, 1, blurParameters_.GetAddressOf());
        context->OMSetBlendState(nullptr, blendFactor, 0xffffffffu);
        ID3D11RenderTargetView* passTarget = blurXTarget_.Get();
        context->OMSetRenderTargets(1, &passTarget, nullptr);
        ID3D11ShaderResourceView* passInput = snapshotView_.Get();
        context->PSSetShaderResources(0, 1, &passInput);
        context->PSSetShader(blurHorizontal_.Get(), nullptr, 0);
        context->Draw(3, 0);

        passInput = nullptr;
        context->PSSetShaderResources(0, 1, &passInput);
        passTarget = blurYTarget_.Get();
        context->OMSetRenderTargets(1, &passTarget, nullptr);
        passInput = blurXView_.Get();
        context->PSSetShaderResources(0, 1, &passInput);
        context->PSSetShader(blurVertical_.Get(), nullptr, 0);
        context->Draw(3, 0);
        blurredView = blurYView_.Get();
    }

    ID3D11ShaderResourceView* passInput = nullptr;
    context->PSSetShaderResources(0, 1, &passInput);
    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetBlendState(blend_.Get(), blendFactor, 0xffffffffu);
    // FL10 cannot use a negative viewport. Crop the draw, preserving the full
    // lens dimensions and its pixel offset for the shader's shape coordinates.
    viewport.TopLeftX = static_cast<float>(visibleLeft);
    viewport.TopLeftY = static_cast<float>(visibleTop);
    viewport.Width = static_cast<float>(visibleWidth);
    viewport.Height = static_cast<float>(visibleHeight);
    context->RSSetViewports(1, &viewport);

    LiquidGlassShaderParameters optical {};
    optical.panelSize[0] = static_cast<float>(region.width) -
        region.markerInsetX * 2.0f;
    optical.panelSize[1] = static_cast<float>(region.height);
    optical.backdropTexelSize[0] = 1.0f / width;
    optical.backdropTexelSize[1] = 1.0f / height;
    optical.cornerRadius = std::max(region.cornerRadius, 0.0f);
    optical.edgeThickness = std::max(region.edgeThickness, 1.0f);
    optical.refractionStrength = std::max(region.refractionStrength, 0.0f);
    optical.dispersionStrength = std::max(region.dispersionStrength, 0.0f);
    optical.blurRadius = std::max(region.blurRadius, 0.0f);
    optical.rimStrength = std::max(region.rimStrength, 0.0f);
    optical.lightPhase = region.lightPhase;
    optical.opacity = std::clamp(region.opacity, 0.0f, 1.0f);
    optical.tintColour[0] = std::clamp(region.tintRed, 0.0f, 1.0f);
    optical.tintColour[1] = std::clamp(region.tintGreen, 0.0f, 1.0f);
    optical.tintColour[2] = std::clamp(region.tintBlue, 0.0f, 1.0f);
    optical.tintStrength = std::clamp(region.tintAlpha, 0.0f, 1.0f);
    optical.cornerExponent = std::clamp(region.cornerExponent, 2.0f, 6.0f);
    optical.backdropUvOrigin[0] = static_cast<float>(localX + offsetX - left) / width;
    optical.backdropUvOrigin[1] = static_cast<float>(localY + offsetY - top) / height;
    optical.backdropUvScale[0] = static_cast<float>(visibleWidth) / width;
    optical.backdropUvScale[1] = static_cast<float>(visibleHeight) / height;
    const float radii[] = { region.topLeftRadius, region.topRightRadius,
        region.bottomRightRadius, region.bottomLeftRadius };
    for (std::size_t index = 0; index < 4u; ++index)
        optical.cornerRadii[index] = radii[index] >= 0.0f ?
            radii[index] : optical.cornerRadius;
    optical.squircleAmount = std::clamp(region.squircleAmount, 0.0f, 1.0f);
    optical.notchAmount = std::clamp(region.notchAmount, 0.0f, 1.0f);
    optical.notchDepth = std::max(region.notchDepth, 0.0f);
    optical.shape = static_cast<float>(region.shape);
    optical.panelOrigin[0] = region.markerInsetX - static_cast<float>(offsetX);
    optical.panelOrigin[1] = -static_cast<float>(offsetY);
    optical.drawSize[0] = static_cast<float>(visibleWidth);
    optical.drawSize[1] = static_cast<float>(visibleHeight);
    optical.contourCount = region.contourCount;
    optical.hasExclusion = region.hasExclusion ? 1u : 0u;
    const auto capByte = [](float value) { return static_cast<std::uint32_t>(
        std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f)); };
    optical.paintCapGradient = region.paintCapGradient ? (1u |
        (capByte(region.capColour[0]) << 8u) | (capByte(region.capColour[1]) << 16u) |
        (capByte(region.capColour[2]) << 24u)) : 0u;
    for (std::size_t index = 0u; index < region.capGradient.size(); ++index)
        optical.capGradient[index] = std::clamp(region.capGradient[index], 0.0f, 1.0f);
    std::copy(region.viewportRect.begin(), region.viewportRect.end(), optical.viewportRect);
    std::copy(region.viewportFade.begin(), region.viewportFade.end(), optical.viewportFade);
    const auto packContour = [](const LiquidGlassRenderRegion::Contour& contour,
        LiquidGlassContourParameters& packed) {
        packed.rect[0] = contour.x;
        packed.rect[1] = contour.y;
        packed.rect[2] = contour.width;
        packed.rect[3] = contour.height;
        const float maximumRadius = std::min(contour.width, contour.height) * 0.5f;
        const float radii[] = { contour.topLeftRadius, contour.topRightRadius,
            contour.bottomRightRadius, contour.bottomLeftRadius };
        for (std::size_t index = 0u; index < 4u; ++index)
            packed.radii[index] = std::clamp(radii[index] > 0.0f ? radii[index] :
                contour.cornerRadius, 0.0f, maximumRadius);
        packed.params[0] = std::clamp(contour.squircleAmount, 0.0f, 1.0f);
        packed.params[1] = std::clamp(contour.squirclePower, 2.0f, 5.0f);
        packed.params[2] = std::clamp(contour.notchAmount, 0.0f, 1.0f);
        packed.params[3] = std::max(contour.notchDepth, 0.0f);
        packed.kind[0] = static_cast<float>(contour.shape);
        packed.kind[1] = std::max(contour.cornerRadius, 0.0f);
    };
    for (std::uint32_t index = 0u; index < region.contourCount; ++index)
        packContour(region.contours[index], optical.contours[index]);
    if (region.hasExclusion) packContour(region.exclusion, optical.exclusion);
    context->UpdateSubresource(opticalParameters_.Get(), 0, nullptr,
                               &optical, 0, 0);
    context->PSSetConstantBuffers(0, 1, opticalParameters_.GetAddressOf());
    ID3D11ShaderResourceView* sources[2] = {
        snapshotView_.Get(), blurredView
    };
    context->PSSetShaderResources(0, 2, sources);
    context->PSSetShader(optical_.Get(), nullptr, 0);
    if (clip)
    {
        context->RSSetState(clippedRasteriser_.Get());
        context->RSSetScissorRects(1, &visibleClip);
    }
    context->Draw(3, 0);
    ID3D11ShaderResourceView* empty[2] = { nullptr, nullptr };
    context->PSSetShaderResources(0, 2, empty);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}

bool LiquidGlassD3D11::composite_ui(ID3D11DeviceContext* context,
    ID3D11ShaderResourceView* ui, ID3D11RenderTargetView* target,
    std::int32_t x, std::int32_t y, std::uint32_t width,
    std::uint32_t height)
{
    if (!context || !ui || !target || !contextState_ ||
        width == 0 || height == 0)
    {
        return false;
    }
    ComPtr<ID3D11DeviceContext1> context1;
    if (FAILED(context->QueryInterface(IID_PPV_ARGS(&context1))))
    {
        return false;
    }
    ContextRestore restore { context1.Get(), nullptr };
    context1->SwapDeviceContextState(contextState_.Get(), &restore.previous);
    context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->VSSetShader(fullscreenVertex_.Get(), nullptr, 0);
    context->PSSetShader(uiPixel_.Get(), nullptr, 0);
    context->PSSetSamplers(0, 1, sampler_.GetAddressOf());
    context->PSSetShaderResources(0, 1, &ui);
    context->RSSetState(rasteriser_.Get());
    D3D11_VIEWPORT viewport {};
    viewport.TopLeftX = static_cast<float>(x);
    viewport.TopLeftY = static_cast<float>(y);
    viewport.Width = static_cast<float>(width);
    viewport.Height = static_cast<float>(height);
    viewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &viewport);
    context->OMSetRenderTargets(1, &target, nullptr);
    context->OMSetDepthStencilState(depthState_.Get(), 0);
    const FLOAT blendFactor[4] = { 0, 0, 0, 0 };
    context->OMSetBlendState(uiBlend_.Get(), blendFactor, 0xffffffffu);
    context->Draw(3, 0);
    ID3D11ShaderResourceView* empty = nullptr;
    context->PSSetShaderResources(0, 1, &empty);
    context->OMSetRenderTargets(0, nullptr, nullptr);
    return true;
}
}

#endif

#include "../../engine/src/platform/win32/liquid_glass_d3d11.h"

#include <dxgi1_2.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;
using vibrance::win32::LiquidGlassD3D11;
using vibrance::win32::LiquidGlassRenderRegion;

namespace vibrance::win32
{
struct LiquidGlassD3D11TestAccess
{
    static std::uint64_t budget(const LiquidGlassD3D11& renderer)
    {
        return renderer.scratchCacheBudget_;
    }
    static std::uint64_t retained_bytes(const LiquidGlassD3D11& renderer)
    {
        // Every benchmark surface uses four-byte RGBA, with three textures.
        std::uint64_t bytes = 0;
        for (const auto& cached : renderer.scratchCache_)
            if (cached.snapshot) bytes += std::uint64_t(cached.width) * cached.height * 12u;
        return bytes;
    }
};
}

int wmain(int argc, wchar_t** argv)
try
{
    if (argc != 2) return 2;
    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level {};
    if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, 0,
            nullptr, 0, D3D11_SDK_VERSION, &device, &level, &context))) return 3;
    ComPtr<IDXGIDevice> dxgi;
    ComPtr<IDXGIAdapter> adapter;
    DXGI_ADAPTER_DESC adapterDescription {};
    if (SUCCEEDED(device.As(&dxgi)) && SUCCEEDED(dxgi->GetAdapter(&adapter)) &&
        SUCCEEDED(adapter->GetDesc(&adapterDescription)))
        std::wcout << L"GPU adapter: " << adapterDescription.Description << L'\n';

    constexpr unsigned width = 2048, height = 3072;
    std::vector<std::uint32_t> pixels(width * height);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
            pixels[y * width + x] = 0xff000000u | ((x * 11u + y * 7u) & 255u) |
                (((x / 19u + y / 13u) & 255u) << 8u) | (((x + y * 3u) & 255u) << 16u);
    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = width; desc.Height = height;
    desc.MipLevels = desc.ArraySize = desc.SampleDesc.Count = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    const D3D11_SUBRESOURCE_DATA data { pixels.data(), width * 4u, 0 };
    ComPtr<ID3D11Texture2D> source, output;
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(device->CreateTexture2D(&desc, &data, &source)) ||
        FAILED(device->CreateTexture2D(&desc, nullptr, &output)) ||
        FAILED(device->CreateRenderTargetView(output.Get(), nullptr, &target))) return 4;
    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0; desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &readback))) return 4;
    const auto query = [&](D3D11_QUERY kind) {
        const D3D11_QUERY_DESC queryDesc { kind, 0 };
        ComPtr<ID3D11Query> value;
        if (FAILED(device->CreateQuery(&queryDesc, &value)))
            throw std::runtime_error("Could not create GPU timing query");
        return value;
    };
    const auto disjoint = query(D3D11_QUERY_TIMESTAMP_DISJOINT);
    const auto start = query(D3D11_QUERY_TIMESTAMP);
    const auto end = query(D3D11_QUERY_TIMESTAMP);
    const auto await = [&](ID3D11Query* value, void* result, UINT bytes) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        HRESULT status;
        while ((status = context->GetData(value, result, bytes,
                D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE)
        {
            if (std::chrono::steady_clock::now() >= deadline)
                throw std::runtime_error("GPU timing query timed out");
            std::this_thread::yield();
        }
        if (FAILED(status)) throw std::runtime_error("GPU timing query failed");
    };
    // Thirty distinct compact sizes fit even the minimum 32 MiB allowance.
    // This isolates the former eight-size allocation cliff from deliberate
    // byte-budget eviction in the larger notification stress cases below.
    {
        LiquidGlassD3D11 lens;
        if (!lens.initialise(device.Get(), argv[1])) return 5;
        const auto compactSweep = [&] {
            for (unsigned index = 0; index < 30u; ++index)
            {
                LiquidGlassRenderRegion compact;
                compact.x = compact.y = 600;
                compact.width = 80; compact.height = 32u + index;
                compact.lightPhase = -1;
                if (!lens.render(context.Get(), source.Get(), target.Get(), compact))
                    throw std::runtime_error("Compact cache regression render failed");
                if (vibrance::win32::LiquidGlassD3D11TestAccess::retained_bytes(lens) >
                    vibrance::win32::LiquidGlassD3D11TestAccess::budget(lens))
                    throw std::runtime_error("Scratch cache exceeded its existing byte allowance");
            }
        };
        for (unsigned sweep = 0; sweep < 4u; ++sweep) compactSweep();
        const auto warmAllocations = lens.scratch_allocation_count();
        for (unsigned sweep = 0; sweep < 4u; ++sweep) compactSweep();
        if (lens.scratch_allocation_count() != warmAllocations)
            throw std::runtime_error("Warm compact lenses must not allocate textures again when their sizes fit the byte allowance");
        std::cout << "Thirty-size bounded scratch regression: pass, allocations="
            << warmAllocations << ", retainedMiB="
            << double(vibrance::win32::LiquidGlassD3D11TestAccess::retained_bytes(lens)) /
                (1024.0 * 1024.0) << '\n';
    }
    for (const unsigned cards : { 15u, 30u })
    for (const unsigned layers : { 1u, 3u })
    {
        LiquidGlassD3D11 lens;
        if (!lens.initialise(device.Get(), argv[1])) return 5;
        std::vector<LiquidGlassRenderRegion> regions;
        for (unsigned index = 0; index < cards; ++index)
            for (unsigned layer = 0; layer < layers; ++layer)
            {
                LiquidGlassRenderRegion card;
                card.x = 80 + (index / 10u) * 480u + layer * 10u;
                card.y = 128 + (index % 10u) * 280u + layer * 7u;
                card.width = 376u - layer * 20u;
                card.height = 72u + index * 3u;
                card.cornerRadius = 24.0f - layer;
                card.lightPhase = -1.0f;
                if (layer)
                {
                    card.hasExclusion = true;
                    card.exclusion.x = -10; card.exclusion.y = -7;
                    card.exclusion.width = float(card.width + 20u);
                    card.exclusion.height = float(card.height);
                    card.exclusion.cornerRadius = card.cornerRadius + 1;
                    card.exclusion.shape = 3;
                }
                regions.push_back(card);
            }
        const auto draw = [&](unsigned frame) {
            const float clear[4] = { 0, 0, 0, 0 };
            context->ClearRenderTargetView(target.Get(), clear);
            for (unsigned index = 0; index <= regions.size(); ++index)
            {
                if (index == regions.size() / 2u)
                {
                    LiquidGlassRenderRegion island;
                    island.x = 1600; island.y = 256;
                    island.width = 176u + (frame % 4u) * 16u; island.height = 40;
                    island.lightPhase = -1;
                    if (!lens.render(context.Get(), source.Get(), target.Get(), island))
                        throw std::runtime_error("Island render failed");
                }
                if (index < regions.size() &&
                    !lens.render(context.Get(), source.Get(), target.Get(), regions[index]))
                    throw std::runtime_error("Card render failed");
            }
        };
        for (unsigned frame = 0; frame < 8; ++frame) draw(frame);
        const auto warmAllocations = lens.scratch_allocation_count();
        std::vector<double> cpuTimes, gpuTimes;
        for (unsigned sample = 0; sample < 24; ++sample)
        {
            context->Begin(disjoint.Get()); context->End(start.Get());
            const auto began = std::chrono::steady_clock::now();
            draw(sample);
            cpuTimes.push_back(std::chrono::duration<double, std::milli>(
                std::chrono::steady_clock::now() - began).count());
            context->End(end.Get()); context->End(disjoint.Get()); context->Flush();
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing {};
            UINT64 first = 0, last = 0;
            await(disjoint.Get(), &timing, sizeof(timing));
            await(start.Get(), &first, sizeof(first)); await(end.Get(), &last, sizeof(last));
            if (timing.Disjoint || !timing.Frequency) throw std::runtime_error("Disjoint GPU timing");
            gpuTimes.push_back(double(last - first) * 1000.0 / double(timing.Frequency));
        }
        context->CopyResource(readback.Get(), output.Get());
        D3D11_MAPPED_SUBRESOURCE mapped {};
        if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 6;
        std::uint64_t hash = 1469598103934665603ull;
        for (unsigned y = 0; y < height; ++y)
        {
            const auto* row = static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch;
            for (unsigned x = 0; x < width * 4u; ++x) hash = (hash ^ row[x]) * 1099511628211ull;
        }
        context->Unmap(readback.Get(), 0);
        std::sort(cpuTimes.begin(), cpuTimes.end()); std::sort(gpuTimes.begin(), gpuTimes.end());
        std::cout << std::fixed << std::setprecision(3) << "Cards=" << cards << " layers=" << layers
            << " lenses=" << regions.size() + 1u << " budgetMiB="
            << vibrance::win32::LiquidGlassD3D11TestAccess::budget(lens) / (1024u * 1024u)
            << " warmAlloc=" << warmAllocations << " measuredAlloc="
            << lens.scratch_allocation_count() - warmAllocations
            << " CPUmedian/p95=" << cpuTimes[12] << '/' << cpuTimes[22]
            << " GPUmedian/p95=" << gpuTimes[12] << '/' << gpuTimes[22]
            << " reuse=" << lens.blur_reuse_count() << " changed=" << lens.blur_change_count()
            << " retainedMiB="
            << double(vibrance::win32::LiquidGlassD3D11TestAccess::retained_bytes(lens)) /
                (1024.0 * 1024.0)
            << " hash=" << std::hex << hash << std::dec << '\n';
    }
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return 1;
}

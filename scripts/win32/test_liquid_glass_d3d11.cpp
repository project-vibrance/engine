#include "../../engine/src/platform/win32/liquid_glass_d3d11.h"

#include <d3d11.h>
#include <dxgi1_2.h>
#include <wrl/client.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <thread>
#include <vector>

using Microsoft::WRL::ComPtr;

namespace
{
bool temporal_reuse_regression(ID3D11Device* device, ID3D11DeviceContext* context,
    const std::wstring& shaderDirectory)
{
    if (GetFileAttributesW((shaderDirectory + L"\\liquid_glass_compare.cso").c_str()) ==
            INVALID_FILE_ATTRIBUTES)
    {
        std::cout << "Exact temporal blur reuse: unavailable in this bundle\n";
        return true;
    }
    for (const auto format : { DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, DXGI_FORMAT_B8G8R8A8_UNORM,
            DXGI_FORMAT_R16G16B16A16_FLOAT })
    {
        vibrance::win32::LiquidGlassD3D11 temporal, reference;
        if (!temporal.initialise(device, shaderDirectory) ||
            !reference.initialise(device, shaderDirectory)) return false;
        constexpr UINT width = 320, height = 192;
        const UINT rowBytes = width * (format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8u : 4u);
        D3D11_TEXTURE2D_DESC description {};
        description.Width = width;
        description.Height = height;
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        description.Format = format;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11Texture2D> source, actual, expected, actualReadback, expectedReadback;
        ComPtr<ID3D11RenderTargetView> sourceTarget, actualTarget, expectedTarget;
        if (FAILED(device->CreateTexture2D(&description, nullptr, &source)) ||
            FAILED(device->CreateTexture2D(&description, nullptr, &actual)) ||
            FAILED(device->CreateTexture2D(&description, nullptr, &expected)) ||
            FAILED(device->CreateRenderTargetView(source.Get(), nullptr, &sourceTarget)) ||
            FAILED(device->CreateRenderTargetView(actual.Get(), nullptr, &actualTarget)) ||
            FAILED(device->CreateRenderTargetView(expected.Get(), nullptr, &expectedTarget)))
            return false;
        description.Usage = D3D11_USAGE_STAGING;
        description.BindFlags = 0;
        description.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        if (FAILED(device->CreateTexture2D(&description, nullptr, &actualReadback)) ||
            FAILED(device->CreateTexture2D(&description, nullptr, &expectedReadback))) return false;
        const D3D11_QUERY_DESC predicateDescription { D3D11_QUERY_OCCLUSION_PREDICATE, 0 };
        ComPtr<ID3D11Predicate> nativePredicate, outerQuery;
        if (FAILED(device->CreatePredicate(&predicateDescription, &nativePredicate)) ||
            FAILED(device->CreatePredicate(&predicateDescription, &outerQuery))) return false;
        context->Begin(nativePredicate.Get());
        context->End(nativePredicate.Get());
        const FLOAT targetColour[4] = { 0.1f, 0.2f, 0.3f, 0.75f };
        std::uint64_t earlierReuses = 0, earlierChanges = 0, earlierComparisons = 0;
        for (UINT frame = 0; frame < 32u; ++frame)
        {
            if (frame == 24u)
            {
                earlierReuses = temporal.blur_reuse_count();
                earlierChanges = temporal.blur_change_count();
                earlierComparisons = temporal.blur_comparison_count();
                if (earlierReuses == 0 || earlierChanges == 0 ||
                    !temporal.initialise(device, shaderDirectory)) return false;
            }
            if (frame == 0 || frame == 24u || (frame >= 8u && frame < 16u))
            {
                const FLOAT colour[4] = { (frame & 1u) ? 0.75f : 1.5f,
                    (frame & 1u) ? 0.2f : 0.5f, 0.125f,
                    frame >= 12u ? 0.0f : ((frame & 1u) ? 0.5f : 1.0f) };
                context->ClearRenderTargetView(sourceTarget.Get(), colour);
            }
            if (frame == 26u)
            {
                // The changed texel is outside the visible lens but inside
                // the Gaussian input halo. It must invalidate retained blur.
                const D3D11_BOX halo { 31, 96, 0, 32, 97, 1 };
                const std::uint64_t pixel = format == DXGI_FORMAT_R16G16B16A16_FLOAT ?
                    0x3c00340038004000ull : 0xffe04020u;
                context->UpdateSubresource(source.Get(), 0, &halo, &pixel,
                    format == DXGI_FORMAT_R16G16B16A16_FLOAT ? 8 : 4, 0);
            }
            if (frame == 29u || frame == 30u)
            {
                const FLOAT signedColour[4] = { -0.5f, frame == 29u ? -0.0f : 0.0f, 2.0f, 1.0f };
                context->ClearRenderTargetView(sourceTarget.Get(), signedColour);
            }
            vibrance::win32::LiquidGlassRenderRegion region;
            region.x = 32;
            region.y = 24;
            region.width = 256;
            region.height = 144;
            region.lightPhase = frame * 0.17f;
            region.blurRadius = frame == 16u ? 0.0f : frame >= 17u ? 30.0f : 7.0f;
            const D3D11_RECT clip { 68, 41, 191, 119 };
            const D3D11_RECT* activeClip = frame == 7u || frame == 10u ? &clip : nullptr;
            if (frame >= 20u && frame < 24u)
            {
                region.viewportRect = { 0, 40.25f, 256, 56.5f };
                region.viewportFade = { 8, 12, 0, 1 };
            }
            if (frame >= 24u) region.refractionStrength = 0.0f;
            const LONG sourceOriginX = frame == 27u ? 1 : frame == 28u ? -1 : 0;
            // Fresh resources make this independent of the optimisation
            // under test, including its per-scratch-set history.
            if (!reference.initialise(device, shaderDirectory)) return false;
            context->ClearRenderTargetView(expectedTarget.Get(), targetColour);
            context->ClearRenderTargetView(actualTarget.Get(), targetColour);
            if (!reference.render(context, source.Get(), expectedTarget.Get(), region,
                    sourceOriginX, 0, activeClip)) return false;
            const bool nativeState = frame == 5u || frame == 6u;
            const BOOL predicateValue = frame == 5u ? FALSE : TRUE;
            if (nativeState) context->SetPredication(nativePredicate.Get(), predicateValue);
            if (frame == 4u) context->Begin(outerQuery.Get());
            if (!temporal.render(context, source.Get(), actualTarget.Get(), region,
                    sourceOriginX, 0, activeClip)) return false;
            if (frame == 4u) context->End(outerQuery.Get());
            if (nativeState)
            {
                ComPtr<ID3D11Predicate> restored;
                BOOL restoredValue = FALSE;
                context->GetPredication(&restored, &restoredValue);
                context->SetPredication(nullptr, FALSE);
                if (restored.Get() != nativePredicate.Get() || restoredValue != predicateValue)
                    return false;
            }
            context->CopyResource(actualReadback.Get(), actual.Get());
            context->CopyResource(expectedReadback.Get(), expected.Get());
            D3D11_MAPPED_SUBRESOURCE actualMap {}, expectedMap {};
            if (FAILED(context->Map(actualReadback.Get(), 0, D3D11_MAP_READ, 0, &actualMap)))
                return false;
            if (FAILED(context->Map(expectedReadback.Get(), 0, D3D11_MAP_READ, 0, &expectedMap)))
            {
                context->Unmap(actualReadback.Get(), 0);
                return false;
            }
            bool same = true;
            for (UINT y = 0; y < height; ++y)
                if (std::memcmp(static_cast<const std::uint8_t*>(actualMap.pData) + y * actualMap.RowPitch,
                        static_cast<const std::uint8_t*>(expectedMap.pData) + y * expectedMap.RowPitch,
                        rowBytes) != 0) same = false;
            context->Unmap(actualReadback.Get(), 0);
            context->Unmap(expectedReadback.Get(), 0);
            if (!same)
            {
                std::cerr << "Temporal blur differs from fresh reference: format "
                          << format << ", frame " << frame << '\n';
                return false;
            }
        }
        std::cout << "Temporal blur format " << format << ": "
                  << earlierReuses + temporal.blur_reuse_count() << " unchanged, "
                  << earlierChanges + temporal.blur_change_count() << " changed, "
                  << earlierComparisons + temporal.blur_comparison_count() << " comparisons\n";
        if (temporal.blur_reuse_count() == 0 || temporal.blur_change_count() == 0) return false;
    }
    return true;
}

bool scratch_cache_regression(vibrance::win32::LiquidGlassD3D11& lens,
    ID3D11Device* device, ID3D11DeviceContext* context)
{
    ComPtr<ID3D11Texture2D> sources[2];
    ComPtr<ID3D11Texture2D> outputs[2];
    ComPtr<ID3D11RenderTargetView> targets[2];
    vibrance::win32::LiquidGlassRenderRegion regions[2];
    for (UINT monitor = 0; monitor < 2u; ++monitor)
    {
        D3D11_TEXTURE2D_DESC description {};
        description.Width = 1920;
        description.Height = 1080;
        description.MipLevels = description.ArraySize = description.SampleDesc.Count = 1;
        description.Format = monitor == 0 ? DXGI_FORMAT_R16G16B16A16_FLOAT :
            DXGI_FORMAT_R8G8B8A8_UNORM;
        description.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        ComPtr<ID3D11RenderTargetView> sourceTarget;
        if (FAILED(device->CreateTexture2D(&description, nullptr, &sources[monitor])) ||
            FAILED(device->CreateRenderTargetView(sources[monitor].Get(), nullptr, &sourceTarget)) ||
            FAILED(device->CreateTexture2D(&description, nullptr, &outputs[monitor])) ||
            FAILED(device->CreateRenderTargetView(outputs[monitor].Get(), nullptr, &targets[monitor])))
            return false;
        const FLOAT backdrop[4] = { 0.5f, 0.75f, 1.0f, 1.0f };
        context->ClearRenderTargetView(sourceTarget.Get(), backdrop);
        regions[monitor].x = 480;
        regions[monitor].y = 270;
        regions[monitor].width = monitor == 0 ? 960 : 800;
        regions[monitor].height = monitor == 0 ? 540 : 400;
        regions[monitor].blurRadius = monitor == 0 ? 30.0f : 7.0f;
    }
    std::uint64_t warmAllocations = 0;
    for (UINT frame = 0; frame < 6u; ++frame)
    {
        for (UINT monitor = 0; monitor < 2u; ++monitor)
            if (!lens.render(context, sources[monitor].Get(), targets[monitor].Get(),
                    regions[monitor])) return false;
        if (frame == 0) warmAllocations = lens.scratch_allocation_count();
    }
    const auto extraAllocations = lens.scratch_allocation_count() - warmAllocations;
    std::cout << "Alternating HDR/SDR scratch allocations after warm-up: "
              << extraAllocations << '\n';
    return extraAllocations == 0 && SUCCEEDED(device->GetDeviceRemovedReason());
}

bool benchmark(vibrance::win32::LiquidGlassD3D11& lens,
    ID3D11Device* device, ID3D11DeviceContext* context)
{
    const auto fail = [](const char* stage) {
        std::cerr << "GPU benchmark failed: " << stage << '\n';
        return false;
    };
    constexpr UINT width = 1920u;
    constexpr UINT height = 1080u;
    std::vector<std::uint32_t> pixels(width * height);
    for (UINT y = 0u; y < height; ++y)
        for (UINT x = 0u; x < width; ++x)
            pixels[y * width + x] = ((x / 8u + y / 8u) & 1u) ?
                0xffff6040u : 0xff1848e0u;
    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1u;
    desc.ArraySize = 1u;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1u;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data { pixels.data(), width * sizeof(std::uint32_t), 0u };
    ComPtr<ID3D11Texture2D> source;
    ComPtr<ID3D11Texture2D> output;
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(device->CreateTexture2D(&desc, &data, &source)) ||
        FAILED(device->CreateTexture2D(&desc, nullptr, &output)) ||
        FAILED(device->CreateRenderTargetView(output.Get(), nullptr, &target)))
        return fail("surface creation");
    D3D11_QUERY_DESC queryDesc { D3D11_QUERY_TIMESTAMP_DISJOINT, 0u };
    ComPtr<ID3D11Query> disjoint;
    ComPtr<ID3D11Query> start;
    ComPtr<ID3D11Query> end;
    if (FAILED(device->CreateQuery(&queryDesc, &disjoint))) return fail("disjoint query creation");
    queryDesc.Query = D3D11_QUERY_TIMESTAMP;
    if (FAILED(device->CreateQuery(&queryDesc, &start)) ||
        FAILED(device->CreateQuery(&queryDesc, &end))) return fail("timestamp query creation");

    for (const bool changing : { false, true })
    for (const bool compact : { false, true })
    for (const bool masked : { false, true })
    for (const bool clipped : { false, true })
    for (const float radius : { 0.0f, 7.0f, 30.0f })
    {
        vibrance::win32::LiquidGlassRenderRegion region;
        region.x = 480;
        region.y = 270;
        region.width = compact ? 400u : 960u;
        region.height = compact ? 96u : 540u;
        region.blurRadius = radius;
        if (masked)
        {
            region.viewportRect = { 0.0f, region.height * 0.4f,
                static_cast<float>(region.width), region.height * 0.2f };
            region.viewportFade = { 0, 0, 1, 1 };
        }
        const D3D11_RECT clip { region.x + static_cast<LONG>(region.width / 2),
            region.y + static_cast<LONG>(region.height / 2),
            region.x + static_cast<LONG>(region.width / 2) + 48,
            region.y + static_cast<LONG>(region.height / 2) + 48 };
        const D3D11_BOX dirtyBox { static_cast<UINT>(clip.left), static_cast<UINT>(clip.top), 0,
            static_cast<UINT>(clip.left + 16), static_cast<UINT>(clip.top + 16), 1 };
        const auto changeBackdrop = [&](UINT frame) {
            if (!changing) return;
            std::array<std::uint32_t, 16 * 16> changedPixels;
            changedPixels.fill((frame & 1u) ? 0xffd06020u : 0xff40a0f0u);
            context->UpdateSubresource(source.Get(), 0, &dirtyBox,
                changedPixels.data(), 16 * sizeof(std::uint32_t), 0);
        };
        for (UINT warm = 0u; warm < 8u; ++warm)
        {
            changeBackdrop(warm);
            if (!lens.render(context, source.Get(), target.Get(), region,
                    0, 0, clipped ? &clip : nullptr)) return fail("warm-up render");
        }
        std::vector<double> gpuMilliseconds;
        std::vector<double> cpuMilliseconds;
        for (UINT sample = 0u; sample < 31u; ++sample)
        {
            changeBackdrop(sample + 8u);
            context->Begin(disjoint.Get());
            context->End(start.Get());
            const auto cpuStart = std::chrono::steady_clock::now();
            if (!lens.render(context, source.Get(), target.Get(), region,
                    0, 0, clipped ? &clip : nullptr)) return fail("timed render");
            const auto cpuEnd = std::chrono::steady_clock::now();
            context->End(end.Get());
            context->End(disjoint.Get());
            context->Flush();
            D3D11_QUERY_DATA_TIMESTAMP_DISJOINT timing {};
            const auto deadline = std::chrono::steady_clock::now() +
                std::chrono::seconds(10);
            const auto awaitQuery = [&](ID3D11Query* query, void* value, UINT bytes) {
                HRESULT status = S_FALSE;
                while ((status = context->GetData(query, value, bytes,
                        D3D11_ASYNC_GETDATA_DONOTFLUSH)) == S_FALSE)
                {
                    if (std::chrono::steady_clock::now() >= deadline) return false;
                    std::this_thread::yield();
                }
                return SUCCEEDED(status);
            };
            if (!awaitQuery(disjoint.Get(), &timing, sizeof(timing)))
                return fail("disjoint query result or timeout");
            UINT64 first = 0u;
            UINT64 last = 0u;
            // Some iGPU drivers publish the disjoint result before each
            // individual timestamp becomes available. Wait for all three.
            if (!awaitQuery(start.Get(), &first, sizeof(first)) ||
                !awaitQuery(end.Get(), &last, sizeof(last)))
                return fail("timestamp query result or timeout");
            if (timing.Disjoint || !timing.Frequency) continue;
            gpuMilliseconds.push_back(static_cast<double>(last - first) * 1000.0 /
                static_cast<double>(timing.Frequency));
            cpuMilliseconds.push_back(std::chrono::duration<double, std::milli>(
                cpuEnd - cpuStart).count());
        }
        if (gpuMilliseconds.empty()) return fail("all timestamps disjoint");
        std::sort(gpuMilliseconds.begin(), gpuMilliseconds.end());
        std::sort(cpuMilliseconds.begin(), cpuMilliseconds.end());
        std::cout << std::fixed << std::setprecision(3)
                  << "Benchmark " << region.width << 'x' << region.height << ", radius " << radius
                  << (clipped ? ", 48x48 clip" : ", full draw")
                  << (masked ? ", 20% viewport" : "")
                  << (changing ? ", changing backdrop" : "")
                  << ": GPU median " << gpuMilliseconds[gpuMilliseconds.size() / 2u]
                  << " ms, CPU submission median "
                  << cpuMilliseconds[cpuMilliseconds.size() / 2u] << " ms\n";
    }
    return true;
}
}

int wmain(int argc, wchar_t** argv)
{
    bool runBenchmark = false;
    bool benchmarkOnly = false;
    unsigned adapterIndex = std::numeric_limits<unsigned>::max();
    const wchar_t* previewPath = nullptr;
    const wchar_t* capturePath = nullptr;
    const wchar_t* comparisonPath = nullptr;
    bool validArguments = argc >= 2;
    for (int argument = 2; argument < argc && validArguments; ++argument)
    {
        const std::wstring option(argv[argument]);
        if (option == L"--benchmark") runBenchmark = true;
        else if (option == L"--benchmark-only") runBenchmark = benchmarkOnly = true;
        else if (option == L"--adapter" && argument + 1 < argc)
        {
            const std::wstring index(argv[++argument]);
            std::size_t consumed = 0u;
            try
            {
                const auto parsed = std::stoul(index, &consumed);
                validArguments = consumed == index.size() && parsed < 32u;
                if (validArguments) adapterIndex = static_cast<unsigned>(parsed);
            }
            catch (...) { validArguments = false; }
        }
        else if ((option == L"--capture" || option == L"--compare") && argument + 1 < argc)
        {
            if (option == L"--capture") capturePath = argv[++argument];
            else comparisonPath = argv[++argument];
        }
        else if (!previewPath && !option.starts_with(L"--")) previewPath = argv[argument];
        else validArguments = false;
    }
    if (!validArguments || (capturePath && comparisonPath))
    {
        std::wcerr << L"Usage: test_liquid_glass_d3d11 shader-directory [preview.ppm] "
            L"[--benchmark | --benchmark-only] [--adapter index] [--capture file | --compare file]\n";
        return 2;
    }
    std::ofstream capture;
    std::ifstream comparison;
    if (capturePath) capture.open(capturePath, std::ios::binary);
    if (comparisonPath) comparison.open(comparisonPath, std::ios::binary);
    if ((capturePath && !capture) || (comparisonPath && !comparison)) return 55;
    std::size_t comparedFrames = 0u;
    std::size_t differingChannels = 0u;
    int maximumChannelDifference = 0;
    bool referenceFinished = false;
    const auto checkReference = [&](const std::vector<std::uint32_t>& actual) {
        if (referenceFinished) return true;
        const auto byteCount = static_cast<std::streamsize>(
            actual.size() * sizeof(std::uint32_t));
        if (capturePath)
        {
            capture.write(reinterpret_cast<const char*>(actual.data()), byteCount);
            return static_cast<bool>(capture);
        }
        if (!comparisonPath) return true;
        std::vector<std::uint32_t> expected(actual.size());
        if (!comparison.read(reinterpret_cast<char*>(expected.data()), byteCount)) return false;
        ++comparedFrames;
        for (std::size_t pixel = 0u; pixel < actual.size(); ++pixel)
            for (UINT channel = 0u; channel < 4u; ++channel)
            {
                const int difference = std::abs(static_cast<int>(
                    (actual[pixel] >> (channel * 8u)) & 0xffu) -
                    static_cast<int>((expected[pixel] >> (channel * 8u)) & 0xffu));
                maximumChannelDifference = std::max(maximumChannelDifference, difference);
                if (difference != 0) ++differingChannels;
            }
        return maximumChannelDifference <= 1;
    };
    constexpr UINT width = 512;
    constexpr UINT height = 256;
    std::vector<std::uint32_t> pixels(width * height);
    for (UINT y = 0; y < height; ++y)
    {
        for (UINT x = 0; x < width; ++x)
        {
            const bool stripe = ((x / 8u) & 1u) != 0u;
            pixels[y * width + x] = stripe ? 0xffff6040u : 0xff1848e0u;
        }
    }

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    D3D_FEATURE_LEVEL level {};
    const D3D_FEATURE_LEVEL requested[] = { D3D_FEATURE_LEVEL_11_0 };
    ComPtr<IDXGIAdapter1> selectedAdapter;
    if (adapterIndex != std::numeric_limits<unsigned>::max())
    {
        ComPtr<IDXGIFactory1> factory;
        if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) ||
            FAILED(factory->EnumAdapters1(adapterIndex, &selectedAdapter)))
        {
            std::cerr << "Requested DXGI adapter is unavailable\n";
            return 3;
        }
    }
    HRESULT result = D3D11CreateDevice(
        selectedAdapter.Get(), selectedAdapter ? D3D_DRIVER_TYPE_UNKNOWN :
            D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested, 1,
        D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(result) && !selectedAdapter)
    {
        result = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0,
                                   requested, 1, D3D11_SDK_VERSION, &device,
                                   &level, &context);
    }
    if (FAILED(result))
    {
        std::cerr << "D3D11CreateDevice failed: " << std::hex << result << '\n';
        return 3;
    }
    ComPtr<IDXGIDevice> dxgiDevice;
    ComPtr<IDXGIAdapter> activeAdapter;
    DXGI_ADAPTER_DESC adapterDescription {};
    if (SUCCEEDED(device.As(&dxgiDevice)) &&
        SUCCEEDED(dxgiDevice->GetAdapter(&activeAdapter)) &&
        SUCCEEDED(activeAdapter->GetDesc(&adapterDescription)))
        std::wcout << L"GPU adapter: " << adapterDescription.Description << L'\n';

    D3D11_TEXTURE2D_DESC desc {};
    desc.Width = width;
    desc.Height = height;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
    D3D11_SUBRESOURCE_DATA data {};
    data.pSysMem = pixels.data();
    data.SysMemPitch = width * sizeof(std::uint32_t);
    ComPtr<ID3D11Texture2D> backdrop;
    result = device->CreateTexture2D(&desc, &data, &backdrop);
    if (FAILED(result))
    {
        std::cerr << "CreateTexture2D failed\n";
        return 4;
    }
    ComPtr<ID3D11RenderTargetView> target;
    if (FAILED(device->CreateRenderTargetView(backdrop.Get(), nullptr, &target)))
    {
        return 5;
    }

    vibrance::win32::LiquidGlassD3D11 lens;
    if (!lens.initialise(device.Get(), argv[1]))
    {
        std::cerr << "Lens initialisation failed\n";
        return 6;
    }
    if (benchmarkOnly) return benchmark(lens, device.Get(), context.Get()) ? 0 : 54;
    vibrance::win32::LiquidGlassRenderRegion region;
    region.x = 64;
    region.y = 32;
    region.width = 384;
    region.height = 192;
    D3D11_VIEWPORT originalViewport {};
    originalViewport.TopLeftX = 3.0f;
    originalViewport.TopLeftY = 4.0f;
    originalViewport.Width = 100.0f;
    originalViewport.Height = 80.0f;
    originalViewport.MaxDepth = 1.0f;
    context->RSSetViewports(1, &originalViewport);
    ID3D11RenderTargetView* originalTarget = target.Get();
    context->OMSetRenderTargets(1, &originalTarget, nullptr);
    if (!lens.render(context.Get(), backdrop.Get(), target.Get(), region))
    {
        std::cerr << "Lens render failed\n";
        return 7;
    }
    UINT viewportCount = 1;
    D3D11_VIEWPORT restoredViewport {};
    context->RSGetViewports(&viewportCount, &restoredViewport);
    ComPtr<ID3D11RenderTargetView> restoredTarget;
    context->OMGetRenderTargets(1, &restoredTarget, nullptr);
    if (viewportCount != 1 || restoredViewport.TopLeftX != 3.0f ||
        restoredViewport.TopLeftY != 4.0f ||
        restoredViewport.Width != 100.0f ||
        restoredViewport.Height != 80.0f ||
        restoredTarget.Get() != target.Get())
    {
        std::cerr << "D3D11 context state was not restored\n";
        return 11;
    }

    desc.Usage = D3D11_USAGE_STAGING;
    desc.BindFlags = 0;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    ComPtr<ID3D11Texture2D> readback;
    if (FAILED(device->CreateTexture2D(&desc, nullptr, &readback)))
    {
        return 8;
    }
    context->CopyResource(readback.Get(), backdrop.Get());
    D3D11_MAPPED_SUBRESOURCE mapped {};
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped)))
    {
        return 9;
    }
    std::size_t insideChanged = 0;
    std::size_t outsideChanged = 0;
    std::vector<std::uint32_t> opticalPixels(width * height);
    std::ofstream preview;
    if (previewPath)
    {
        preview.open(previewPath, std::ios::binary);
        preview << "P6\n" << width << ' ' << height << "\n255\n";
    }
    for (UINT y = 0; y < height; ++y)
    {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < width; ++x)
        {
            opticalPixels[y * width + x] = row[x];
            if (preview)
            {
                const char rgb[3] = {
                    static_cast<char>(row[x] & 0xffu),
                    static_cast<char>((row[x] >> 8u) & 0xffu),
                    static_cast<char>((row[x] >> 16u) & 0xffu)
                };
                preview.write(rgb, sizeof(rgb));
            }
            if (row[x] == pixels[y * width + x])
            {
                continue;
            }
            const bool inside = x >= static_cast<UINT>(region.x) &&
                x < static_cast<UINT>(region.x) + region.width &&
                y >= static_cast<UINT>(region.y) &&
                y < static_cast<UINT>(region.y) + region.height;
            ++(inside ? insideChanged : outsideChanged);
        }
    }
    context->Unmap(readback.Get(), 0);
    if (!checkReference(opticalPixels)) return 56;
    std::cout << "Changed inside: " << insideChanged
              << ", outside: " << outsideChanged << '\n';
    if (insideChanged <= 1000 || outsideChanged != 0) return 10;

    // The native compositor clip applies only to the final optical draw.
    // Sampling and blur must still match the full, unclipped reference.
    context->UpdateSubresource(backdrop.Get(), 0, nullptr, pixels.data(),
        width * sizeof(std::uint32_t), 0);
    const D3D11_VIEWPORT nativeViewports[2] = {
        { 11.5f, 9.25f, 123.5f, 87.75f, 0.125f, 0.875f },
        { 191.0f, 17.0f, 96.0f, 40.0f, 0.25f, 0.75f }
    };
    const D3D11_RECT nativeScissors[2] = {
        { 8, 12, 96, 74 }, { 101, 92, 251, 173 }
    };
    D3D11_RASTERIZER_DESC nativeRasteriserDesc {};
    nativeRasteriserDesc.FillMode = D3D11_FILL_WIREFRAME;
    nativeRasteriserDesc.CullMode = D3D11_CULL_FRONT;
    nativeRasteriserDesc.DepthClipEnable = TRUE;
    nativeRasteriserDesc.ScissorEnable = TRUE;
    ComPtr<ID3D11RasterizerState> nativeRasteriser;
    if (FAILED(device->CreateRasterizerState(&nativeRasteriserDesc,
            &nativeRasteriser))) return 16;
    context->RSSetState(nativeRasteriser.Get());
    context->RSSetViewports(2, nativeViewports);
    context->RSSetScissorRects(2, nativeScissors);
    const D3D11_RECT opticalClip {
        region.x - 24, region.y + 40,
        region.x + static_cast<LONG>(region.width / 2u),
        region.y + static_cast<LONG>(region.height) + 24
    };
    if (!lens.render(context.Get(), backdrop.Get(), target.Get(), region,
            0, 0, &opticalClip)) return 17;

    UINT nativeViewportCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    UINT nativeScissorCount = D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE;
    D3D11_VIEWPORT restoredNativeViewports[
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] {};
    D3D11_RECT restoredNativeScissors[
        D3D11_VIEWPORT_AND_SCISSORRECT_OBJECT_COUNT_PER_PIPELINE] {};
    ComPtr<ID3D11RasterizerState> restoredRasteriser;
    context->RSGetViewports(&nativeViewportCount, restoredNativeViewports);
    context->RSGetScissorRects(&nativeScissorCount, restoredNativeScissors);
    context->RSGetState(&restoredRasteriser);
    context->OMGetRenderTargets(1, &restoredTarget, nullptr);
    bool nativeStateRestored = nativeViewportCount == 2 &&
        nativeScissorCount == 2 &&
        restoredRasteriser.Get() == nativeRasteriser.Get() &&
        restoredTarget.Get() == target.Get();
    for (UINT i = 0; i < 2; ++i)
    {
        const auto& actualViewport = restoredNativeViewports[i];
        const auto& expectedViewport = nativeViewports[i];
        const auto& actualScissor = restoredNativeScissors[i];
        const auto& expectedScissor = nativeScissors[i];
        nativeStateRestored = nativeStateRestored &&
            actualViewport.TopLeftX == expectedViewport.TopLeftX &&
            actualViewport.TopLeftY == expectedViewport.TopLeftY &&
            actualViewport.Width == expectedViewport.Width &&
            actualViewport.Height == expectedViewport.Height &&
            actualViewport.MinDepth == expectedViewport.MinDepth &&
            actualViewport.MaxDepth == expectedViewport.MaxDepth &&
            actualScissor.left == expectedScissor.left &&
            actualScissor.top == expectedScissor.top &&
            actualScissor.right == expectedScissor.right &&
            actualScissor.bottom == expectedScissor.bottom;
    }
    if (!nativeStateRestored)
    {
        std::cerr << "Clipped draw did not restore native D3D11 rasteriser state\n";
        return 18;
    }

    context->CopyResource(readback.Get(), backdrop.Get());
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
            &mapped))) return 19;
    std::size_t clippedInsideChanged = 0;
    std::size_t clippedOutsideChanged = 0;
    std::size_t clippedReferenceMismatches = 0;
    for (UINT y = 0; y < height; ++y)
    {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 0; x < width; ++x)
        {
            const bool insideIntersection =
                static_cast<LONG>(x) >= std::max(
                    static_cast<LONG>(region.x), opticalClip.left) &&
                static_cast<LONG>(x) < std::min(
                    region.x + static_cast<LONG>(region.width), opticalClip.right) &&
                static_cast<LONG>(y) >= std::max(
                    static_cast<LONG>(region.y), opticalClip.top) &&
                static_cast<LONG>(y) < std::min(
                    region.y + static_cast<LONG>(region.height), opticalClip.bottom);
            const std::size_t index = y * width + x;
            if (row[x] != pixels[index])
            {
                ++(insideIntersection ? clippedInsideChanged : clippedOutsideChanged);
            }
            if (insideIntersection && row[x] != opticalPixels[index])
            {
                ++clippedReferenceMismatches;
            }
        }
    }
    context->Unmap(readback.Get(), 0);
    std::cout << "Clipped changed inside intersection: " << clippedInsideChanged
              << ", outside: " << clippedOutsideChanged
              << ", reference mismatches: " << clippedReferenceMismatches << '\n';
    if (clippedInsideChanged <= 1000 || clippedOutsideChanged != 0 ||
        clippedReferenceMismatches != 0) return 20;

    // Uniform input isolates lens coverage from refraction, blur and the rim.
    // Interior cross bands and corner neighbourhoods must have no dark holes,
    // whichever supported corner exponent is selected.
    const std::vector<std::uint32_t> whitePixels(width * height, 0xffffffffu);
    D3D11_TEXTURE2D_DESC whiteDesc = desc;
    whiteDesc.Usage = D3D11_USAGE_DEFAULT;
    whiteDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    whiteDesc.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA whiteData { whitePixels.data(),
        width * sizeof(std::uint32_t), 0 };
    ComPtr<ID3D11Texture2D> whiteBackdrop;
    if (FAILED(device->CreateTexture2D(&whiteDesc, &whiteData,
            &whiteBackdrop))) return 21;
    auto coverageRegion = region;
    coverageRegion.cornerRadius = 28.0f;
    coverageRegion.blurRadius = 0.0f;
    coverageRegion.refractionStrength = 0.0f;
    coverageRegion.dispersionStrength = 0.0f;
    coverageRegion.rimStrength = 0.0f;
    coverageRegion.opacity = 1.0f;
    coverageRegion.tintAlpha = 0.0f;
    constexpr UINT innerCorners[4][2] = {
        { 54, 54 }, { 329, 54 }, { 54, 137 }, { 329, 137 }
    };
    const FLOAT dark[4] = { 16.0f / 255.0f, 16.0f / 255.0f,
        16.0f / 255.0f, 1.0f };
    const auto bright = [](std::uint32_t pixel) {
        return (pixel & 0xffu) >= 248u &&
            ((pixel >> 8u) & 0xffu) >= 248u &&
            ((pixel >> 16u) & 0xffu) >= 248u &&
            (pixel >> 24u) == 255u;
    };
    std::vector<std::uint32_t> coveragePixels(width * height);
    for (const float exponent : { 2.0f, 4.0f, 6.0f })
    {
        coverageRegion.cornerExponent = exponent;
        context->ClearRenderTargetView(target.Get(), dark);
        if (!lens.render(context.Get(), whiteBackdrop.Get(), target.Get(),
                coverageRegion)) return 22;
        context->CopyResource(readback.Get(), backdrop.Get());
        if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
                &mapped))) return 23;
        const auto readPixel = [&](UINT x, UINT y) {
            const auto* row = reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
            return row[x];
        };
        std::size_t crossFailures = 0;
        std::size_t innerCornerFailures = 0;
        std::size_t outsideFailures = 0;
        for (UINT y = 0; y < height; ++y)
        {
            for (UINT x = 0; x < width; ++x)
            {
                coveragePixels[y * width + x] = readPixel(x, y);
                const bool insideBounds = x >= 64u && x < 448u &&
                    y >= 32u && y < 224u;
                if (!insideBounds)
                {
                    if (readPixel(x, y) != 0xff101010u) ++outsideFailures;
                    continue;
                }
                const UINT localX = x - 64u;
                const UINT localY = y - 32u;
                const bool horizontalBand = localX >= 2u && localX < 382u &&
                    localY >= 28u && localY < 164u;
                const bool verticalBand = localX >= 28u && localX < 356u &&
                    localY >= 2u && localY < 190u;
                if ((horizontalBand || verticalBand) && !bright(readPixel(x, y)))
                {
                    ++crossFailures;
                }
            }
        }
        for (const auto& corner : innerCorners)
        {
            for (UINT y = corner[1] - 1u; y <= corner[1] + 1u; ++y)
            {
                for (UINT x = corner[0] - 1u; x <= corner[0] + 1u; ++x)
                {
                    if (!bright(readPixel(64u + x, 32u + y))) ++innerCornerFailures;
                }
            }
        }
        // All four pixels are inside the panel bounds but outside its rounded
        // silhouette for exponents 2, 4 and 6; they must retain the dark target.
        for (const UINT y : { 32u, 223u })
        {
            for (const UINT x : { 64u, 447u })
            {
                if (readPixel(x, y) != 0xff101010u) ++outsideFailures;
            }
        }
        context->Unmap(readback.Get(), 0);
        std::cout << "Coverage exponent " << exponent
                  << ": cross failures: " << crossFailures
                  << ", inner corner failures: " << innerCornerFailures
                  << ", outside failures: " << outsideFailures << '\n';
        if (crossFailures != 0 || innerCornerFailures != 0 ||
            outsideFailures != 0) return 24;
    }

    // A 10-pixel marker inset leaves room for the notch's 9.9-pixel flares.
    // Check pixels outside the 364-pixel body so merely forwarding the shape
    // parameters without drawing the flares cannot pass this regression.
    auto notchRegion = coverageRegion;
    notchRegion.shape = 4u;
    notchRegion.cornerExponent = 4.0f;
    notchRegion.notchAmount = 1.0f;
    notchRegion.notchDepth = 55.0f;
    notchRegion.markerInsetX = 10.0f;
    context->ClearRenderTargetView(target.Get(), dark);
    if (!lens.render(context.Get(), whiteBackdrop.Get(), target.Get(),
            notchRegion)) return 25;
    context->CopyResource(readback.Get(), backdrop.Get());
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
            &mapped))) return 26;
    const auto notchPixel = [&](UINT localX, UINT localY) {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(mapped.pData) +
                (32u + localY) * mapped.RowPitch);
        return row[64u + localX];
    };
    const std::size_t notchBodyFailures =
        static_cast<std::size_t>(!bright(notchPixel(11u, 1u))) +
        static_cast<std::size_t>(!bright(notchPixel(372u, 1u))) +
        static_cast<std::size_t>(!bright(notchPixel(192u, 96u)));
    const std::size_t notchFlareFailures =
        static_cast<std::size_t>(!bright(notchPixel(8u, 1u))) +
        static_cast<std::size_t>(!bright(notchPixel(375u, 1u)));
    const std::size_t notchCutoutFailures =
        static_cast<std::size_t>(notchPixel(8u, 8u) != 0xff101010u) +
        static_cast<std::size_t>(notchPixel(375u, 8u) != 0xff101010u);
    const std::size_t notchBottomFailures =
        static_cast<std::size_t>(notchPixel(10u, 191u) != 0xff101010u) +
        static_cast<std::size_t>(notchPixel(373u, 191u) != 0xff101010u) +
        static_cast<std::size_t>(notchPixel(0u, 191u) != 0xff101010u) +
        static_cast<std::size_t>(notchPixel(383u, 191u) != 0xff101010u);
    context->Unmap(readback.Get(), 0);
    std::cout << "Notch coverage body failures: " << notchBodyFailures
              << ", flare failures: " << notchFlareFailures
              << ", cutout failures: " << notchCutoutFailures
              << ", bottom failures: " << notchBottomFailures << '\n';
    if (notchBodyFailures != 0 || notchFlareFailures != 0 ||
        notchCutoutFailures != 0 || notchBottomFailures != 0) return 27;

    // Moving the full lens partly outside the target must crop its existing
    // silhouette, rather than round a new corner at the screen's edge.
    const std::int32_t partialPositions[4][2] = {
        { -20, -16 }, { 300, 180 }, { -500, -300 }, { 600, 300 }
    };
    const auto nearlySamePixel = [](std::uint32_t actual, std::uint32_t expected) {
        for (UINT shift = 0; shift < 32u; shift += 8u)
        {
            if (std::abs(static_cast<int>((actual >> shift) & 0xffu) -
                    static_cast<int>((expected >> shift) & 0xffu)) > 1)
                return false;
        }
        return true;
    };
    for (UINT position = 0; position < 4u; ++position)
    {
        auto partialRegion = coverageRegion;
        partialRegion.x = partialPositions[position][0];
        partialRegion.y = partialPositions[position][1];
        context->ClearRenderTargetView(target.Get(), dark);
        if (!lens.render(context.Get(), whiteBackdrop.Get(), target.Get(),
                partialRegion)) return 28;
        context->CopyResource(readback.Get(), backdrop.Get());
        if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
                &mapped))) return 29;
        std::size_t partialReferenceFailures = 0;
        std::size_t partialOutsideFailures = 0;
        std::size_t partialInsideChanged = 0;
        for (UINT y = 0; y < height; ++y)
        {
            const auto* row = reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
            for (UINT x = 0; x < width; ++x)
            {
                const auto localX = static_cast<std::int64_t>(x) - partialRegion.x;
                const auto localY = static_cast<std::int64_t>(y) - partialRegion.y;
                if (localX >= 0 && localY >= 0 &&
                    localX < partialRegion.width && localY < partialRegion.height)
                {
                    const auto reference = coveragePixels[
                        static_cast<std::size_t>(32 + localY) * width + 64 + localX];
                    if (!nearlySamePixel(row[x], reference)) ++partialReferenceFailures;
                    if (row[x] != 0xff101010u) ++partialInsideChanged;
                }
                else if (row[x] != 0xff101010u) ++partialOutsideFailures;
            }
        }
        context->Unmap(readback.Get(), 0);
        std::cout << "Off-screen lens " << partialRegion.x << ',' << partialRegion.y
                  << ": reference failures: " << partialReferenceFailures
                  << ", outside failures: " << partialOutsideFailures
                  << ", changed inside: " << partialInsideChanged << '\n';
        if (partialReferenceFailures != 0 || partialOutsideFailures != 0 ||
            (position < 2u ? partialInsideChanged <= 1000u : partialInsideChanged != 0u))
            return 30;
    }

    std::vector<std::uint32_t> samplePixels(width * height);
    const auto renderSample = [&](ID3D11Texture2D* source,
        const vibrance::win32::LiquidGlassRenderRegion& sampleRegion) {
        context->ClearRenderTargetView(target.Get(), dark);
        if (!lens.render(context.Get(), source, target.Get(), sampleRegion))
            return false;
        context->CopyResource(readback.Get(), backdrop.Get());
        if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
                &mapped))) return false;
        for (UINT y = 0; y < height; ++y)
        {
            const auto* row = reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
            std::copy_n(row, width, samplePixels.begin() + y * width);
        }
        context->Unmap(readback.Get(), 0);
        return checkReference(samplePixels);
    };

    // Native viewport fades retain the complete lens geometry. Check every
    // row across both edges, including the small header controls which used
    // to be subdivided into separate optical strips.
    for (const unsigned buttonHeight : { 192u, 22u })
    {
        auto maskedRegion = coverageRegion;
        maskedRegion.shape = 1u;
        maskedRegion.height = buttonHeight;
        maskedRegion.width = buttonHeight == 22u ? 78u : region.width;
        maskedRegion.y = buttonHeight == 22u ? 48 : region.y;
        maskedRegion.viewportRect = { 0, 52.0f - maskedRegion.y,
            static_cast<float>(maskedRegion.width), 152 };
        maskedRegion.viewportFade = { 16, 20, 0, 1 };
        if (!renderSample(whiteBackdrop.Get(), maskedRegion)) return 60;
        const auto smooth = [](float distance, float extent) {
            const float t = std::clamp(distance / extent, 0.0f, 1.0f);
            return t * t * (3.0f - 2.0f * t);
        };
        unsigned fadeFailures = 0;
        for (unsigned y = 0; y < height; ++y)
        {
            const float point = y + 0.5f;
            const bool inside = point >= 52 && point < 204 &&
                point >= maskedRegion.y && point < maskedRegion.y + maskedRegion.height;
            const float alpha = inside ? std::min(smooth(point - 52, 16), smooth(204 - point, 20)) : 0;
            const int actual = samplePixels[y * width + maskedRegion.x + maskedRegion.width / 2] & 255u;
            if (std::abs(actual - (16 + 239 * alpha)) > 2) ++fadeFailures;
        }
        std::cout << "Continuous viewport fade, lens height " << buttonHeight
                  << ": " << fadeFailures << " pixel failures\n";
        if (fadeFailures) return 61;
    }

    // Blur must affect the centre, where the edge's refraction band is zero.
    // Compare stripe contrast there: an edge-only frost blend cannot pass.
    D3D11_SUBRESOURCE_DATA stripedData { pixels.data(),
        width * sizeof(std::uint32_t), 0 };
    ComPtr<ID3D11Texture2D> stripedBackdrop;
    if (FAILED(device->CreateTexture2D(&whiteDesc, &stripedData,
            &stripedBackdrop))) return 31;
    auto frostedRegion = coverageRegion;
    const auto centreContrast = [&]() {
        std::uint64_t sum = 0u;
        std::uint64_t sumSquares = 0u;
        std::uint64_t count = 0u;
        for (UINT y = 104u; y < 152u; ++y)
        {
            for (UINT x = 160u; x < 352u; ++x)
            {
                const auto red = samplePixels[y * width + x] & 0xffu;
                sum += red;
                sumSquares += red * red;
                ++count;
            }
        }
        const double mean = static_cast<double>(sum) / count;
        return static_cast<double>(sumSquares) / count - mean * mean;
    };
    if (!renderSample(stripedBackdrop.Get(), frostedRegion)) return 32;
    const auto clearContrast = centreContrast();
    std::size_t clearReferenceFailures = 0u;
    for (UINT y = 104u; y < 152u; ++y)
        for (UINT x = 160u; x < 352u; ++x)
            if (samplePixels[y * width + x] != pixels[y * width + x])
                ++clearReferenceFailures;
    frostedRegion.blurRadius = 30.0f;
    if (!renderSample(stripedBackdrop.Get(), frostedRegion)) return 32;
    const auto frostedContrast = centreContrast();
    std::cout << "Centre blur stripe contrast, clear: " << clearContrast
              << ", frosted: " << frostedContrast
              << ", clear reference failures: " << clearReferenceFailures << '\n';
    if (clearReferenceFailures != 0u || clearContrast < 2000.0 ||
        frostedContrast * 3.0 >= clearContrast) return 33;

    // Dense Gaussian sampling must progressively smooth small details in both
    // axes. At maximum blur, ten-pixel stripes form continuous transitions,
    // rather than the secondary hard bands produced by sparse wide offsets.
    for (UINT pattern = 0u; pattern < 3u; ++pattern)
    {
        std::vector<std::uint32_t> detailPixels(width * height);
        for (UINT y = 0u; y < height; ++y)
        {
            for (UINT x = 0u; x < width; ++x)
            {
                const bool light = pattern == 0u ? ((x + y) & 1u) != 0u :
                    (((pattern == 1u ? x : y) / 10u) & 1u) != 0u;
                const auto value = light ? 224u : 64u;
                detailPixels[y * width + x] = 0xff000000u |
                    value | (value << 8u) | (value << 16u);
            }
        }
        D3D11_SUBRESOURCE_DATA detailData { detailPixels.data(),
            width * sizeof(std::uint32_t), 0 };
        ComPtr<ID3D11Texture2D> detailBackdrop;
        if (FAILED(device->CreateTexture2D(&whiteDesc, &detailData,
                &detailBackdrop))) return 39;
        auto detailRegion = coverageRegion;
        double precedingContrast = 0.0;
        double originalContrast = 0.0;
        for (const float blur : { 0.0f, 6.0f, 12.0f, 18.0f, 24.0f, 30.0f })
        {
            detailRegion.blurRadius = blur;
            if (!renderSample(detailBackdrop.Get(), detailRegion)) return 40;
            const auto contrast = centreContrast();
            std::cout << "Dense blur pattern " << pattern << ", radius " << blur
                      << ": centre contrast " << contrast << '\n';
            if (blur == 0.0f)
            {
                originalContrast = contrast;
                for (UINT y = 104u; y < 152u; ++y)
                    for (UINT x = 160u; x < 352u; ++x)
                        if (samplePixels[y * width + x] != detailPixels[y * width + x])
                            return 41;
            }
            else if (contrast > precedingContrast + 1.0 ||
                contrast > originalContrast * 0.99) return 42;
            precedingContrast = contrast;
        }
        if (precedingContrast > (pattern == 0u ? 1.0 : 10.0)) return 43;
        if (pattern != 0u)
        {
            int largestCurvature = 0;
            for (UINT y = 105u; y < 151u; ++y)
            {
                for (UINT x = 161u; x < 351u; ++x)
                {
                    const auto centre = y * width + x;
                    const UINT axisStep = pattern == 1u ? 1u : width;
                    const int previous = samplePixels[centre - axisStep] & 0xffu;
                    const int current = samplePixels[centre] & 0xffu;
                    const int next = samplePixels[centre + axisStep] & 0xffu;
                    largestCurvature = std::max(largestCurvature,
                        std::abs(next - 2 * current + previous));
                }
            }
            std::cout << "Dense stripe pattern " << pattern
                      << ": largest transition curvature " << largestCurvature << '\n';
            if (largestCurvature > 7) return 44;
        }
    }

    // Zero tint preserves the white source; full tint reaches the chosen
    // colour independently of the source. Verify a centre area, not one pixel.
    auto tintedRegion = coverageRegion;
    tintedRegion.tintRed = 0.2f;
    tintedRegion.tintGreen = 0.55f;
    tintedRegion.tintBlue = 0.8f;
    std::size_t tintEndpointFailures = 0u;
    for (const float amount : { 0.0f, 1.0f })
    {
        tintedRegion.tintAlpha = amount;
        if (!renderSample(whiteBackdrop.Get(), tintedRegion)) return 34;
        for (UINT y = 112u; y < 144u; ++y)
        {
            for (UINT x = 224u; x < 288u; ++x)
            {
                const auto pixel = samplePixels[y * width + x];
                const int expected[4] = {
                    amount == 0.0f ? 255 : 51,
                    amount == 0.0f ? 255 : 140,
                    amount == 0.0f ? 255 : 204,
                    255
                };
                for (UINT channel = 0u; channel < 4u; ++channel)
                    if (std::abs(static_cast<int>(
                            (pixel >> (channel * 8u)) & 0xffu) - expected[channel]) > 1)
                        ++tintEndpointFailures;
            }
        }
    }
    std::cout << "Tint endpoint failures: " << tintEndpointFailures << '\n';
    if (tintEndpointFailures != 0u) return 35;

    // The merge amount opens a curved neck between two caps. Its source cap
    // continues left into the main island; its detached end stays circular.
    auto bridgeRegion = coverageRegion;
    bridgeRegion.x = 136;
    bridgeRegion.y = 96;
    bridgeRegion.width = 192u;
    bridgeRegion.height = 64u;
    bridgeRegion.shape = 5u;
    bridgeRegion.notchAmount = 0.0f;
    if (!renderSample(whiteBackdrop.Get(), bridgeRegion)) return 36;
    const auto bridgePixel = [&](UINT localX, UINT localY) {
        return samplePixels[(96u + localY) * width + 136u + localX];
    };
    const bool unmergedNeckEmpty = bridgePixel(96u, 12u) == 0xff101010u;
    bridgeRegion.notchAmount = 1.0f;
    if (!renderSample(whiteBackdrop.Get(), bridgeRegion)) return 36;
    const bool bridgeCoverageCorrect = unmergedNeckEmpty &&
        bright(bridgePixel(96u, 12u)) &&
        bright(bridgePixel(32u, 32u)) &&
        bright(bridgePixel(160u, 32u)) &&
        bright(bridgePixel(0u, 4u)) &&
        bridgePixel(191u, 3u) == 0xff101010u;
    const auto bridgeReference = samplePixels;
    std::cout << "Liquid bridge neck and cap coverage: "
              << (bridgeCoverageCorrect ? "pass" : "fail") << '\n';
    if (!bridgeCoverageCorrect) return 37;

    bridgeRegion.x = -40;
    bridgeRegion.y = -20;
    if (!renderSample(whiteBackdrop.Get(), bridgeRegion)) return 36;
    std::size_t bridgeCropFailures = 0u;
    for (UINT y = 0u; y < height; ++y)
    {
        for (UINT x = 0u; x < width; ++x)
        {
            const auto localX = static_cast<std::int64_t>(x) - bridgeRegion.x;
            const auto localY = static_cast<std::int64_t>(y) - bridgeRegion.y;
            const auto expected = localX < bridgeRegion.width &&
                localY < bridgeRegion.height ? bridgeReference[
                    static_cast<std::size_t>(96 + localY) * width + 136 + localX] :
                0xff101010u;
            if (!nearlySamePixel(samplePixels[y * width + x], expected))
                ++bridgeCropFailures;
        }
    }
    std::cout << "Off-screen liquid bridge crop failures: "
              << bridgeCropFailures << '\n';
    if (bridgeCropFailures != 0u) return 38;

    // One native lens covers the main shell, merge neck and detached circle.
    // Its neck's left cap must stop at its own bounds, preserving main corners.
    auto unionRegion = coverageRegion;
    unionRegion.x = 64;
    unionRegion.y = 80;
    unionRegion.width = 284u;
    unionRegion.height = 96u;
    unionRegion.shape = 0u;
    unionRegion.contourCount = 3u;
    unionRegion.contours[0].width = 192.0f;
    unionRegion.contours[0].height = 96.0f;
    unionRegion.contours[0].cornerRadius = 24.0f;
    unionRegion.contours[0].shape = 1u;
    unionRegion.contours[1].x = 128.0f;
    unionRegion.contours[1].y = 16.0f;
    unionRegion.contours[1].width = 156.0f;
    unionRegion.contours[1].height = 64.0f;
    unionRegion.contours[1].notchAmount = 1.0f;
    unionRegion.contours[1].shape = 5u;
    unionRegion.contours[2].x = 220.0f;
    unionRegion.contours[2].y = 16.0f;
    unionRegion.contours[2].width = 64.0f;
    unionRegion.contours[2].height = 64.0f;
    unionRegion.contours[2].shape = 2u;
    if (!renderSample(whiteBackdrop.Get(), unionRegion)) return 45;
    const auto unionPixel = [&](UINT localX, UINT localY) {
        return samplePixels[(80u + localY) * width + 64u + localX];
    };
    const bool unionCoverageCorrect = bright(unionPixel(96u, 48u)) &&
        bright(unionPixel(208u, 48u)) && bright(unionPixel(252u, 48u)) &&
        unionPixel(0u, 0u) == 0xff101010u &&
        unionPixel(0u, 16u) == 0xff101010u &&
        unionPixel(208u, 94u) == 0xff101010u &&
        unionPixel(283u, 16u) == 0xff101010u;
    std::cout << "Union shell, bounded neck and circle coverage: "
              << (unionCoverageCorrect ? "pass" : "fail") << '\n';
    if (!unionCoverageCorrect) return 46;
    const auto unionReference = samplePixels;
    auto croppedUnion = unionRegion;
    croppedUnion.x = -40;
    croppedUnion.y = -20;
    if (!renderSample(whiteBackdrop.Get(), croppedUnion)) return 45;
    std::size_t unionCropFailures = 0u;
    for (UINT y = 0u; y < height; ++y)
    {
        for (UINT x = 0u; x < width; ++x)
        {
            const auto localX = static_cast<std::int64_t>(x) - croppedUnion.x;
            const auto localY = static_cast<std::int64_t>(y) - croppedUnion.y;
            const auto expected = localX < croppedUnion.width &&
                localY < croppedUnion.height ? unionReference[
                    static_cast<std::size_t>(80 + localY) * width + 64 + localX] :
                0xff101010u;
            if (!nearlySamePixel(samplePixels[y * width + x], expected))
                ++unionCropFailures;
        }
    }
    std::cout << "Off-screen union crop failures: " << unionCropFailures << '\n';
    if (unionCropFailures != 0u) return 47;

    // Exclusion follows the front card's rounded silhouette, not its rectangle.
    // Its centre remains untouched while pixels beyond its curved corner fill.
    auto excludedRegion = coverageRegion;
    excludedRegion.hasExclusion = true;
    excludedRegion.exclusion.x = 128.0f;
    excludedRegion.exclusion.y = 64.0f;
    excludedRegion.exclusion.width = 128.0f;
    excludedRegion.exclusion.height = 64.0f;
    excludedRegion.exclusion.cornerRadius = 16.0f;
    excludedRegion.exclusion.shape = 1u;
    if (!renderSample(whiteBackdrop.Get(), excludedRegion)) return 48;
    const bool exclusionCorrect = samplePixels[128u * width + 256u] == 0xff101010u &&
        bright(samplePixels[96u * width + 192u]) &&
        bright(samplePixels[128u * width + 160u]);
    std::cout << "Front card exclusion coverage: "
              << (exclusionCorrect ? "pass" : "fail") << '\n';
    if (!exclusionCorrect) return 49;

    const std::vector<std::uint32_t> neutralPixels(width * height, 0xff606060u);
    D3D11_SUBRESOURCE_DATA neutralData { neutralPixels.data(),
        width * sizeof(std::uint32_t), 0 };
    ComPtr<ID3D11Texture2D> neutralBackdrop;
    if (FAILED(device->CreateTexture2D(&whiteDesc, &neutralData,
            &neutralBackdrop))) return 50;
    // A joined contour has one outer rim. The old main shell's edge inside
    // the neck cannot generate a second bright line through the union.
    unionRegion.rimStrength = 1.0f;
    if (!renderSample(neutralBackdrop.Get(), unionRegion)) return 50;
    const auto internalEdge = unionPixel(191u, 48u) & 0xffu;
    const auto outerEdge = unionPixel(283u, 48u) & 0xffu;
    std::cout << "Union rim, internal edge: " << internalEdge
              << ", outer edge: " << outerEdge << '\n';
    if (std::abs(static_cast<int>(internalEdge) - 96) > 1 || outerEdge < 160u)
        return 51;

    // The cap is painted once across the complete union. Overlapping main,
    // bridge and circle pixels at the same height must have equal brightness.
    unionRegion.rimStrength = 0.0f;
    unionRegion.paintCapGradient = true;
    unionRegion.capGradient = { 1.0f, 0.0f, 0.0f, 1.0f };
    if (!renderSample(neutralBackdrop.Get(), unionRegion)) return 50;
    std::size_t gradientFailures = 0u;
    for (const UINT localX : { 96u, 191u, 208u, 252u })
        if (std::abs(static_cast<int>(unionPixel(localX, 48u) & 0xffu) - 49) > 1)
            ++gradientFailures;
    if ((unionPixel(96u, 8u) & 0xffu) >= 20u ||
        (unionPixel(96u, 87u) & 0xffu) <= 80u) ++gradientFailures;
    std::cout << "Shared cap gradient failures: " << gradientFailures << '\n';
    if (gradientFailures != 0u) return 52;

    unionRegion.capColour = { 0.9f, 0.9f, 0.9f };
    if (!renderSample(neutralBackdrop.Get(), unionRegion)) return 50;
    std::size_t lightGradientFailures = 0u;
    for (const UINT localX : { 96u, 191u, 208u, 252u })
        if (std::abs(static_cast<int>(unionPixel(localX, 48u) & 0xffu) - 162) > 2)
            ++lightGradientFailures;
    if ((unionPixel(96u, 8u) & 0xffu) < 200u ||
        (unionPixel(96u, 87u) & 0xffu) > 130u) ++lightGradientFailures;
    std::cout << "Themed light cap gradient failures: " << lightGradientFailures << '\n';
    if (lightGradientFailures != 0u) return 52;

    auto invalidMask = unionRegion;
    invalidMask.contourCount = 4u;
    if (lens.render(context.Get(), neutralBackdrop.Get(), target.Get(), invalidMask))
        return 53;
    invalidMask = unionRegion;
    invalidMask.contours[1].width = std::numeric_limits<float>::quiet_NaN();
    if (lens.render(context.Get(), neutralBackdrop.Get(), target.Get(), invalidMask))
        return 53;
    invalidMask = excludedRegion;
    invalidMask.exclusion.height = 0.0f;
    if (lens.render(context.Get(), neutralBackdrop.Get(), target.Get(), invalidMask))
        return 53;
    invalidMask = unionRegion;
    invalidMask.capGradient[0] = std::numeric_limits<float>::quiet_NaN();
    if (lens.render(context.Get(), neutralBackdrop.Get(), target.Get(), invalidMask))
        return 53;

    invalidMask = unionRegion;
    invalidMask.capColour[1] = std::numeric_limits<float>::quiet_NaN();
    if (lens.render(context.Get(), neutralBackdrop.Get(), target.Get(), invalidMask))
        return 53;

    // Missing compositor pixels must not replace an existing surface with
    // opaque black. The island's UI is composited separately by DWM.
    std::fill(pixels.begin(), pixels.end(), 0xff000000u);
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    desc.CPUAccessFlags = 0;
    D3D11_SUBRESOURCE_DATA blackData { pixels.data(),
        width * sizeof(std::uint32_t), 0 };
    ComPtr<ID3D11Texture2D> emptyBackdrop;
    if (FAILED(device->CreateTexture2D(&desc, &blackData,
            &emptyBackdrop))) return 12;
    const FLOAT grey[4] = { 0.3f, 0.4f, 0.5f, 1.0f };
    context->ClearRenderTargetView(target.Get(), grey);
    if (!lens.render(context.Get(), emptyBackdrop.Get(), target.Get(),
            region)) return 13;
    context->CopyResource(readback.Get(), backdrop.Get());
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
            &mapped))) return 14;
    const auto* middle = reinterpret_cast<const std::uint32_t*>(
        static_cast<const std::uint8_t*>(mapped.pData) +
            (height / 2u) * mapped.RowPitch);
    const std::uint32_t centre = middle[width / 2u];
    context->Unmap(readback.Get(), 0);
    const auto near_channel = [](std::uint32_t actual, int expected) {
        return std::abs(static_cast<int>(actual) - expected) <= 2;
    };
    if (!(near_channel(centre & 0xffu, 77) &&
        near_channel((centre >> 8u) & 0xffu, 102) &&
        near_channel((centre >> 16u) & 0xffu, 128))) return 15;
    for (const float viewportY : { -80.0f, -60.0f, -20.0f, 0.0f,
            80.25f, 180.0f, 192.0f, 250.0f })
    {
        auto viewportRegion = frostedRegion;
        viewportRegion.blurRadius = 7.0f;
        viewportRegion.refractionStrength = 24.0f;
        viewportRegion.viewportRect = { 0, viewportY,
            static_cast<float>(viewportRegion.width), 60 };
        viewportRegion.viewportFade = { 12, 18, 0.1f, 1 };
        if (!renderSample(stripedBackdrop.Get(), viewportRegion)) return 80;
    }
    if (comparisonPath)
    {
        if (comparison.peek() != std::char_traits<char>::eof()) return 57;
        std::cout << "Reference frames: " << comparedFrames
                  << ", differing channels: " << differingChannels
                  << ", maximum channel difference: " << maximumChannelDifference << '\n';
    }
    referenceFinished = true;
    // Reinitialisation creates a fresh GPU constant buffer. Rendering exactly
    // the same kernel and extent must upload it again and retain the image.
    frostedRegion.blurRadius = 7.0f;
    if (!renderSample(stripedBackdrop.Get(), frostedRegion)) return 58;
    const auto beforeReinitialisation = samplePixels;
    if (!lens.initialise(device.Get(), argv[1]) ||
        !renderSample(stripedBackdrop.Get(), frostedRegion) ||
        samplePixels != beforeReinitialisation) return 59;
    std::cout << "Reinitialised blur kernel reference: pass\n";

    // A compositor frame can alternate islands of several sizes. Retired
    // scratch sets must be overwritten with each island's current backdrop.
    std::array<std::vector<std::uint32_t>, 9> pooledReferences;
    for (UINT cycle = 0u; cycle < 3u; ++cycle)
    {
        for (UINT size = 0u; size < pooledReferences.size(); ++size)
        {
            auto pooledRegion = frostedRegion;
            pooledRegion.width -= size * 24u;
            pooledRegion.height -= size * 8u;
            ID3D11Texture2D* currentSource = cycle == 1u ?
                whiteBackdrop.Get() : stripedBackdrop.Get();
            if (!renderSample(currentSource, pooledRegion)) return 60;
            if (cycle == 0u) pooledReferences[size] = samplePixels;
            if (cycle == 2u && samplePixels != pooledReferences[size]) return 61;
        }
    }
    std::cout << "Alternating scratch sizes and live sources: pass\n";

    // A typed DWM target can be sampled directly; typeless targets first need
    // a cropped typed copy. Both routes must produce exactly the same lens.
    std::size_t sourceOriginFailures = 0u;
    for (const float radius : { 0.0f, 7.0f, 30.0f })
    {
        auto originRegion = region;
        originRegion.x = 160;
        originRegion.y = 96;
        originRegion.width = 128;
        originRegion.height = 64;
        originRegion.blurRadius = radius;
        if (!renderSample(stripedBackdrop.Get(), originRegion)) return 66;
        const auto directReference = samplePixels;
        const auto margin = static_cast<LONG>(std::ceil(std::max({
            radius * 5.0f, originRegion.refractionStrength +
                originRegion.dispersionStrength + 2.0f, 4.0f })));
        const D3D11_BOX copyBox {
            static_cast<UINT>(std::max<LONG>(0, originRegion.x - margin)),
            static_cast<UINT>(std::max<LONG>(0, originRegion.y - margin)), 0,
            std::min<UINT>(width, originRegion.x + originRegion.width + margin),
            std::min<UINT>(height, originRegion.y + originRegion.height + margin), 1
        };
        auto croppedDescription = whiteDesc;
        croppedDescription.Width = copyBox.right - copyBox.left;
        croppedDescription.Height = copyBox.bottom - copyBox.top;
        ComPtr<ID3D11Texture2D> croppedSource;
        if (FAILED(device->CreateTexture2D(&croppedDescription, nullptr,
                &croppedSource))) return 67;
        context->CopySubresourceRegion(croppedSource.Get(), 0, 0, 0, 0,
            stripedBackdrop.Get(), 0, &copyBox);
        context->ClearRenderTargetView(target.Get(), dark);
        if (!lens.render(context.Get(), croppedSource.Get(), target.Get(),
                originRegion, static_cast<LONG>(copyBox.left),
                static_cast<LONG>(copyBox.top))) return 68;
        context->CopyResource(readback.Get(), backdrop.Get());
        if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
                &mapped))) return 69;
        for (UINT y = 0; y < height; ++y)
        {
            const auto* row = reinterpret_cast<const std::uint32_t*>(
                static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
            for (UINT x = 0; x < width; ++x)
                if (row[x] != directReference[y * width + x]) ++sourceOriginFailures;
        }
        context->Unmap(readback.Get(), 0);
    }
    std::cout << "Direct and cropped source reference failures: "
              << sourceOriginFailures << '\n';
    if (sourceOriginFailures) return 70;

    std::size_t typelessSourceFailures = 0u;
    for (const auto format : { DXGI_FORMAT_R8G8B8A8_UNORM,
            DXGI_FORMAT_R8G8B8A8_UNORM_SRGB })
    {
        auto typedDescription = whiteDesc;
        typedDescription.Format = format;
        typedDescription.BindFlags |= D3D11_BIND_RENDER_TARGET;
        ComPtr<ID3D11Texture2D> typedSource;
        ComPtr<ID3D11Texture2D> typelessSource;
        ComPtr<ID3D11RenderTargetView> typedTarget;
        ComPtr<ID3D11RenderTargetView> typelessTarget;
        if (FAILED(device->CreateTexture2D(&typedDescription, nullptr,
                &typedSource)) ||
            FAILED(device->CreateRenderTargetView(typedSource.Get(), nullptr,
                &typedTarget))) return 71;
        typedDescription.Format = DXGI_FORMAT_R8G8B8A8_TYPELESS;
        D3D11_RENDER_TARGET_VIEW_DESC typedView {};
        typedView.Format = format;
        typedView.ViewDimension = D3D11_RTV_DIMENSION_TEXTURE2D;
        if (FAILED(device->CreateTexture2D(&typedDescription, nullptr,
                &typelessSource)) ||
            FAILED(device->CreateRenderTargetView(typelessSource.Get(), &typedView,
                &typelessTarget))) return 72;
        for (const float radius : { 0.0f, 7.0f, 30.0f })
        {
            auto typelessRegion = region;
            typelessRegion.blurRadius = radius;
            const D3D11_RECT typelessClip { 68, 36, 282, 198 };
            context->CopyResource(typedSource.Get(), stripedBackdrop.Get());
            context->CopyResource(typelessSource.Get(), stripedBackdrop.Get());
            if (!lens.render(context.Get(), typedSource.Get(), typedTarget.Get(),
                    typelessRegion, 0, 0, &typelessClip)) return 73;
            context->CopyResource(readback.Get(), typedSource.Get());
            if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
                    &mapped))) return 74;
            std::vector<std::uint32_t> typedReference(width * height);
            for (UINT y = 0; y < height; ++y)
            {
                const auto* row = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
                std::copy_n(row, width, typedReference.begin() + y * width);
            }
            context->Unmap(readback.Get(), 0);
            if (!lens.render(context.Get(), typelessSource.Get(), typelessTarget.Get(),
                    typelessRegion, 0, 0, &typelessClip, format)) return 75;
            context->CopyResource(readback.Get(), typelessSource.Get());
            if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
                    &mapped))) return 76;
            for (UINT y = 0; y < height; ++y)
            {
                const auto* row = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
                for (UINT x = 0; x < width; ++x)
                    if (row[x] != typedReference[y * width + x]) ++typelessSourceFailures;
            }
            context->Unmap(readback.Get(), 0);
        }
        if (lens.render(context.Get(), typelessSource.Get(), typelessTarget.Get(),
                region, 0, 0, nullptr, DXGI_FORMAT_B8G8R8A8_UNORM)) return 77;
    }
    std::cout << "Typed and typeless same-target source reference failures: "
              << typelessSourceFailures << '\n';
    if (typelessSourceFailures) return 78;

    // DWM can paint a large island through many small, disjoint clips. Every
    // clip must retain its full Gaussian/refraction neighbourhood, even after
    // another clip or a different live backdrop has populated the scratch set.
    const D3D11_RECT samplingClips[] = {
        { 64, 32, 65, 33 }, { 447, 223, 448, 224 },
        { 64, 32, 448, 36 }, { 64, 32, 68, 224 },
        { 442, 32, 448, 224 }, { 64, 218, 448, 224 },
        { 79, 47, 113, 83 }, { 229, 111, 232, 114 },
        { 0, 0, 512, 256 }
    };
    std::size_t samplingClipFailures = 0u;
    for (const float radius : { 0.0f, 7.0f, 30.0f })
    {
        auto clippedRegion = region;
        clippedRegion.blurRadius = radius;
        clippedRegion.refractionStrength = 36.0f;
        clippedRegion.dispersionStrength = 6.0f;
        for (ID3D11Texture2D* liveSource : { stripedBackdrop.Get(), whiteBackdrop.Get() })
        {
            if (!renderSample(liveSource, clippedRegion)) return 62;
            const auto fullReference = samplePixels;
            for (const auto& samplingClip : samplingClips)
            {
                ID3D11Texture2D* previousSource = liveSource == stripedBackdrop.Get() ?
                    whiteBackdrop.Get() : stripedBackdrop.Get();
                if (!lens.render(context.Get(), previousSource, target.Get(),
                        clippedRegion)) return 63;
                context->ClearRenderTargetView(target.Get(), dark);
                if (!lens.render(context.Get(), liveSource, target.Get(),
                        clippedRegion, 0, 0, &samplingClip)) return 63;
                context->CopyResource(readback.Get(), backdrop.Get());
                if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0,
                        &mapped))) return 64;
                for (UINT y = 0; y < height; ++y)
                {
                    const auto* row = reinterpret_cast<const std::uint32_t*>(
                        static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
                    for (UINT x = 0; x < width; ++x)
                    {
                        const bool inside = static_cast<LONG>(x) >= samplingClip.left &&
                            static_cast<LONG>(x) < samplingClip.right &&
                            static_cast<LONG>(y) >= samplingClip.top &&
                            static_cast<LONG>(y) < samplingClip.bottom;
                        if (row[x] != (inside ? fullReference[y * width + x] : 0xff101010u))
                            ++samplingClipFailures;
                    }
                }
                context->Unmap(readback.Get(), 0);
            }
        }
    }
    std::cout << "Clipped sampling neighbourhood reference failures: "
              << samplingClipFailures << '\n';
    if (samplingClipFailures) return 65;
    if (!scratch_cache_regression(lens, device.Get(), context.Get())) return 79;
    if (!temporal_reuse_regression(device.Get(), context.Get(), argv[1])) return 81;
    return runBenchmark && !benchmark(lens, device.Get(), context.Get()) ? 54 : 0;
}

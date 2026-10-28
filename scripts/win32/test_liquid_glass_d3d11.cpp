#include "../../engine/src/platform/win32/liquid_glass_d3d11.h"
#include "../../engine/src/platform/win32/composition_raw_backdrop_clip.h"
#include "../../engine/src/platform/win32/liquid_glass_output_bounds.h"

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

namespace vibrance::win32
{
struct LiquidGlassD3D11TestAccess
{
    static bool opaque_sampling_suppressed(const LiquidGlassD3D11& renderer,
        const LiquidGlassRenderRegion& region)
    {
        return renderer.blurHistory_.radius < 0.0f &&
            renderer.scratchWidth_ <= region.width + 8u &&
            renderer.scratchHeight_ <= region.height + 8u;
    }
};
}

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
    bool useWarp = false;
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
        else if (option == L"--warp") useWarp = true;
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
    if (!validArguments || (capturePath && comparisonPath) ||
        (useWarp && adapterIndex != std::numeric_limits<unsigned>::max()))
    {
        std::wcerr << L"Usage: test_liquid_glass_d3d11 shader-directory [preview.ppm] "
            L"[--benchmark | --benchmark-only] [--adapter index | --warp] [--capture file | --compare file]\n";
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
            useWarp ? D3D_DRIVER_TYPE_WARP : D3D_DRIVER_TYPE_HARDWARE, nullptr, 0, requested, 1,
        D3D11_SDK_VERSION, &device, &level, &context);
    if (FAILED(result) && !selectedAdapter && !useWarp)
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

    // The shared primitive opens a curved neck between two round caps.
    auto bridgeRegion = coverageRegion;
    bridgeRegion.x = 136;
    bridgeRegion.y = 96;
    bridgeRegion.width = 132u;
    bridgeRegion.height = 64u;
    bridgeRegion.shape = 5u;
    bridgeRegion.notchAmount = 0.0f;
    if (!renderSample(whiteBackdrop.Get(), bridgeRegion)) return 36;
    const auto bridgePixel = [&](UINT localX, UINT localY) {
        return samplePixels[(96u + localY) * width + 136u + localX];
    };
    const bool unmergedNeckEmpty = bridgePixel(66u, 12u) == 0xff101010u;
    // At a real gap even the centreline must disconnect. Previously a
    // residual half-pixel smoothing term left a dark/translucent thread here.
    const bool unmergedCentreEmpty = bridgePixel(66u, 32u) == 0xff101010u;
    unsigned previousNeckCoverage = 0u;
    for (unsigned step = 0u; step <= 40u; ++step)
    {
        bridgeRegion.notchAmount = float(step) / 40.0f;
        if (!renderSample(whiteBackdrop.Get(), bridgeRegion)) return 36;
        unsigned coverage = 0u;
        for (UINT y = 0u; y < 64u; ++y)
            coverage += bridgePixel(66u, y) & 0xffu;
        if (coverage < previousNeckCoverage) return 37;
        previousNeckCoverage = coverage;
        if (step <= 4u && bridgePixel(66u, 32u) != 0xff101010u) return 37;
    }
    bridgeRegion.notchAmount = 1.0f;
    if (!renderSample(whiteBackdrop.Get(), bridgeRegion)) return 36;
    const bool bridgeCoverageCorrect = unmergedNeckEmpty && unmergedCentreEmpty &&
        bright(bridgePixel(66u, 12u)) &&
        bright(bridgePixel(32u, 32u)) &&
        bright(bridgePixel(100u, 32u)) &&
        bridgePixel(0u, 4u) == 0xff101010u &&
        bridgePixel(131u, 3u) == 0xff101010u;
    const auto bridgeReference = samplePixels;
    std::cout << "Liquid bridge neck and cap coverage: "
              << (bridgeCoverageCorrect ? "pass" : "fail") << '\n';
    if (!bridgeCoverageCorrect) return 37;

    // Pull the same surfaces apart at full material opacity. The neck must
    // narrow, while both outer caps remain round, until a genuine gap opens.
    unsigned previousNeck = 255u;
    for (UINT extent = 132u; extent <= 172u; extent += 2u)
    {
        bridgeRegion.width = extent;
        if (!renderSample(whiteBackdrop.Get(), bridgeRegion)) return 36;
        const auto neck = bridgePixel(extent / 2u, 32u) & 0xffu;
        if (neck > previousNeck || bridgePixel(0u, 4u) != 0xff101010u ||
            bridgePixel(extent - 1u, 4u) != 0xff101010u ||
            !bright(bridgePixel(32u, 32u)) || !bright(bridgePixel(extent - 32u, 32u))) return 37;
        previousNeck = neck;
    }
    if (previousNeck != 16u) return 37;
    bridgeRegion.width = 132u;

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
    unionRegion.contours[1].width = 140.0f;
    unionRegion.contours[1].height = 64.0f;
    unionRegion.contours[1].notchAmount = 1.0f;
    unionRegion.contours[1].shape = 5u;
    unionRegion.contours[2].x = 204.0f;
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

    // Solid compact shells still use the native contour union. Their cap
    // replaces the optical result, even while the complete surface fades.
    // Verify exact pixels and that preparation retains no Gaussian history,
    // no new comparison draw, and only the small snapshot guard.
    for (const bool excluded : { false, true })
    for (const float opacity : { 1.0f, 0.35f })
    {
        auto opaque = unionRegion;
        opaque.paintCapGradient = true;
        opaque.capGradient = { 1.0f, 1.0f, 0.2f, 0.8f };
        opaque.capColour = { 0.2f, 0.4f, 0.6f };
        opaque.opacity = opacity;
        opaque.blurRadius = 30.0f;
        opaque.refractionStrength = 36.0f;
        opaque.dispersionStrength = 6.0f;
        opaque.rimStrength = 1.0f;
        opaque.hasExclusion = excluded;
        opaque.exclusion = opaque.contours[0];
        opaque.exclusion.x = 48.0f;
        opaque.exclusion.y = 24.0f;
        opaque.exclusion.width = 72.0f;
        opaque.exclusion.height = 40.0f;
        opaque.viewportRect = { 0.0f, 0.0f, 284.0f, 96.0f };
        opaque.viewportFade = { 12.0f, 15.0f, 0.3f, 1.0f };
        const auto comparisons = lens.blur_comparison_count();
        if (!renderSample(stripedBackdrop.Get(), opaque) ||
            lens.blur_comparison_count() != comparisons ||
            !vibrance::win32::LiquidGlassD3D11TestAccess::opaque_sampling_suppressed(lens, opaque))
            return 84;
        const auto opaquePixels = samplePixels;
        auto noOptics = opaque;
        noOptics.blurRadius = noOptics.refractionStrength =
            noOptics.dispersionStrength = noOptics.rimStrength = 0.0f;
        if (!renderSample(whiteBackdrop.Get(), noOptics) || samplePixels != opaquePixels)
            return 85;

        // Reopening an expanded/translucent shell must immediately recover
        // filtering and sample the newly changed live source.
        auto translucent = opaque;
        translucent.capGradient[1] = 0.5f;
        if (!renderSample(stripedBackdrop.Get(), translucent) ||
            vibrance::win32::LiquidGlassD3D11TestAccess::opaque_sampling_suppressed(lens, translucent))
            return 86;
        const auto stripedPixels = samplePixels;
        if (!renderSample(whiteBackdrop.Get(), translucent) || samplePixels == stripedPixels)
            return 87;
        if (!renderSample(stripedBackdrop.Get(), opaque) || samplePixels != opaquePixels ||
            !vibrance::win32::LiquidGlassD3D11TestAccess::opaque_sampling_suppressed(lens, opaque))
            return 88;
        auto invalid = opaque;
        invalid.capGradient[0] = std::numeric_limits<float>::quiet_NaN();
        if (lens.render(context.Get(), stripedBackdrop.Get(), target.Get(), invalid)) return 89;
    }
    std::cout << "Opaque union optical preparation, coverage and reopen transitions: pass\n";

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

    // A backing card exposes only its bottom seven pixels. Its front-card
    // cutout hides paint; it must not become a second refracting edge. DWM
    // may provide an intermediate clipped to the backing's outer contour,
    // so bending downwards from the cutout samples transparent black beyond
    // the real bottom edge. Keep refraction enabled and compare the exposed
    // strip with the same physical card before its front card occludes it.
    auto backingRegion = coverageRegion;
    backingRegion.x = 78;
    backingRegion.y = 107;
    backingRegion.width = 356;
    backingRegion.height = 72;
    backingRegion.shape = 1u;
    backingRegion.cornerRadius = 16;
    backingRegion.topLeftRadius = backingRegion.topRightRadius =
        backingRegion.bottomLeftRadius = backingRegion.bottomRightRadius = 16;
    backingRegion.refractionStrength = 20;
    backingRegion.dispersionStrength = 2;
    backingRegion.edgeThickness = 22;
    backingRegion.rimStrength = 0.25f;
    backingRegion.lightPhase = -1;
    backingRegion.tintAlpha = 0.08f;
    backingRegion.exclusion.x = -10;
    backingRegion.exclusion.y = -7;
    backingRegion.exclusion.width = 376;
    backingRegion.exclusion.height = 72;
    backingRegion.exclusion.cornerRadius = 16;
    backingRegion.exclusion.topLeftRadius = backingRegion.exclusion.topRightRadius =
        backingRegion.exclusion.bottomLeftRadius = backingRegion.exclusion.bottomRightRadius = 16;
    backingRegion.exclusion.shape = 1u;
    {
        auto backing = backingRegion;
        backing.hasExclusion = true;
        const D3D11_RECT fullOutput { backing.x, backing.y,
            backing.x + LONG(backing.width), backing.y + LONG(backing.height) };
        const auto exposed = vibrance::win32::liquid_glass_output_bounds(backing, fullOutput);
        if (exposed.left != fullOutput.left || exposed.right != fullOutput.right ||
            exposed.bottom != fullOutput.bottom || exposed.top <= fullOutput.top + 40)
            return 92;
        backing.hasExclusion = false;
        const auto complete = vibrance::win32::liquid_glass_output_bounds(backing, fullOutput);
        if (std::memcmp(&complete, &fullOutput, sizeof(fullOutput)) != 0) return 92;
        std::cout << "Backing-card output rows: " << exposed.bottom - exposed.top
                  << " instead of " << fullOutput.bottom - fullOutput.top
                  << "; source sampling halo retained\n";
    }
    std::vector<std::uint32_t> clippedCardPixels(width * height);
    for (UINT y = 0; y < height; ++y)
        for (UINT x = 0; x < width; ++x)
        {
            const float qx = std::abs(x + 0.5f - (78 + 356 * 0.5f)) - (356 * 0.5f - 16);
            const float qy = std::abs(y + 0.5f - (107 + 72 * 0.5f)) - (72 * 0.5f - 16);
            const float dx = std::max(qx, 0.0f), dy = std::max(qy, 0.0f);
            const float distance = std::sqrt(dx * dx + dy * dy) +
                std::min(std::max(qx, qy), 0.0f) - 16;
            clippedCardPixels[y * width + x] = distance <= 0 ? 0xff60c090u : 0u;
        }
    D3D11_SUBRESOURCE_DATA clippedCardData { clippedCardPixels.data(), width * 4u, 0 };
    ComPtr<ID3D11Texture2D> clippedCardBackdrop;
    if (FAILED(device->CreateTexture2D(&whiteDesc, &clippedCardData,
            &clippedCardBackdrop))) return 82;

    // Model the Composition raw dependency before the optical shader runs.
    // Subtracting the front card here removes source texels that the exposed
    // backing strip still refracts. The exclusion belongs to output coverage.
    VibranceCompositionRegion dependencyRegion {};
    dependencyRegion.width = backingRegion.width;
    dependencyRegion.height = backingRegion.height;
    dependencyRegion.hasExclusion = 1u;
    dependencyRegion.exclusion.x = backingRegion.exclusion.x;
    dependencyRegion.exclusion.y = backingRegion.exclusion.y;
    dependencyRegion.exclusion.width = backingRegion.exclusion.width;
    dependencyRegion.exclusion.height = backingRegion.exclusion.height;
    dependencyRegion.exclusion.cornerRadius = backingRegion.exclusion.cornerRadius;
    const auto rawDependency = vibrance::win32::composition_raw_backdrop_source_region(
        dependencyRegion);
    const auto dependencyPixels = [&](bool subtractExclusion) {
        auto result = clippedCardPixels;
        if (!subtractExclusion) return result;
        const auto& cutout = dependencyRegion.exclusion;
        for (UINT y = 0; y < height; ++y)
            for (UINT x = 0; x < width; ++x)
            {
                const float qx = std::abs(x + 0.5f - backingRegion.x -
                    cutout.x - cutout.width * 0.5f) -
                    (cutout.width * 0.5f - cutout.cornerRadius);
                const float qy = std::abs(y + 0.5f - backingRegion.y -
                    cutout.y - cutout.height * 0.5f) -
                    (cutout.height * 0.5f - cutout.cornerRadius);
                const float dx = std::max(qx, 0.0f), dy = std::max(qy, 0.0f);
                const float distance = std::hypot(dx, dy) +
                    std::min(std::max(qx, qy), 0.0f) - cutout.cornerRadius;
                if (distance <= 0.0f) result[y * width + x] = 0u;
            }
        return result;
    };
    const auto oldDependencyPixels = dependencyPixels(true);
    const auto retainedDependencyPixels = dependencyPixels(rawDependency.hasExclusion != 0u);
    const D3D11_SUBRESOURCE_DATA oldDependencyData {
        oldDependencyPixels.data(), width * 4u, 0 };
    const D3D11_SUBRESOURCE_DATA retainedDependencyData {
        retainedDependencyPixels.data(), width * 4u, 0 };
    ComPtr<ID3D11Texture2D> oldDependencyBackdrop, retainedDependencyBackdrop;
    if (FAILED(device->CreateTexture2D(&whiteDesc, &oldDependencyData,
            &oldDependencyBackdrop)) ||
        FAILED(device->CreateTexture2D(&whiteDesc, &retainedDependencyData,
            &retainedDependencyBackdrop))) return 90;
    for (const float radius : { 7.0f, 30.0f })
    {
        backingRegion.blurRadius = radius;
        backingRegion.hasExclusion = false;
        backingRegion.viewportFade = {};
        if (!renderSample(clippedCardBackdrop.Get(), backingRegion)) return 82;
        const auto physicalCardReference = samplePixels;
        backingRegion.hasExclusion = true;
        for (const bool viewportEnabled : { false, true })
        {
            backingRegion.viewportRect = { -78, 33, 512, 80 };
            backingRegion.viewportFade = { 0, 0, 1, viewportEnabled ? 1.0f : 0.0f };
            if (!renderSample(clippedCardBackdrop.Get(), backingRegion)) return 82;
            const auto fullStripReference = samplePixels;
            std::size_t stripFailures = 0, darkPixels = 0;
            for (UINT y = 172; y < 179; ++y)
                for (UINT x = 130; x < 380; ++x)
                {
                    const auto index = y * width + x;
                    if (!nearlySamePixel(samplePixels[index], physicalCardReference[index]))
                        ++stripFailures;
                    if (((samplePixels[index] >> 8u) & 0xffu) < 128u) ++darkPixels;
                }
            const auto greenAt = [&](UINT y) { return (samplePixels[y * width + 256] >> 8u) & 0xffu; };
            const bool bottomRimRetained = greenAt(178) > greenAt(175) + 15u;
            std::cout << "Backing-card refraction, blur " << radius
                      << ", viewport " << viewportEnabled << ": " << stripFailures
                      << " reference failures, " << darkPixels << " dark pixels, bottom rim "
                      << (bottomRimRetained ? "retained" : "missing") << '\n';
            if (stripFailures || darkPixels || !bottomRimRetained) return 83;

            if (!renderSample(oldDependencyBackdrop.Get(), backingRegion)) return 90;
            const auto starvedPixels = samplePixels;
            if (!renderSample(retainedDependencyBackdrop.Get(), backingRegion)) return 90;
            std::size_t starvedStripPixels = 0, retainedStripFailures = 0;
            for (UINT y = 172; y < 179; ++y)
                for (UINT x = 130; x < 380; ++x)
                {
                    const auto index = y * width + x;
                    if (((starvedPixels[index] >> 8u) & 0xffu) + 16u <
                            ((fullStripReference[index] >> 8u) & 0xffu))
                        ++starvedStripPixels;
                    if (!nearlySamePixel(samplePixels[index], fullStripReference[index]))
                        ++retainedStripFailures;
                }
            std::cout << "Raw backing dependency, blur " << radius
                      << ", viewport " << viewportEnabled << ": " << starvedStripPixels
                      << " darkened pixels with exclusion, " << retainedStripFailures
                      << " corrected-source differences\n";
            if (starvedStripPixels == 0u || retainedStripFailures != 0u) return 91;

            context->ClearRenderTargetView(target.Get(), dark);
            for (LONG y = 0; y < static_cast<LONG>(height); y += 8)
            {
                const D3D11_RECT clip { 0, y, static_cast<LONG>(width),
                    std::min(y + 8, static_cast<LONG>(height)) };
                if (!lens.render(context.Get(), clippedCardBackdrop.Get(), target.Get(),
                        backingRegion, 0, 0, &clip)) return 82;
            }
            context->CopyResource(readback.Get(), backdrop.Get());
            if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 82;
            std::size_t tiledFailures = 0;
            for (UINT y = 0; y < height; ++y)
            {
                const auto* row = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
                for (UINT x = 0; x < width; ++x)
                    if (row[x] != fullStripReference[y * width + x]) ++tiledFailures;
            }
            context->Unmap(readback.Get(), 0);
            std::cout << "Backing-card tiled draw differences: " << tiledFailures << '\n';
            if (tiledFailures) return 84;
        }
    }

    // A thin strip still refracts into pixels behind its predecessor. With
    // reflection disabled, allocation must include displacement PLUS Gaussian
    // support; the old full-height lens hid a missing combined margin.
    std::size_t thinStripFailures = 0u;
    for (const float radius : { 5.0f, 9.0f, 12.0f })
    for (const float strength : { 20.0f, 40.0f, 80.0f })
    {
        auto thin = backingRegion;
        thin.viewportFade = {};
        thin.rimStrength = 0.0f;
        thin.blurRadius = radius;
        thin.refractionStrength = strength;
        thin.hasExclusion = false;
        if (!renderSample(stripedBackdrop.Get(), thin)) return 93;
        const auto complete = samplePixels;
        thin.hasExclusion = true;
        thin.exclusion.shape = 0u;
        thin.exclusion.height = float(thin.height) + 6.0f;
        if (!renderSample(stripedBackdrop.Get(), thin)) return 93;
        const UINT y = thin.y + thin.height - 1u;
        for (UINT x = 130u; x < 380u; ++x)
            if (!nearlySamePixel(samplePixels[y * width + x], complete[y * width + x]))
                ++thinStripFailures;
    }
    std::cout << "Thin backing strip, no rim, displaced Gaussian reference failures: "
              << thinStripFailures << '\n';
    if (thinStripFailures != 0u) return 94;

    const std::vector<std::uint32_t> neutralPixels(width * height, 0xff606060u);
    D3D11_SUBRESOURCE_DATA neutralData { neutralPixels.data(),
        width * sizeof(std::uint32_t), 0 };
    ComPtr<ID3D11Texture2D> neutralBackdrop;
    if (FAILED(device->CreateTexture2D(&whiteDesc, &neutralData,
            &neutralBackdrop))) return 50;
    // A joined contour has one outer rim. The old main shell's edge inside
    // the neck cannot generate a second bright line through the union.
    unionRegion.rimStrength = 0.0f;
    if (!renderSample(neutralBackdrop.Get(), unionRegion)) return 50;
    const auto unlitTopEdge = unionPixel(96u, 0u) & 0xffu;
    const auto unlitBottomEdge = unionPixel(96u, 95u) & 0xffu;
    const auto unlitSideEdge = unionPixel(0u, 48u) & 0xffu;
    unionRegion.rimStrength = 1.0f;
    if (!renderSample(neutralBackdrop.Get(), unionRegion)) return 50;
    const auto internalEdge = unionPixel(191u, 48u) & 0xffu;
    const auto topEdge = unionPixel(96u, 0u) & 0xffu;
    const auto bottomEdge = unionPixel(96u, 95u) & 0xffu;
    const auto sideEdge = unionPixel(0u, 48u) & 0xffu;
    std::cout << "Union rim, internal edge: " << internalEdge
              << ", top: " << topEdge << ", bottom: " << bottomEdge
              << ", side: " << sideEdge << '\n';
    if (std::abs(static_cast<int>(internalEdge) - 96) > 1 ||
        topEdge < unlitTopEdge + 30u || bottomEdge < unlitBottomEdge + 30u ||
        sideEdge >= std::min(topEdge, bottomEdge))
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

    // Model several real windows crossing one lens, including black panels
    // and DWM targets whose alpha channel is unused. The destination already
    // contains those windows; tint must cover all of them continuously.
    constexpr std::uint32_t windowColours[] = {
        0x000000u, 0x010101u, 0x040404u, 0x070707u,
        0x101010u, 0x303030u, 0xf0f0f0u, 0x2070b0u
    };
    std::size_t backdropCoverageFailures = 0u;
    std::size_t backdropBlurFailures = 0u;
    std::array<std::vector<std::uint32_t>, 4u> opaqueWindowReferences;
    for (const std::uint32_t sourceAlpha : { 0xff000000u, 0x10000000u, 0u })
    {
        for (UINT y = 0u; y < height; ++y)
            for (UINT x = 0u; x < width; ++x)
                pixels[y * width + x] = sourceAlpha | windowColours[(x / 32u + y / 32u) % 8u];
        D3D11_SUBRESOURCE_DATA windowData { pixels.data(), width * sizeof(std::uint32_t), 0 };
        ComPtr<ID3D11Texture2D> windowBackdrop;
        if (FAILED(device->CreateTexture2D(&whiteDesc, &windowData, &windowBackdrop))) return 12;
        for (const float tint : { 0.0f, 0.5f, 1.0f })
        for (const float opacity : { 0.5f, 1.0f })
        for (const bool clipped : { false, true })
        {
            auto windowRegion = coverageRegion;
            windowRegion.tintRed = 0.2f;
            windowRegion.tintGreen = 0.55f;
            windowRegion.tintBlue = 0.8f;
            windowRegion.tintAlpha = tint;
            windowRegion.opacity = opacity;
            const D3D11_RECT windowClip { 192, 64, 384, 192 };
            context->CopyResource(backdrop.Get(), windowBackdrop.Get());
            if (!lens.render(context.Get(), windowBackdrop.Get(), target.Get(), windowRegion,
                    0, 0, clipped ? &windowClip : nullptr)) return 13;
            context->CopyResource(readback.Get(), backdrop.Get());
            if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 14;
            // Stay inside the silhouette so expected coverage is exactly one.
            for (UINT y = 72u; y < 184u; ++y)
            {
                const auto* row = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
                for (UINT x = 96u; x < 416u; ++x)
                {
                    const bool inside = !clipped || (x >= 192u && x < 384u);
                    const float tintChannels[] = { windowRegion.tintRed, windowRegion.tintGreen, windowRegion.tintBlue };
                    for (UINT channel = 0u; channel < 3u; ++channel)
                    {
                        const float source = float((pixels[y * width + x] >> (8u * channel)) & 255u);
                        const float tinted = source * (1.0f - tint) + 255.0f * tintChannels[channel] * tint;
                        const float expected = inside ? tinted * opacity + source * (1.0f - opacity) : source;
                        const auto actual = (row[x] >> (8u * channel)) & 255u;
                        if (std::abs(float(actual) - expected) > 2.0f) ++backdropCoverageFailures;
                    }
                }
            }
            context->Unmap(readback.Get(), 0);
        }
        std::size_t referenceIndex = 0u;
        for (const float radius : { 7.0f, 30.0f })
        for (const float tint : { 0.0f, 0.6f })
        {
            auto windowRegion = coverageRegion;
            windowRegion.blurRadius = radius;
            windowRegion.tintAlpha = tint;
            if (!renderSample(windowBackdrop.Get(), windowRegion)) return 13;
            auto& reference = opaqueWindowReferences[referenceIndex++];
            if (sourceAlpha == 0xff000000u) reference = samplePixels;
            else
                for (std::size_t pixel = 0u; pixel < samplePixels.size(); ++pixel)
                    if (!nearlySamePixel(samplePixels[pixel], reference[pixel])) ++backdropBlurFailures;
        }
    }
    std::cout << "Dark and mixed-window backdrop coverage failures: " << backdropCoverageFailures << '\n';
    std::cout << "Mixed-window blurred source-alpha reference failures: " << backdropBlurFailures << '\n';
    if (backdropCoverageFailures != 0u || backdropBlurFailures != 0u) return 15;
    // A valid DWM texture can still contain unavailable black areas. Its
    // separate destination models the desktop composited behind that surface.
    // No tint must preserve that desktop exactly, including at the lens rim;
    // tint must remain continuous without replacing it with opaque black.
    std::size_t unavailableBackdropFailures = 0u;
    std::vector<std::uint32_t> desktopPixels(width * height);
    for (UINT y = 0u; y < height; ++y)
        for (UINT x = 0u; x < width; ++x)
            desktopPixels[y * width + x] = 0xff000000u | windowColours[(x / 32u + y / 32u) % 8u];
    D3D11_SUBRESOURCE_DATA desktopData { desktopPixels.data(), width * sizeof(std::uint32_t), 0 };
    ComPtr<ID3D11Texture2D> desktopBackdrop;
    if (FAILED(device->CreateTexture2D(&whiteDesc, &desktopData, &desktopBackdrop))) return 12;
    for (const std::uint32_t emptyPixel : { 0u, 0xff000000u })
    for (const bool partiallyAvailable : { false, true })
    {
        std::fill(pixels.begin(), pixels.end(), emptyPixel);
        if (partiallyAvailable)
            for (UINT y = 0u; y < height; ++y)
                for (UINT x = 0u; x < width / 2u; ++x)
                    pixels[y * width + x] = (emptyPixel & 0xff000000u) | (desktopPixels[y * width + x] & 0xffffffu);
        D3D11_SUBRESOURCE_DATA emptyData { pixels.data(), width * sizeof(std::uint32_t), 0 };
        ComPtr<ID3D11Texture2D> unavailableBackdrop;
        if (FAILED(device->CreateTexture2D(&whiteDesc, &emptyData, &unavailableBackdrop))) return 12;
        for (const float tint : { 0.0f, 0.5f, 1.0f })
        for (const float radius : { 0.0f, 7.0f, 30.0f })
        {
            // Isolate tint continuity across the available/unavailable seam.
            // Full-source tests above cover the blur across real windows.
            if (partiallyAvailable && radius != 0.0f) continue;
            auto fallbackRegion = coverageRegion;
            fallbackRegion.tintAlpha = tint;
            fallbackRegion.rimStrength = partiallyAvailable ? 0.0f : 0.6f;
            fallbackRegion.blurRadius = radius;
            context->CopyResource(backdrop.Get(), desktopBackdrop.Get());
            if (!lens.render(context.Get(), unavailableBackdrop.Get(), target.Get(), fallbackRegion)) return 13;
            context->CopyResource(readback.Get(), backdrop.Get());
            if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 14;
            for (UINT y = 0u; y < height; ++y)
            {
                const auto* row = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
                for (UINT x = 0u; x < width; ++x)
                {
                    if (tint == 0.0f)
                    {
                        if (row[x] != desktopPixels[y * width + x]) ++unavailableBackdropFailures;
                    }
                    else if (x >= 96u && x < 416u && y >= 72u && y < 184u)
                    {
                        const float tintChannels[] = { fallbackRegion.tintRed, fallbackRegion.tintGreen, fallbackRegion.tintBlue };
                        for (UINT channel = 0u; channel < 3u; ++channel)
                        {
                            const float desktop = float((desktopPixels[y * width + x] >> (8u * channel)) & 255u);
                            const float expected = desktop * (1.0f - tint) + 255.0f * tintChannels[channel] * tint;
                            const auto actual = (row[x] >> (8u * channel)) & 255u;
                            if (std::abs(float(actual) - expected) > 2.0f) ++unavailableBackdropFailures;
                        }
                    }
                }
            }
            context->Unmap(readback.Get(), 0);
        }
    }
    std::cout << "Unavailable backdrop transparency and rim failures: " << unavailableBackdropFailures << '\n';
    if (unavailableBackdropFailures != 0u) return 15;
    // Real opaque windows often contain sparse white text over black. Draw
    // into that same desktop target: a faded optical pass otherwise retains
    // the original sharp white pixels even though its blurred input is valid.
    std::fill(pixels.begin(), pixels.end(), 0xff000000u);
    for (UINT y = 80u; y < 176u; y += 24u)
        for (UINT x = 112u; x < 400u; x += 24u)
            for (UINT stroke = 0u; stroke < 6u; ++stroke)
                pixels[(y + stroke) * width + x] = 0xffffffffu;
    context->UpdateSubresource(backdrop.Get(), 0, nullptr, pixels.data(), width * sizeof(std::uint32_t), 0);
    auto sparseTextRegion = coverageRegion;
    sparseTextRegion.blurRadius = 30.0f;
    if (!lens.render(context.Get(), backdrop.Get(), target.Get(), sparseTextRegion)) return 13;
    context->CopyResource(readback.Get(), backdrop.Get());
    if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 14;
    std::size_t retainedSharpText = 0u;
    for (UINT y = 80u; y < 176u; y += 24u)
    {
        const auto* row = reinterpret_cast<const std::uint32_t*>(
            static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
        for (UINT x = 112u; x < 400u; x += 24u)
            if ((row[x] & 255u) > 32u) ++retainedSharpText;
    }
    context->Unmap(readback.Get(), 0);
    std::cout << "Sparse white text retained after full blur: " << retainedSharpText << '\n';
    if (retainedSharpText != 0u) return 82;

    // A refracted tap can land on perfectly black source pixels while the
    // original desktop pixel contains a bright glyph. Treating that tap as
    // unavailable blends the original glyph back in, breaking refraction at
    // exactly those dark transitions. Check both edges on the live target,
    // including unused source alpha and disjoint native compositor clips.
    std::size_t refractionGlyphFailures = 0u;
    std::size_t refractionOutsideFailures = 0u;
    std::size_t refractionGlyphSamples = 0u;
    for (const std::uint32_t sourceAlpha : { 0u, 0xff000000u })
    {
        std::fill(pixels.begin(), pixels.end(), sourceAlpha);
        const UINT glyphColumns[] = { static_cast<UINT>(region.x) + 6u,
            static_cast<UINT>(region.x) + region.width - 7u };
        for (UINT y = 80u; y < 176u; y += 24u)
            for (const UINT x : glyphColumns)
                for (UINT stroke = 0u; stroke < 6u; ++stroke)
                    pixels[(y + stroke) * width + x] = sourceAlpha | 0xffffffu;
        for (const UINT x : { 30u, 480u })
            pixels[100u * width + x] = sourceAlpha | 0xffffffu;
        D3D11_SUBRESOURCE_DATA glyphData { pixels.data(), width * sizeof(std::uint32_t), 0 };
        ComPtr<ID3D11Texture2D> glyphBackdrop;
        if (FAILED(device->CreateTexture2D(&whiteDesc, &glyphData, &glyphBackdrop))) return 12;
        auto refractedTextRegion = coverageRegion;
        refractedTextRegion.shape = 0u;
        refractedTextRegion.edgeThickness = 28.0f;
        refractedTextRegion.refractionStrength = 24.0f;
        for (const unsigned clipSide : { 0u, 1u, 2u })
        {
            const LONG centreX = region.x + static_cast<LONG>(region.width / 2u);
            const D3D11_RECT glyphClip {
                clipSide == 2u ? centreX : region.x, region.y,
                clipSide == 1u ? centreX : region.x + static_cast<LONG>(region.width),
                region.y + static_cast<LONG>(region.height) };
            context->CopyResource(backdrop.Get(), glyphBackdrop.Get());
            if (!lens.render(context.Get(), glyphBackdrop.Get(), target.Get(),
                    refractedTextRegion, 0, 0, clipSide ? &glyphClip : nullptr)) return 13;
            context->CopyResource(readback.Get(), backdrop.Get());
            if (FAILED(context->Map(readback.Get(), 0, D3D11_MAP_READ, 0, &mapped))) return 14;
            for (UINT y = 80u; y < 176u; y += 24u)
                for (UINT stroke = 0u; stroke < 6u; ++stroke)
                {
                    const auto* row = reinterpret_cast<const std::uint32_t*>(
                        static_cast<const std::uint8_t*>(mapped.pData) +
                        (y + stroke) * mapped.RowPitch);
                    for (const UINT x : glyphColumns)
                    {
                        const bool affected = !clipSide ||
                            (x >= static_cast<UINT>(glyphClip.left) &&
                                x < static_cast<UINT>(glyphClip.right));
                        const UINT expected = affected ? 0u : 255u;
                        if (std::abs(static_cast<int>(row[x] & 255u) -
                                static_cast<int>(expected)) > 1) ++refractionGlyphFailures;
                        ++refractionGlyphSamples;
                    }
                }
            for (UINT y = 0u; y < height; ++y)
            {
                const auto* row = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(mapped.pData) + y * mapped.RowPitch);
                for (UINT x = 0u; x < width; ++x)
                    if (x < static_cast<UINT>(region.x) ||
                        x >= static_cast<UINT>(region.x) + region.width ||
                        y < static_cast<UINT>(region.y) ||
                        y >= static_cast<UINT>(region.y) + region.height)
                        if (row[x] != pixels[y * width + x]) ++refractionOutsideFailures;
            }
            context->Unmap(readback.Get(), 0);
        }
    }
    std::cout << "Sparse text refraction failures: " << refractionGlyphFailures
              << " / " << refractionGlyphSamples << ", outside lens: "
              << refractionOutsideFailures << '\n';
    if (refractionGlyphFailures != 0u || refractionOutsideFailures != 0u) return 83;
    // A genuinely unavailable resource is rejected before any GPU submission.
    if (lens.render(context.Get(), nullptr, target.Get(), coverageRegion)) return 15;
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
                originRegion.dispersionStrength + 2.0f,
            originRegion.rimStrength > 0.0f ? std::min(static_cast<float>(
                std::min(originRegion.width, originRegion.height)) * 1.6f, 120.0f) + 2.0f : 0.0f,
            4.0f })));
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

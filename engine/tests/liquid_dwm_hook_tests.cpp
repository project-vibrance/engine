// Exercise the production adapters with controlled native objects. DllMain is
// never invoked and the fixture neither opens nor modifies the running DWM.
#include "../src/platform/win32/liquid_glass_dwm_present.cpp"

#include <iostream>
#include <limits>
#include <latch>
#include <thread>

namespace
{
unsigned normalDraws = 0u;
HRESULT draw_spy(void*, void*, void*) { ++normalDraws; return S_FALSE; }
HRESULT entry_draw_spy(void*, void*, const void*, float, std::uint32_t) { return S_FALSE; }
template <std::size_t Size, typename Value>
void write_at(std::array<std::uint8_t, Size>& bytes, std::size_t offset, const Value& value)
{
    std::memcpy(bytes.data() + offset, &value, sizeof(value));
}
}

int main()
{
    int failures = 0;
    const auto check = [&](bool value, const char* message) {
        if (!value) { std::cerr << message << '\n'; ++failures; }
    };
    // Deliver real window messages to the production observer without loading
    // the extension into DWM. Other sessions must not revoke this session.
    Windows10LifecycleMonitor monitor;
    monitor.sessionId = 7u;
    WNDCLASSW fixtureWindowClass {};
    fixtureWindowClass.hInstance = GetModuleHandleW(nullptr);
    fixtureWindowClass.lpfnWndProc = &Windows10LifecycleMonitor::window_proc;
    fixtureWindowClass.lpszClassName = L"VibranceWindows10SessionFixture";
    check(RegisterClassW(&fixtureWindowClass) != 0u, "The session fixture must register its window");
    HWND fixtureWindow = CreateWindowExW(0u, fixtureWindowClass.lpszClassName, L"", WS_POPUP,
        0, 0, 0, 0, nullptr, nullptr, fixtureWindowClass.hInstance, &monitor);
    check(fixtureWindow != nullptr, "The session fixture must create its hidden window");
    if (fixtureWindow)
    {
        auto generation = monitor.gate.generation();
        SendMessageW(fixtureWindow, WM_WTSSESSION_CHANGE, WTS_REMOTE_DISCONNECT, 8u);
        check(monitor.gate.generation() == generation && !monitor.sessionPolicy.blocked(),
            "Another session's disconnect must not block this session");
        SendMessageW(fixtureWindow, WM_WTSSESSION_CHANGE, WTS_REMOTE_DISCONNECT, 7u);
        check(monitor.gate.generation() != generation && monitor.sessionPolicy.blocked(),
            "This session's disconnect must immediately invalidate its admitted generation");
        generation = monitor.gate.generation();
        SendMessageW(fixtureWindow, WM_WTSSESSION_CHANGE, WTS_REMOTE_CONNECT, 7u);
        check(monitor.gate.generation() != generation &&
            monitor.sessionPolicy.allows(vibrance::win32::LiquidDwmWindows10Session::remote),
            "A remote reconnect must allow fresh samples without requiring console attachment");
        generation = monitor.gate.generation();
        SendMessageW(fixtureWindow, WM_DISPLAYCHANGE, 0u, 0u);
        check(monitor.gate.generation() != generation, "Display broadcasts must retain their transition guard");
        DestroyWindow(fixtureWindow);
    }
    UnregisterClassW(fixtureWindowClass.lpszClassName, fixtureWindowClass.hInstance);
    // Separate real WARP devices expose accidental cross-device submissions
    // without resetting a GPU or installing anything in the live compositor.
    ComPtr<ID3D11Device> firstDevice, secondDevice;
    ComPtr<ID3D11DeviceContext> firstContext;
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0u,
        nullptr, 0u, D3D11_SDK_VERSION, &firstDevice, nullptr, &firstContext)),
        "The device-lifetime fixture must create its first WARP device");
    check(SUCCEEDED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, 0u,
        nullptr, 0u, D3D11_SDK_VERSION, &secondDevice, nullptr, nullptr)),
        "The device-lifetime fixture must create its second WARP device");
    if (firstDevice && secondDevice && firstContext)
    {
        ComPtr<ID3D11Device> contextDevice;
        firstContext->GetDevice(&contextDevice);
        check(windows10_device_matches(firstDevice.Get(), contextDevice.Get()),
            "A native context and its device must have matching COM identity");
        check(!windows10_device_matches(firstDevice.Get(), secondDevice.Get()) &&
            !windows10_device_matches(firstDevice.Get(), nullptr),
            "Foreign or missing native devices must not reach the optical pass");
        rendererDevice = firstDevice;
        D3D11_TEXTURE2D_DESC textureDescription {};
        textureDescription.Width = 32u; textureDescription.Height = 32u;
        textureDescription.MipLevels = 1u; textureDescription.ArraySize = 1u;
        textureDescription.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        textureDescription.SampleDesc.Count = 1u;
        ComPtr<ID3D11Texture2D> background;
        check(SUCCEEDED(firstDevice->CreateTexture2D(&textureDescription, nullptr, &background)),
            "The resource retirement fixture must create its background");
        displayBackgrounds[0].texture = background;
        captureBackgrounds[0].texture = background;
        ddaBackgrounds[0].texture = background;
        std::latch acquired(1), released(1);
        std::thread activeDraw([&] {
            std::lock_guard<std::mutex> busy(rendererMutex);
            acquired.count_down();
            released.wait();
        });
        acquired.wait();
        check(!retire_windows10_gpu_resources() && rendererDevice,
            "Resource retirement must defer without blocking an active draw");
        released.count_down();
        activeDraw.join();
        check(retire_windows10_gpu_resources() && !rendererDevice && !displayBackgrounds[0].texture &&
            !captureBackgrounds[0].texture && !ddaBackgrounds[0].texture,
            "Quiescent retirement must release all cached native GPU resources");
    }
    const auto now = GetTickCount64();
    if (now >= LiquidDwmWindows10RenderGate::settlingMilliseconds)
    {
        for (auto tick = now - LiquidDwmWindows10RenderGate::settlingMilliseconds; tick <= now; tick += 250u)
            windows10Lifecycle.gate.observe(tick, windows10Lifecycle.gate.generation(), true);
        check(windows10Lifecycle.gate.allows(now), "The device loss fixture must first admit a stable session");
    }
    readyEvent = CreateEventW(nullptr, TRUE, TRUE, nullptr);
    check(readyEvent != nullptr, "The device loss fixture must create its private readiness event");
    windows10_suspend_optical_render(DXGI_ERROR_DEVICE_REMOVED);
    check(!windows10Lifecycle.gate.allows(GetTickCount64()) &&
        readyEvent && WaitForSingleObject(readyEvent, 0u) == WAIT_TIMEOUT,
        "Device loss must immediately revoke rendering and native readiness");
    if (readyEvent) CloseHandle(readyEvent);
    readyEvent = nullptr;
    alignas(16) std::array<std::uint8_t, 512> module {};
    alignas(16) std::array<std::uint8_t, 256> entry {};
    alignas(16) std::array<std::uint8_t, 128> geometry {};
    alignas(16) std::array<std::uint8_t, 256> primitive {};
    alignas(16) std::array<std::uint8_t, 128> brush {};
    alignas(16) std::array<std::uint8_t, 640> renderDevice {};
    alignas(16) std::array<std::uint8_t, 128> executionContext {};
    dwmcoreModule = reinterpret_cast<HMODULE>(module.data());
    windows10Adapter = true;
    hookConfig.drawListEntryVtableRva = 32u;
    hookConfig.colourBrushVtableRva = 64u;
    const auto entryVtable = reinterpret_cast<std::uintptr_t>(module.data() + 32u);
    const auto brushVtable = reinterpret_cast<std::uintptr_t>(module.data() + 64u);
    write_at(entry, 0x10u, entryVtable);
    write_at(entry, 0xb8u, geometry.data());
    write_at(entry, 0xc0u, primitive.data());
    // A retained entry has no queued primitive until Render appends one.
    const std::array<float, 4> bounds { 0.0f, 0.0f, 300.0f, 100.0f };
    write_at(geometry, 0x48u, bounds);
    write_at(brush, 0x68u, brushVtable);
    const std::array<float, 4> colour { liquidMarkerRed / 255.0f,
        liquidMarkerGreen / 255.0f, liquidMarkerBlue / 255.0f, liquidMarkerAlpha / 255.0f };
    write_at(brush, 0x50u, colour);
    float observedColour[4] {};
    check(read_colour_brush(brush.data() + 0x68u, observedColour) && marker_slot(observedColour) == 0u,
        "The Windows 10 IUnknown brush base must decode the correct marker colour");
    LiquidDwmRegion region;
    region.width = 300u; region.height = 100u; region.active = 1u;
    region.hwnd = 1u; region.ownerPid = GetCurrentProcessId(); region.regionId = 42u;
    tag_native_marker_entry(entry.data(), 0u, 300.0f, 100.0f, region);
    LiquidNativeEntryIdentity emptyIdentity {};
    check(read_liquid_native_entry_identity(entry.data(), emptyIdentity),
        "An empty Windows 10 primitive queue must not invalidate the entry");
    write_at(entry, 0x40u, primitive.data());
    write_at(entry, 0x48u, primitive.data() + 0x4cu);
    write_at(entry, 0x50u, primitive.data() + 0x4cu);
    LiquidNativeEntryIdentity queuedIdentity {};
    check(read_liquid_native_entry_identity(entry.data(), queuedIdentity) &&
        liquid_native_entry_identity_matches(emptyIdentity, queuedIdentity),
        "Appending Windows 10 render work must preserve the retained marker identity");
    originalCommonEffectEmit = &draw_spy;
    originalBrushEffectEmit = &draw_spy;
    const std::array<float, 16> matrix { 1,0,0,0, 0,1,0,0, 0,0,1,0, 120,45,0,1 };
    write_at(executionContext, 0x08u, renderDevice.data());
    auto* context = reinterpret_cast<ID3D11DeviceContext*>(0x1234u);
    write_at(renderDevice, 0x258u, context);
    write_at(primitive, 0x08u, matrix);
    LiquidNativeBatchMarker marker;
    check(read_windows10_marker(entry.data() + 0x10u, executionContext.data(), marker) && marker.complete,
        "A tagged Windows 10 entry must reach its native device context");
    check(marker.entry == entry.data() && marker.context == context && marker.slot == 0u,
        "The Windows 10 CBaseDrawListEntry adjustment must preserve marker identity");
    check(std::equal(matrix.begin(), matrix.end(), marker.matrix) &&
        std::equal(bounds.begin(), bounds.end(), marker.bounds), "Native matrix and bounds were changed");
    D3D11_TEXTURE2D_DESC output {};
    output.Width = 512u; output.Height = 256u;
    LiquidGlassRenderRegion lens;
    check(liquid_native_batch_region(marker, region, output, lens) &&
        lens.x == 120 && lens.y == 45 && lens.width == 300u && lens.height == 100u,
        "Windows 10 markers must use their native output transform for lens placement");
    write_at(geometry, 0x48u, std::array<float, 4> { 1,0,300,100 });
    marker = {};
    check(!read_windows10_marker(entry.data() + 0x10u, executionContext.data(), marker),
        "Recycled geometry must invalidate its retained marker tag");
    check(windows10_common_emit_hook(nullptr, executionContext.data(), entry.data() + 0x10u) == S_FALSE &&
        windows10_brush_emit_hook(nullptr, executionContext.data(), entry.data() + 0x10u) == S_FALSE && normalDraws == 2u,
        "Ordinary draws must be forwarded through both Windows 10 geometry hooks");
    tag_native_marker_entry(entry.data(), 0u, 300.0f, 100.0f, region);
    marker = {};
    check(read_windows10_marker(entry.data() + 0x10u, executionContext.data(), marker) && marker.complete,
        "The queued transform must decode without an execution-context setter callback");
    originalDrawListEntryRender = &entry_draw_spy;
    inContentDraw = true;
    check(draw_list_entry_render_hook(entry.data(), nullptr, matrix.data(), 1.0f, 0u) == S_FALSE,
        "Ordinary brush reuse must forward the original retained-entry draw");
    inContentDraw = false;
    marker = {};
    check(!read_windows10_marker(entry.data() + 0x10u, executionContext.data(), marker),
        "Ordinary brush reuse must remove the old marker despite identical geometry");
    tag_native_marker_entry(entry.data(), 0u, 300.0f, 100.0f, region);
    check(draw_list_entry_render_hook(entry.data(), nullptr, matrix.data(), 1.0f, 0u) == S_FALSE,
        "Cached draws outside brush generation must be forwarded");
    marker = {};
    check(read_windows10_marker(entry.data() + 0x10u, executionContext.data(), marker),
        "Cached marker draws outside brush generation must retain their tag");
    std::array<float, 16> invalid = matrix; invalid[0] = std::numeric_limits<float>::quiet_NaN();
    write_at(primitive, 0x08u, invalid);
    marker = {};
    check(read_windows10_marker(entry.data() + 0x10u, executionContext.data(), marker) && !marker.complete,
        "Invalid queued transforms must not reach the renderer");
    write_at(primitive, 0x08u, matrix);
    Windows10PrimitiveQueue queue;
    check(read_windows10_primitive_queue(entry.data(), queue) && consume_windows10_primitive(entry.data(), queue),
        "The marker emitter must consume a single queued primitive");
    check(!read_windows10_primitive_queue(entry.data(), queue),
        "A consumed single primitive must leave the queue empty");
    write_at(entry, 0x48u, primitive.data() + 2u * windows10PrimitiveStride);
    write_at(entry, 0x50u, primitive.data() + 2u * windows10PrimitiveStride);
    auto secondMatrix = matrix; secondMatrix[12] = 200.0f;
    write_at(primitive, windows10PrimitiveStride + 0x08u, secondMatrix);
    check(read_windows10_primitive_queue(entry.data(), queue) && consume_windows10_primitive(entry.data(), queue),
        "The marker emitter must consume exactly one item from a longer queue");
    marker = {};
    check(read_windows10_marker(entry.data() + 0x10u, executionContext.data(), marker) && marker.complete &&
        marker.matrix[12] == 200.0f,
        "Remaining queued draws must preserve their own transform and retained marker tag");
    check(!consume_windows10_primitive(entry.data(), queue),
        "A changed queue must not be consumed using an outdated snapshot");
    write_at(entry, 0x48u, primitive.data() + 1u);
    marker = {};
    check(read_windows10_marker(entry.data() + 0x10u, executionContext.data(), marker) && !marker.complete,
        "A malformed queue must not reach the renderer");
    // Exercise both actual emitter hooks without a GPU context: recognised
    // sentinels still need consuming when the optical pass cannot execute.
    write_at(renderDevice, 0x258u, static_cast<ID3D11DeviceContext*>(nullptr));
    for (unsigned frame = 0u; frame < 512u; ++frame)
    {
        write_at(entry, 0x48u, primitive.data() + windows10PrimitiveStride);
        const auto emit = frame % 2u ? &windows10_brush_emit_hook : &windows10_common_emit_hook;
        check(emit(nullptr, executionContext.data(), entry.data() + 0x10u) == S_OK &&
            !read_windows10_primitive_queue(entry.data(), queue),
            "Repeated marker overrides must consume work without growing the native queue");
    }
    check(normalDraws == 2u, "Recognised markers must not leak through the ordinary geometry emitter");
    write_at(entry, 0x48u, primitive.data() + windows10PrimitiveStride);
    windows10DrawingMarker = true;
    check(windows10_common_emit_hook(nullptr, executionContext.data(), entry.data() + 0x10u) == S_FALSE &&
        normalDraws == 3u && read_windows10_primitive_queue(entry.data(), queue),
        "Re-entrant geometry calls must forward once without custom rendering or queue consumption");
    windows10DrawingMarker = false;

    // The same compiled extension must continue decoding Windows 11 command
    // objects and its +0x68 brush colour independently of the legacy adapter.
    windows10Adapter = false;
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(module.data());
    dos->e_magic = IMAGE_DOS_SIGNATURE; dos->e_lfanew = 128;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(module.data() + dos->e_lfanew);
    nt->Signature = IMAGE_NT_SIGNATURE; nt->OptionalHeader.SizeOfImage = static_cast<DWORD>(module.size());
    entry.fill(0u); geometry.fill(0u); brush.fill(0u);
    write_at(entry, 0u, entryVtable);
    write_at(entry, 0x10u, geometry.data());
    write_at(entry, 0x18u, primitive.data());
    write_at(entry, 0x30u, colour);
    write_at(geometry, 0x38u, bounds);
    write_at(brush, 0u, brushVtable); write_at(brush, 0x68u, colour);
    check(read_colour_brush(brush.data(), observedColour) && marker_slot(observedColour) == 0u,
        "Windows 11 must retain its existing brush colour offset");
    tag_native_marker_entry(entry.data(), 0u, 300.0f, 100.0f, region);
    alignas(16) std::array<std::uint8_t, 96> command {};
    write_at(command, 0u, std::uint32_t { 1u });
    write_at(command, 0x08u, entry.data());
    write_at(command, 0x10u, matrix);
    write_at(executionContext, 0x10u, context);
    marker = {};
    check(read_liquid_native_batch_marker(command.data(), executionContext.data(), marker) && marker.complete &&
        marker.context == context && marker.entry == entry.data() && marker.slot == 0u,
        "Windows 11 must retain its existing rendering-command decoder");
    check(std::equal(matrix.begin(), matrix.end(), marker.matrix), "The Windows 11 matrix must come from its command object");
    write_at(command, 0u, std::uint32_t { 2u }); marker = {};
    check(!read_liquid_native_batch_marker(command.data(), executionContext.data(), marker),
        "Windows 11 state-only commands must remain excluded");
    std::cout << "Windows 10 native marker adapter checks completed.\n";
    return failures ? 1 : 0;
}

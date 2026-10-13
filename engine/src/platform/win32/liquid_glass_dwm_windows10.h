#pragma once

// Included inside the extension's anonymous namespace. Windows 10 22H2 emits
// CBaseDrawListEntry geometry directly, without Windows 11 rendering commands.
constexpr std::uintptr_t windows10PrimitiveStride = 0x4cu;
struct Windows10PrimitiveQueue
{
    std::uint8_t* begin = nullptr;
    std::uint8_t* end = nullptr;
};

bool read_windows10_primitive_queue(void* entry, Windows10PrimitiveQueue& queue) noexcept
{
    __try
    {
        const auto* bytes = static_cast<const std::uint8_t*>(entry);
        queue.begin = *reinterpret_cast<std::uint8_t* const*>(bytes + 0x40);
        queue.end = *reinterpret_cast<std::uint8_t* const*>(bytes + 0x48);
        const auto begin = reinterpret_cast<std::uintptr_t>(queue.begin);
        const auto end = reinterpret_cast<std::uintptr_t>(queue.end);
        return begin && end > begin && (end - begin) % windows10PrimitiveStride == 0u &&
            end - begin <= windows10PrimitiveStride * 4096u;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool consume_windows10_primitive(void* entry, const Windows10PrimitiveQueue& expected) noexcept
{
    __try
    {
        Windows10PrimitiveQueue queue;
        if (!read_windows10_primitive_queue(entry, queue) || queue.begin != expected.begin ||
            queue.end != expected.end) return false;
        const auto remaining = reinterpret_cast<std::uintptr_t>(queue.end) -
            reinterpret_cast<std::uintptr_t>(queue.begin) - windows10PrimitiveStride;
        // Match EmitGeometryImpl: erase exactly the first queued primitive.
        // Otherwise a suppressed marker accumulates work on every Render call.
        if (remaining) std::memmove(queue.begin, queue.begin + windows10PrimitiveStride, remaining);
        *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(entry) + 0x48) =
            queue.end - windows10PrimitiveStride;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool read_windows10_marker(void* baseEntry, void* executionContext,
    LiquidNativeBatchMarker& marker, Windows10PrimitiveQueue* queued = nullptr) noexcept
{
    if (!baseEntry || !executionContext) return false;
    marker.entry = static_cast<std::uint8_t*>(baseEntry) - 0x10;
    LiquidNativeEntryIdentity identity {};
    if (!read_liquid_native_entry_identity(marker.entry, identity) ||
        !find_liquid_native_marker_tag(marker.entry, identity, marker)) return false;
    Windows10PrimitiveQueue queue;
    if (!read_windows10_primitive_queue(marker.entry, queue)) { status(226); return true; }
    if (queued) *queued = queue;
    std::copy(std::begin(identity.bounds), std::end(identity.bounds), marker.bounds);
    __try
    {
        // Render stores its transform in the queued primitive at +8. DWM sets
        // the execution context's matrix later, inside the original emitter;
        // reading that state here would use a previous entry's transform.
        std::memcpy(marker.matrix, queue.begin + 0x08, sizeof(marker.matrix));
        for (float value : marker.matrix) if (!std::isfinite(value)) return true;
        const auto* renderDevice = *reinterpret_cast<const std::uint8_t* const*>(
            static_cast<const std::uint8_t*>(executionContext) + 0x08);
        marker.context = renderDevice ? *reinterpret_cast<ID3D11DeviceContext* const*>(
            renderDevice + 0x258) : nullptr;
        marker.complete = marker.context != nullptr;
        if (marker.complete) status(227);
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { marker.complete = false; }
    return true;
}

thread_local bool windows10DrawingMarker = false;
HRESULT windows10_emit_hook(EffectEmitFn original, void* effect, void* executionContext, void* entry)
{
    if (windows10DrawingMarker) return original(effect, executionContext, entry);
    LiquidNativeBatchMarker marker {};
    Windows10PrimitiveQueue queue;
    if (read_windows10_marker(entry, executionContext, marker, &queue))
    {
        struct DrawScope
        {
            bool& active;
            explicit DrawScope(bool& value) : active(value) { active = true; }
            ~DrawScope() { active = false; }
        } scope(windows10DrawingMarker);
        (void)render_native_marker(marker, executionContext);
        if (queue.begin && consume_windows10_primitive(marker.entry, queue)) return S_OK;
        status(228);
        // A re-entrant native flush may already have consumed the snapshot.
        // Never submit that same primitive a second time after custom drawing.
        if (queue.begin) return S_OK;
    }
    return original(effect, executionContext, entry);
}

HRESULT windows10_common_emit_hook(void* effect, void* executionContext, void* entry)
{
    status(146);
    return windows10_emit_hook(originalCommonEffectEmit, effect, executionContext, entry);
}

HRESULT windows10_brush_emit_hook(void* effect, void* executionContext, void* entry)
{
    status(156);
    return windows10_emit_hook(originalBrushEffectEmit, effect, executionContext, entry);
}

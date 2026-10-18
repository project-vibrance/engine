#pragma once

// Included inside liquid_glass_dwm_present.cpp's anonymous namespace. These
// offsets describe the native batch ABI verified against the broker's current
// dwmcore fingerprint. The broker must reject another binary before installing
// the hooks; guarded reads and concrete entry vtable checks remain mandatory.
struct LiquidNativeBatchMarker
{
    void* entry = nullptr;
    ID3D11DeviceContext* context = nullptr;
    std::uint32_t slot = liquidDwmRegionSlots;
    float colour[4] {};
    float bounds[4] {};
    float matrix[16] {};
    float taggedWidth = 0.0f;
    float taggedHeight = 0.0f;
    LiquidDwmRegion shapeSnapshot {};
    bool hasShapeSnapshot = false;
    bool complete = false;
};

struct LiquidNativeEntryIdentity
{
    std::uintptr_t vtable = 0u;
    const std::uint8_t* geometry = nullptr;
    const void* effect = nullptr;
    const void* colourResource = nullptr;
    float tint[4] {};
    float bounds[4] {};
};

struct LiquidNativeMarkerTag
{
    void* entry = nullptr;
    LiquidNativeEntryIdentity identity {};
    std::uint32_t slot = liquidDwmRegionSlots;
    float width = 0.0f;
    float height = 0.0f;
    LiquidDwmRegion shapeSnapshot {};
    bool hasShapeSnapshot = false;
    ULONGLONG lastSeenTick = 0u;
};

std::mutex nativeMarkerTagsMutex;
std::array<LiquidNativeMarkerTag, 128u> nativeMarkerTags {};

bool read_liquid_native_entry_identity(void* pointer,
    LiquidNativeEntryIdentity& identity) noexcept
{
    if (!pointer || !dwmcoreModule) return false;
    __try
    {
        const auto* entry = static_cast<const std::uint8_t*>(pointer);
        if (windows10Adapter)
        {
            // EmitGeometry receives the CBaseDrawListEntry interface;
            // CHWDrawListEntry::Render receives its complete object instead.
            identity.vtable = *reinterpret_cast<const std::uintptr_t*>(entry + 0x10);
            if (identity.vtable != reinterpret_cast<std::uintptr_t>(dwmcoreModule) +
                    hookConfig.drawListEntryVtableRva) return false;
            identity.geometry = *reinterpret_cast<const std::uint8_t* const*>(entry + 0xb8);
            // GetRenderingEffect's +0xb0 read is relative to that +0x10 base.
            identity.effect = *reinterpret_cast<const void* const*>(entry + 0xc0);
            if (!identity.geometry || !identity.effect) return false;
            // +0x40/+0x48/+0x50 are a temporary primitive queue, not colour
            // identity. Render appends work after tagging; EmitGeometry then
            // consumes it. The queue can also be empty on the first draw.
            identity.colourResource = nullptr;
            std::fill(std::begin(identity.tint), std::end(identity.tint), 0.0f);
            std::memcpy(identity.bounds, identity.geometry + 0x48, sizeof(identity.bounds));
            for (float value : identity.bounds) if (!std::isfinite(value)) return false;
            return true;
        }
        const auto* base = reinterpret_cast<const std::uint8_t*>(dwmcoreModule);
        const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
        if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew <= 0)
            return false;
        const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            base + dos->e_lfanew);
        if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
        const auto imageSize = nt->OptionalHeader.SizeOfImage;
        const auto baseAddress = reinterpret_cast<std::uintptr_t>(base);
        identity.vtable = *reinterpret_cast<const std::uintptr_t*>(entry);
        // Tagged entries can have concrete subclasses. Require their vtable to
        // belong to the same fingerprinted module, then compare that exact
        // recorded identity when the native batch later consumes the entry.
        if (imageSize < sizeof(void*) || identity.vtable < baseAddress ||
            identity.vtable - baseAddress > imageSize - sizeof(void*))
            return false;
        identity.geometry = *reinterpret_cast<const std::uint8_t* const*>(
            entry + 0x10);
        identity.effect = *reinterpret_cast<const void* const*>(entry + 0x18);
        identity.colourResource = *reinterpret_cast<const void* const*>(
            entry + 0x28);
        if (!identity.geometry) return false;
        std::memcpy(identity.tint, entry + 0x30, sizeof(identity.tint));
        std::memcpy(identity.bounds, identity.geometry + 0x38,
            sizeof(identity.bounds));
        for (float value : identity.bounds)
            if (!std::isfinite(value)) return false;
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool liquid_native_entry_identity_matches(
    const LiquidNativeEntryIdentity& first,
    const LiquidNativeEntryIdentity& second) noexcept
{
    // Cached entries can be repurposed without changing their three original
    // pointer fields. Compare the retained tint and bounds byte for byte too.
    return first.vtable == second.vtable &&
        first.geometry == second.geometry && first.effect == second.effect &&
        first.colourResource == second.colourResource &&
        std::memcmp(first.tint, second.tint, sizeof(first.tint)) == 0 &&
        std::memcmp(first.bounds, second.bounds, sizeof(first.bounds)) == 0;
}

void untag_native_marker_entry(void* entry) noexcept
{
    if (!entry) return;
    try
    {
        std::lock_guard<std::mutex> lock(nativeMarkerTagsMutex);
        for (auto& tag : nativeMarkerTags)
            if (tag.entry == entry) tag = {};
    }
    catch (...) {}
}

void tag_native_marker_entry(void* entry, std::uint32_t slot,
    float width, float height, const LiquidDwmRegion& published) noexcept
{
    LiquidNativeEntryIdentity identity {};
    if (slot >= liquidDwmRegionSlots || !std::isfinite(width) ||
        !std::isfinite(height) || width <= 0.0f || height <= 0.0f ||
        width > 32768.0f || height > 32768.0f ||
        published.version != liquidDwmProtocolVersion || published.active != 1u ||
        !published.ownerPid || !published.hwnd ||
        !published.width || !published.height ||
        !read_liquid_native_entry_identity(entry, identity))
    {
        if (windows10Adapter) status(225);
        untag_native_marker_entry(entry);
        return;
    }
    if (windows10Adapter) status(224);
    try
    {
        std::lock_guard<std::mutex> lock(nativeMarkerTagsMutex);
        const auto now = GetTickCount64();
        LiquidNativeMarkerTag* destination = nullptr;
        for (auto& tag : nativeMarkerTags)
        {
            if (tag.entry == entry)
            {
                destination = &tag;
                break;
            }
            if (!tag.entry && !destination) destination = &tag;
        }
        // DWM can retain an idle entry indefinitely. Bound storage by evicting
        // the least recently used entry only when the table is actually full.
        if (!destination)
            destination = &*std::min_element(nativeMarkerTags.begin(),
                nativeMarkerTags.end(), [](const auto& first, const auto& second) {
                    return first.lastSeenTick < second.lastSeenTick;
                });
        *destination = {};
        destination->entry = entry;
        destination->identity = identity;
        destination->slot = slot;
        destination->width = width;
        destination->height = height;
        // Retained geometry can outlive the current animation frame's shared
        // publication. Keep its material shape together with its native size.
        destination->shapeSnapshot = published;
        destination->hasShapeSnapshot = true;
        destination->lastSeenTick = now;
    }
    catch (...) {}
}

bool find_liquid_native_marker_tag(void* entry,
    const LiquidNativeEntryIdentity& identity,
    LiquidNativeBatchMarker& marker) noexcept
{
    try
    {
        std::lock_guard<std::mutex> lock(nativeMarkerTagsMutex);
        const auto now = GetTickCount64();
        for (auto& tag : nativeMarkerTags)
        {
            if (tag.entry != entry) continue;
            if (!liquid_native_entry_identity_matches(tag.identity, identity))
            {
                tag = {};
                return false;
            }
            marker.slot = tag.slot;
            marker.taggedWidth = tag.width;
            marker.taggedHeight = tag.height;
            marker.shapeSnapshot = tag.shapeSnapshot;
            marker.hasShapeSnapshot = tag.hasShapeSnapshot;
            // Idle, occluded and low-cadence windows can reuse this entry long
            // after its content-generation callback. Identity and live slot
            // ownership validate it; elapsed time cannot establish its lifetime.
            tag.lastSeenTick = now;
            return true;
        }
    }
    catch (...) {}
    return false;
}

bool preserve_native_marker_entry(void* entry) noexcept
{
    LiquidNativeEntryIdentity identity {};
    if (!read_liquid_native_entry_identity(entry, identity))
    {
        untag_native_marker_entry(entry);
        return false;
    }
    // Retained Entry::Render calls may arrive outside the original marker draw
    // scope. Refresh only an existing tag with its full identity;
    // never classify a new entry merely because it shares the same address.
    LiquidNativeBatchMarker marker {};
    return find_liquid_native_marker_tag(entry, identity, marker);
}

void* read_liquid_native_command_entry(void* command) noexcept
{
    if (!command) return nullptr;
    __try
    {
        const auto* bytes = static_cast<const std::uint8_t*>(command);
        const auto kind = *reinterpret_cast<const std::uint32_t*>(bytes);
        // State-only commands use different layouts. Rendering commands retain
        // an AddRef on their entry until the native batch has been executed.
        return kind == 1u || kind == 4u || kind == 5u ?
            *reinterpret_cast<void* const*>(bytes + 0x08) : nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return nullptr; }
}

bool read_liquid_native_entry_colour(void* pointer,
    float (&colour)[4]) noexcept
{
    __try
    {
        const auto* entry = static_cast<const std::uint8_t*>(pointer);
        // The entry stores a tint and an optional animated colour resource.
        // Match the effective premultiplied colour used by ComputeVertexColor.
        // The effect at +0x18 is shared between unrelated solid-colour draws.
        std::memcpy(colour, entry + 0x30, sizeof(colour));
        const auto* colourResource = *reinterpret_cast<const std::uint8_t* const*>(
            entry + 0x28);
        if (colourResource)
        {
            float resourceColour[4] {};
            std::memcpy(resourceColour, colourResource + 0x48, sizeof(resourceColour));
            for (int index = 0; index < 3; ++index)
                colour[index] *= resourceColour[index] * resourceColour[3];
            colour[3] *= resourceColour[3];
        }
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool read_liquid_native_marker_geometry(void* command, void* executionContext,
    const LiquidNativeEntryIdentity& identity,
    LiquidNativeBatchMarker& marker) noexcept
{
    __try
    {
        const auto* bytes = static_cast<const std::uint8_t*>(command);
        const auto* entry = static_cast<const std::uint8_t*>(marker.entry);
        // Confirm the native object tuple again before dereferencing geometry.
        if (*reinterpret_cast<const std::uintptr_t*>(entry) != identity.vtable ||
            *reinterpret_cast<const std::uint8_t* const*>(entry + 0x10) !=
                identity.geometry ||
            *reinterpret_cast<const void* const*>(entry + 0x18) !=
                identity.effect) return false;
        std::memcpy(marker.bounds, identity.geometry + 0x38, sizeof(marker.bounds));
        std::memcpy(marker.matrix, bytes + 0x10, sizeof(marker.matrix));
        marker.context = *reinterpret_cast<ID3D11DeviceContext* const*>(
            static_cast<const std::uint8_t*>(executionContext) + 0x10);
        return marker.context != nullptr;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool read_liquid_native_batch_marker(void* command, void* executionContext,
    LiquidNativeBatchMarker& marker) noexcept
{
    if (!executionContext) return false;
    marker.entry = read_liquid_native_command_entry(command);
    LiquidNativeEntryIdentity identity {};
    if (!read_liquid_native_entry_identity(marker.entry, identity)) return false;
    if (!find_liquid_native_marker_tag(marker.entry, identity, marker))
    {
        // Keep the verified base-class colour path as a fallback, but tagged
        // subclasses are classified only by the observed native draw scope.
        if (!hookConfig.drawListEntryVtableRva || identity.vtable !=
                reinterpret_cast<std::uintptr_t>(dwmcoreModule) +
                    hookConfig.drawListEntryVtableRva ||
            !read_liquid_native_entry_colour(marker.entry, marker.colour))
            return false;
        status(148);
        marker.slot = marker_slot(marker.colour);
        if (marker.slot == liquidDwmRegionSlots) return false;
    }
    // Suppress a recognised sentinel even if a later guarded geometry read
    // fails. The caller will record the failure without drawing that colour.
    marker.complete = read_liquid_native_marker_geometry(command,
        executionContext, identity, marker);
    return true;
}

bool flush_liquid_native_batch(void* executionContext,
    ID3D11DeviceContext* context) noexcept
{
    if (!nativeBatchFlush || !context) return false;
    __try
    {
        const auto type = context->GetType();
        detail(155, static_cast<LONG>(type));
        if (type != D3D11_DEVICE_CONTEXT_IMMEDIATE) return false;
        // Geometry emitted before this command must reach the output before
        // the live backdrop is copied. Flushing a deferred context cannot do so.
        nativeBatchFlush(executionContext, windows10Adapter ? 0u : 0x80000000u);
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool liquid_native_batch_region(const LiquidNativeBatchMarker& marker,
    const LiquidDwmRegion& published, const D3D11_TEXTURE2D_DESC& target,
    LiquidGlassRenderRegion& region) noexcept
{
    for (float value : marker.matrix)
        if (!std::isfinite(value)) return false;
    for (float value : marker.bounds)
        if (!std::isfinite(value)) return false;
    const auto* matrix = marker.matrix;
    constexpr float tolerance = 0.0001f;
    // The optical pass currently supports a translated, uniformly scaled 2D
    // lens. Reject skew, rotation, reflection and perspective rather than
    // substituting screen coordinates from a different compositor output.
    if (std::abs(matrix[1]) > tolerance ||
        std::abs(matrix[4]) > tolerance ||
        std::abs(matrix[3]) > tolerance ||
        std::abs(matrix[7]) > tolerance ||
        matrix[15] <= tolerance) return false;
    const double scaleX = matrix[0] / static_cast<double>(matrix[15]);
    const double scaleY = matrix[5] / static_cast<double>(matrix[15]);
    if (scaleX <= 0.0 || scaleY <= 0.0 || scaleX > 8.0 || scaleY > 8.0 ||
        std::abs(scaleX - scaleY) > std::max(scaleX, scaleY) * 0.01)
        return false;
    const double localWidth = marker.bounds[2] - marker.bounds[0];
    const double localHeight = marker.bounds[3] - marker.bounds[1];
    if (!std::isfinite(published.markerInsetX) || published.markerInsetX < 0.0f)
        return false;
    const double expectedWidth = marker.taggedWidth > 0.0f ?
        marker.taggedWidth : static_cast<double>(published.width) +
            2.0 * published.markerInsetX;
    const double expectedHeight = marker.taggedHeight > 0.0f ?
        marker.taggedHeight : static_cast<double>(published.height);
    if (localWidth <= 0.0 || localHeight <= 0.0 ||
        std::abs(localWidth - expectedWidth) > 2.0 ||
        std::abs(localHeight - expectedHeight) > 2.0) return false;
    const double translationX = matrix[12] /
        static_cast<double>(matrix[15]);
    const double translationY = matrix[13] /
        static_cast<double>(matrix[15]);
    const double left = marker.bounds[0] * scaleX + translationX;
    const double top = marker.bounds[1] * scaleY + translationY;
    const double right = marker.bounds[2] * scaleX + translationX;
    const double bottom = marker.bounds[3] * scaleY + translationY;
    if (target.Width == 0u || target.Height == 0u ||
        !std::isfinite(left) || !std::isfinite(top) ||
        !std::isfinite(right) || !std::isfinite(bottom) ||
        left < static_cast<double>(INT32_MIN) || top < static_cast<double>(INT32_MIN) ||
        right > static_cast<double>(INT32_MAX) || bottom > static_cast<double>(INT32_MAX) ||
        right <= left || bottom <= top || right - left > 32768.0 ||
        bottom - top > 32768.0) return false;
    const auto roundedLeft = static_cast<std::int32_t>(std::floor(left));
    const auto roundedTop = static_cast<std::int32_t>(std::floor(top));
    const auto roundedRight = static_cast<std::int32_t>(std::ceil(right));
    const auto roundedBottom = static_cast<std::int32_t>(std::ceil(bottom));
    const auto roundedWidth = static_cast<std::int64_t>(roundedRight) - roundedLeft;
    const auto roundedHeight = static_cast<std::int64_t>(roundedBottom) - roundedTop;
    if (roundedWidth <= 0 || roundedHeight <= 0 ||
        roundedWidth > 32768 || roundedHeight > 32768) return false;
    const float scale = static_cast<float>((scaleX + scaleY) * 0.5);
    region.x = roundedLeft;
    region.y = roundedTop;
    region.width = static_cast<std::uint32_t>(roundedWidth);
    region.height = static_cast<std::uint32_t>(roundedHeight);
    region.cornerRadius = published.cornerRadius * scale;
    region.edgeThickness = published.edgeThickness * scale;
    region.refractionStrength = published.refractionStrength * scale;
    region.dispersionStrength = published.dispersionStrength * scale;
    region.blurRadius = published.blurRadius * scale;
    region.rimStrength = published.rimStrength;
    region.opacity = published.opacity;
    region.tintRed = published.tintRed;
    region.tintGreen = published.tintGreen;
    region.tintBlue = published.tintBlue;
    region.tintAlpha = published.tintAlpha;
    region.cornerExponent = published.cornerExponent;
    region.shape = published.shape;
    region.squircleAmount = published.squircleAmount;
    region.notchAmount = published.notchAmount;
    region.notchDepth = published.notchDepth * scale;
    region.topLeftRadius = published.topLeftRadius * scale;
    region.topRightRadius = published.topRightRadius * scale;
    region.bottomRightRadius = published.bottomRightRadius * scale;
    region.bottomLeftRadius = published.bottomLeftRadius * scale;
    region.markerInsetX = published.markerInsetX * scale;
    if (published.contourCount > region.contours.size() ||
        published.hasExclusion > 1u || (published.paintCapGradient & 255u) > 1u) return false;
    region.contourCount = published.contourCount;
    region.hasExclusion = published.hasExclusion != 0u;
    region.paintCapGradient = (published.paintCapGradient & 1u) != 0u;
    for (unsigned channel = 0; channel < 3; ++channel)
        region.capColour[channel] = float((published.paintCapGradient >> (8u + channel * 8u)) & 255u) / 255.0f;
    std::copy(std::begin(published.capGradient), std::end(published.capGradient),
        region.capGradient.begin());
    for (unsigned index = 0; index < 4; ++index) region.viewportRect[index] = published.viewportRect[index] * scale;
    region.viewportFade = { published.viewportFade[0] * scale, published.viewportFade[1] * scale,
        published.viewportFade[2], published.viewportFade[3] };
    const auto copyContour = [scale](const auto& source, auto& destination) {
        destination.x = source.x * scale;
        destination.y = source.y * scale;
        destination.width = source.width * scale;
        destination.height = source.height * scale;
        destination.cornerRadius = source.cornerRadius * scale;
        destination.topLeftRadius = source.topLeftRadius * scale;
        destination.topRightRadius = source.topRightRadius * scale;
        destination.bottomRightRadius = source.bottomRightRadius * scale;
        destination.bottomLeftRadius = source.bottomLeftRadius * scale;
        destination.squircleAmount = source.squircleAmount;
        destination.squirclePower = source.squirclePower;
        destination.notchAmount = source.notchAmount;
        destination.notchDepth = source.notchDepth * scale;
        destination.shape = source.shape;
    };
    for (std::size_t index = 0u; index < region.contourCount; ++index)
        copyContour(published.contours[index], region.contours[index]);
    if (region.hasExclusion) copyContour(published.exclusion, region.exclusion);
    region.lightPhase = static_cast<float>(GetTickCount64() % 6000u) *
        (6.2831853f / 6000.0f);
    return true;
}

bool render_native_marker(const LiquidNativeBatchMarker& marker, void* executionContext)
{
    status(147);
    try
    {
        if (!marker.complete || !marker.context) { status(152); return true; }
        ComPtr<ID3D11DeviceContext> retainedContext = marker.context;
        ComPtr<ID3D11Device> retainedDevice;
        if (windows10Adapter && !windows10_optical_render_ready())
        { status(230); return true; }
        retainedContext->GetDevice(&retainedDevice);
        if (!retainedDevice) { status(152); return true; }
        const auto waitStart = performance_tick();
        auto state = find_renderer_state(retainedDevice.Get());
        if (!state) { status(152); return true; }
        std::unique_lock<std::mutex> rendererLock(state->drawMutex, std::defer_lock);
        if (windows10Adapter)
        {
            // A transition never waits behind another optical draw. The
            // observer retires resources once every device lease is released.
            if (!rendererLock.try_lock()) { status(230); return true; }
            if (!windows10_optical_render_ready()) { status(230); return true; }
            const HRESULT reason = retainedDevice->GetDeviceRemovedReason();
            if (FAILED(reason)) { windows10_suspend_optical_render(reason); return true; }
        }
        else rendererLock.lock();
        const auto lockAcquired = performance_tick();
        const auto flushStarted = performance_tick();
        if (!flush_liquid_native_batch(executionContext, marker.context))
        {
            status(152);
            record_region_draw(marker.slot, state->adapterLuid, false,
                elapsed_microseconds(waitStart, lockAcquired), 0u, E_FAIL,
                elapsed_microseconds(flushStarted, performance_tick()));
            return true;
        }
        const auto flushMicroseconds = elapsed_microseconds(flushStarted, performance_tick());
        auto* context = marker.context;
        ID3D11RenderTargetView* boundTargets[D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT] {};
        context->OMGetRenderTargets(D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT,
            boundTargets, nullptr);
        std::array<ComPtr<ID3D11RenderTargetView>,
            D3D11_SIMULTANEOUS_RENDER_TARGET_COUNT> targets;
        for (std::size_t index = 0; index < targets.size(); ++index)
            targets[index].Attach(boundTargets[index]);
        if (!targets[0] || std::any_of(targets.begin() + 1, targets.end(),
                [](const auto& target) { return target.Get() != nullptr; }))
        {
            status(153);
            return true;
        }
        ComPtr<ID3D11Resource> resource;
        targets[0]->GetResource(&resource);
        ComPtr<ID3D11Texture2D> texture;
        D3D11_RENDER_TARGET_VIEW_DESC viewDescription {};
        targets[0]->GetDesc(&viewDescription);
        if (!resource || FAILED(resource.As(&texture)) ||
            viewDescription.ViewDimension != D3D11_RTV_DIMENSION_TEXTURE2D ||
            viewDescription.Texture2D.MipSlice != 0u)
        {
            status(153);
            return true;
        }
        D3D11_TEXTURE2D_DESC description {};
        texture->GetDesc(&description);
        if (windows10Adapter)
        {
            ComPtr<ID3D11Device> targetDevice;
            texture->GetDevice(&targetDevice);
            if (!windows10_device_matches(retainedDevice.Get(), targetDevice.Get()))
            {
                status(232);
                windows10_suspend_optical_render(DXGI_ERROR_INVALID_CALL);
                return true;
            }
        }
        detail(157, static_cast<LONG>(description.Width));
        detail(158, static_cast<LONG>(description.Height));
        detail(159, static_cast<LONG>(description.Format));
        if (description.ArraySize != 1u || description.SampleDesc.Count != 1u ||
            description.Width == 0u || description.Height == 0u ||
            description.Width > 32768u || description.Height > 32768u ||
            static_cast<std::uint64_t>(description.Width) * description.Height >
                32u * 1024u * 1024u)
        {
            status(153);
            return true;
        }
        LiquidDwmRegion published {};
        LiquidGlassRenderRegion region {};
        if (!read_capture_region(description, marker.slot, published) ||
            (marker.hasShapeSnapshot &&
                (published.ownerPid != marker.shapeSnapshot.ownerPid ||
                    published.hwnd != marker.shapeSnapshot.hwnd ||
                    published.regionId != marker.shapeSnapshot.regionId)))
        {
            status(153);
            return true;
        }
        // The live slot proves that the retained entry still belongs to an
        // active island. Its native geometry and captured shape determine this
        // draw, rather than the next frame's independently published geometry.
        auto shape = marker.hasShapeSnapshot ? marker.shapeSnapshot : published;
        // Shape follows the queued native geometry. Optical controls remain
        // live even when DWM reuses that geometry's retained draw-list entry.
        shape.refractionStrength = published.refractionStrength;
        shape.dispersionStrength = published.dispersionStrength;
        shape.blurRadius = published.blurRadius;
        shape.rimStrength = published.rimStrength;
        shape.opacity = published.opacity;
        shape.tintRed = published.tintRed;
        shape.tintGreen = published.tintGreen;
        shape.tintBlue = published.tintBlue;
        shape.tintAlpha = published.tintAlpha;
        shape.paintCapGradient = published.paintCapGradient;
        std::copy(std::begin(published.capGradient), std::end(published.capGradient),
            std::begin(shape.capGradient));
        std::copy(std::begin(published.viewportRect), std::end(published.viewportRect), std::begin(shape.viewportRect));
        std::copy(std::begin(published.viewportFade), std::end(published.viewportFade), std::begin(shape.viewportFade));
        shape.viewportRect[0] += static_cast<float>(published.x - shape.x);
        shape.viewportRect[1] += static_cast<float>(published.y - shape.y);
        if (published.width == shape.width && published.height == shape.height)
        {
            // Retained marker geometry can stay unchanged while a neighbour
            // moves within a union or a card's cutout changes its position.
            shape.contourCount = published.contourCount;
            shape.hasExclusion = published.hasExclusion;
            std::copy(std::begin(published.contours), std::end(published.contours),
                std::begin(shape.contours));
            shape.exclusion = published.exclusion;
        }
        if (!liquid_native_batch_region(marker, shape, description, region))
        {
            status(153);
            return true;
        }
        if (static_cast<std::int64_t>(region.x) + region.width <= 0 ||
            static_cast<std::int64_t>(region.y) + region.height <= 0 ||
            static_cast<std::int64_t>(region.x) >= description.Width ||
            static_cast<std::int64_t>(region.y) >= description.Height)
            return true;
        UINT viewportCount = 0u;
        context->RSGetViewports(&viewportCount, nullptr);
        if (viewportCount != 1u)
        {
            status(153);
            return true;
        }
        D3D11_VIEWPORT viewport {};
        context->RSGetViewports(&viewportCount, &viewport);
        if (!std::isfinite(viewport.TopLeftX) ||
            !std::isfinite(viewport.TopLeftY) ||
            !std::isfinite(viewport.Width) || !std::isfinite(viewport.Height) ||
            viewport.Width <= 0.0f || viewport.Height <= 0.0f ||
            std::abs(viewport.TopLeftX) > 32768.0f ||
            std::abs(viewport.TopLeftY) > 32768.0f ||
            viewport.Width > 32768.0f || viewport.Height > 32768.0f)
        {
            status(153);
            return true;
        }
        D3D11_RECT clip {
            std::max<LONG>(0, static_cast<LONG>(std::ceil(viewport.TopLeftX))),
            std::max<LONG>(0, static_cast<LONG>(std::ceil(viewport.TopLeftY))),
            std::min<LONG>(description.Width, static_cast<LONG>(std::floor(
                viewport.TopLeftX + viewport.Width))),
            std::min<LONG>(description.Height, static_cast<LONG>(std::floor(
                viewport.TopLeftY + viewport.Height)))
        };
        ComPtr<ID3D11RasterizerState> rasteriser;
        context->RSGetState(&rasteriser);
        D3D11_RASTERIZER_DESC rasteriserDescription {};
        if (rasteriser) rasteriser->GetDesc(&rasteriserDescription);
        if (rasteriserDescription.ScissorEnable)
        {
            UINT scissorCount = 0u;
            context->RSGetScissorRects(&scissorCount, nullptr);
            if (scissorCount != 1u)
            {
                status(153);
                return true;
            }
            D3D11_RECT scissor {};
            context->RSGetScissorRects(&scissorCount, &scissor);
            clip.left = std::max(clip.left, scissor.left);
            clip.top = std::max(clip.top, scissor.top);
            clip.right = std::min(clip.right, scissor.right);
            clip.bottom = std::min(clip.bottom, scissor.bottom);
        }
        if (clip.right <= clip.left || clip.bottom <= clip.top)
            return true;
        const auto submitStart = performance_tick();
        if (windows10Adapter)
        {
            const HRESULT reason = retainedDevice->GetDeviceRemovedReason();
            if (FAILED(reason)) { windows10_suspend_optical_render(reason); return true; }
            if (!windows10_optical_render_ready()) { status(230); return true; }
        }
        record_native_render_attempt();
        bool rendered = false;
        if (ensure_renderer(*state))
        {
            // The renderer takes its own cropped snapshot before writing the
            // target. Use the verified RTV format for typeless resources too,
            // avoiding a second copy of the same backdrop on every draw.
            const auto format = backdrop_copy_format(description.Format, viewDescription.Format);
            rendered = format != DXGI_FORMAT_UNKNOWN &&
                state->renderer.render(context, texture.Get(), targets[0].Get(), region,
                    0, 0, &clip, format);
        }
        if (rendered)
        {
            record_native_render_success();
            status(151);
            status(activeDdaTarget ? 111u : inCaptureRender ? 120u : 6u);
            status(liquidDwmRegionStatusBase + marker.slot);
        }
        else
        {
            record_native_render_failure();
            status(152);
        }
        record_region_draw(marker.slot, state->adapterLuid, rendered,
            elapsed_microseconds(waitStart, lockAcquired),
            elapsed_microseconds(submitStart, performance_tick()),
            rendered ? S_OK : E_FAIL, flushMicroseconds);
        if (windows10Adapter)
        {
            const HRESULT reason = retainedDevice->GetDeviceRemovedReason();
            if (FAILED(reason)) windows10_suspend_optical_render(reason);
        }
    }
    catch (...) { status(152); }
    return true;
}

thread_local bool nativeMarkerDrawing = false;

bool render_batch_marker(void* command, void* executionContext)
{
    // Native flushing can re-enter the batch emitters. Forward those calls to
    // DWM rather than recursively taking this device's optical draw lock.
    if (nativeMarkerDrawing) return false;
    LiquidNativeBatchMarker marker {};
    if (!read_liquid_native_batch_marker(command, executionContext, marker)) return false;
    struct DrawScope
    {
        DrawScope() { nativeMarkerDrawing = true; }
        ~DrawScope() { nativeMarkerDrawing = false; }
    } scope;
    return render_native_marker(marker, executionContext);
}

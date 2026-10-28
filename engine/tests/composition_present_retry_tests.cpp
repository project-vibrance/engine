#include "../src/platform/win32/composition_present_retry.h"
#include "../src/platform/win32/composition_damage.h"
#include "../src/renderer/vulkan/composition_frame_delivery.h"

#include <array>
#include <iostream>
#include <vector>

int main()
{
    using namespace vibrance::win32;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };

    static_assert(VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_TIMEOUT_MS <= 1u,
        "A shared UI thread must not inherit the former one-second interop wait");
    CompositionLatencyHint stalledDisplay;
    CompositionLatencyHint healthyDisplay;
    check(!stalledDisplay.may_prepare(false, 100u) &&
        !stalledDisplay.may_prepare(false, 116u) &&
        healthyDisplay.may_prepare(true, 116u),
        "An unsignalled output must defer immediately without preventing another display's work");
    check(stalledDisplay.may_prepare(false, 132u) &&
        stalledDisplay.may_prepare(false, 133u) &&
        stalledDisplay.may_prepare(false, 148u),
        "A hybrid driver's stuck latency hint must fall back to nonblocking Present1 without throttling later frames");
    CompositionLatencyHint transientDisplay;
    check(!transientDisplay.may_prepare(false, 200u) &&
        transientDisplay.may_prepare(true, 216u) &&
        !transientDisplay.may_prepare(false, 240u) &&
        !transientDisplay.may_prepare(false, 256u),
        "An ordinary latency signal must reset the transient deferral interval");

    CompositionPresentStall transition;
    check(!transition.needs_recovery(VIBRANCE_COMPOSITION_PRESENT_DEFERRED, true, 0u) &&
        !transition.needs_recovery(VIBRANCE_COMPOSITION_PRESENT_DEFERRED, true, 999u) &&
        transition.needs_recovery(VIBRANCE_COMPOSITION_PRESENT_DEFERRED, true, 1000u),
        "A visible target that never accepts its pending frame must request independent bridge recovery");
    check(!transition.needs_recovery(VIBRANCE_COMPOSITION_PRESENT_DEFERRED, false, 2000u) &&
        !transition.needs_recovery(VIBRANCE_COMPOSITION_PRESENT_DEFERRED, true, 3000u) &&
        !transition.needs_recovery(VIBRANCE_COMPOSITION_PRESENTED, true, 3500u) &&
        !transition.needs_recovery(VIBRANCE_COMPOSITION_PRESENT_DEFERRED, true, 4000u),
        "Hidden windows and successful frames must reset the stalled-output recovery interval");

    CompositionPresentRetry retry;
    std::uint64_t mutexKey = VIBRANCE_COMPOSITION_VULKAN_RELEASE_KEY;
    std::uint32_t copies = 0u;
    std::uint32_t presents = 0u;
    std::array<int, 4> damage {};
    const std::array<int, 4> expectedDamage { 24, 40, 280, 96 };
    const auto prepare = [&]() -> std::uint32_t {
        ++copies;
        if (mutexKey != VIBRANCE_COMPOSITION_VULKAN_RELEASE_KEY)
        {
            return VIBRANCE_COMPOSITION_PRESENT_DEFERRED;
        }
        damage = expectedDamage;
        mutexKey = VIBRANCE_COMPOSITION_VULKAN_ACQUIRE_KEY;
        return VIBRANCE_COMPOSITION_PRESENTED;
    };
    const auto busyPresent = [&] {
        ++presents;
        check(damage == expectedDamage,
            "Busy presents must retain the copied frame's dirty rectangle");
        return VIBRANCE_COMPOSITION_PRESENT_DEFERRED;
    };

    check(retry.deliver(2u, prepare, busyPresent) == VIBRANCE_COMPOSITION_PRESENT_DEFERRED,
        "A busy DXGI present must keep the copied frame pending");
    for (unsigned attempt = 0u; attempt < 8u; ++attempt)
    {
        check(retry.deliver(2u, prepare, busyPresent) == VIBRANCE_COMPOSITION_PRESENT_DEFERRED,
            "A copied frame must remain retryable whilst DXGI is busy");
    }
    check(copies == 1u && presents == 9u,
        "Busy retries must reach Present1 without reacquiring the released keyed mutex or copying again");
    check(retry.deliver(0u, prepare, busyPresent) == VIBRANCE_COMPOSITION_PRESENT_DEFERRED &&
            copies == 1u && presents == 9u,
        "A different buffer must not overwrite a pending DXGI back buffer");

    check(retry.deliver(2u, prepare, [&] {
        ++presents;
        check(damage == expectedDamage,
            "The successful retry must present the original dirty rectangle");
        return VIBRANCE_COMPOSITION_PRESENTED;
    }) == VIBRANCE_COMPOSITION_PRESENTED && copies == 1u,
        "A busy frame must recover when DXGI accepts it without waiting for another Vulkan frame");
    mutexKey = VIBRANCE_COMPOSITION_VULKAN_RELEASE_KEY;
    check(retry.deliver(0u, prepare, [] { return VIBRANCE_COMPOSITION_PRESENT_FAILED; }) ==
            VIBRANCE_COMPOSITION_PRESENT_FAILED && copies == 2u,
        "A new frame must copy normally after the previous frame is presented");
    mutexKey = VIBRANCE_COMPOSITION_VULKAN_RELEASE_KEY;
    check(retry.deliver(1u, prepare, [] { return VIBRANCE_COMPOSITION_PRESENTED; }) ==
            VIBRANCE_COMPOSITION_PRESENTED && copies == 3u,
        "A permanent DXGI failure must release the retry slot for a later frame");

    unsigned skippedPresents = 0u;
    check(retry.deliver(0u, [] { return VIBRANCE_COMPOSITION_PRESENT_DEFERRED; }, [&] {
        ++skippedPresents;
        return VIBRANCE_COMPOSITION_PRESENTED;
    }) == VIBRANCE_COMPOSITION_PRESENT_DEFERRED && skippedPresents == 0u,
        "A deferred copy must not present unprepared pixels");
    mutexKey = VIBRANCE_COMPOSITION_VULKAN_RELEASE_KEY;
    check(retry.deliver(0u, prepare, [] { return VIBRANCE_COMPOSITION_PRESENTED; }) ==
            VIBRANCE_COMPOSITION_PRESENTED && copies == 4u,
        "A deferred preparation must be attempted again before the next present");

    // One completed frame can be retried after another pending frame has
    // published a different native mask. Its pixels remain uploaded, but its
    // own glass/cap state must be restored before the next Present attempt.
    std::vector<bool> uploaded(2u, false);
    std::array<unsigned, 2u> frameUploads {}, framePublishes {};
    unsigned publishedFrame = 99u;
    const auto deliverFrame = [&](unsigned frame, std::uint32_t result) {
        return present_completed_composition_frame(uploaded[frame],
            [&] { ++frameUploads[frame]; return true; },
            [&] { ++framePublishes[frame]; publishedFrame = frame; },
            [&] {
                check(publishedFrame == frame,
                    "Every completed-frame Present must use that frame's native mask and cap state");
                return result;
            });
    };
    check(deliverFrame(0u, VIBRANCE_COMPOSITION_PRESENT_DEFERRED) ==
        VIBRANCE_COMPOSITION_PRESENT_DEFERRED,
        "A completed frame must remain uploaded while its Present is deferred");
    check(deliverFrame(1u, VIBRANCE_COMPOSITION_PRESENT_DEFERRED) ==
        VIBRANCE_COMPOSITION_PRESENT_DEFERRED && publishedFrame == 1u,
        "Another pending frame may publish a different native mask between retries");
    check(deliverFrame(0u, VIBRANCE_COMPOSITION_PRESENTED) ==
        VIBRANCE_COMPOSITION_PRESENTED && publishedFrame == 0u &&
        frameUploads == std::array<unsigned, 2u>{1u, 1u} &&
        framePublishes == std::array<unsigned, 2u>{2u, 1u},
        "Retrying an uploaded frame must restore its own glass without another pixel upload");
    bool failedUpload = false;
    unsigned failedPublishCalls = 0u, failedPresentCalls = 0u;
    check(!present_completed_composition_frame(failedUpload,
        [] { return false; }, [&] { ++failedPublishCalls; },
        [&] { ++failedPresentCalls; return VIBRANCE_COMPOSITION_PRESENTED; }) &&
        !failedUpload && failedPublishCalls == 0u && failedPresentCalls == 0u,
        "A failed upload must not replace native regions or present unprepared pixels");

    CompositionPresentDamageHistory history;
    const CompositionDamageRect full {0, 0, 64, 32};
    const CompositionDamageRect largeOptical {0, 0, 60, 30};
    for (std::uint32_t index = 0; index < VIBRANCE_COMPOSITION_MAX_BUFFERS; ++index)
    {
        const auto first = history.prepare(index, 64, 32, {}, largeOptical);
        check(first.copy == full && first.present == full,
            "Every rotating back buffer must initialise its complete image");
        history.commit(index);
    }
    const CompositionDamageRect a {10, 8, 14, 12}, b {24, 9, 28, 13};
    const auto partial = history.prepare(0, 64, 32, a, largeOptical);
    check(partial.copy == a && partial.present == largeOptical,
        "A complete optical invalidation halo must not widen sharp UI copy damage");
    // A copied frame may remain pending while DXGI is busy. Changes announced
    // to another buffer still have to reach it before a later accepted frame.
    history.prepare(1, 64, 32, b, {});
    history.commit(1);
    const auto accumulated = history.prepare(0, 64, 32, {}, {});
    check(accumulated.copy == composition_damage_union(a, b) &&
        accumulated.present == largeOptical,
        "Both copy and optical histories must survive until successful present");
    history.commit(0);
    const auto opticalOnly = history.prepare(0, 64, 32, {}, largeOptical);
    check(opticalOnly.copy.empty() && opticalOnly.present == largeOptical,
        "Optical-only invalidation must retain fresh UI pixels without another GPU copy");
    check(history.prepare(0, 80, 40, {}, {}).copy == CompositionDamageRect{0, 0, 80, 40},
        "Resize must discard old damage history and initialise the new image");

    // Compare all pixels of three retained buffers against a complete copy
    // whilst a widget moves and shrinks under an unchanged large glass halo.
    // This checks transparent erasure and missed updates, not only rectangles.
    history.reset();
    std::array<std::vector<std::uint32_t>, VIBRANCE_COMPOSITION_MAX_BUFFERS> retained;
    for (auto& image : retained) image.assign(64u * 32u, 0xdeadbeefu);
    std::vector<std::uint32_t> source(64u * 32u, 0u);
    CompositionDamageRect previous {};
    std::uint64_t copiedPixels = 0u, fullCopyPixels = 0u;
    for (unsigned frame = 0; frame < 100u; ++frame)
    {
        const auto index = frame % VIBRANCE_COMPOSITION_MAX_BUFFERS;
        std::fill(source.begin(), source.end(), 0u);
        for (unsigned y = 3u; y < 6u; ++y)
            for (unsigned x = 3u; x < 58u; ++x) source[y * 64u + x] = 0xff00ff00u;
        const auto x = static_cast<std::int32_t>(8u + frame % 31u);
        const auto extent = static_cast<std::int32_t>(4u + frame % 5u);
        const CompositionDamageRect current {x, 14, x + extent, 14 + extent};
        for (auto y = current.top; y < current.bottom; ++y)
            for (auto column = current.left; column < current.right; ++column)
                source[y * 64u + column] = 0xffff0000u;
        const auto update = history.prepare(index, 64u, 32u,
            composition_damage_union(previous, current), largeOptical);
        for (auto y = update.copy.top; y < update.copy.bottom; ++y)
            for (auto column = update.copy.left; column < update.copy.right; ++column)
                retained[index][y * 64u + column] = source[y * 64u + column];
        check(retained[index] == source,
            "Partial copies must match complete copies, including erased positions and retained background");
        copiedPixels += update.copy.pixels();
        fullCopyPixels += full.pixels();
        history.commit(index);
        previous = current;
    }
    check(copiedPixels * 4u < fullCopyPixels,
        "Small UI changes under a large glass halo should copy substantially fewer pixels");
    history.reset();
    const CompositionDamageRect oldLens { 2, 2, 18, 18 };
    const CompositionDamageRect newLens { 40, 8, 60, 28 };
    for (unsigned index = 0; index < VIBRANCE_COMPOSITION_MAX_BUFFERS; ++index)
    {
        history.prepare(index, 64, 32, {}, oldLens);
        history.commit(index);
    }
    std::vector<std::uint32_t> desktop(64u * 32u);
    const auto nativePixel = [&](int x, int y, CompositionDamageRect lens) {
        return x >= lens.left && x < lens.right && y >= lens.top && y < lens.bottom ?
            0xff808080u : 0xff000000u | static_cast<std::uint32_t>(x + y * 64);
    };
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x) desktop[y * 64 + x] = nativePixel(x, y, oldLens);
    const auto moved = history.prepare(0, 64, 32, a, newLens);
    check(moved.copy == a && moved.present == composition_damage_union(oldLens, newLens),
        "Moving a lens must invalidate its old optical footprint without widening the UI copy");
    for (int y = moved.present.top; y < moved.present.bottom; ++y)
        for (int x = moved.present.left; x < moved.present.right; ++x)
            desktop[y * 64 + x] = nativePixel(x, y, newLens);
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x)
            check(desktop[y * 64 + x] == nativePixel(x, y, newLens),
                "A moved native lens must leave every exposed desktop pixel equal to a complete repaint");
    // A deferred attempt must not forget the optical footprint still visible.
    const auto deferred = history.prepare(0, 64, 32, {}, newLens);
    check(deferred.present == composition_damage_union(oldLens, newLens),
        "A deferred optical move must retain the previous accepted lens footprint");
    history.commit(0);
    const auto removed = history.prepare(0, 64, 32, {}, {});
    check(removed.copy.empty() && removed.present == newLens,
        "Removing a lens must repaint its former desktop halo without another UI texture copy");
    for (int y = removed.present.top; y < removed.present.bottom; ++y)
        for (int x = removed.present.left; x < removed.present.right; ++x)
            desktop[y * 64 + x] = nativePixel(x, y, {});
    for (int y = 0; y < 32; ++y)
        for (int x = 0; x < 64; ++x)
            check(desktop[y * 64 + x] == nativePixel(x, y, {}),
                "Removing a native lens must restore the desktop without retained optical trails");
    history.commit(0);
    const auto settled = history.prepare(0, 64, 32, {}, {});
    check(settled.copy.empty() && settled.present.empty(),
        "A completed optical removal must not retain recurring desktop invalidation");
    return passed ? 0 : 1;
}

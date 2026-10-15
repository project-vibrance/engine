#include "../src/platform/win32/composition_present_retry.h"

#include <array>
#include <iostream>

int main()
{
    using namespace vibrance::win32;
    bool passed = true;
    const auto check = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };

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
    return passed ? 0 : 1;
}

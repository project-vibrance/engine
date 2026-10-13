#include "../src/renderer/vulkan/timestamp_diagnostics.h"

#include <array>
#include <cmath>
#include <iostream>
#include <limits>

namespace
{
    bool close(double actual, double expected)
    {
        return std::abs(actual - expected) <=
            1.0e-12 + std::abs(expected) * 1.0e-12;
    }
}

int main()
{
    using vibrance::renderer_diagnostics::supports_timestamp_clock;
    using vibrance::renderer_diagnostics::timestamp_elapsed_milliseconds;

    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition)
        {
            std::cerr << "Timestamp diagnostics test failed: " << message << '\n';
            passed = false;
        }
    };

    expect(supports_timestamp_clock(1u, 0.125) &&
        supports_timestamp_clock(36u, 1.0) &&
        supports_timestamp_clock(64u, 1000.0),
        "Supported counter widths and positive periods must be accepted");
    expect(!supports_timestamp_clock(0u, 1.0) &&
        !supports_timestamp_clock(65u, 1.0) &&
        !supports_timestamp_clock(std::numeric_limits<uint32_t>::max(), 1.0),
        "Counter widths outside 1 to 64 bits must be rejected");
    expect(!supports_timestamp_clock(36u, 0.0) &&
        !supports_timestamp_clock(36u, -0.0) &&
        !supports_timestamp_clock(36u, -1.0),
        "Zero and negative clock periods must be rejected");
    expect(!supports_timestamp_clock(36u, std::numeric_limits<double>::infinity()) &&
        !supports_timestamp_clock(36u, -std::numeric_limits<double>::infinity()) &&
        !supports_timestamp_clock(36u, std::numeric_limits<double>::quiet_NaN()),
        "Non-finite clock periods must be rejected");

    expect(close(timestamp_elapsed_milliseconds(100u, 250100u, 64u, 2.5), 0.625),
        "Clock ticks must be converted from nanoseconds to milliseconds");
    expect(close(timestamp_elapsed_milliseconds(20u, 28u, 64u, 0.125), 0.000001),
        "Fractional nanosecond periods must retain their precision");
    expect(close(timestamp_elapsed_milliseconds(123u, 123u, 36u, 10.0), 0.0),
        "Equal checkpoints must produce a zero duration");
    expect(close(timestamp_elapsed_milliseconds(1u, 0u, 1u, 1000.0), 0.001),
        "The minimum supported counter width must wrap correctly");

    const uint64_t maximumTimestamp = std::numeric_limits<uint64_t>::max();
    expect(close(timestamp_elapsed_milliseconds(
        maximumTimestamp - 4u, 7u, 64u, 1000.0), 0.012),
        "A 64-bit counter wrap must use unsigned subtraction without a 64-bit shift");
    expect(close(timestamp_elapsed_milliseconds(
        0u, uint64_t { 1u } << 40u, 64u, 1.0), 1099511.627776),
        "A full-width counter must preserve ticks above the lower 32 bits");

    const uint64_t counterPeriod = uint64_t { 1u } << 36u;
    expect(close(timestamp_elapsed_milliseconds(
        counterPeriod - 6u, 9u, 36u, 20.0), 0.0003),
        "Restricted-width counters must wrap at their valid bit count");
    const uint64_t beginWithHighBits =
        (uint64_t { 0xa5fu } << 36u) | (counterPeriod - 4u);
    const uint64_t endWithHighBits = (uint64_t { 0x2c1u } << 36u) | 5u;
    expect(close(timestamp_elapsed_milliseconds(
        beginWithHighBits, endWithHighBits, 36u, 1000.0), 0.009),
        "Bits outside the counter width must not affect a wrapped interval");

    const std::array<uint64_t, 4> checkpoints = {
        counterPeriod - 100u, counterPeriod - 40u, 30u, 55u };
    const std::array<double, 3> expectedIntervals = {
        0.00015, 0.000175, 0.0000625 };
    double summedMilliseconds = 0.0;
    for (std::size_t index = 0u; index < expectedIntervals.size(); ++index)
    {
        const double interval = timestamp_elapsed_milliseconds(
            checkpoints[index], checkpoints[index + 1u], 36u, 2.5);
        expect(close(interval, expectedIntervals[index]),
            "Each checkpoint interval must retain its duration across a counter wrap");
        summedMilliseconds += interval;
    }
    expect(close(summedMilliseconds, 0.0003875) &&
        close(summedMilliseconds, timestamp_elapsed_milliseconds(
            checkpoints.front(), checkpoints.back(), 36u, 2.5)),
        "Partitioned checkpoint intervals must sum to the full frame duration");

    return passed ? 0 : 1;
}

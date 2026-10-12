#pragma once

#include <atomic>
#include <cstdint>

namespace vibrance::win32
{
enum class LiquidDwmWindows10Session : std::uint32_t
{
    unknown, inactive, console, remote
};

// WTS notifications are authoritative when DWM's virtual account cannot query
// session information. A remote-session metric alone must never override a
// disconnect, lock or logoff notification.
class LiquidDwmWindows10SessionPolicy
{
public:
    void notify(std::uint32_t event) noexcept
    {
        switch (event)
        {
        case 2u: case 4u: case 6u: case 7u: case 11u:
            blocked_ = true;
            break;
        case 1u: case 3u: case 5u: case 8u:
            blocked_ = false;
            break;
        }
    }
    bool allows(LiquidDwmWindows10Session session) const noexcept
    {
        return !blocked_ && (session == LiquidDwmWindows10Session::console ||
            session == LiquidDwmWindows10Session::remote);
    }
    bool blocked() const noexcept { return blocked_; }
private:
    bool blocked_ = false;
};

// A single observer publishes session samples; render callbacks may invalidate
// them concurrently. An old sample cannot admit a newer transition generation.
class LiquidDwmWindows10RenderGate
{
public:
    static constexpr std::uint64_t settlingMilliseconds = 2000u;
    static constexpr std::uint64_t sampleLifetimeMilliseconds = 1000u;

    void invalidate() noexcept { generation_.fetch_add(1u, std::memory_order_acq_rel); }
    std::uint64_t generation() const noexcept { return generation_.load(std::memory_order_acquire); }

    void observe(std::uint64_t now, std::uint64_t sampledGeneration, bool activeSession) noexcept
    {
        if (!stable_ || !activeSession || sampledGeneration != generation() ||
            sampledGeneration != observedGeneration_ || now < lastObservation_ ||
            now - lastObservation_ > sampleLifetimeMilliseconds)
        {
            stableSince_ = now;
            stable_ = activeSession && sampledGeneration == generation();
        }
        observedGeneration_ = sampledGeneration;
        lastObservation_ = now;
        if (!activeSession) stable_ = false;
        const bool settled = stable_ && now >= stableSince_ &&
            now - stableSince_ >= settlingMilliseconds;
        sampledAt_.store(now, std::memory_order_release);
        admittedGeneration_.store(settled ? sampledGeneration : 0u, std::memory_order_release);
    }

    bool allows(std::uint64_t now) const noexcept
    {
        const auto current = generation();
        const auto sampledAt = sampledAt_.load(std::memory_order_acquire);
        return admittedGeneration_.load(std::memory_order_acquire) == current &&
            now >= sampledAt && now - sampledAt <= sampleLifetimeMilliseconds &&
            generation() == current;
    }

private:
    std::atomic<std::uint64_t> generation_ { 1u }, admittedGeneration_ { 0u }, sampledAt_ { 0u };
    std::uint64_t observedGeneration_ = 0u, stableSince_ = 0u, lastObservation_ = 0u;
    bool stable_ = false;
};
}

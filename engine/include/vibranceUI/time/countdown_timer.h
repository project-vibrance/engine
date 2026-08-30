#pragma once

#include <vibranceUI/export.h>

#include <cstdint>
#include <string>

enum class CountdownTimerStatus : std::uint8_t
{
    eIdle,
    eRunning,
    ePaused,
    eFinished
};

struct CountdownTimerSnapshot
{
    CountdownTimerStatus status = CountdownTimerStatus::eIdle;
    std::uint32_t selectedSeconds = 15u * 60u;
    std::uint32_t durationSeconds = 0u;
    std::uint32_t remainingSeconds = 0u;
    float remainingFraction = 0.0f;

    bool active() const
    {
        return status != CountdownTimerStatus::eIdle;
    }
};

// Monotonic countdown state. The UI supplies its existing steady GLFW clock,
// which keeps this model deterministic and independent from rendering rate.
class VIBRANCE_ENGINE_API CountdownTimer
{
public:
    static constexpr std::uint32_t kMaximumSeconds = 120u * 60u;

    explicit CountdownTimer(
        std::uint32_t selectedSeconds = 15u * 60u);

    void select_seconds(std::uint32_t seconds);
    void select_minutes(std::uint32_t minutes);

    bool start(double currentTimeSeconds);
    bool pause(double currentTimeSeconds);
    bool resume(double currentTimeSeconds);
    bool toggle_pause(double currentTimeSeconds);
    void cancel();
    bool update(double currentTimeSeconds);

    CountdownTimerSnapshot snapshot(double currentTimeSeconds) const;
    CountdownTimerStatus status() const;
    std::uint32_t selected_seconds() const;
    bool active() const;

private:
    double remaining_exact(double currentTimeSeconds) const;

    CountdownTimerStatus timerStatus = CountdownTimerStatus::eIdle;
    std::uint32_t selectedSeconds = 15u * 60u;
    std::uint32_t durationSeconds = 0u;
    double remainingAtAnchorSeconds = 0.0;
    double anchorTimeSeconds = 0.0;
};

VIBRANCE_ENGINE_API std::string format_countdown_time(
    std::uint32_t totalSeconds);

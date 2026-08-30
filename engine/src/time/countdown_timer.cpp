#include <vibranceUI/time/countdown_timer.h>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

CountdownTimer::CountdownTimer(std::uint32_t selectedSeconds)
{
    select_seconds(selectedSeconds);
}

void CountdownTimer::select_seconds(std::uint32_t seconds)
{
    selectedSeconds = std::min(seconds, kMaximumSeconds);
}

void CountdownTimer::select_minutes(std::uint32_t minutes)
{
    select_seconds(std::min(minutes, 120u) * 60u);
}

bool CountdownTimer::start(double currentTimeSeconds)
{
    if (selectedSeconds == 0u)
    {
        return false;
    }
    timerStatus = CountdownTimerStatus::eRunning;
    durationSeconds = selectedSeconds;
    remainingAtAnchorSeconds = static_cast<double>(durationSeconds);
    anchorTimeSeconds = currentTimeSeconds;
    return true;
}

bool CountdownTimer::pause(double currentTimeSeconds)
{
    if (timerStatus != CountdownTimerStatus::eRunning)
    {
        return false;
    }
    remainingAtAnchorSeconds = remaining_exact(currentTimeSeconds);
    anchorTimeSeconds = currentTimeSeconds;
    timerStatus = remainingAtAnchorSeconds <= 0.0 ?
        CountdownTimerStatus::eFinished :
        CountdownTimerStatus::ePaused;
    return true;
}

bool CountdownTimer::resume(double currentTimeSeconds)
{
    if (timerStatus != CountdownTimerStatus::ePaused ||
        remainingAtAnchorSeconds <= 0.0)
    {
        return false;
    }
    anchorTimeSeconds = currentTimeSeconds;
    timerStatus = CountdownTimerStatus::eRunning;
    return true;
}

bool CountdownTimer::toggle_pause(double currentTimeSeconds)
{
    return timerStatus == CountdownTimerStatus::ePaused ?
        resume(currentTimeSeconds) : pause(currentTimeSeconds);
}

void CountdownTimer::cancel()
{
    timerStatus = CountdownTimerStatus::eIdle;
    durationSeconds = 0u;
    remainingAtAnchorSeconds = 0.0;
    anchorTimeSeconds = 0.0;
}

bool CountdownTimer::update(double currentTimeSeconds)
{
    if (timerStatus != CountdownTimerStatus::eRunning ||
        remaining_exact(currentTimeSeconds) > 0.0)
    {
        return false;
    }
    timerStatus = CountdownTimerStatus::eFinished;
    remainingAtAnchorSeconds = 0.0;
    anchorTimeSeconds = currentTimeSeconds;
    return true;
}

CountdownTimerSnapshot CountdownTimer::snapshot(
    double currentTimeSeconds) const
{
    const double exact = remaining_exact(currentTimeSeconds);
    const std::uint32_t remaining = static_cast<std::uint32_t>(
        std::ceil(std::max(exact, 0.0) - 1e-9));
    CountdownTimerSnapshot value = {};
    value.status = timerStatus;
    value.selectedSeconds = selectedSeconds;
    value.durationSeconds = durationSeconds;
    value.remainingSeconds = remaining;
    value.remainingFraction = durationSeconds > 0u ?
        std::clamp(
            static_cast<float>(exact / static_cast<double>(durationSeconds)),
            0.0f,
            1.0f) :
        0.0f;
    return value;
}

CountdownTimerStatus CountdownTimer::status() const
{
    return timerStatus;
}

std::uint32_t CountdownTimer::selected_seconds() const
{
    return selectedSeconds;
}

bool CountdownTimer::active() const
{
    return timerStatus != CountdownTimerStatus::eIdle;
}

double CountdownTimer::remaining_exact(double currentTimeSeconds) const
{
    if (timerStatus == CountdownTimerStatus::eRunning)
    {
        return std::max(
            remainingAtAnchorSeconds -
                std::max(currentTimeSeconds - anchorTimeSeconds, 0.0),
            0.0);
    }
    if (timerStatus == CountdownTimerStatus::ePaused)
    {
        return std::max(remainingAtAnchorSeconds, 0.0);
    }
    return 0.0;
}

std::string format_countdown_time(std::uint32_t totalSeconds)
{
    const std::uint32_t hours = totalSeconds / 3600u;
    const std::uint32_t minutes = (totalSeconds % 3600u) / 60u;
    const std::uint32_t seconds = totalSeconds % 60u;

    std::ostringstream stream;
    if (hours > 0u)
    {
        stream << hours << ':'
               << std::setfill('0') << std::setw(2) << minutes << ':'
               << std::setw(2) << seconds;
    }
    else if (totalSeconds == 0u)
    {
        stream << "00:00";
    }
    else
    {
        stream << minutes << ':'
               << std::setfill('0') << std::setw(2) << seconds;
    }
    return stream.str();
}

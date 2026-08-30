#include <vibranceUI/time/countdown_timer.h>

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "countdown timer test failed: " << message << '\n';
        }
        return condition;
    }
}

int main()
{
    bool passed = true;
    CountdownTimer configuredTimer { 42u };
    passed &= expect(
        configuredTimer.selected_seconds() == 42u,
        "construction should accept an initial duration");

    CountdownTimer timer;

    passed &= expect(
        timer.selected_seconds() == 900u,
        "the default selection should be fifteen minutes");
    passed &= expect(timer.start(10.0), "a non-zero timer should start");
    passed &= expect(
        timer.snapshot(10.0).remainingSeconds == 900u,
        "starting should preserve the complete selected duration");
    passed &= expect(
        timer.snapshot(10.2).remainingSeconds == 900u &&
            timer.snapshot(11.0).remainingSeconds == 899u,
        "display seconds should use countdown-style ceiling");

    passed &= expect(timer.pause(20.25), "a running timer should pause");
    const CountdownTimerSnapshot paused = timer.snapshot(200.0);
    passed &= expect(
        paused.status == CountdownTimerStatus::ePaused &&
            paused.remainingSeconds == 890u,
        "a paused timer should not consume time");
    passed &= expect(timer.resume(300.0), "a paused timer should resume");
    passed &= expect(
        timer.snapshot(301.0).remainingSeconds == 889u,
        "a resumed timer should continue from its paused remainder");

    timer.cancel();
    timer.select_minutes(200u);
    passed &= expect(
        timer.selected_seconds() == CountdownTimer::kMaximumSeconds,
        "selections should clamp to 120 minutes");

    timer.select_seconds(2u);
    passed &= expect(timer.start(0.0), "a two-second timer should start");
    passed &= expect(timer.update(2.0), "a timer should report completion once");
    const CountdownTimerSnapshot finished = timer.snapshot(2.0);
    passed &= expect(
        finished.status == CountdownTimerStatus::eFinished &&
            finished.remainingSeconds == 0u,
        "a completed timer should remain visible at zero");
    passed &= expect(!timer.update(3.0), "completion should not repeat");
    passed &= expect(
        timer.start(3.0) &&
            timer.snapshot(3.0).remainingSeconds == 2u,
        "starting a completed timer should repeat its selected duration");

    timer.cancel();
    timer.select_seconds(0u);
    passed &= expect(!timer.start(5.0), "zero duration should not start");

    passed &= expect(format_countdown_time(0u) == "00:00", "zero format");
    passed &= expect(format_countdown_time(1u) == "0:01", "seconds format");
    passed &= expect(format_countdown_time(900u) == "15:00", "minutes format");
    passed &= expect(
        format_countdown_time(7200u) == "2:00:00",
        "hours format");

    return passed ? 0 : 1;
}

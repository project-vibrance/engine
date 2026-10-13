#include "../src/platform/win32/liquid_glass_dwm_windows10_lifecycle_policy.h"

#include <iostream>
#include <latch>
#include <thread>

int main()
{
    using vibrance::win32::LiquidDwmWindows10RenderGate;
    using vibrance::win32::LiquidDwmWindows10SessionPolicy;
    using Session = vibrance::win32::LiquidDwmWindows10Session;
    int failures = 0;
    const auto check = [&](bool value, const char* message) {
        if (!value) { std::cerr << message << '\n'; ++failures; }
    };
    LiquidDwmWindows10RenderGate gate;
    check(!gate.allows(100u), "Startup must wait for a known stable console session");
    for (std::uint64_t now = 100u; now <= 2100u; now += 250u)
    {
        gate.observe(now, gate.generation(), true);
        check(gate.allows(now) == (now == 2100u), "Console rendering must wait for the full settling interval");
    }
    const auto oldGeneration = gate.generation();
    gate.invalidate();
    check(!gate.allows(2100u), "A display event must revoke admission immediately");
    gate.observe(2200u, oldGeneration, true);
    check(!gate.allows(2200u), "An in-flight pre-transition sample must not reopen rendering");
    for (std::uint64_t now = 2300u; now <= 4300u; now += 250u) gate.observe(now, gate.generation(), true);
    check(gate.allows(4300u), "A fresh stable console session must recover after a display event");
    gate.observe(4400u, gate.generation(), false);
    check(!gate.allows(4400u), "An unknown or disconnected session must revoke admission");
    for (std::uint64_t now = 4500u; now <= 6500u; now += 250u) gate.observe(now, gate.generation(), true);
    check(gate.allows(6500u), "Reconnecting must resume after a new settling interval");
    check(!gate.allows(7501u), "A stalled observer must fail closed");
    gate.observe(7600u, gate.generation(), true);
    check(!gate.allows(7600u), "A stalled observer must not resume from one sample");
    for (std::uint64_t now = 7850u; now <= 9600u; now += 250u) gate.observe(now, gate.generation(), true);
    check(gate.allows(9600u), "Repeated fresh samples must recover the observer");
    gate.observe(10u, gate.generation(), true);
    check(!gate.allows(10u), "A backwards clock must reset the settling interval");
    gate.invalidate();
    for (std::uint64_t now = 20u; now <= 2020u; now += 250u) gate.observe(now, gate.generation(), true);
    check(gate.allows(2020u), "A fresh device generation must recover normally");
    gate.invalidate();
    check(!gate.allows(2020u), "Device loss must revoke the previously admitted generation");
    for (std::uint64_t now = 2100u; now <= 4100u; now += 250u) gate.observe(now, gate.generation(), true);
    check(gate.allows(4100u), "The concurrent sample fixture must first admit a stable generation");
    std::latch sampled(1), releaseSample(1);
    std::thread observer([&] {
        const auto captured = gate.generation();
        sampled.count_down();
        releaseSample.wait();
        gate.observe(4200u, captured, true);
    });
    sampled.wait();
    gate.invalidate();
    releaseSample.count_down();
    observer.join();
    check(!gate.allows(4200u), "A racing observer must not readmit a revoked device generation");

    // Reproduce the actual console-to-Enhanced Session transition. An active
    // RDP connection must recover, even when it is no longer the console.
    LiquidDwmWindows10RenderGate transitionGate;
    LiquidDwmWindows10SessionPolicy session;
    for (std::uint64_t now = 100u; now <= 2100u; now += 250u)
        transitionGate.observe(now, transitionGate.generation(), session.allows(Session::console));
    check(transitionGate.allows(2100u), "The transition fixture must admit its initial console");
    session.notify(2u); // WTS_CONSOLE_DISCONNECT
    transitionGate.invalidate();
    for (std::uint64_t now = 2200u; now <= 4200u; now += 250u)
        transitionGate.observe(now, transitionGate.generation(), session.allows(Session::remote));
    check(!transitionGate.allows(4200u), "A remote metric must not bypass a disconnect notification");
    session.notify(3u); // WTS_REMOTE_CONNECT
    transitionGate.invalidate();
    for (std::uint64_t now = 4300u; now <= 6300u; now += 250u)
    {
        transitionGate.observe(now, transitionGate.generation(), session.allows(Session::remote));
        check(transitionGate.allows(now) == (now == 6300u),
            "Stable RDP must recover only after the complete settling interval");
    }
    session.notify(7u); // WTS_SESSION_LOCK
    transitionGate.invalidate();
    check(!session.allows(Session::remote) && !transitionGate.allows(6300u),
        "Locking an active remote session must immediately revoke rendering");
    session.notify(8u); // WTS_SESSION_UNLOCK
    for (std::uint64_t now = 6400u; now <= 8400u; now += 250u)
        transitionGate.observe(now, transitionGate.generation(), session.allows(Session::remote));
    check(transitionGate.allows(8400u), "Unlocking must recover a stable remote session");
    check(!session.allows(Session::unknown) && !session.allows(Session::inactive),
        "Missing or inactive session evidence must never admit rendering");
    session.notify(4u); // WTS_REMOTE_DISCONNECT
    check(!session.allows(Session::remote), "Remote disconnect must block cached remote-session evidence");
    session.notify(1u); // WTS_CONSOLE_CONNECT
    check(session.allows(Session::console), "Returning to the console must clear the disconnect block");
    session.notify(6u); // WTS_SESSION_LOGOFF
    check(!session.allows(Session::console), "Logoff must block cached console evidence");
    return failures ? 1 : 0;
}

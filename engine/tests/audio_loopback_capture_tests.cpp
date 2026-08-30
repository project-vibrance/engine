#include <vibranceUI/audio/loopback_capture.h>
#include <vibranceUI/audio/spectrum.h>

#include <iostream>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "audio loopback capture test failed: "
                << message << '\n';
        }
        return condition;
    }
}

int main()
{
    bool passed = true;
    AudioSpectrumProcessor spectrum;

    AudioLoopbackCapture systemCapture(spectrum);
    passed &= expect(
        !systemCapture.captures_process_tree(),
        "system output should be the reusable default");
    passed &= expect(
        !systemCapture.set_target_process_id(42u),
        "system capture should ignore process targets");

    AudioLoopbackCaptureOptions processOptions;
    processOptions.mode = AudioLoopbackCaptureMode::eProcessTree;
    AudioLoopbackCapture processCapture(spectrum, processOptions);
    passed &= expect(
        processCapture.captures_process_tree(),
        "process-tree mode should be explicit");
    passed &= expect(
        processCapture.set_target_process_id(42u),
        "the first process target should report a change");
    passed &= expect(
        !processCapture.set_target_process_id(42u),
        "repeating a process target should remain inert");
    passed &= expect(
        processCapture.target_process_id() == 42u,
        "the selected process should be observable");

    AudioLoopbackCapture emptyCapture(AudioLoopbackCaptureCallbacks {});
    passed &= expect(
        !emptyCapture.start(),
        "capture should reject a missing sample consumer");

#ifdef _WIN32
    processCapture.set_target_process_id(0u);
    passed &= expect(
        AudioLoopbackCapture::platform_supported(),
        "Windows should expose the loopback backend");
    passed &= expect(
        processCapture.start(),
        "a valid Windows capture should start");
    passed &= expect(
        processCapture.running(),
        "a started capture should report running");
    passed &= expect(
        processCapture.start(),
        "repeated start should be idempotent");
    processCapture.stop();
    passed &= expect(
        !processCapture.running(),
        "stop should clear running state");
    passed &= expect(
        processCapture.start(),
        "a stopped capture should restart");
    processCapture.stop();
#else
    passed &= expect(
        !AudioLoopbackCapture::platform_supported(),
        "unsupported platforms should report their capability");
    passed &= expect(
        !processCapture.start(),
        "unsupported platforms should fail start without spawning a worker");
    passed &= expect(
        !processCapture.running(),
        "a failed start should not report a running capture");
#endif

    return passed ? 0 : 1;
}

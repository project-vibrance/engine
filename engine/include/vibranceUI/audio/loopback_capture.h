#pragma once

#include <vibranceUI/export.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <span>

class AudioSpectrumProcessor;

enum class AudioLoopbackCaptureMode : std::uint8_t
{
    eSystemOutput,
    eProcessTree
};

struct AudioLoopbackCaptureOptions
{
    AudioLoopbackCaptureMode mode = AudioLoopbackCaptureMode::eSystemOutput;
    std::uint32_t targetProcessId = 0u;
};

struct AudioLoopbackCaptureCallbacks
{
    // Callbacks run on the capture worker. The sample span remains valid only
    // for the duration of the call and always contains downmixed mono floats.
    std::function<void(std::span<const float>, float sampleRateHz)> onSamples;
    std::function<void()> onReset;
};

// Platform loopback source with no renderer or product dependencies. The
// processor constructor is the simplest path for visualizers; callbacks allow
// the same capture backend to feed recording, metering, or custom analysis.
class VIBRANCE_ENGINE_API AudioLoopbackCapture
{
public:
    // The referenced processor must outlive the capture object.
    explicit AudioLoopbackCapture(
        AudioSpectrumProcessor& processor,
        AudioLoopbackCaptureOptions options = {});
    explicit AudioLoopbackCapture(
        AudioLoopbackCaptureCallbacks callbacks,
        AudioLoopbackCaptureOptions options = {});
    ~AudioLoopbackCapture();

    AudioLoopbackCapture(const AudioLoopbackCapture&) = delete;
    AudioLoopbackCapture& operator=(const AudioLoopbackCapture&) = delete;
    AudioLoopbackCapture(AudioLoopbackCapture&&) = delete;
    AudioLoopbackCapture& operator=(AudioLoopbackCapture&&) = delete;

    static bool platform_supported();
    bool start();
    void stop();
    bool running() const;

    bool captures_process_tree() const;
    std::uint32_t target_process_id() const;
    // Returns true when the process target changed. System-output capture
    // ignores this operation and returns false.
    bool set_target_process_id(std::uint32_t processId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

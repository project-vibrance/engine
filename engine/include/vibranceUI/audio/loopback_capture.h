// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

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


    std::function<void(std::span<const float>, float sampleRateHz)> onSamples;
    std::function<void()> onReset;

    std::function<void(float)> onSourceVolume;
};




class VIBRANCE_ENGINE_API AudioLoopbackCapture
{
public:

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

    bool set_mode(AudioLoopbackCaptureMode mode);
    std::uint32_t target_process_id() const;


    bool set_target_process_id(std::uint32_t processId);

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

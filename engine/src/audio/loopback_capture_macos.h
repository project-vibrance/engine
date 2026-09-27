#pragma once
#include <vibranceUI/audio/loopback_capture.h>
#include <atomic>

bool mac_audio_capture_supported();
void mac_audio_capture_loop(const AudioLoopbackCaptureCallbacks& callbacks,
    const AudioLoopbackCaptureOptions& options,
    std::atomic<std::uint32_t>& targetProcessId,
    std::atomic_bool& requested);

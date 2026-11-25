// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/audio/loopback_capture.h>
#include <atomic>
#include <string>

struct UnixAudioCaptureTarget
{
    std::uint32_t processId = 0;
    std::string application;
};

void unix_audio_capture_loop(const AudioLoopbackCaptureCallbacks& callbacks,
    const std::atomic<AudioLoopbackCaptureMode>& mode,
    const std::atomic<std::shared_ptr<const UnixAudioCaptureTarget>>& target,
    std::atomic_bool& captureRequested);

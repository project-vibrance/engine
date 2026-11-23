// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/audio/loopback_capture.h>
#include <atomic>

void unix_audio_capture_loop(const AudioLoopbackCaptureCallbacks& callbacks,
    const std::atomic<AudioLoopbackCaptureMode>& mode, const std::atomic<std::uint32_t>& targetProcessId,
    std::atomic_bool& captureRequested);

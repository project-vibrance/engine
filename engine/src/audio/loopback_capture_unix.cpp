// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "loopback_capture_unix.h"

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <charconv>
#include <chrono>
#include <fstream>
#include <string>
#include <string_view>
#include <thread>

namespace
{
constexpr unsigned sampleRate = 48000;

bool in_process_tree(const char* id, std::uint32_t target)
{
    if (!id) return false;
    const std::string_view text(id);
    std::uint32_t pid = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), pid);
    if (parsed.ec != std::errc() || parsed.ptr != text.data() + text.size()) return false;
#if defined(__linux__)
    while (pid > 1 && pid != target)
    {
        std::ifstream status("/proc/" + std::to_string(pid) + "/stat");
        std::string line;
        if (!std::getline(status, line)) return false;
        const auto nameEnd = line.rfind(')');
        if (nameEnd == std::string::npos || nameEnd + 4 >= line.size()) return false;
        std::uint32_t parent = 0;
        if (std::from_chars(line.data() + nameEnd + 4, line.data() + line.size(), parent).ec != std::errc() ||
            parent == pid) return false;
        pid = parent;
    }
#endif
    return pid == target;
}

class MonitorCapture
{
public:
    MonitorCapture(const AudioLoopbackCaptureCallbacks& callbacks, std::uint32_t process) :
        callbacks(callbacks), process(process) {}

    ~MonitorCapture() { disconnect_stream(); }

    bool connect()
    {
        if (!loop) return false;
        context.reset(pa_context_new(pa_mainloop_get_api(loop.get()), "Vibrance"));
        return context && pa_context_connect(context.get(), nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) == 0;
    }

    bool tick()
    {
        if (pa_mainloop_iterate(loop.get(), 0, nullptr) < 0) return false;
        if (failed) return false;
        const auto state = pa_context_get_state(context.get());
        if (!PA_CONTEXT_IS_GOOD(state)) return false;
        if (state != PA_CONTEXT_READY) return true;
        const auto now = std::chrono::steady_clock::now();
        if (now >= nextDiscovery)
        {
            nextDiscovery = now + std::chrono::milliseconds(750);
            if (process)
            {
                bestInput = PA_INVALID_INDEX;
                request(pa_context_get_sink_input_info_list(context.get(), input_info, this));
            }
            else request(pa_context_get_server_info(context.get(), server_info, this));
        }
        if (!stream) return true;
        if (!PA_STREAM_IS_GOOD(pa_stream_get_state(stream.get()))) return false;
        for (auto bytes = pa_stream_readable_size(stream.get()); bytes && bytes != static_cast<std::size_t>(-1);
             bytes = pa_stream_readable_size(stream.get()))
        {
            const void* data = nullptr;
            std::size_t count = 0;
            if (pa_stream_peek(stream.get(), &data, &count) < 0) return false;
            if (!count) break;
            if (data) callbacks.onSamples({static_cast<const float*>(data), count / sizeof(float)}, float(sampleRate));
            if (pa_stream_drop(stream.get()) < 0) return false;
        }
        return true;
    }

private:
    static void request(pa_operation* operation)
    {
        if (operation) pa_operation_unref(operation);
    }

    void disconnect_stream() noexcept
    {
        if (!stream) return;
        pa_stream_disconnect(stream.get());
        stream.reset();
        if (callbacks.onReset)
            try { callbacks.onReset(); } catch (...) {}
    }

    static void server_info(pa_context* context, const pa_server_info* info, void* user)
    {
        if (info && info->default_sink_name)
            request(pa_context_get_sink_info_by_name(context, info->default_sink_name, sink_info, user));
    }

    static void input_info(pa_context* context, const pa_sink_input_info* info, int end, void* user) try
    {
        auto& self = *static_cast<MonitorCapture*>(user);
        if (end > 0)
        {
            if (self.bestInput != PA_INVALID_INDEX)
                request(pa_context_get_sink_info_by_index(context, self.bestSink, sink_info, user));
            else self.disconnect_stream();
        }
        else if (info && !info->corked &&
            (self.bestInput == PA_INVALID_INDEX || info->index == self.activeInput) &&
            in_process_tree(pa_proplist_gets(info->proplist, PA_PROP_APPLICATION_PROCESS_ID), self.process))
        {
            self.bestInput = info->index;
            self.bestSink = info->sink;
            self.gain = info->mute ? 0.0f : float(pa_sw_volume_to_linear(pa_cvolume_avg(&info->volume)));
        }
    }
    catch (...)
    {
        static_cast<MonitorCapture*>(user)->failed = true;
    }

    static void sink_info(pa_context*, const pa_sink_info* info, int end, void* user) try
    {
        if (end || !info || !info->monitor_source_name) return;
        auto& self = *static_cast<MonitorCapture*>(user);
        const auto input = self.process ? self.bestInput : PA_INVALID_INDEX;
        if (self.callbacks.onSourceVolume) self.callbacks.onSourceVolume(self.process ? self.gain : 1.0f);
        if (self.stream && self.source == info->monitor_source_name && self.activeInput == input) return;
        self.disconnect_stream();
        self.source = info->monitor_source_name;
        self.activeInput = input;
        const pa_sample_spec spec {PA_SAMPLE_FLOAT32NE, sampleRate, 1};
        self.stream.reset(pa_stream_new(self.context.get(), "Output monitor", &spec, nullptr));
        if (!self.stream) return;
        if ((input != PA_INVALID_INDEX && pa_stream_set_monitor_stream(self.stream.get(), input) < 0) ||
            pa_stream_connect_record(self.stream.get(), self.source.c_str(), nullptr, PA_STREAM_DONT_MOVE) < 0)
            self.stream.reset();
    }
    catch (...)
    {
        static_cast<MonitorCapture*>(user)->failed = true;
    }

    const AudioLoopbackCaptureCallbacks& callbacks;
    std::uint32_t process = 0;
    std::uint32_t bestInput = PA_INVALID_INDEX;
    std::uint32_t bestSink = PA_INVALID_INDEX;
    std::uint32_t activeInput = PA_INVALID_INDEX;
    float gain = 1.0f;
    bool failed = false;
    std::string source;
    std::chrono::steady_clock::time_point nextDiscovery;
    std::unique_ptr<pa_mainloop, decltype(&pa_mainloop_free)> loop {pa_mainloop_new(), pa_mainloop_free};
    std::unique_ptr<pa_context, decltype(&pa_context_unref)> context {nullptr, pa_context_unref};
    std::unique_ptr<pa_stream, decltype(&pa_stream_unref)> stream {nullptr, pa_stream_unref};
};
}

void unix_audio_capture_loop(const AudioLoopbackCaptureCallbacks& callbacks,
    const std::atomic<AudioLoopbackCaptureMode>& mode, const std::atomic<std::uint32_t>& targetProcessId,
    std::atomic_bool& captureRequested)
{
    try
    {
        while (captureRequested)
        {
            const auto selectedMode = mode.load();
            const auto pid = selectedMode == AudioLoopbackCaptureMode::eProcessTree ? targetProcessId.load() : 0u;
            if (selectedMode == AudioLoopbackCaptureMode::eProcessTree && !pid)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }
            MonitorCapture capture(callbacks, pid);
            if (capture.connect())
            {
                while (captureRequested && mode == selectedMode &&
                    (selectedMode == AudioLoopbackCaptureMode::eSystemOutput || targetProcessId == pid) && capture.tick())
                    std::this_thread::sleep_for(std::chrono::milliseconds(12));
            }
            if (captureRequested) std::this_thread::sleep_for(std::chrono::milliseconds(75));
        }
    }
    catch (...)
    {
        captureRequested = false;
    }
}

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "loopback_capture_unix.h"

#include <pulse/pulseaudio.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <chrono>
#include <fstream>
#include <map>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
constexpr unsigned sampleRate = 48000;

bool in_process_tree(const char* id, std::uint32_t target)
{
    if (!id || !target) return false;
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

std::string normalise_application(std::string_view value)
{
    if (value.ends_with(".desktop")) value.remove_suffix(8);
    if (const auto instance = value.find(".instance"); instance != value.npos)
        value = value.substr(0, instance);
    std::string result(value);
    std::transform(result.begin(), result.end(), result.begin(),
        [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return result;
}

std::string_view short_application(std::string_view value)
{
    const auto separator = value.rfind('.');
    return separator == value.npos ? value : value.substr(separator + 1);
}

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr auto blockDuration = 10ms;
constexpr std::size_t blockFrames = sampleRate / 100;
constexpr std::size_t bufferFrames = blockFrames * 12;
constexpr std::uint32_t frameBytes = 2 * sizeof(float);

struct StereoFrame
{
    float left = 0.0f;
    float right = 0.0f;
};

using StereoBlock = std::array<StereoFrame, blockFrames>;
using RouteKey = std::pair<std::uint32_t, std::uint32_t>;

struct Route
{
    std::string source;
    float gain;
    bool silent;
};

class MonitorStream
{
public:
    explicit MonitorStream(const Route& route) : source(route.source), silent(route.silent) {}

    ~MonitorStream()
    {
        if (stream) pa_stream_disconnect(stream.get());
    }

    bool connect(pa_context* context, std::uint32_t input)
    {
        const pa_sample_spec spec {PA_SAMPLE_FLOAT32NE, sampleRate, 2};
        stream.reset(pa_stream_new(context, "Output monitor", &spec, nullptr));
        if (!stream) return false;
        constexpr auto unset = static_cast<std::uint32_t>(-1);
        const pa_buffer_attr buffer {bufferFrames * frameBytes, unset, unset, unset, blockFrames * 2 * frameBytes};
        const auto flags = static_cast<pa_stream_flags_t>(
            PA_STREAM_DONT_MOVE | PA_STREAM_ADJUST_LATENCY | PA_STREAM_DONT_INHIBIT_AUTO_SUSPEND);
        return (input == PA_INVALID_INDEX || pa_stream_set_monitor_stream(stream.get(), input) == 0) &&
            pa_stream_connect_record(stream.get(), source.c_str(), &buffer, flags) == 0;
    }

    void set_silent(bool value)
    {
        if (silent != value)
        {
            start = 0;
            available = 0;
            primed = false;
            silent = value;
        }
    }

    bool read()
    {
        const auto state = pa_stream_get_state(stream.get());
        if (state != PA_STREAM_READY) return PA_STREAM_IS_GOOD(state);
        for (;;)
        {
            const auto bytes = pa_stream_readable_size(stream.get());
            if (bytes == 0) return true;
            if (bytes == static_cast<std::size_t>(-1)) return false;
            const void* data = nullptr;
            std::size_t count = 0;
            if (pa_stream_peek(stream.get(), &data, &count) < 0) return false;
            if (count == 0) return true;
            if (!silent)
            {
                const auto* samples = static_cast<const float*>(data);
                const auto frames = count / frameBytes;
                for (std::size_t i = frames > bufferFrames ? frames - bufferFrames : 0; i < frames; ++i)
                {
                    queue[(start + available) % bufferFrames] =
                        samples ? StereoFrame{samples[2 * i], samples[2 * i + 1]} : StereoFrame{};
                    if (available == bufferFrames) start = (start + 1) % bufferFrames;
                    else ++available;
                }
                lastPacket = Clock::now();
            }
            if (pa_stream_drop(stream.get()) < 0) return false;
        }
    }

    bool mix(StereoBlock& output, Clock::time_point now)
    {
        if (available == 0)
        {
            if (now - lastPacket > 50ms) primed = false;
            return false;
        }
        if (!primed)
        {
            const auto* buffer = pa_stream_get_buffer_attr(stream.get());
            const auto threshold = std::clamp<std::size_t>(
                (buffer ? buffer->fragsize / frameBytes : blockFrames) + blockFrames,
                blockFrames * 2, bufferFrames / 2);
            if (available < threshold && now - lastPacket < 40ms) return false;
            primed = true;
        }

        const auto frames = std::min(available, output.size());
        for (std::size_t i = 0; i < frames; ++i)
        {
            const auto& frame = queue[(start + i) % bufferFrames];
            output[i].left += frame.left;
            output[i].right += frame.right;
        }
        start = (start + frames) % bufferFrames;
        available -= frames;
        return true;
    }

    const std::string source;

private:
    std::array<StereoFrame, bufferFrames> queue {};
    std::size_t start = 0;
    std::size_t available = 0;
    bool primed = false;
    bool silent;
    Clock::time_point lastPacket;
    std::unique_ptr<pa_stream, decltype(&pa_stream_unref)> stream {nullptr, pa_stream_unref};
};

class MonitorCapture
{
public:
    MonitorCapture(const AudioLoopbackCaptureCallbacks& callbacks, const UnixAudioCaptureTarget* target) :
        callbacks(callbacks), process(target ? target->processId : 0),
        application(target ? normalise_application(target->application) : std::string{}),
        processOnly(target != nullptr) {}

    ~MonitorCapture()
    {
        if (published && callbacks.onReset)
            try { callbacks.onReset(); } catch (...) {}
    }

    bool connect()
    {
        if (!loop) return false;
        context.reset(pa_context_new(pa_mainloop_get_api(loop.get()), "Vibrance"));
        return context && pa_context_connect(context.get(), nullptr, PA_CONTEXT_NOAUTOSPAWN, nullptr) == 0;
    }

    bool tick()
    {
        const auto timeout = nextBlock == Clock::time_point{} ? 20000us :
            std::clamp(std::chrono::duration_cast<std::chrono::microseconds>(nextBlock - Clock::now()), 0us, 20000us);
        if (pa_mainloop_prepare(loop.get(), static_cast<int>(timeout.count())) < 0 ||
            pa_mainloop_poll(loop.get()) < 0 || pa_mainloop_dispatch(loop.get()) < 0 || failed)
            return false;

        const auto state = pa_context_get_state(context.get());
        if (!PA_CONTEXT_IS_GOOD(state)) return false;
        if (state != PA_CONTEXT_READY) return true;
        if (!subscribed)
        {
            auto mask = PA_SUBSCRIPTION_MASK_SINK;
            if (processOnly)
                mask = static_cast<pa_subscription_mask_t>(mask | PA_SUBSCRIPTION_MASK_SINK_INPUT | PA_SUBSCRIPTION_MASK_CLIENT);
            pa_context_set_subscribe_callback(context.get(), subscription, this);
            if (auto* request = pa_context_subscribe(context.get(), mask, nullptr, nullptr))
                pa_operation_unref(request);
            subscribed = true;
        }

        const auto now = Clock::now();
        if (!discovery && now >= nextDiscovery)
        {
            nextDiscovery = now + 750ms;
            if (processOnly)
            {
                matchingClients.clear();
                inputs.clear();
                discovery.reset(pa_context_get_client_info_list(context.get(), client_info, this));
                if (!discovery) discover_inputs();
            }
            else discover_sinks();
        }
        for (auto it = monitors.begin(); it != monitors.end();)
        {
            if (it->second.read()) ++it;
            else
            {
                it = monitors.erase(it);
                nextDiscovery = std::min(nextDiscovery, now + 100ms);
            }
        }
        if ((!monitors.empty() || silenceBlocks) && now >= nextBlock) publish(now);
        return true;
    }

private:
    struct Input
    {
        std::uint32_t index;
        std::uint32_t sink;
        int score;
        float gain;
        bool silent;
    };

    static void subscription(pa_context*, pa_subscription_event_type_t, std::uint32_t, void* user)
    {
        auto& self = *static_cast<MonitorCapture*>(user);
        self.nextDiscovery = std::min(self.nextDiscovery, Clock::now() + 20ms);
    }

    int match(const pa_proplist* properties) const
    {
        if (!properties) return 0;
        const char* pid = pa_proplist_gets(properties, "pipewire.sec.pid");
        if (!pid) pid = pa_proplist_gets(properties, PA_PROP_APPLICATION_PROCESS_ID);
        if (in_process_tree(pid, process)) return 3;
        if (application.empty()) return 0;

        int score = 0;
        for (const char* key : {PA_PROP_APPLICATION_ID, PA_PROP_APPLICATION_NAME,
                PA_PROP_APPLICATION_PROCESS_BINARY, "pipewire.access.portal.app_id", "node.name"})
        {
            const char* value = pa_proplist_gets(properties, key);
            if (!value) continue;
            const auto name = normalise_application(value);
            if (name == application) return 2;
            if (short_application(name) == application || name == short_application(application))
                score = 1;
        }
        return score;
    }

    void discover_inputs()
    {
        discovery.reset(pa_context_get_sink_input_info_list(context.get(), input_info, this));
    }

    void discover_sinks()
    {
        routes.clear();
        discovery.reset(pa_context_get_sink_info_list(context.get(), sink_info, this));
    }

    static void client_info(pa_context*, const pa_client_info* info, int end, void* user) try
    {
        auto& self = *static_cast<MonitorCapture*>(user);
        if (end) self.discover_inputs();
        else if (info)
        {
            const int score = self.match(info->proplist);
            if (score) self.matchingClients.emplace(info->index, score);
        }
    }
    catch (...)
    {
        static_cast<MonitorCapture*>(user)->failed = true;
    }

    static void input_info(pa_context*, const pa_sink_input_info* info, int end, void* user) try
    {
        auto& self = *static_cast<MonitorCapture*>(user);
        if (end)
        {
            if (end > 0)
            {
                self.bestScore = 0;
                for (const auto& input : self.inputs)
                    if (!input.silent) self.bestScore = std::max(self.bestScore, input.score);
                if (!self.bestScore)
                    for (const auto& input : self.inputs) self.bestScore = std::max(self.bestScore, input.score);
                self.discover_sinks();
            }
            else self.discovery.reset();
            return;
        }
        if (!info) return;
        const auto client = self.matchingClients.find(info->client);
        const int score = std::max(self.match(info->proplist),
            client == self.matchingClients.end() ? 0 : client->second);
        if (score)
        {
            const auto gain = static_cast<float>(pa_sw_volume_to_linear(pa_cvolume_avg(&info->volume)));
            self.inputs.push_back({info->index, info->sink, score, gain, info->corked || info->mute || gain == 0.0f});
        }
    }
    catch (...)
    {
        static_cast<MonitorCapture*>(user)->failed = true;
    }

    static void sink_info(pa_context*, const pa_sink_info* info, int end, void* user) try
    {
        auto& self = *static_cast<MonitorCapture*>(user);
        if (end)
        {
            self.discovery.reset();
            if (end > 0) self.reconcile();
            return;
        }
        if (!info || !info->monitor_source_name || !*info->monitor_source_name) return;
        if (!self.processOnly)
        {
            self.routes.emplace(RouteKey{info->index, PA_INVALID_INDEX},
                Route{info->monitor_source_name, 1.0f, info->mute != 0});
            return;
        }
        for (const auto& input : self.inputs)
        {
            if (input.sink == info->index && input.score == self.bestScore)
                self.routes.emplace(RouteKey{info->index, input.index},
                    Route{info->monitor_source_name, input.gain, input.silent || info->mute});
        }
    }
    catch (...)
    {
        static_cast<MonitorCapture*>(user)->failed = true;
    }

    void reconcile()
    {
        std::erase_if(monitors, [this](const auto& entry) {
            const auto desired = routes.find(entry.first);
            return desired == routes.end() || desired->second.source != entry.second.source;
        });
        for (const auto& [key, route] : routes)
        {
            const auto [monitor, inserted] = monitors.try_emplace(key, route);
            if (inserted && !monitor->second.connect(context.get(), key.second))
            {
                monitors.erase(monitor);
                continue;
            }
            monitor->second.set_silent(route.silent);
        }
        const float gain = routes.size() == 1 ? routes.begin()->second.gain : 1.0f;
        if (callbacks.onSourceVolume && gain != appliedGain)
        {
            callbacks.onSourceVolume(gain);
            appliedGain = gain;
        }
    }

    void publish(Clock::time_point now)
    {
        StereoBlock audio {};
        bool received = false;
        for (auto& [key, monitor] : monitors) received |= monitor.mix(audio, now);

        if (received) silenceBlocks = 4;
        else if (silenceBlocks) --silenceBlocks;
        else
        {
            nextBlock = {};
            return;
        }

        float midPower = 0.0f, sidePower = 0.0f;
        for (const auto& frame : audio)
        {
            const float mid = frame.left + frame.right;
            const float side = frame.left - frame.right;
            midPower += mid * mid;
            sidePower += side * side;
        }
        const float rightWeight = sidePower > midPower * 4.0f ? -1.0f : 1.0f;
        std::array<float, blockFrames> mono {};
        for (std::size_t i = 0; i < mono.size(); ++i)
            mono[i] = 0.5f * (audio[i].left + rightWeight * audio[i].right);

        callbacks.onSamples(mono, float(sampleRate));
        published = true;
        if (now - nextBlock >= blockDuration) nextBlock = now;
        nextBlock += blockDuration;
    }

    const AudioLoopbackCaptureCallbacks& callbacks;
    const std::uint32_t process;
    const std::string application;
    const bool processOnly;
    std::unordered_map<std::uint32_t, int> matchingClients;
    std::vector<Input> inputs;
    std::map<RouteKey, Route> routes;
    int bestScore = 0;
    float appliedGain = -1.0f;
    unsigned silenceBlocks = 0;
    bool subscribed = false;
    bool published = false;
    bool failed = false;
    Clock::time_point nextDiscovery;
    Clock::time_point nextBlock;
    std::unique_ptr<pa_mainloop, decltype(&pa_mainloop_free)> loop {pa_mainloop_new(), pa_mainloop_free};
    std::unique_ptr<pa_context, decltype(&pa_context_unref)> context {nullptr, pa_context_unref};
    std::unique_ptr<pa_operation, decltype(&pa_operation_unref)> discovery {nullptr, pa_operation_unref};
    std::map<RouteKey, MonitorStream> monitors;
};
}

void unix_audio_capture_loop(const AudioLoopbackCaptureCallbacks& callbacks,
    const std::atomic<AudioLoopbackCaptureMode>& mode,
    const std::atomic<std::shared_ptr<const UnixAudioCaptureTarget>>& target,
    std::atomic_bool& captureRequested)
{
    try
    {
        while (captureRequested)
        {
            const auto selectedMode = mode.load();
            const auto selected = selectedMode == AudioLoopbackCaptureMode::eProcessTree ? target.load() : nullptr;
            if (selected && !selected->processId && selected->application.empty())
            {
                std::this_thread::sleep_for(20ms);
                continue;
            }
            MonitorCapture capture(callbacks, selected.get());
            if (capture.connect())
            {
                while (captureRequested && mode == selectedMode &&
                    (!selected || target.load() == selected) && capture.tick()) {}
            }
            if (captureRequested && mode == selectedMode && (!selected || target.load() == selected))
                std::this_thread::sleep_for(75ms);
        }
    }
    catch (...)
    {
        captureRequested = false;
    }
}

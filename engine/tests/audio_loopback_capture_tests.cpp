#include <vibranceUI/audio/loopback_capture.h>
#include <vibranceUI/audio/spectrum.h>

#include <iostream>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <atomic>
#include <chrono>
#include <thread>
#endif

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

#if defined(_WIN32)
    struct SilentRenderer
    {
        IMMDeviceEnumerator* enumerator = nullptr;
        IMMDevice* endpoint = nullptr;
        IAudioClient* client = nullptr;
        IAudioRenderClient* renderer = nullptr;
        WAVEFORMATEX* format = nullptr;
        UINT32 bufferFrames = 0;

        ~SilentRenderer()
        {
            if (client) client->Stop();
            if (renderer) renderer->Release();
            if (client) client->Release();
            if (format) CoTaskMemFree(format);
            if (endpoint) endpoint->Release();
            if (enumerator) enumerator->Release();
        }
        bool refill()
        {
            UINT32 padding = 0;
            if (FAILED(client->GetCurrentPadding(&padding)) || padding > bufferFrames) return false;
            const auto frames = bufferFrames - padding;
            if (!frames) return true;
            BYTE* data = nullptr;
            return SUCCEEDED(renderer->GetBuffer(frames, &data)) &&
                SUCCEEDED(renderer->ReleaseBuffer(frames, AUDCLNT_BUFFERFLAGS_SILENT));
        }
        bool start()
        {
            GUID session {};
            return SUCCEEDED(CoCreateInstance(CLSID_MMDeviceEnumerator, nullptr,
                    CLSCTX_ALL, IID_IMMDeviceEnumerator, reinterpret_cast<void**>(&enumerator))) &&
                SUCCEEDED(enumerator->GetDefaultAudioEndpoint(eRender, eConsole, &endpoint)) &&
                SUCCEEDED(endpoint->Activate(IID_IAudioClient, CLSCTX_ALL, nullptr,
                    reinterpret_cast<void**>(&client))) &&
                SUCCEEDED(client->GetMixFormat(&format)) && SUCCEEDED(CoCreateGuid(&session)) &&
                SUCCEEDED(client->Initialize(AUDCLNT_SHAREMODE_SHARED, 0, 1000000, 0, format, &session)) &&
                SUCCEEDED(client->GetBufferSize(&bufferFrames)) &&
                SUCCEEDED(client->GetService(IID_IAudioRenderClient, reinterpret_cast<void**>(&renderer))) &&
                refill() && SUCCEEDED(client->Start());
        }
    };

    int check_process_capture_format()
    {
        const auto initialised = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(initialised)) return 77;
        int result = 77;
        {
            // Only this fixture process renders, and every submitted frame is
            // explicitly silent. Never capture another application's audio.
            SilentRenderer source;
            if (source.start())
            {
                std::atomic<unsigned> packets = 0;
                std::atomic<bool> correctRate = true, silent = true;
                AudioLoopbackCaptureOptions options;
                options.mode = AudioLoopbackCaptureMode::eProcessTree;
                options.targetProcessId = GetCurrentProcessId();
                AudioLoopbackCapture capture(AudioLoopbackCaptureCallbacks {
                    [&](std::span<const float> samples, float rate) {
                        if (rate != 48000.0f) correctRate = false;
                        for (const auto sample : samples) if (sample != 0.0f) silent = false;
                        ++packets;
                    }, {}, {}
                }, options);
                if (capture.start())
                {
                    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
                    while (packets < 4 && std::chrono::steady_clock::now() < deadline)
                    {
                        if (!source.refill()) break;
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                    capture.stop();
                    result = expect(packets >= 4, "the fixture process must deliver loopback packets") &&
                        expect(correctRate, "Windows process capture must match the macOS 48 kHz reference") &&
                        expect(silent, "the silent fixture must not capture other processes") ? 0 : 1;
                    std::cout << "Own-process silent capture: " << packets << " packets; 48 kHz="
                        << correctRate << "; silent=" << silent << '\n';
                }
            }
        }
        CoUninitialize();
        return result;
    }
#endif
}

int main(int argc, char** argv)
{
#if defined(_WIN32)
    if (argc > 1 && std::string_view(argv[1]) == "--process-format")
        return check_process_capture_format();
#endif
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
    passed &= expect(processCapture.set_mode(AudioLoopbackCaptureMode::eSystemOutput) &&
        !processCapture.captures_process_tree(), "capture scope must switch to the system mix");
    passed &= expect(!processCapture.set_mode(AudioLoopbackCaptureMode::eSystemOutput),
        "repeating the selected capture scope must remain inert");
    passed &= expect(processCapture.set_mode(AudioLoopbackCaptureMode::eProcessTree) &&
        processCapture.captures_process_tree() && processCapture.target_process_id() == 42u,
        "restoring process capture must retain the selected process");

    AudioLoopbackCapture emptyCapture(AudioLoopbackCaptureCallbacks {});
    passed &= expect(
        !emptyCapture.start(),
        "capture should reject a missing sample consumer");

#if defined(_WIN32) || defined(__APPLE__)
    processCapture.set_target_process_id(0u);
    passed &= expect(
        AudioLoopbackCapture::platform_supported(),
        "Windows and macOS should expose the loopback backend");
    passed &= expect(
        processCapture.start(),
        "a valid platform capture should start");
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

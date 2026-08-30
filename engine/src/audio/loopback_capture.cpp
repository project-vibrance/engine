#include <vibranceUI/audio/loopback_capture.h>
#include <vibranceUI/audio/spectrum.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <initguid.h>
#include <audioclient.h>
#include <mmdeviceapi.h>
#include <mmreg.h>
#include <propidl.h>
#endif

namespace
{
#ifdef _WIN32
    template <typename T>
    void release_com(T*& value)
    {
        if (value)
        {
            value->Release();
            value = nullptr;
        }
    }

    enum AudioClientActivationType : std::uint32_t
    {
        kAudioClientActivationDefault = 0u,
        kAudioClientActivationProcessLoopback = 1u
    };

    enum ProcessLoopbackMode : std::uint32_t
    {
        kProcessLoopbackIncludeTargetProcessTree = 0u,
        kProcessLoopbackExcludeTargetProcessTree = 1u
    };

    // MinGW exposes ActivateAudioInterfaceAsync but not the newer
    // audioclientactivationparams.h declarations from the Windows SDK.
    struct AudioClientProcessLoopbackParams
    {
        DWORD targetProcessId = 0u;
        ProcessLoopbackMode processLoopbackMode =
            kProcessLoopbackIncludeTargetProcessTree;
    };

    struct AudioClientActivationParams
    {
        AudioClientActivationType activationType =
            kAudioClientActivationDefault;
        AudioClientProcessLoopbackParams processLoopbackParams {};
    };

    constexpr wchar_t kVirtualAudioDeviceProcessLoopback[] =
        L"VAD\\Process_Loopback";

    class AudioInterfaceActivationHandler final :
        public IActivateAudioInterfaceCompletionHandler
    {
    public:
        AudioInterfaceActivationHandler()
        {
            completionEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
            CoCreateFreeThreadedMarshaler(
                static_cast<IUnknown*>(this),
                &freeThreadedMarshaler);
        }

        ~AudioInterfaceActivationHandler()
        {
            release_com(audioClient);
            release_com(freeThreadedMarshaler);
            if (completionEvent)
            {
                CloseHandle(completionEvent);
            }
        }

        HRESULT STDMETHODCALLTYPE QueryInterface(
            REFIID interfaceId,
            void** result) override
        {
            if (!result)
            {
                return E_POINTER;
            }
            *result = nullptr;
            if (IsEqualIID(interfaceId, IID_IUnknown) ||
                IsEqualIID(
                    interfaceId,
                    IID_IActivateAudioInterfaceCompletionHandler))
            {
                *result = static_cast<
                    IActivateAudioInterfaceCompletionHandler*>(this);
                AddRef();
                return S_OK;
            }
            if (freeThreadedMarshaler)
            {
                return freeThreadedMarshaler->QueryInterface(
                    interfaceId,
                    result);
            }
            return E_NOINTERFACE;
        }

        ULONG STDMETHODCALLTYPE AddRef() override
        {
            return ++referenceCount;
        }

        ULONG STDMETHODCALLTYPE Release() override
        {
            const ULONG remaining = --referenceCount;
            if (remaining == 0u)
            {
                delete this;
            }
            return remaining;
        }

        HRESULT STDMETHODCALLTYPE ActivateCompleted(
            IActivateAudioInterfaceAsyncOperation* operation) override
        {
            HRESULT activation = E_UNEXPECTED;
            IUnknown* activatedInterface = nullptr;
            HRESULT callResult = operation ? operation->GetActivateResult(
                &activation,
                &activatedInterface) : E_POINTER;
            if (SUCCEEDED(callResult) && SUCCEEDED(activation) &&
                activatedInterface)
            {
                callResult = activatedInterface->QueryInterface(
                    IID_IAudioClient,
                    reinterpret_cast<void**>(&audioClient));
            }
            release_com(activatedInterface);
            activationResult = FAILED(callResult) ? callResult : activation;
            if (completionEvent)
            {
                SetEvent(completionEvent);
            }
            return S_OK;
        }

        HANDLE event_handle() const
        {
            return completionEvent;
        }

        HRESULT result() const
        {
            return activationResult;
        }

        IAudioClient* detach_audio_client()
        {
            IAudioClient* result = audioClient;
            audioClient = nullptr;
            return result;
        }

    private:
        std::atomic<ULONG> referenceCount { 1u };
        IUnknown* freeThreadedMarshaler = nullptr;
        IAudioClient* audioClient = nullptr;
        HANDLE completionEvent = nullptr;
        HRESULT activationResult = E_PENDING;
    };

    HRESULT activate_process_audio_client(
        DWORD processId,
        IAudioClient** audioClient)
    {
        if (!audioClient || processId == 0u)
        {
            return E_INVALIDARG;
        }
        *audioClient = nullptr;
        auto* handler = new (std::nothrow)
            AudioInterfaceActivationHandler();
        if (!handler)
        {
            return E_OUTOFMEMORY;
        }
        if (!handler->event_handle())
        {
            const HRESULT result = HRESULT_FROM_WIN32(GetLastError());
            handler->Release();
            return result;
        }

        AudioClientActivationParams params {};
        params.activationType = kAudioClientActivationProcessLoopback;
        params.processLoopbackParams.targetProcessId = processId;
        params.processLoopbackParams.processLoopbackMode =
            kProcessLoopbackIncludeTargetProcessTree;
        PROPVARIANT activationVariant {};
        activationVariant.vt = VT_BLOB;
        activationVariant.blob.cbSize = sizeof(params);
        activationVariant.blob.pBlobData =
            reinterpret_cast<BYTE*>(&params);

        IActivateAudioInterfaceAsyncOperation* operation = nullptr;
        HRESULT result = ActivateAudioInterfaceAsync(
            kVirtualAudioDeviceProcessLoopback,
            IID_IAudioClient,
            &activationVariant,
            handler,
            &operation);
        if (SUCCEEDED(result))
        {
            const DWORD waitResult = WaitForSingleObject(
                handler->event_handle(),
                5000u);
            if (waitResult == WAIT_OBJECT_0)
            {
                result = handler->result();
                if (SUCCEEDED(result))
                {
                    *audioClient = handler->detach_audio_client();
                }
            }
            else
            {
                result = waitResult == WAIT_TIMEOUT
                    ? HRESULT_FROM_WIN32(ERROR_TIMEOUT)
                    : HRESULT_FROM_WIN32(GetLastError());
            }
        }
        release_com(operation);
        handler->Release();
        return result;
    }

    constexpr GUID kAudioSubtypePcm {
        0x00000001,
        0x0000,
        0x0010,
        { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
    };

    constexpr GUID kAudioSubtypeIeeeFloat {
        0x00000003,
        0x0000,
        0x0010,
        { 0x80, 0x00, 0x00, 0xaa, 0x00, 0x38, 0x9b, 0x71 }
    };

    bool is_float_mix_format(const WAVEFORMATEX& format)
    {
        if (format.wFormatTag == WAVE_FORMAT_IEEE_FLOAT)
        {
            return true;
        }
        if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        {
            return false;
        }
        const auto& extensible =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        return IsEqualGUID(extensible.SubFormat, kAudioSubtypeIeeeFloat);
    }

    bool is_pcm_mix_format(const WAVEFORMATEX& format)
    {
        if (format.wFormatTag == WAVE_FORMAT_PCM)
        {
            return true;
        }
        if (format.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        {
            return false;
        }
        const auto& extensible =
            reinterpret_cast<const WAVEFORMATEXTENSIBLE&>(format);
        return IsEqualGUID(extensible.SubFormat, kAudioSubtypePcm);
    }

    float read_pcm24_sample(const BYTE* bytes)
    {
        std::int32_t value =
            static_cast<std::int32_t>(bytes[0]) |
            (static_cast<std::int32_t>(bytes[1]) << 8) |
            (static_cast<std::int32_t>(bytes[2]) << 16);
        if ((value & 0x00800000) != 0)
        {
            value |= static_cast<std::int32_t>(0xFF000000u);
        }
        return static_cast<float>(value) / 8388608.0f;
    }
#endif
}

struct AudioLoopbackCapture::Impl
{
    Impl(
        AudioLoopbackCaptureCallbacks requestedCallbacks,
        AudioLoopbackCaptureOptions requestedOptions) :
        callbacks(std::move(requestedCallbacks)),
        options(requestedOptions),
        targetProcessId(requestedOptions.targetProcessId)
    {
    }

    ~Impl()
    {
        stop();
    }

    bool start()
    {
        if (!AudioLoopbackCapture::platform_supported() ||
            !callbacks.onSamples)
        {
            return false;
        }
        std::lock_guard lifecycleLock(lifecycleMutex);
        if (captureRequested)
        {
            return true;
        }
        if (captureThread.joinable())
        {
            captureThread.join();
        }
        captureRequested = true;
        try
        {
            captureThread = std::thread(&Impl::capture_loop, this);
        }
        catch (...)
        {
            captureRequested = false;
            return false;
        }
        return true;
    }

    void stop()
    {
        std::lock_guard lifecycleLock(lifecycleMutex);
        captureRequested = false;
        if (captureThread.joinable())
        {
            if (captureThread.get_id() == std::this_thread::get_id())
            {
                return;
            }
            captureThread.join();
        }
    }

    bool publish_samples(
        std::span<const float> samples,
        float sampleRateHz)
    {
        try
        {
            callbacks.onSamples(samples, sampleRateHz);
            return true;
        }
        catch (...)
        {
            captureRequested = false;
            return false;
        }
    }

    void publish_reset() noexcept
    {
        if (!callbacks.onReset)
        {
            return;
        }
        try
        {
            callbacks.onReset();
        }
        catch (...)
        {
        }
    }

    void capture_loop()
    {
#ifdef _WIN32
        HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const bool ownsCom = SUCCEEDED(result);
        if (FAILED(result) && result != RPC_E_CHANGED_MODE)
        {
            publish_reset();
            captureRequested = false;
            return;
        }

        while (captureRequested)
        {
            const bool processOnly = options.mode ==
                AudioLoopbackCaptureMode::eProcessTree;
            const std::uint32_t capturedProcessId = processOnly
                ? targetProcessId.load()
                : 0u;
            if (processOnly && capturedProcessId == 0u)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                continue;
            }

            IMMDeviceEnumerator* enumerator = nullptr;
            IMMDevice* device = nullptr;
            IAudioClient* audioClient = nullptr;
            IAudioCaptureClient* captureClient = nullptr;
            WAVEFORMATEX* allocatedMixFormat = nullptr;
            WAVEFORMATEX processFormat {};
            WAVEFORMATEX* mixFormat = nullptr;
            HANDLE audioReadyEvent = nullptr;
            bool started = false;

            if (processOnly)
            {
                result = activate_process_audio_client(
                    capturedProcessId,
                    &audioClient);
                processFormat.wFormatTag = WAVE_FORMAT_PCM;
                processFormat.nChannels = 2u;
                processFormat.nSamplesPerSec = 44100u;
                processFormat.wBitsPerSample = 16u;
                processFormat.nBlockAlign = static_cast<WORD>(
                    processFormat.nChannels *
                    processFormat.wBitsPerSample / 8u);
                processFormat.nAvgBytesPerSec =
                    processFormat.nSamplesPerSec *
                    processFormat.nBlockAlign;
                mixFormat = &processFormat;
            }
            else
            {
                result = CoCreateInstance(
                    CLSID_MMDeviceEnumerator,
                    nullptr,
                    CLSCTX_ALL,
                    IID_IMMDeviceEnumerator,
                    reinterpret_cast<void**>(&enumerator));
                if (SUCCEEDED(result))
                {
                    result = enumerator->GetDefaultAudioEndpoint(
                        eRender,
                        eConsole,
                        &device);
                }
                if (SUCCEEDED(result))
                {
                    result = device->Activate(
                        IID_IAudioClient,
                        CLSCTX_ALL,
                        nullptr,
                        reinterpret_cast<void**>(&audioClient));
                }
                if (SUCCEEDED(result))
                {
                    result = audioClient->GetMixFormat(&allocatedMixFormat);
                    mixFormat = allocatedMixFormat;
                }
            }

            if (SUCCEEDED(result) && mixFormat)
            {
                audioReadyEvent = CreateEventW(nullptr, FALSE, FALSE, nullptr);
                if (!audioReadyEvent)
                {
                    result = HRESULT_FROM_WIN32(GetLastError());
                }
            }
            if (SUCCEEDED(result))
            {
                DWORD streamFlags = AUDCLNT_STREAMFLAGS_LOOPBACK |
                    AUDCLNT_STREAMFLAGS_EVENTCALLBACK;
                if (processOnly)
                {
                    streamFlags |= AUDCLNT_STREAMFLAGS_AUTOCONVERTPCM |
                        AUDCLNT_STREAMFLAGS_SRC_DEFAULT_QUALITY;
                }
                result = audioClient->Initialize(
                    AUDCLNT_SHAREMODE_SHARED,
                    streamFlags,
                    0,
                    0,
                    mixFormat,
                    nullptr);
            }
            if (SUCCEEDED(result))
            {
                result = audioClient->SetEventHandle(audioReadyEvent);
            }
            if (SUCCEEDED(result))
            {
                result = audioClient->GetService(
                    IID_IAudioCaptureClient,
                    reinterpret_cast<void**>(&captureClient));
            }
            if (SUCCEEDED(result))
            {
                result = audioClient->Start();
                started = SUCCEEDED(result);
            }

            bool captureValid = SUCCEEDED(result);
            std::vector<float> monoSamples;
            const float sampleRateHz = mixFormat
                ? static_cast<float>(std::max<DWORD>(
                    mixFormat->nSamplesPerSec,
                    1u))
                : 44100.0f;
            while (captureRequested && captureValid &&
                (!processOnly ||
                    targetProcessId.load() == capturedProcessId))
            {
                const DWORD waitResult = WaitForSingleObject(
                    audioReadyEvent,
                    100u);
                if (waitResult == WAIT_TIMEOUT)
                {
                    continue;
                }
                if (waitResult != WAIT_OBJECT_0)
                {
                    captureValid = false;
                    break;
                }

                UINT32 packetFrames = 0u;
                HRESULT packetResult = captureClient->GetNextPacketSize(
                    &packetFrames);
                while (captureRequested && SUCCEEDED(packetResult) &&
                    packetFrames > 0u &&
                    (!processOnly ||
                        targetProcessId.load() == capturedProcessId))
                {
                    BYTE* data = nullptr;
                    UINT32 frameCount = 0u;
                    DWORD flags = 0u;
                    if (FAILED(captureClient->GetBuffer(
                            &data,
                            &frameCount,
                            &flags,
                            nullptr,
                            nullptr)))
                    {
                        captureValid = false;
                        break;
                    }

                    const std::uint16_t channels = std::max<std::uint16_t>(
                        mixFormat->nChannels,
                        1u);
                    const bool silent =
                        (flags & AUDCLNT_BUFFERFLAGS_SILENT) != 0u ||
                        data == nullptr;
                    const bool floatMix = is_float_mix_format(*mixFormat);
                    const bool pcmMix = is_pcm_mix_format(*mixFormat);

                    monoSamples.assign(frameCount, 0.0f);
                    for (UINT32 frame = 0u; frame < frameCount; ++frame)
                    {
                        float& mono = monoSamples[frame];
                        if (!silent)
                        {
                            for (std::uint16_t channel = 0u;
                                 channel < channels;
                                 ++channel)
                            {
                                const std::size_t sampleIndex =
                                    static_cast<std::size_t>(frame) *
                                    channels + channel;
                                if (floatMix && mixFormat->wBitsPerSample == 32u)
                                {
                                    mono += reinterpret_cast<const float*>(
                                        data)[sampleIndex];
                                }
                                else if (pcmMix &&
                                    mixFormat->wBitsPerSample == 16u)
                                {
                                    mono += static_cast<float>(
                                        reinterpret_cast<const std::int16_t*>(
                                            data)[sampleIndex]) / 32768.0f;
                                }
                                else if (pcmMix &&
                                    mixFormat->wBitsPerSample == 24u)
                                {
                                    const BYTE* sampleBytes = data +
                                        static_cast<std::size_t>(frame) *
                                            mixFormat->nBlockAlign +
                                        channel * 3u;
                                    mono += read_pcm24_sample(sampleBytes);
                                }
                                else if (pcmMix &&
                                    mixFormat->wBitsPerSample == 32u)
                                {
                                    mono += static_cast<float>(
                                        reinterpret_cast<const std::int32_t*>(
                                            data)[sampleIndex]) /
                                        2147483648.0f;
                                }
                            }
                            mono /= static_cast<float>(channels);
                        }
                    }

                    captureValid = publish_samples(
                        monoSamples,
                        sampleRateHz);
                    captureClient->ReleaseBuffer(frameCount);
                    if (!captureValid)
                    {
                        break;
                    }
                    packetResult = captureClient->GetNextPacketSize(
                        &packetFrames);
                }
                if (FAILED(packetResult))
                {
                    captureValid = false;
                }
            }

            if (started)
            {
                audioClient->Stop();
            }
            if (allocatedMixFormat)
            {
                CoTaskMemFree(allocatedMixFormat);
            }
            release_com(captureClient);
            release_com(audioClient);
            release_com(device);
            release_com(enumerator);
            if (audioReadyEvent)
            {
                CloseHandle(audioReadyEvent);
            }

            // Clear stale output when a source disappears, changes, or fails.
            // System capture may retain its final levels only for an explicit
            // stop, matching useful freeze behavior for generic tools.
            if (captureRequested || processOnly)
            {
                publish_reset();
            }

            if (captureRequested &&
                (!processOnly ||
                    targetProcessId.load() == capturedProcessId))
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            }
        }

        if (ownsCom)
        {
            CoUninitialize();
        }
#endif
    }

    AudioLoopbackCaptureCallbacks callbacks;
    AudioLoopbackCaptureOptions options;
    std::atomic<std::uint32_t> targetProcessId { 0u };
    std::atomic_bool captureRequested { false };
    std::mutex lifecycleMutex;
    std::thread captureThread;
};

AudioLoopbackCapture::AudioLoopbackCapture(
    AudioSpectrumProcessor& processor,
    AudioLoopbackCaptureOptions options) :
    AudioLoopbackCapture(
        AudioLoopbackCaptureCallbacks {
            [&processor](std::span<const float> samples, float sampleRateHz) {
                if (processor.sample_rate() != sampleRateHz)
                {
                    processor.set_sample_rate(sampleRateHz);
                }
                processor.push_samples(samples);
            },
            [&processor]() {
                processor.reset();
            }
        },
        options)
{
}

AudioLoopbackCapture::AudioLoopbackCapture(
    AudioLoopbackCaptureCallbacks callbacks,
    AudioLoopbackCaptureOptions options) :
    impl(std::make_unique<Impl>(std::move(callbacks), options))
{
}

AudioLoopbackCapture::~AudioLoopbackCapture() = default;

bool AudioLoopbackCapture::platform_supported()
{
#ifdef _WIN32
    return true;
#else
    return false;
#endif
}

bool AudioLoopbackCapture::start()
{
    return impl->start();
}

void AudioLoopbackCapture::stop()
{
    impl->stop();
}

bool AudioLoopbackCapture::running() const
{
    return impl->captureRequested.load();
}

bool AudioLoopbackCapture::captures_process_tree() const
{
    return impl->options.mode == AudioLoopbackCaptureMode::eProcessTree;
}

std::uint32_t AudioLoopbackCapture::target_process_id() const
{
    return impl->targetProcessId.load();
}

bool AudioLoopbackCapture::set_target_process_id(std::uint32_t processId)
{
    if (!captures_process_tree())
    {
        return false;
    }
    return impl->targetProcessId.exchange(processId) != processId;
}

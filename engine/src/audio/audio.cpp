#include <vibranceUI/audio/audio.h>
#include <vibranceUI/core/logger.h>
#include <algorithm>
#include <climits>
#include <filesystem>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(VIBRANCE_HAS_SOLOUD)
#include <soloud.h>
#include <soloud_wav.h>
#endif

#if defined(VIBRANCE_HAS_FFMPEG)
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/channel_layout.h>
#include <libavutil/error.h>
#include <libswresample/swresample.h>
}
#endif

namespace
{
    float clamp_volume(float volume)
    {
        return std::clamp(volume, 0.0f, 4.0f);
    }

    float clamp_pan(float pan)
    {
        return std::clamp(pan, -1.0f, 1.0f);
    }

    std::filesystem::path normalise_audio_path(const std::filesystem::path& path)
    {
        // Cache clips by absolute path so repeated loads share one decoded asset
        return std::filesystem::absolute(path).lexically_normal();
    }

#if defined(VIBRANCE_HAS_FFMPEG)
    struct AVFormatContextDeleter
    {
        void operator()(AVFormatContext* context) const
        {
            avformat_close_input(&context);
        }
    };

    struct AVCodecContextDeleter
    {
        void operator()(AVCodecContext* context) const
        {
            avcodec_free_context(&context);
        }
    };

    struct AVFrameDeleter
    {
        void operator()(AVFrame* frame) const
        {
            av_frame_free(&frame);
        }
    };

    struct AVPacketDeleter
    {
        void operator()(AVPacket* packet) const
        {
            av_packet_free(&packet);
        }
    };

    struct SwrContextDeleter
    {
        void operator()(SwrContext* context) const
        {
            swr_free(&context);
        }
    };

    struct AVChannelLayoutScope
    {
        AVChannelLayout value {};

        ~AVChannelLayoutScope()
        {
            av_channel_layout_uninit(&value);
        }
    };

    struct DecodedFfmpegAudio
    {
        std::vector<float> planarSamples;
        uint32_t sampleRate = 0u;
        uint32_t channels = 0u;

        bool valid() const
        {
            return !planarSamples.empty() &&
                sampleRate > 0u &&
                channels > 0u &&
                planarSamples.size() % channels == 0u &&
                planarSamples.size() <= static_cast<std::size_t>(UINT_MAX);
        }
    };

    DecodedFfmpegAudio decode_audio_track_ffmpeg(const std::filesystem::path& path)
    {
        DecodedFfmpegAudio decoded = {};

        AVFormatContext* rawFormatContext = nullptr;
        const std::string pathString = path.string();
        if (avformat_open_input(&rawFormatContext, pathString.c_str(), nullptr, nullptr) < 0)
        {
            return decoded;
        }
        std::unique_ptr<AVFormatContext, AVFormatContextDeleter> formatContext(rawFormatContext);
        if (avformat_find_stream_info(formatContext.get(), nullptr) < 0)
        {
            return decoded;
        }

        const int streamIndex = av_find_best_stream(
            formatContext.get(),
            AVMEDIA_TYPE_AUDIO,
            -1,
            -1,
            nullptr,
            0);
        if (streamIndex < 0)
        {
            return decoded;
        }

        AVStream* stream = formatContext->streams[streamIndex];
        const AVCodec* decoder = avcodec_find_decoder(stream->codecpar->codec_id);
        if (decoder == nullptr)
        {
            return decoded;
        }

        std::unique_ptr<AVCodecContext, AVCodecContextDeleter> codecContext(avcodec_alloc_context3(decoder));
        if (!codecContext ||
            avcodec_parameters_to_context(codecContext.get(), stream->codecpar) < 0 ||
            avcodec_open2(codecContext.get(), decoder, nullptr) < 0 ||
            codecContext->sample_rate <= 0 ||
            codecContext->ch_layout.nb_channels <= 0)
        {
            return decoded;
        }

        decoded.sampleRate = static_cast<uint32_t>(codecContext->sample_rate);
        decoded.channels = codecContext->ch_layout.nb_channels == 1 ? 1u : 2u;

        AVChannelLayoutScope outputLayout;
        av_channel_layout_default(&outputLayout.value, static_cast<int>(decoded.channels));
        SwrContext* rawResampleContext = nullptr;
        if (swr_alloc_set_opts2(
                &rawResampleContext,
                &outputLayout.value,
                AV_SAMPLE_FMT_FLTP,
                codecContext->sample_rate,
                &codecContext->ch_layout,
                codecContext->sample_fmt,
                codecContext->sample_rate,
                0,
                nullptr) < 0 ||
            rawResampleContext == nullptr)
        {
            return {};
        }
        std::unique_ptr<SwrContext, SwrContextDeleter> resampleContext(rawResampleContext);
        if (swr_init(resampleContext.get()) < 0)
        {
            return {};
        }

        std::unique_ptr<AVFrame, AVFrameDeleter> frame(av_frame_alloc());
        std::unique_ptr<AVPacket, AVPacketDeleter> packet(av_packet_alloc());
        if (!frame || !packet)
        {
            return {};
        }

        std::vector<std::vector<float>> channelSamples(decoded.channels);
        auto convert_frame = [&]() -> bool {
            const int outputCapacity = swr_get_out_samples(resampleContext.get(), frame->nb_samples);
            if (outputCapacity <= 0)
            {
                return false;
            }

            std::vector<float> converted(
                static_cast<std::size_t>(outputCapacity) *
                static_cast<std::size_t>(decoded.channels));
            std::vector<uint8_t*> outputData(decoded.channels);
            for (uint32_t channel = 0u; channel < decoded.channels; ++channel)
            {
                outputData[channel] = reinterpret_cast<uint8_t*>(
                    converted.data() +
                    static_cast<std::size_t>(channel) *
                    static_cast<std::size_t>(outputCapacity));
            }

            const uint8_t* const* inputData =
                reinterpret_cast<const uint8_t* const*>(frame->extended_data);
            const int convertedCount = swr_convert(
                resampleContext.get(),
                outputData.data(),
                outputCapacity,
                inputData,
                frame->nb_samples);
            if (convertedCount < 0)
            {
                return false;
            }

            for (uint32_t channel = 0u; channel < decoded.channels; ++channel)
            {
                const float* begin =
                    converted.data() +
                    static_cast<std::size_t>(channel) *
                    static_cast<std::size_t>(outputCapacity);
                channelSamples[channel].insert(
                    channelSamples[channel].end(),
                    begin,
                    begin + convertedCount);
            }
            return true;
        };

        bool decodeSucceeded = true;
        auto receive_frames = [&]() {
            while (decodeSucceeded)
            {
                const int receiveResult = avcodec_receive_frame(codecContext.get(), frame.get());
                if (receiveResult == AVERROR(EAGAIN) || receiveResult == AVERROR_EOF)
                {
                    break;
                }
                if (receiveResult < 0 || !convert_frame())
                {
                    decodeSucceeded = false;
                    break;
                }
            }
        };

        while (decodeSucceeded && av_read_frame(formatContext.get(), packet.get()) >= 0)
        {
            if (packet->stream_index == streamIndex &&
                avcodec_send_packet(codecContext.get(), packet.get()) >= 0)
            {
                receive_frames();
            }
            av_packet_unref(packet.get());
        }
        if (decodeSucceeded && avcodec_send_packet(codecContext.get(), nullptr) >= 0)
        {
            receive_frames();
        }
        if (!decodeSucceeded || channelSamples.empty() || channelSamples.front().empty())
        {
            return {};
        }

        const std::size_t samplesPerChannel = channelSamples.front().size();
        decoded.planarSamples.reserve(samplesPerChannel * decoded.channels);
        for (const std::vector<float>& samples : channelSamples)
        {
            if (samples.size() != samplesPerChannel)
            {
                return {};
            }
            decoded.planarSamples.insert(
                decoded.planarSamples.end(),
                samples.begin(),
                samples.end());
        }
        return decoded;
    }
#endif
}

struct AudioEngine::Impl
{
    explicit Impl(bool enabled);
    ~Impl();

    bool available() const;
    std::string backend_name() const;
    AudioClipHandle load_clip(const std::filesystem::path& path);
    AudioClipHandle find_clip(const std::filesystem::path& path) const;
    void unload_clip(AudioClipHandle clip);
    AudioVoiceHandle play(AudioClipHandle clip, const AudioPlayOptions& options);
    AudioVoiceHandle play(const std::filesystem::path& path, const AudioPlayOptions& options);
    void stop(AudioVoiceHandle voice);
    void stop(AudioClipHandle clip);
    void stop_all();
    bool is_playing(AudioVoiceHandle voice) const;
    void set_paused(AudioVoiceHandle voice, bool paused);
    void set_looping(AudioVoiceHandle voice, bool loop);
    void set_volume(AudioVoiceHandle voice, float volume);
    void set_global_volume(float volume);
    float global_volume() const;
    double clip_length(AudioClipHandle clip) const;
    std::filesystem::path clip_path(AudioClipHandle clip) const;

    Logger* logger = Logger::fetch_logger();
    mutable std::mutex mutex;
    float masterVolume = 1.0f;
    bool enabled = true;

#if defined(VIBRANCE_HAS_SOLOUD)
    struct Clip
    {
        // SoLoud Wav handles WAV, OGG and MP3 decoding behind the same object type
        std::filesystem::path path;
        std::unique_ptr<SoLoud::Wav> wav;
        double lengthSeconds = 0.0;
        std::uintmax_t fileSize = 0u;
        std::filesystem::file_time_type writeTime {};
    };

    mutable SoLoud::Soloud soloud;
    bool initialised = false;
    std::unordered_map<uint32_t, Clip> clips;
    std::unordered_map<std::string, uint32_t> clipIdsByPath;
    uint32_t nextClipId = 1u;
#endif
};

AudioEngine::Impl::Impl(bool enabled) : enabled(enabled)
{
    if (!enabled)
    {
        if (logger)
        {
            logger->info("Audio disabled for this engine instance.");
        }
        return;
    }

#if defined(VIBRANCE_HAS_SOLOUD)
#if defined(__APPLE__)
    constexpr unsigned int backendId = SoLoud::Soloud::COREAUDIO;
#else
    constexpr unsigned int backendId = SoLoud::Soloud::MINIAUDIO;
#endif

    const SoLoud::result result = soloud.init(
        SoLoud::Soloud::CLIP_ROUNDOFF,
        backendId,
        SoLoud::Soloud::AUTO,
        2048u,
        2u);

    if (result != SoLoud::SO_NO_ERROR)
    {
        if (logger)
        {
            logger->error("Audio init failed: " + std::string(soloud.getErrorString(result)));
        }
        return;
    }

    initialised = true;
    soloud.setGlobalVolume(masterVolume);
    if (logger)
    {
        const char* backendName = soloud.getBackendString();
        logger->info(
            "Audio initialised with SoLoud backend: " +
            std::string(backendName ? backendName : "unknown"));
    }
#else
    if (logger)
    {
        logger->warning("Audio disabled: SoLoud was not found at configure time.");
    }
#endif
}

AudioEngine::Impl::~Impl()
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    if (initialised)
    {
        soloud.stopAll();
        clips.clear();
        clipIdsByPath.clear();
        soloud.deinit();
        initialised = false;
    }
#endif
}

bool AudioEngine::Impl::available() const
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    return initialised;
#else
    return false;
#endif
}

std::string AudioEngine::Impl::backend_name() const
{
    if (!enabled)
    {
        return "disabled";
    }

#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    if (!initialised)
    {
        return "unavailable";
    }
    const char* backend = soloud.getBackendString();
    return backend ? backend : "unknown";
#else
    return "disabled";
#endif
}

AudioClipHandle AudioEngine::Impl::load_clip(const std::filesystem::path& path)
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    AudioClipHandle handle = {};
    if (!initialised)
    {
        return handle;
    }

    const std::filesystem::path absolutePath = normalise_audio_path(path);
    const std::string cacheKey = absolutePath.string();
    if (const auto existing = clipIdsByPath.find(cacheKey); existing != clipIdsByPath.end())
    {
        const auto clip = clips.find(existing->second);
        std::error_code error;
        const std::uintmax_t fileSize = std::filesystem::file_size(absolutePath, error);
        const std::uintmax_t safeFileSize = error ? 0u : fileSize;
        error.clear();
        const auto writeTime = std::filesystem::last_write_time(absolutePath, error);
        const auto safeWriteTime = error ? std::filesystem::file_time_type {} : writeTime;
        if (clip != clips.end() &&
            clip->second.fileSize == safeFileSize &&
            clip->second.writeTime == safeWriteTime)
        {
            handle.id = existing->second;
            return handle;
        }
        if (clip != clips.end())
        {
            if (clip->second.wav)
            {
                soloud.stopAudioSource(*clip->second.wav);
            }
            clips.erase(clip);
        }
        clipIdsByPath.erase(existing);
    }

    if (!std::filesystem::exists(absolutePath))
    {
        if (logger)
        {
            logger->warning("Audio clip missing: " + absolutePath.string());
        }
        return handle;
    }

    auto wav = std::make_unique<SoLoud::Wav>();
    SoLoud::result result = wav->load(absolutePath.string().c_str());
    bool loadedWithFfmpeg = false;
#if defined(VIBRANCE_HAS_FFMPEG)
    if (result != SoLoud::SO_NO_ERROR)
    {
        DecodedFfmpegAudio decodedAudio = decode_audio_track_ffmpeg(absolutePath);
        if (decodedAudio.valid())
        {
            result = wav->loadRawWave(
                decodedAudio.planarSamples.data(),
                static_cast<unsigned int>(decodedAudio.planarSamples.size()),
                static_cast<float>(decodedAudio.sampleRate),
                decodedAudio.channels,
                true,
                false);
            loadedWithFfmpeg = result == SoLoud::SO_NO_ERROR;
        }
    }
#endif
    if (result != SoLoud::SO_NO_ERROR)
    {
        if (logger)
        {
            logger->error("Failed to load audio clip " + absolutePath.string() + ": " + soloud.getErrorString(result));
        }
        return handle;
    }

    const uint32_t clipId = nextClipId++;
    handle.id = clipId;
    Clip clip = {};
    clip.path = absolutePath;
    clip.lengthSeconds = wav->getLength();
    clip.wav = std::move(wav);
    std::error_code stampError;
    clip.fileSize = std::filesystem::file_size(absolutePath, stampError);
    if (stampError)
    {
        stampError.clear();
        clip.fileSize = 0u;
    }
    clip.writeTime = std::filesystem::last_write_time(absolutePath, stampError);
    if (stampError)
    {
        clip.writeTime = {};
    }
    clips.emplace(clipId, std::move(clip));
    clipIdsByPath.emplace(cacheKey, clipId);

    if (logger)
    {
        logger->info(
            "Loaded audio clip " +
            absolutePath.filename().string() +
            (loadedWithFfmpeg ? " through FFmpeg" : "") +
            " as handle " +
            std::to_string(clipId) +
            ".");
    }
    return handle;
#else
    (void)path;
    return {};
#endif
}

AudioClipHandle AudioEngine::Impl::find_clip(const std::filesystem::path& path) const
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    AudioClipHandle handle = {};
    const std::filesystem::path absolutePath = normalise_audio_path(path);
    if (const auto existing = clipIdsByPath.find(absolutePath.string()); existing != clipIdsByPath.end())
    {
        handle.id = existing->second;
    }
    return handle;
#else
    (void)path;
    return {};
#endif
}

void AudioEngine::Impl::unload_clip(AudioClipHandle clip)
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = clips.find(clip.id);
    if (it == clips.end())
    {
        return;
    }

    if (initialised && it->second.wav)
    {
        soloud.stopAudioSource(*it->second.wav);
    }
    clipIdsByPath.erase(it->second.path.string());
    clips.erase(it);
#else
    (void)clip;
#endif
}

AudioVoiceHandle AudioEngine::Impl::play(AudioClipHandle clip, const AudioPlayOptions& options)
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    AudioVoiceHandle voice = {};
    if (!initialised)
    {
        return voice;
    }

    const auto it = clips.find(clip.id);
    if (it == clips.end() || !it->second.wav)
    {
        return voice;
    }

    const SoLoud::handle soloudVoice = soloud.play(
        *it->second.wav,
        clamp_volume(options.volume),
        clamp_pan(options.pan),
        options.paused);
    if (soloudVoice == 0u)
    {
        return voice;
    }

    soloud.setLooping(soloudVoice, options.loop);
    voice.id = soloudVoice;
    return voice;
#else
    (void)clip;
    (void)options;
    return {};
#endif
}

AudioVoiceHandle AudioEngine::Impl::play(const std::filesystem::path& path, const AudioPlayOptions& options)
{
    const AudioClipHandle clip = load_clip(path);
    return play(clip, options);
}

void AudioEngine::Impl::stop(AudioVoiceHandle voice)
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    if (initialised && voice.valid())
    {
        soloud.stop(voice.id);
    }
#else
    (void)voice;
#endif
}

void AudioEngine::Impl::stop(AudioClipHandle clip)
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    if (!initialised)
    {
        return;
    }

    const auto it = clips.find(clip.id);
    if (it != clips.end() && it->second.wav)
    {
        soloud.stopAudioSource(*it->second.wav);
    }
#else
    (void)clip;
#endif
}

void AudioEngine::Impl::stop_all()
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    if (initialised)
    {
        soloud.stopAll();
    }
#endif
}

bool AudioEngine::Impl::is_playing(AudioVoiceHandle voice) const
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    return initialised && voice.valid() && soloud.isValidVoiceHandle(voice.id);
#else
    (void)voice;
    return false;
#endif
}

void AudioEngine::Impl::set_paused(AudioVoiceHandle voice, bool paused)
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    if (initialised && voice.valid())
    {
        soloud.setPause(voice.id, paused);
    }
#else
    (void)voice;
    (void)paused;
#endif
}

void AudioEngine::Impl::set_looping(AudioVoiceHandle voice, bool loop)
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    if (initialised && voice.valid())
    {
        soloud.setLooping(voice.id, loop);
    }
#else
    (void)voice;
    (void)loop;
#endif
}

void AudioEngine::Impl::set_volume(AudioVoiceHandle voice, float volume)
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    if (initialised && voice.valid())
    {
        soloud.setVolume(voice.id, clamp_volume(volume));
    }
#else
    (void)voice;
    (void)volume;
#endif
}

void AudioEngine::Impl::set_global_volume(float volume)
{
    std::lock_guard<std::mutex> lock(mutex);
    masterVolume = clamp_volume(volume);
#if defined(VIBRANCE_HAS_SOLOUD)
    if (initialised)
    {
        soloud.setGlobalVolume(masterVolume);
    }
#endif
}

float AudioEngine::Impl::global_volume() const
{
    std::lock_guard<std::mutex> lock(mutex);
    return masterVolume;
}

double AudioEngine::Impl::clip_length(AudioClipHandle clip) const
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = clips.find(clip.id);
    return it != clips.end() ? it->second.lengthSeconds : 0.0;
#else
    (void)clip;
    return 0.0;
#endif
}

std::filesystem::path AudioEngine::Impl::clip_path(AudioClipHandle clip) const
{
#if defined(VIBRANCE_HAS_SOLOUD)
    std::lock_guard<std::mutex> lock(mutex);
    const auto it = clips.find(clip.id);
    return it != clips.end() ? it->second.path : std::filesystem::path {};
#else
    (void)clip;
    return {};
#endif
}

AudioEngine::AudioEngine(bool enabled) : impl(std::make_unique<Impl>(enabled)) {}

AudioEngine::~AudioEngine() = default;

AudioEngine::AudioEngine(AudioEngine&&) noexcept = default;

AudioEngine& AudioEngine::operator=(AudioEngine&&) noexcept = default;

bool AudioEngine::available() const
{
    return impl->available();
}

std::string AudioEngine::backend_name() const
{
    return impl->backend_name();
}

AudioClipHandle AudioEngine::load_clip(const std::filesystem::path& path)
{
    return impl->load_clip(path);
}

AudioClipHandle AudioEngine::find_clip(const std::filesystem::path& path) const
{
    return impl->find_clip(path);
}

void AudioEngine::unload_clip(AudioClipHandle clip)
{
    impl->unload_clip(clip);
}

AudioVoiceHandle AudioEngine::play(AudioClipHandle clip, const AudioPlayOptions& options)
{
    return impl->play(clip, options);
}

AudioVoiceHandle AudioEngine::play(const std::filesystem::path& path, const AudioPlayOptions& options)
{
    return impl->play(path, options);
}

void AudioEngine::stop(AudioVoiceHandle voice)
{
    impl->stop(voice);
}

void AudioEngine::stop(AudioClipHandle clip)
{
    impl->stop(clip);
}

void AudioEngine::stop_all()
{
    impl->stop_all();
}

bool AudioEngine::is_playing(AudioVoiceHandle voice) const
{
    return impl->is_playing(voice);
}

void AudioEngine::set_paused(AudioVoiceHandle voice, bool paused)
{
    impl->set_paused(voice, paused);
}

void AudioEngine::set_looping(AudioVoiceHandle voice, bool loop)
{
    impl->set_looping(voice, loop);
}

void AudioEngine::set_volume(AudioVoiceHandle voice, float volume)
{
    impl->set_volume(voice, volume);
}

void AudioEngine::set_global_volume(float volume)
{
    impl->set_global_volume(volume);
}

float AudioEngine::global_volume() const
{
    return impl->global_volume();
}

double AudioEngine::clip_length(AudioClipHandle clip) const
{
    return impl->clip_length(clip);
}

std::filesystem::path AudioEngine::clip_path(AudioClipHandle clip) const
{
    return impl->clip_path(clip);
}

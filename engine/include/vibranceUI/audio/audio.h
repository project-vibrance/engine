#pragma once
#include <vibranceUI/export.h>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

struct AudioClipHandle
{
    // Stable clip id for cached decoded audio
    uint32_t id = 0;

    bool valid() const
    {
        return id != 0u;
    }

    explicit operator bool() const
    {
        return valid();
    }
};

struct AudioVoiceHandle
{
    // Runtime voice id returned by SoLoud playback
    uint32_t id = 0;

    bool valid() const
    {
        return id != 0u;
    }

    explicit operator bool() const
    {
        return valid();
    }
};

struct AudioPlayOptions
{
    // Per-playback controls, leaving the loaded clip reusable
    float volume = 1.0f;
    float pan = 0.0f;
    bool loop = false;
    bool paused = false;
};

class VIBRANCE_ENGINE_API AudioEngine
{
    public:
    // Small facade around the optional SoLoud implementation
    explicit AudioEngine(bool enabled = true);
    ~AudioEngine();

    AudioEngine(const AudioEngine&) = delete;
    AudioEngine& operator=(const AudioEngine&) = delete;
    AudioEngine(AudioEngine&&) noexcept;
    AudioEngine& operator=(AudioEngine&&) noexcept;

    bool available() const;
    std::string backend_name() const;

    // SoLoud loads ogg, wav, mp3, and flac directly. When static FFmpeg is
    // enabled, other media containers can provide a decoded audio track too.
    AudioClipHandle load_clip(const std::filesystem::path& path);
    AudioClipHandle find_clip(const std::filesystem::path& path) const;
    void unload_clip(AudioClipHandle clip);

    // Playing by path loads or reuses the cached clip before starting a voice
    AudioVoiceHandle play(AudioClipHandle clip, const AudioPlayOptions& options = {});
    AudioVoiceHandle play(const std::filesystem::path& path, const AudioPlayOptions& options = {});

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

    private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

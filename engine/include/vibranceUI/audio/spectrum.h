#pragma once

#include <vibranceUI/export.h>

#include <cstddef>
#include <memory>
#include <span>
#include <vector>

// Ready-to-use defaults for a responsive visual spectrum. Most applications
// can construct AudioSpectrumProcessor without changing any of these values.
struct AudioSpectrumOptions
{
    std::size_t bandCount = 6u;
    float minimumFrequencyHz = 20.0f;
    float maximumFrequencyHz = 6000.0f;
    std::vector<float> bandWeights {
        1.0f,
        0.9f,
        1.15f,
        1.2f,
        1.6f,
        1.8f
    };

    float decibelFloor = -70.0f;
    float decibelRange = 60.0f;
    float quietThreshold = 0.015f;
    float outputGain = 1.0f;
    float attackRate = 60.0f;
    float releaseRate = 20.0f;
    float maximumChangePerSecond = 18.0f;
    std::size_t lowBandCount = 2u;
    float lowBandCompression = 2.2f;
    float highBandCompression = 1.4f;
};

// Source-independent spectrum analysis. Feed mono samples from a microphone,
// decoded file, loopback capture, or any other source; call update once per UI
// frame and read normalised levels in the range [0, 1].
class VIBRANCE_ENGINE_API AudioSpectrumProcessor
{
public:
    static constexpr std::size_t kFftLength = 1024u;

    explicit AudioSpectrumProcessor(
        AudioSpectrumOptions options = {},
        float sampleRateHz = 44100.0f);
    ~AudioSpectrumProcessor();

    AudioSpectrumProcessor(const AudioSpectrumProcessor&) = delete;
    AudioSpectrumProcessor& operator=(const AudioSpectrumProcessor&) = delete;
    AudioSpectrumProcessor(AudioSpectrumProcessor&&) noexcept;
    AudioSpectrumProcessor& operator=(AudioSpectrumProcessor&&) noexcept;

    void set_sample_rate(float sampleRateHz);
    float sample_rate() const;
    std::size_t band_count() const;

    void push_sample(float monoSample);
    void push_samples(std::span<const float> monoSamples);
    void update(double deltaSeconds);

    std::vector<float> levels() const;
    std::size_t copy_levels(std::span<float> destination) const;

    // clear_levels immediately silences published output without disrupting a
    // partially collected FFT block. reset also discards buffered samples.
    void clear_levels();
    void reset();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

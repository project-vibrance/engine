#include <vibranceUI/audio/spectrum.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "audio spectrum test failed: " << message << '\n';
        }
        return condition;
    }

    std::vector<float> sine_wave(
        float frequencyHz,
        float sampleRateHz,
        std::size_t sampleCount)
    {
        std::vector<float> samples(sampleCount);
        for (std::size_t i = 0; i < sampleCount; ++i)
        {
            samples[i] = 0.8f * std::sin(
                2.0f * kPi * frequencyHz *
                static_cast<float>(i) / sampleRateHz);
        }
        return samples;
    }
}

int main()
{
    bool passed = true;

    AudioSpectrumProcessor spectrum;
    passed &= expect(spectrum.band_count() == 6u, "the default should be ready for six-bar UIs");
    passed &= expect(spectrum.sample_rate() == 44100.0f, "the default sample rate should be 44.1 kHz");

    const auto initialLevels = spectrum.levels();
    passed &= expect(
        std::all_of(initialLevels.begin(), initialLevels.end(), [](float value) {
            return value == 0.0f;
        }),
        "a new processor should publish silence");

    const auto bass = sine_wave(
        100.0f,
        spectrum.sample_rate(),
        AudioSpectrumProcessor::kFftLength * 3u);
    spectrum.push_samples(bass);
    spectrum.update(1.0 / 60.0);
    const auto activeLevels = spectrum.levels();
    passed &= expect(
        std::any_of(activeLevels.begin(), activeLevels.end(), [](float value) {
            return value > 0.05f;
        }),
        "a sine wave should produce a visible spectrum");
    passed &= expect(
        std::all_of(activeLevels.begin(), activeLevels.end(), [](float value) {
            return value >= 0.0f && value <= 1.0f;
        }),
        "published levels should stay normalised");

    std::array<float, 6> copiedLevels {};
    passed &= expect(
        spectrum.copy_levels(copiedLevels) == copiedLevels.size(),
        "copy_levels should fill fixed-size UI storage without allocation");
    passed &= expect(copiedLevels == std::array<float, 6> {
        activeLevels[0], activeLevels[1], activeLevels[2],
        activeLevels[3], activeLevels[4], activeLevels[5] },
        "copy_levels should preserve level order");

    spectrum.reset();
    const auto resetLevels = spectrum.levels();
    passed &= expect(
        std::all_of(resetLevels.begin(), resetLevels.end(), [](float value) {
            return value == 0.0f;
        }),
        "reset should discard buffered samples and published levels");

    AudioSpectrumOptions compactOptions;
    compactOptions.bandCount = 3u;
    AudioSpectrumProcessor compactSpectrum(compactOptions, 48000.0f);
    passed &= expect(compactSpectrum.band_count() == 3u, "custom band counts should be supported");
    passed &= expect(compactSpectrum.levels().size() == 3u, "output should match the requested band count");

    return passed ? 0 : 1;
}

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

    // Repeated presentation ticks must not manufacture motion without input.
    // In particular, the second bar's decorative floor must respect the gate.
    for (int tick = 0; tick < 120; ++tick)
    {
        spectrum.update(1.0 / 120.0);
    }
    const auto idleLevels = spectrum.levels();
    passed &= expect(
        std::all_of(idleLevels.begin(), idleLevels.end(), [](float value) {
            return value == 0.0f;
        }),
        "presentation updates without samples must remain silent");

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

    // This mixed, pulsed signal was measured against 07df895's Dynamic Island
    // tuning. Protect the balance of all six bars, rather than only checking
    // that a full-volume sine wave produces some non-zero output.
    AudioSpectrumProcessor tunedSpectrum;
    std::array<double, 6> totals {};
    for (int block = 0; block < 240; ++block)
    {
        for (int sample = 0; sample < 512; ++sample)
        {
            const float time = float(block * 512 + sample) / 44100.0f;
            const float kick = block % 20 < 3 ? 0.32f : 0.025f;
            const float presence = block % 32 < 16 ? 0.18f : 0.055f;
            const float value = kick * std::sin(2.0f * kPi * 80.0f * time)
                + 0.09f * std::sin(2.0f * kPi * 240.0f * time)
                + presence * std::sin(2.0f * kPi * 650.0f * time)
                + 0.12f * std::sin(2.0f * kPi * 1500.0f * time)
                + 0.07f * std::sin(2.0f * kPi * 3200.0f * time)
                + 0.04f * std::sin(2.0f * kPi * 5200.0f * time);
            tunedSpectrum.push_sample(value);
        }
        tunedSpectrum.update(512.0 / 44100.0);
        if (block >= 32)
        {
            const auto levels = tunedSpectrum.levels();
            for (std::size_t band = 0; band < totals.size(); ++band)
                totals[band] += levels[band];
        }
    }
    constexpr std::array<double, 6> reference {
        0.247246010, 0.200440706, 0.168757,
        0.817089, 0.999999940, 0.947458620
    };
    for (std::size_t band = 0; band < totals.size(); ++band)
        passed &= expect(std::abs(totals[band] / 208.0 - reference[band]) < 0.002,
            "the six-bar response must retain 07df895's musical tuning");

    return passed ? 0 : 1;
}

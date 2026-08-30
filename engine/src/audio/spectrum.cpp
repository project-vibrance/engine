#include <vibranceUI/audio/spectrum.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <mutex>
#include <numeric>
#include <utility>

namespace
{
    constexpr float kPi = 3.14159265358979323846f;

    std::uint32_t bit_reverse(std::uint32_t value, std::uint32_t bits)
    {
        std::uint32_t reversed = 0u;
        for (std::uint32_t i = 0u; i < bits; ++i)
        {
            reversed = (reversed << 1u) | (value & 1u);
            value >>= 1u;
        }
        return reversed;
    }

    AudioSpectrumOptions normalise_options(AudioSpectrumOptions options)
    {
        options.bandCount = std::clamp<std::size_t>(
            options.bandCount,
            1u,
            AudioSpectrumProcessor::kFftLength / 2u);
        options.minimumFrequencyHz = std::max(options.minimumFrequencyHz, 1.0f);
        options.maximumFrequencyHz = std::max(
            options.maximumFrequencyHz,
            options.minimumFrequencyHz);
        options.decibelRange = std::max(options.decibelRange, 1.0f);
        options.quietThreshold = std::max(options.quietThreshold, 0.0f);
        options.outputGain = std::max(options.outputGain, 0.0f);
        options.attackRate = std::max(options.attackRate, 0.0f);
        options.releaseRate = std::max(options.releaseRate, 0.0f);
        options.maximumChangePerSecond = std::max(
            options.maximumChangePerSecond,
            0.0f);
        options.lowBandCount = std::min(options.lowBandCount, options.bandCount);
        options.lowBandCompression = std::max(options.lowBandCompression, 0.01f);
        options.highBandCompression = std::max(options.highBandCompression, 0.01f);

        // A custom band count gets neutral weights unless one weight is
        // supplied for every band. This makes configuration mistakes benign.
        if (options.bandWeights.size() != options.bandCount)
        {
            options.bandWeights.assign(options.bandCount, 1.0f);
        }
        for (float& weight : options.bandWeights)
        {
            weight = std::max(weight, 0.0f);
        }
        return options;
    }
}

struct AudioSpectrumProcessor::Impl
{
    static constexpr std::size_t kMagnitudeCount = kFftLength / 2u;

    explicit Impl(AudioSpectrumOptions requestedOptions, float requestedSampleRate) :
        options(normalise_options(std::move(requestedOptions))),
        bandBins(options.bandCount),
        bandBinCounts(options.bandCount, 1u),
        pendingBandSumSquares(options.bandCount, 0.0f),
        bandSumSquares(options.bandCount, 0.0f),
        targetLevels(options.bandCount, 0.0f),
        displayedLevels(options.bandCount, 0.0f)
    {
        initialise_fft_unlocked(requestedSampleRate);
    }

    void initialise_fft_unlocked(float requestedSampleRate)
    {
        sampleRateHz = std::max(requestedSampleRate, 1.0f);
        constexpr std::uint32_t log2Length = 10u;
        for (std::size_t i = 0; i < kFftLength; ++i)
        {
            const float position = static_cast<float>(i) /
                static_cast<float>(kFftLength - 1u);
            window[i] = 0.5f * (1.0f - std::cos(2.0f * kPi * position));
            bitReversal[i] = bit_reverse(static_cast<std::uint32_t>(i), log2Length);
        }

        const double logMinimum = std::log10(
            static_cast<double>(options.minimumFrequencyHz));
        const double logMaximum = std::log10(
            static_cast<double>(options.maximumFrequencyHz));
        const double frequencyRange = logMaximum - logMinimum;

        for (std::size_t band = 0; band < options.bandCount; ++band)
        {
            const double bandStart = logMinimum + static_cast<double>(band) *
                frequencyRange / static_cast<double>(options.bandCount);
            const double bandEnd = logMinimum + static_cast<double>(band + 1u) *
                frequencyRange / static_cast<double>(options.bandCount);
            const float lowFrequency = static_cast<float>(std::pow(10.0, bandStart));
            const float highFrequency = static_cast<float>(std::pow(10.0, bandEnd));
            const auto frequency_to_bin = [this](float frequency) {
                const float exact = frequency / sampleRateHz *
                    static_cast<float>(kFftLength);
                return static_cast<std::uint32_t>(std::clamp(
                    exact,
                    0.0f,
                    static_cast<float>(kMagnitudeCount - 1u)));
            };
            const std::uint32_t lowIndex = frequency_to_bin(lowFrequency);
            const std::uint32_t highIndex = std::max(
                lowIndex,
                frequency_to_bin(highFrequency));

            bandBins[band].clear();
            bandBins[band].reserve(highIndex - lowIndex + 1u);
            for (std::uint32_t bin = lowIndex; bin <= highIndex; ++bin)
            {
                bandBins[band].push_back(bin);
            }
            bandBinCounts[band] = std::max<std::size_t>(bandBins[band].size(), 1u);
        }
    }

    void push_sample_unlocked(float sample)
    {
        sampleBlock[sampleCursor++] = std::clamp(sample, -1.0f, 1.0f);
        if (sampleCursor < kFftLength)
        {
            return;
        }

        sampleCursor = 0u;
        process_block_unlocked();
    }

    void process_block_unlocked()
    {
        for (std::size_t i = 0; i < kFftLength; ++i)
        {
            fftBuffer[i] = { sampleBlock[i] * window[i], 0.0f };
        }

        fft_unlocked();

        const float magnitudeScale = 2.0f / static_cast<float>(kFftLength);
        for (std::size_t i = 0; i < magnitudes.size(); ++i)
        {
            const float real = std::abs(fftBuffer[i].real());
            const float imaginary = std::abs(fftBuffer[i].imag());
            const float magnitude =
                (std::max(real, imaginary) + 0.4f * std::min(real, imaginary)) *
                magnitudeScale;
            magnitudes[i] = magnitude < 1.0e-12f ? 0.0f : magnitude;
        }

        std::fill(
            pendingBandSumSquares.begin(),
            pendingBandSumSquares.end(),
            0.0f);
        for (std::size_t band = 0; band < options.bandCount; ++band)
        {
            for (const std::uint32_t bin : bandBins[band])
            {
                const float value = magnitudes[bin];
                pendingBandSumSquares[band] += value * value;
            }
        }

        std::lock_guard outputLock(outputMutex);
        bandSumSquares = pendingBandSumSquares;
    }

    void fft_unlocked()
    {
        for (std::size_t i = 0; i < kFftLength; ++i)
        {
            const std::size_t reversed = bitReversal[i];
            if (reversed > i)
            {
                std::swap(fftBuffer[i], fftBuffer[reversed]);
            }
        }

        for (std::size_t length = 2u; length <= kFftLength; length <<= 1u)
        {
            const std::size_t half = length >> 1u;
            const float theta = -2.0f * kPi / static_cast<float>(length);
            const std::complex<float> step(std::cos(theta), std::sin(theta));
            for (std::size_t blockStart = 0u;
                 blockStart < kFftLength;
                 blockStart += length)
            {
                std::complex<float> rotation(1.0f, 0.0f);
                for (std::size_t offset = 0u; offset < half; ++offset)
                {
                    const std::complex<float> even = fftBuffer[blockStart + offset];
                    const std::complex<float> odd = rotation *
                        fftBuffer[blockStart + offset + half];
                    fftBuffer[blockStart + offset] = even + odd;
                    fftBuffer[blockStart + offset + half] = even - odd;
                    rotation *= step;
                }
            }
        }
    }

    void clear_levels_unlocked()
    {
        std::fill(bandSumSquares.begin(), bandSumSquares.end(), 0.0f);
        std::fill(targetLevels.begin(), targetLevels.end(), 0.0f);
        std::fill(displayedLevels.begin(), displayedLevels.end(), 0.0f);
    }

    AudioSpectrumOptions options;
    std::array<std::complex<float>, kFftLength> fftBuffer {};
    std::array<float, kFftLength> window {};
    std::array<float, kMagnitudeCount> magnitudes {};
    std::array<float, kFftLength> sampleBlock {};
    std::array<std::uint32_t, kFftLength> bitReversal {};
    std::vector<std::vector<std::uint32_t>> bandBins;
    std::vector<std::size_t> bandBinCounts;
    std::vector<float> pendingBandSumSquares;
    std::vector<float> bandSumSquares;
    std::vector<float> targetLevels;
    std::vector<float> displayedLevels;
    std::size_t sampleCursor = 0u;
    float sampleRateHz = 44100.0f;
    mutable std::mutex inputMutex;
    mutable std::mutex outputMutex;
};

AudioSpectrumProcessor::AudioSpectrumProcessor(
    AudioSpectrumOptions options,
    float sampleRateHz) :
    impl(std::make_unique<Impl>(std::move(options), sampleRateHz))
{
}

AudioSpectrumProcessor::~AudioSpectrumProcessor() = default;
AudioSpectrumProcessor::AudioSpectrumProcessor(AudioSpectrumProcessor&&) noexcept = default;
AudioSpectrumProcessor& AudioSpectrumProcessor::operator=(AudioSpectrumProcessor&&) noexcept = default;

void AudioSpectrumProcessor::set_sample_rate(float sampleRateHz)
{
    std::lock_guard inputLock(impl->inputMutex);
    std::lock_guard outputLock(impl->outputMutex);
    impl->initialise_fft_unlocked(sampleRateHz);
    impl->sampleCursor = 0u;
    impl->sampleBlock.fill(0.0f);
    impl->magnitudes.fill(0.0f);
    std::fill(
        impl->pendingBandSumSquares.begin(),
        impl->pendingBandSumSquares.end(),
        0.0f);
    impl->clear_levels_unlocked();
}

float AudioSpectrumProcessor::sample_rate() const
{
    std::lock_guard lock(impl->inputMutex);
    return impl->sampleRateHz;
}

std::size_t AudioSpectrumProcessor::band_count() const
{
    return impl->options.bandCount;
}

void AudioSpectrumProcessor::push_sample(float monoSample)
{
    std::lock_guard lock(impl->inputMutex);
    impl->push_sample_unlocked(monoSample);
}

void AudioSpectrumProcessor::push_samples(std::span<const float> monoSamples)
{
    std::lock_guard lock(impl->inputMutex);
    for (const float sample : monoSamples)
    {
        impl->push_sample_unlocked(sample);
    }
}

void AudioSpectrumProcessor::update(double deltaSeconds)
{
    const float delta = static_cast<float>(std::clamp(
        deltaSeconds,
        0.0,
        1.0 / 30.0));
    std::lock_guard lock(impl->outputMutex);

    std::vector<float>& targets = impl->targetLevels;
    std::fill(targets.begin(), targets.end(), 0.0f);
    for (std::size_t band = 0; band < impl->options.bandCount; ++band)
    {
        const float binCount = static_cast<float>(impl->bandBinCounts[band]);
        const float rms = std::sqrt(impl->bandSumSquares[band] / binCount);
        const float decibels = 20.0f * std::log10(std::max(rms, 1.0e-7f));
        float normalised = std::clamp(
            (decibels - impl->options.decibelFloor) /
                impl->options.decibelRange,
            0.0f,
            1.0f);
        normalised *= impl->options.bandWeights[band];
        normalised *= impl->options.outputGain;
        normalised = std::pow(
            normalised,
            band < impl->options.lowBandCount
                ? impl->options.lowBandCompression
                : impl->options.highBandCompression);
        targets[band] = std::clamp(normalised, 0.0f, 1.0f);
    }

    const float average = std::accumulate(
        targets.begin(),
        targets.end(),
        0.0f) / static_cast<float>(impl->options.bandCount);
    if (average < impl->options.quietThreshold)
    {
        std::fill(targets.begin(), targets.end(), 0.0f);
    }

    for (std::size_t band = 0; band < impl->options.bandCount; ++band)
    {
        const float current = impl->displayedLevels[band];
        const float target = targets[band];
        const float rate = target > current
            ? impl->options.attackRate
            : impl->options.releaseRate;
        const float alpha = 1.0f - std::exp(-rate * delta);
        const float next = current + (target - current) * alpha;
        const float maximumStep = impl->options.maximumChangePerSecond * delta;
        impl->displayedLevels[band] = current + std::clamp(
            next - current,
            -maximumStep,
            maximumStep);
    }
}

std::vector<float> AudioSpectrumProcessor::levels() const
{
    std::lock_guard lock(impl->outputMutex);
    return impl->displayedLevels;
}

std::size_t AudioSpectrumProcessor::copy_levels(
    std::span<float> destination) const
{
    std::lock_guard lock(impl->outputMutex);
    const std::size_t copied = std::min(
        destination.size(),
        impl->displayedLevels.size());
    std::copy_n(impl->displayedLevels.begin(), copied, destination.begin());
    return copied;
}

void AudioSpectrumProcessor::clear_levels()
{
    std::lock_guard lock(impl->outputMutex);
    impl->clear_levels_unlocked();
}

void AudioSpectrumProcessor::reset()
{
    std::lock_guard inputLock(impl->inputMutex);
    std::lock_guard outputLock(impl->outputMutex);
    impl->sampleCursor = 0u;
    impl->sampleBlock.fill(0.0f);
    impl->magnitudes.fill(0.0f);
    std::fill(
        impl->pendingBandSumSquares.begin(),
        impl->pendingBandSumSquares.end(),
        0.0f);
    impl->clear_levels_unlocked();
}

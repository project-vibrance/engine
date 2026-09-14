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

    constexpr std::size_t kHopLengthDivisor = 4u;

    constexpr float kRmsContribution = 0.76f;
    constexpr float kPeakContribution = 0.24f;
    constexpr float kFirstBarRmsContribution = 0.96f;
    constexpr float kFirstBarPeakContribution = 0.04f;

    constexpr float kNeighbourContribution = 0.10f;
    constexpr float kGlobalContribution = 0.05f;

    constexpr float kTransientSensitivity = 2.35f;
    constexpr float kTransientMaximum = 0.16f;
    constexpr float kTransientDecayRate = 30.0f;

    constexpr float kResponseGamma = 0.94f;

    constexpr float kTargetAttackSmoothingRate = 92.0f;
    constexpr float kTargetReleaseSmoothingRate = 64.0f;
    constexpr float kMinimumAttackSpringRate = 38.0f;
    constexpr float kMinimumReleaseSpringRate = 31.0f;
    constexpr float kFirstBarPumpReleaseSpringBoost = 5.0f;
    constexpr float kVelocityLimitAttackMultiplier = 3.6f;
    constexpr float kVelocityLimitReleaseMultiplier = 3.0f;

    constexpr float kKickEnergyGateStart = 0.035f;
    constexpr float kKickEnergyGateEnd = 0.22f;
    constexpr float kKickRiseStart = 0.012f;
    constexpr float kKickRiseEnd = 0.080f;
    constexpr float kKickRelativeRiseStart = 0.08f;
    constexpr float kKickRelativeRiseEnd = 0.52f;
    constexpr float kKickContrastStart = 0.055f;
    constexpr float kKickContrastEnd = 0.42f;
    constexpr float kKickFluxStart = 0.018f;
    constexpr float kKickFluxEnd = 0.115f;
    constexpr float kLowFrequencyBedAttackRate = 3.2f;
    constexpr float kLowFrequencyBedReleaseRate = 1.45f;
    constexpr float kKickDecayRate = 30.0f;
    constexpr float kKickPumpAmount = 0.18f;
    constexpr float kFirstBarKickPumpAmount = 0.80f;
    constexpr float kFirstBarLowBassPumpBonus = 0.12f;
    constexpr float kLowBassClearanceStart = 0.40f;
    constexpr float kLowBassClearanceEnd = 0.78f;
    constexpr float kKickHighBandFalloff = 0.40f;
    constexpr float kKickActivityTriggerStart = 0.18f;
    constexpr float kKickActivityTriggerEnd = 0.64f;
    constexpr float kKickActivityDecayRate = 1.0f;

    constexpr float kBandGateStart = 0.075f;
    constexpr float kBandGateEnd = 0.34f;
    constexpr float kBandGateFloor = 0.24f;
    constexpr float kFirstBarGateStart = 0.12f;
    constexpr float kFirstBarGateEnd = 0.40f;
    constexpr float kFirstBarGateFloor = 0.015f;
    constexpr float kFirstBarKickPresenceStart = 0.085f;
    constexpr float kFirstBarKickPresenceEnd = 0.30f;
    constexpr float kFirstBarKickFluxStart = 0.030f;
    constexpr float kFirstBarKickFluxEnd = 0.17f;
    constexpr float kFirstBarContaminationStart = 0.055f;
    constexpr float kFirstBarContaminationEnd = 0.34f;
    constexpr float kFirstBarContaminationSuppression = 0.82f;
    constexpr float kFirstBarContaminationRatio = 1.12f;
    constexpr float kFirstBarActiveBassDuck = 0.56f;
    constexpr float kFirstBarHighBassDuckScale = 0.72f;

    constexpr float kSecondBarCurve = 1.85f;
    constexpr float kSecondBarGateStart = 0.030f;
    constexpr float kSecondBarGateEnd = 0.28f;
    constexpr float kSecondBarGateFloor = 0.18f;
    constexpr float kSecondBarLowResponseLift = 0.08f;
    constexpr float kSecondBarLowResponseStart = 0.012f;
    constexpr float kSecondBarLowResponseEnd = 0.20f;
    constexpr float kSecondBarActivityFloor = 0.13f;
    constexpr float kSecondBarActivityFloorBoost = 0.04f;
    constexpr float kSecondBarActivityFloorStart = 0.18f;
    constexpr float kSecondBarActivityFloorEnd = 0.54f;
    constexpr float kSecondBarDotFloor = 0.015f;
    constexpr float kSecondBarDotBlendStart = 0.025f;
    constexpr float kSecondBarDotBlendEnd = 0.13f;
    constexpr float kSecondBarKickIsolationStart = 0.08f;
    constexpr float kSecondBarKickIsolationEnd = 0.40f;
    constexpr float kSecondBarKickLeakageScale = 0.22f;
    constexpr float kSecondBarDriveRiseRate = 94.0f;
    constexpr float kSecondBarDriveReleaseRate = 60.0f;
    constexpr float kSecondBarBedAttackRate = 3.6f;
    constexpr float kSecondBarBedReleaseRate = 1.8f;
    constexpr float kSecondBarFullResponseStart = 0.992f;
    constexpr float kSecondBarFullResponseEnd = 0.9998f;
    constexpr float kSecondBarDominanceStart = 1.85f;
    constexpr float kSecondBarDominanceEnd = 2.55f;
    constexpr float kSecondBarCrestFluxStart = 0.20f;
    constexpr float kSecondBarCrestFluxEnd = 0.62f;
    constexpr float kSecondBarCrestRiseStart = 0.012f;
    constexpr float kSecondBarCrestRiseEnd = 0.060f;
    constexpr float kSecondBarCrestExcessStart = 0.030f;
    constexpr float kSecondBarCrestExcessEnd = 0.16f;
    constexpr float kSecondBarCrestEnergyStart = 0.24f;
    constexpr float kSecondBarCrestEnergyEnd = 0.64f;
    constexpr float kSecondBarCrestDominanceStart = 0.92f;
    constexpr float kSecondBarCrestDominanceEnd = 1.52f;
    constexpr float kSecondBarCrestKickCorrelationStart = 0.020f;
    constexpr float kSecondBarCrestKickCorrelationEnd = 0.105f;
    constexpr float kSecondBarCrestKickSuppression = 0.76f;
    constexpr float kSecondBarCrestTriggerLevel = 0.50f;
    constexpr float kSecondBarCrestRearmFlux = 0.12f;
    constexpr float kSecondBarCrestRearmRise = 0.010f;
    constexpr float kSecondBarCrestDecayRate = 19.0f;
    constexpr float kSecondBarCrestLift = 0.96f;
    constexpr float kSecondBarCrestCooldownSeconds = 0.13f;
    constexpr float kSecondBarIdleTargetAttackRate = 104.0f;
    constexpr float kSecondBarCrestTargetAttackRate = 205.0f;
    constexpr float kSecondBarTargetReleaseRate = 108.0f;
    constexpr float kSecondBarIdleAttackSpringRate = 68.0f;
    constexpr float kSecondBarCrestAttackSpringRate = 112.0f;
    constexpr float kSecondBarReleaseSpringRate = 74.0f;
    constexpr float kSecondBarUpperResistance = 0.34f;
    constexpr float kSecondBarUpperResistanceStart = 0.42f;
    constexpr float kSecondBarUpperResistanceEnd = 0.76f;

    constexpr float kThirdBarCurve = 1.16f;
    constexpr float kFourthBarCurve = 0.72f;
    constexpr float kFifthBarCurve = 0.76f;
    constexpr float kSixthBarCurve = 0.86f;
    constexpr float kThirdBarKickScale = 0.80f;
    constexpr float kFourthBarKickScale = 1.18f;
    constexpr float kThirdBarPresenceScale = 0.92f;
    constexpr float kThirdBarPresenceReleaseStart = 0.82f;
    constexpr float kThirdBarPresenceReleaseEnd = 0.995f;
    constexpr float kFourthBarPresenceBoost = 0.13f;
    constexpr float kFourthBarPresenceBoostStart = 0.10f;
    constexpr float kFourthBarPresenceBoostEnd = 0.72f;
    constexpr float kFifthBarPresenceBoost = 0.11f;
    constexpr float kSixthBarPresenceBoost = 0.055f;

    constexpr float kUpperBandSharedTransientScale = 0.52f;
    constexpr float kUpperBandLocalContrast = 0.42f;
    constexpr float kUpperBandMotionInputScale = 5.0f;
    constexpr float kUpperBandMotionAmount = 0.17f;
    constexpr float kUpperBandMotionAttackRate = 34.0f;
    constexpr float kUpperBandMotionReleaseRate = 22.0f;
    constexpr float kUpperBandKickBaseScale = 0.30f;
    constexpr float kUpperBandKickLocalScale = 0.70f;
    constexpr float kUpperBandLocalOnsetStart = 0.010f;
    constexpr float kUpperBandLocalOnsetEnd = 0.095f;

    float smoothstep(float edge0, float edge1, float value)
    {
        if (edge1 <= edge0)
        {
            return value >= edge1 ? 1.0f : 0.0f;
        }

        const float x = std::clamp(
            (value - edge0) / (edge1 - edge0),
            0.0f,
            1.0f);
        return x * x * (3.0f - 2.0f * x);
    }

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
        options.normalisationPeakTarget = std::isfinite(options.normalisationPeakTarget) ?
            std::clamp(options.normalisationPeakTarget, 0.0f, 1.0f) : 0.0f;

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
    static constexpr std::size_t kHopLength =
        std::max<std::size_t>(kFftLength / kHopLengthDivisor, 1u);

    explicit Impl(AudioSpectrumOptions requestedOptions, float requestedSampleRate) :
        options(normalise_options(std::move(requestedOptions))),
        bandBins(options.bandCount),
        bandBinCounts(options.bandCount, 1u),
        pendingBandSumSquares(options.bandCount, 0.0f),
        pendingBandPeaks(options.bandCount, 0.0f),
        pendingBandPositiveFlux(options.bandCount, 0.0f),
        bandSumSquares(options.bandCount, 0.0f),
        bandPeaks(options.bandCount, 0.0f),
        bandPositiveFlux(options.bandCount, 0.0f),
        rawLevels(options.bandCount, 0.0f),
        previousRawLevels(options.bandCount, 0.0f),
        rawDeltaLevels(options.bandCount, 0.0f),
        bandMotionLevels(options.bandCount, 0.0f),
        shapedLevels(options.bandCount, 0.0f),
        targetLevels(options.bandCount, 0.0f),
        smoothedTargetLevels(options.bandCount, 0.0f),
        displayedLevels(options.bandCount, 0.0f),
        displayVelocities(options.bandCount, 0.0f)
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

        const float rawSample = std::clamp(sample, -1.0f, 1.0f);
        sampleBlock[sampleCursor] = rawSample;
        // Apply the gain belonging to this packet, not the most recent gain
        // to an entire overlapping FFT window. Keep both forms for live toggles.
        normalisedSampleBlock[sampleCursor] = sourceVolume > 0.0f ?
            rawSample / sourceVolume : 0.0f;
        sampleCursor = (sampleCursor + 1u) % kFftLength;

        if (samplesCollected < kFftLength)
        {
            ++samplesCollected;
            if (samplesCollected == kFftLength)
            {
                samplesSinceFft = 0u;
                process_block_unlocked();
            }
            return;
        }

        ++samplesSinceFft;
        if (samplesSinceFft < kHopLength)
        {
            return;
        }

        samplesSinceFft = 0u;
        process_block_unlocked();
    }

    void process_block_unlocked()
    {
        float signalPeak = 0.0f;
        for (std::size_t i = 0; i < kFftLength; ++i)
        {
            const std::size_t sourceIndex = (sampleCursor + i) % kFftLength;
            const float sample = normalisationEnabled ?
                normalisedSampleBlock[sourceIndex] : sampleBlock[sourceIndex];
            fftBuffer[i] = { sample * window[i], 0.0f };
            if (options.normalisationPeakTarget > 0.0f)
                signalPeak = std::max(signalPeak, std::abs(normalisedSampleBlock[sourceIndex]));
        }

        float signalGain = 1.0f;
        if (options.normalisationPeakTarget > 0.0f)
        {
            // Keep one gain reference across beats and quiet passages instead
            // of independently boosting each band/frame and flattening attacks.
            const float release = std::exp(-static_cast<float>(kHopLength) /
                (sampleRateHz * 5.0f));
            normalisationPeak = std::max(signalPeak, normalisationPeak * release);
            if (normalisationEnabled && signalPeak >= 1.0e-5f)
                signalGain = std::clamp(options.normalisationPeakTarget /
                    std::max(normalisationPeak, 1.0e-5f), 1.0f, 1000.0f);
        }

        fft_unlocked();

        const float magnitudeScale = signalGain * 2.0f / static_cast<float>(kFftLength);
        for (std::size_t i = 0; i < magnitudes.size(); ++i)
        {

            const float magnitude = std::abs(fftBuffer[i]) * magnitudeScale;
            magnitudes[i] = magnitude < 1.0e-12f ? 0.0f : magnitude;
        }

        std::fill(
            pendingBandSumSquares.begin(),
            pendingBandSumSquares.end(),
            0.0f);
        std::fill(
            pendingBandPeaks.begin(),
            pendingBandPeaks.end(),
            0.0f);
        std::fill(
            pendingBandPositiveFlux.begin(),
            pendingBandPositiveFlux.end(),
            0.0f);

        for (std::size_t band = 0; band < options.bandCount; ++band)
        {
            float peak = 0.0f;
            float fluxSumSquares = 0.0f;
            for (const std::uint32_t bin : bandBins[band])
            {
                const float value = magnitudes[bin];
                pendingBandSumSquares[band] += value * value;
                peak = std::max(peak, value);

                if (hasPreviousMagnitudeSpectrum)
                {
                    const float difference =
                        std::max(value - previousMagnitudes[bin], 0.0f);
                    fluxSumSquares += difference * difference;
                }
            }
            pendingBandPeaks[band] = peak;

            if (hasPreviousMagnitudeSpectrum)
            {
                const float binCount =
                    static_cast<float>(bandBinCounts[band]);
                const float fluxRms =
                    std::sqrt(fluxSumSquares / binCount);
                const float currentRms = std::sqrt(
                    pendingBandSumSquares[band] / binCount);
                pendingBandPositiveFlux[band] = std::clamp(
                    fluxRms / std::max(currentRms, 1.0e-6f),
                    0.0f,
                    2.0f);
            }
        }

        previousMagnitudes = magnitudes;
        hasPreviousMagnitudeSpectrum = true;

        std::lock_guard outputLock(outputMutex);
        bandSumSquares = pendingBandSumSquares;
        bandPeaks = pendingBandPeaks;
        bandPositiveFlux = pendingBandPositiveFlux;
        ++spectrumRevision;
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
        std::fill(bandPeaks.begin(), bandPeaks.end(), 0.0f);
        std::fill(bandPositiveFlux.begin(), bandPositiveFlux.end(), 0.0f);
        std::fill(rawLevels.begin(), rawLevels.end(), 0.0f);
        std::fill(previousRawLevels.begin(), previousRawLevels.end(), 0.0f);
        std::fill(rawDeltaLevels.begin(), rawDeltaLevels.end(), 0.0f);
        std::fill(bandMotionLevels.begin(), bandMotionLevels.end(), 0.0f);
        std::fill(shapedLevels.begin(), shapedLevels.end(), 0.0f);
        std::fill(targetLevels.begin(), targetLevels.end(), 0.0f);
        std::fill(smoothedTargetLevels.begin(), smoothedTargetLevels.end(), 0.0f);
        std::fill(displayedLevels.begin(), displayedLevels.end(), 0.0f);
        std::fill(displayVelocities.begin(), displayVelocities.end(), 0.0f);
        previousGlobalEnergy = 0.0f;
        transientEnvelope = 0.0f;
        previousKickEnergy = 0.0f;
        lowFrequencyBed = 0.0f;
        kickEnvelope = 0.0f;
        kickActivityEnvelope = 0.0f;
        secondBarDrive = 0.0f;
        secondBarBed = 0.0f;
        secondBarCrestEnvelope = 0.0f;
        secondBarCrestCooldown = 0.0f;
        secondBarCrestArmed = true;
        lastCrestSpectrumRevision = spectrumRevision;
    }

    bool normalisationEnabled = false;
    float sourceVolume = 1.0f;
    float normalisationPeak = 0.0f;
    AudioSpectrumOptions options;
    std::array<std::complex<float>, kFftLength> fftBuffer {};
    std::array<float, kFftLength> window {};
    std::array<float, kMagnitudeCount> magnitudes {};
    std::array<float, kMagnitudeCount> previousMagnitudes {};
    std::array<float, kFftLength> sampleBlock {};
    std::array<float, kFftLength> normalisedSampleBlock {};
    std::array<std::uint32_t, kFftLength> bitReversal {};
    std::vector<std::vector<std::uint32_t>> bandBins;
    std::vector<std::size_t> bandBinCounts;
    std::vector<float> pendingBandSumSquares;
    std::vector<float> pendingBandPeaks;
    std::vector<float> pendingBandPositiveFlux;
    std::vector<float> bandSumSquares;
    std::vector<float> bandPeaks;
    std::vector<float> bandPositiveFlux;
    std::vector<float> rawLevels;
    std::vector<float> previousRawLevels;
    std::vector<float> rawDeltaLevels;
    std::vector<float> bandMotionLevels;
    std::vector<float> shapedLevels;
    std::vector<float> targetLevels;
    std::vector<float> smoothedTargetLevels;
    std::vector<float> displayedLevels;
    std::vector<float> displayVelocities;
    std::size_t sampleCursor = 0u;
    std::size_t samplesCollected = 0u;
    std::size_t samplesSinceFft = 0u;
    float previousGlobalEnergy = 0.0f;
    float transientEnvelope = 0.0f;
    float previousKickEnergy = 0.0f;
    float lowFrequencyBed = 0.0f;
    float kickEnvelope = 0.0f;
    float kickActivityEnvelope = 0.0f;
    float secondBarDrive = 0.0f;
    float secondBarBed = 0.0f;
    float secondBarCrestEnvelope = 0.0f;
    float secondBarCrestCooldown = 0.0f;
    bool secondBarCrestArmed = true;
    bool hasPreviousMagnitudeSpectrum = false;
    std::uint64_t spectrumRevision = 0u;
    std::uint64_t lastCrestSpectrumRevision = 0u;
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

void AudioSpectrumProcessor::set_normalisation_enabled(bool enabled)
{
    std::lock_guard lock(impl->inputMutex);
    if (impl->normalisationEnabled == enabled) return;
    impl->normalisationEnabled = enabled;
    if (enabled) impl->normalisationPeak = 0.0f;
    if (impl->samplesCollected == kFftLength)
    {
        impl->process_block_unlocked();
    }
}

void AudioSpectrumProcessor::set_source_volume(float volume)
{
    std::lock_guard lock(impl->inputMutex);
    impl->sourceVolume = std::isfinite(volume) ? std::clamp(volume, 0.0f, 1.0f) : 1.0f;
}

void AudioSpectrumProcessor::set_sample_rate(float sampleRateHz)
{
    std::lock_guard inputLock(impl->inputMutex);
    std::lock_guard outputLock(impl->outputMutex);
    impl->initialise_fft_unlocked(sampleRateHz);
    impl->sampleCursor = 0u;
    impl->samplesCollected = 0u;
    impl->samplesSinceFft = 0u;
    impl->sampleBlock.fill(0.0f);
    impl->normalisedSampleBlock.fill(0.0f);
    impl->normalisationPeak = 0.0f;
    impl->magnitudes.fill(0.0f);
    impl->previousMagnitudes.fill(0.0f);
    impl->hasPreviousMagnitudeSpectrum = false;
    std::fill(
        impl->pendingBandSumSquares.begin(),
        impl->pendingBandSumSquares.end(),
        0.0f);
    std::fill(
        impl->pendingBandPeaks.begin(),
        impl->pendingBandPeaks.end(),
        0.0f);
    std::fill(
        impl->pendingBandPositiveFlux.begin(),
        impl->pendingBandPositiveFlux.end(),
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

    std::vector<float>& raw = impl->rawLevels;
    std::vector<float>& rawDelta = impl->rawDeltaLevels;
    std::vector<float>& bandMotion = impl->bandMotionLevels;
    std::vector<float>& shaped = impl->shapedLevels;
    std::vector<float>& targets = impl->targetLevels;

    for (std::size_t band = 0; band < impl->options.bandCount; ++band)
    {
        const float binCount = static_cast<float>(impl->bandBinCounts[band]);
        const float rms = std::sqrt(impl->bandSumSquares[band] / binCount);
        const float peak = impl->bandPeaks[band];
        const float visualMagnitude = band == 0u
            ? rms * kFirstBarRmsContribution +
                peak * kFirstBarPeakContribution
            : rms * kRmsContribution + peak * kPeakContribution;

        const float decibels = 20.0f *
            std::log10(std::max(visualMagnitude, 1.0e-7f));

        float normalised = std::clamp(
            (decibels - impl->options.decibelFloor) /
                impl->options.decibelRange,
            0.0f,
            1.0f);

        normalised *= impl->options.bandWeights[band];
        normalised *= impl->options.outputGain;
        normalised = std::pow(
            std::clamp(normalised, 0.0f, 1.0f),
            band < impl->options.lowBandCount
                ? impl->options.lowBandCompression
                : impl->options.highBandCompression);

        raw[band] = std::clamp(normalised, 0.0f, 1.0f);
    }

    if (impl->options.bandCount > 1u)
    {
        const float contamination = std::max(
            raw[1] - raw[0] * kFirstBarContaminationRatio,
            0.0f);
        const float contaminationAmount = smoothstep(
            kFirstBarContaminationStart,
            kFirstBarContaminationEnd,
            contamination);
        raw[0] *= 1.0f -
            kFirstBarContaminationSuppression * contaminationAmount;
    }

    float lowBandSpectralFlux = 0.0f;
    float lowBandFluxWeightSum = 0.0f;

    for (std::size_t band = 0; band < impl->options.bandCount; ++band)
    {
        rawDelta[band] =
            raw[band] - impl->previousRawLevels[band];

        if (band == 0u)
        {
            lowBandSpectralFlux +=
                std::max(rawDelta[band], 0.0f);
            lowBandFluxWeightSum += 1.0f;
        }

        if (band >= 2u)
        {
            const float motionInput = std::clamp(
                rawDelta[band] * kUpperBandMotionInputScale,
                -1.0f,
                1.0f);

            const float motionRate =
                std::abs(motionInput) > std::abs(bandMotion[band])
                    ? kUpperBandMotionAttackRate
                    : kUpperBandMotionReleaseRate;

            const float motionAlpha =
                1.0f - std::exp(-motionRate * delta);

            bandMotion[band] +=
                (motionInput - bandMotion[band]) *
                motionAlpha;
        }

        impl->previousRawLevels[band] = raw[band];
    }

    if (lowBandFluxWeightSum > 0.0f)
    {
        lowBandSpectralFlux /= lowBandFluxWeightSum;
    }

    for (std::size_t band = 0; band < impl->options.bandCount; ++band)
    {
        const float centre = raw[band];
        const float left = band > 0u ? raw[band - 1u] : centre;
        const float right = band + 1u < impl->options.bandCount
            ? raw[band + 1u]
            : centre;

        if (band <= 1u)
        {
            shaped[band] = centre;
        }
        else
        {
            shaped[band] =
                centre * (1.0f - 2.0f * kNeighbourContribution) +
                left * kNeighbourContribution +
                right * kNeighbourContribution;
        }
    }

    float energySquared = 0.0f;
    for (const float value : shaped)
    {
        energySquared += value * value;
    }

    const float globalEnergy = std::sqrt(
        energySquared / static_cast<float>(impl->options.bandCount));

    const float positiveEnergyChange = std::max(
        globalEnergy - impl->previousGlobalEnergy,
        0.0f);
    impl->previousGlobalEnergy = globalEnergy;

    const float transientImpulse = std::min(
        positiveEnergyChange * kTransientSensitivity,
        kTransientMaximum);

    impl->transientEnvelope = std::max(
        transientImpulse,
        impl->transientEnvelope * std::exp(-kTransientDecayRate * delta));

    float kickEnergy = 0.0f;
    float kickWeightSum = 0.0f;

    if (impl->options.bandCount > 0u)
    {
        kickEnergy += shaped[0];
        kickWeightSum += 1.0f;
    }

    if (kickWeightSum > 0.0f)
    {
        kickEnergy /= kickWeightSum;
    }

    const float previousKickEnergy = impl->previousKickEnergy;
    const float kickRise = std::max(
        kickEnergy - previousKickEnergy,
        0.0f);

    const float relativeKickRise = kickRise /
        std::max(previousKickEnergy, 0.055f);

    impl->previousKickEnergy = kickEnergy;

    const float bedRate = kickEnergy > impl->lowFrequencyBed
        ? kLowFrequencyBedAttackRate
        : kLowFrequencyBedReleaseRate;
    const float bedAlpha = 1.0f - std::exp(-bedRate * delta);
    impl->lowFrequencyBed +=
        (kickEnergy - impl->lowFrequencyBed) * bedAlpha;

    const float kickExcess = std::max(
        kickEnergy - impl->lowFrequencyBed,
        0.0f);
    const float kickContrast = kickExcess /
        std::max(impl->lowFrequencyBed, 0.065f);

    const float absoluteKickOnset = smoothstep(
        kKickRiseStart,
        kKickRiseEnd,
        kickRise);

    const float relativeKickOnset = smoothstep(
        kKickRelativeRiseStart,
        kKickRelativeRiseEnd,
        relativeKickRise);

    const float contrastKickOnset = smoothstep(
        kKickContrastStart,
        kKickContrastEnd,
        kickContrast);

    const float fluxKickOnset = smoothstep(
        kKickFluxStart,
        kKickFluxEnd,
        lowBandSpectralFlux);

    const float kickEnergyGate = smoothstep(
        kKickEnergyGateStart,
        kKickEnergyGateEnd,
        kickEnergy);

    const float kickOnset = std::max(
        fluxKickOnset,
        std::max(
            absoluteKickOnset,
            std::max(
                relativeKickOnset * 0.92f,
                contrastKickOnset)));

    const float kickImpulse = kickOnset * kickEnergyGate;

    impl->kickEnvelope = std::max(
        kickImpulse,
        impl->kickEnvelope * std::exp(-kKickDecayRate * delta));

    const float kickActivityTrigger = smoothstep(
        kKickActivityTriggerStart,
        kKickActivityTriggerEnd,
        kickImpulse);

    impl->kickActivityEnvelope = std::max(
        kickActivityTrigger,
        impl->kickActivityEnvelope *
            std::exp(-kKickActivityDecayRate * delta));

    if (impl->options.bandCount > 1u)
    {
        const float kickIsolation = smoothstep(
            kSecondBarKickIsolationStart,
            kSecondBarKickIsolationEnd,
            impl->kickEnvelope);

        const float correlatedKickRise =
            std::max(rawDelta[0], 0.0f) *
            kSecondBarKickLeakageScale *
            kickIsolation;

        const float secondBarInput = std::clamp(
            raw[1] - correlatedKickRise,
            0.0f,
            1.0f);

        const float secondBarDriveRate =
            secondBarInput > impl->secondBarDrive
                ? kSecondBarDriveRiseRate
                : kSecondBarDriveReleaseRate;

        const float secondBarDriveAlpha =
            1.0f - std::exp(-secondBarDriveRate * delta);

        impl->secondBarDrive +=
            (secondBarInput - impl->secondBarDrive) *
            secondBarDriveAlpha;

        const float secondBarBedRate =
            raw[1] > impl->secondBarBed
                ? kSecondBarBedAttackRate
                : kSecondBarBedReleaseRate;

        const float secondBarBedAlpha =
            1.0f - std::exp(-secondBarBedRate * delta);

        impl->secondBarBed +=
            (raw[1] - impl->secondBarBed) *
            secondBarBedAlpha;

        impl->secondBarCrestCooldown = std::max(
            0.0f,
            impl->secondBarCrestCooldown - delta);

        impl->secondBarCrestEnvelope *=
            std::exp(-kSecondBarCrestDecayRate * delta);

        if (impl->lastCrestSpectrumRevision != impl->spectrumRevision)
        {
            impl->lastCrestSpectrumRevision = impl->spectrumRevision;

            const float positiveSecondRise =
                std::max(rawDelta[1], 0.0f);

            const float secondBarFlux =
                impl->bandPositiveFlux[1];

            if (!impl->secondBarCrestArmed &&
                impl->secondBarCrestCooldown <= 0.0f &&
                secondBarFlux <= kSecondBarCrestRearmFlux &&
                positiveSecondRise <= kSecondBarCrestRearmRise)
            {
                impl->secondBarCrestArmed = true;
            }

            if (impl->secondBarCrestArmed)
            {
                float neighbourEnergy = raw[0];
                if (impl->options.bandCount > 2u)
                {
                    neighbourEnergy = 0.5f * (raw[0] + raw[2]);
                }

                const float secondBarExcess =
                    std::max(raw[1] - impl->secondBarBed, 0.0f);

                const float crestFlux = smoothstep(
                    kSecondBarCrestFluxStart,
                    kSecondBarCrestFluxEnd,
                    secondBarFlux);

                const float crestRise = smoothstep(
                    kSecondBarCrestRiseStart,
                    kSecondBarCrestRiseEnd,
                    positiveSecondRise);

                const float crestExcess = smoothstep(
                    kSecondBarCrestExcessStart,
                    kSecondBarCrestExcessEnd,
                    secondBarExcess);

                const float crestEnergy = smoothstep(
                    kSecondBarCrestEnergyStart,
                    kSecondBarCrestEnergyEnd,
                    raw[1]);

                const float crestDominance = smoothstep(
                    kSecondBarCrestDominanceStart,
                    kSecondBarCrestDominanceEnd,
                    raw[1] / std::max(neighbourEnergy, 0.001f));

                const float barOneRise =
                    std::max(rawDelta[0], 0.0f);

                const float kickCorrelation = smoothstep(
                    kSecondBarCrestKickCorrelationStart,
                    kSecondBarCrestKickCorrelationEnd,
                    std::max(
                        barOneRise - positiveSecondRise * 0.70f,
                        0.0f));

                const float crestShape =
                    std::max(crestRise, crestExcess * 0.72f);

                float crestImpulse =
                    crestFlux *
                    (0.52f + 0.48f * crestShape) *
                    (0.56f + 0.44f * crestEnergy) *
                    (0.84f + 0.16f * crestDominance);

                crestImpulse *=
                    1.0f -
                    kSecondBarCrestKickSuppression *
                        kickCorrelation;

                if (crestImpulse >= kSecondBarCrestTriggerLevel)
                {
                    impl->secondBarCrestEnvelope = std::max(
                        impl->secondBarCrestEnvelope,
                        crestImpulse);
                    impl->secondBarCrestCooldown =
                        kSecondBarCrestCooldownSeconds;
                    impl->secondBarCrestArmed = false;
                }
            }
        }
    }
    else
    {
        impl->secondBarDrive = 0.0f;
        impl->secondBarBed = 0.0f;
        impl->secondBarCrestEnvelope = 0.0f;
        impl->secondBarCrestCooldown = 0.0f;
        impl->secondBarCrestArmed = true;
    }

    float gate = 1.0f;
    if (impl->options.quietThreshold > 0.0f)
    {
        gate = smoothstep(
            impl->options.quietThreshold * 0.55f,
            impl->options.quietThreshold * 1.35f,
            globalEnergy);
    }

    for (std::size_t band = 0; band < impl->options.bandCount; ++band)
    {
        const float bandPosition = impl->options.bandCount > 1u
            ? static_cast<float>(band) /
                static_cast<float>(impl->options.bandCount - 1u)
            : 0.0f;

        const float transientWeight = 0.70f - 0.24f * bandPosition;
        const bool isFirstBar = band == 0u;
        const bool isSecondBar = band == 1u;
        const bool isUpperBar = band >= 2u;

        float target = 0.0f;

        if (isSecondBar)
        {
            target = impl->secondBarDrive;
        }
        else if (isFirstBar)
        {
            target = raw[0];
        }
        else
        {
            float globalContribution = kGlobalContribution;
            if (isUpperBar)
            {
                globalContribution *= 0.55f;
            }

            const float transientContribution = isUpperBar
                ? kUpperBandSharedTransientScale
                : 1.0f;

            target =
                shaped[band] * (1.0f - globalContribution) +
                globalEnergy * globalContribution;

            target +=
                impl->transientEnvelope *
                transientWeight *
                transientContribution;
        }

        if (isUpperBar)
        {
            const float left = shaped[band - 1u];
            const float right = band + 1u < impl->options.bandCount
                ? shaped[band + 1u]
                : shaped[band];

            const float neighbourMean =
                0.5f * (left + right);

            const float localContrast =
                shaped[band] - neighbourMean;

            target +=
                localContrast *
                kUpperBandLocalContrast;

            target +=
                bandMotion[band] *
                kUpperBandMotionAmount;
        }

        float localGate = 1.0f;
        float localGateFloor = kBandGateFloor;

        if (isFirstBar)
        {
            localGate = smoothstep(
                kFirstBarGateStart,
                kFirstBarGateEnd,
                raw[band]);
            localGateFloor = kFirstBarGateFloor;
        }
        else if (isSecondBar)
        {
            localGate = smoothstep(
                kSecondBarGateStart,
                kSecondBarGateEnd,
                impl->secondBarDrive);
            localGateFloor = kSecondBarGateFloor;
        }
        else
        {
            localGate = smoothstep(
                kBandGateStart,
                kBandGateEnd,
                raw[band]);
        }

        target *=
            localGateFloor +
            (1.0f - localGateFloor) * localGate;

        if (!isSecondBar)
        {
            target *= gate;
        }

        target = std::pow(
            std::clamp(target, 0.0f, 1.0f),
            kResponseGamma);

        float kickScale =
            1.0f - kKickHighBandFalloff * bandPosition;

        if (isSecondBar)
        {
            kickScale = 0.0f;
        }
        else if (isUpperBar)
        {
            const float localOnset = smoothstep(
                kUpperBandLocalOnsetStart,
                kUpperBandLocalOnsetEnd,
                std::max(rawDelta[band], 0.0f));

            kickScale *=
                kUpperBandKickBaseScale +
                kUpperBandKickLocalScale *
                localOnset;

            if (band == 2u)
            {
                kickScale *= kThirdBarKickScale;
            }
            else if (band == 3u)
            {
                kickScale *= kFourthBarKickScale;
            }
        }

        if (isFirstBar)
        {
            const float lowPresence = smoothstep(
                kFirstBarKickPresenceStart,
                kFirstBarKickPresenceEnd,
                raw[0]);

            const float localKickFlux = smoothstep(
                kFirstBarKickFluxStart,
                kFirstBarKickFluxEnd,
                impl->bandPositiveFlux[0]);

            kickScale *=
                lowPresence *
                localKickFlux;
        }

        const float lowBassClearance =
            1.0f - smoothstep(
                kLowBassClearanceStart,
                kLowBassClearanceEnd,
                impl->lowFrequencyBed);

        float kickPumpAmount = kKickPumpAmount;
        if (isFirstBar)
        {
            kickPumpAmount = std::min(
                1.0f,
                kFirstBarKickPumpAmount +
                    kFirstBarLowBassPumpBonus *
                    lowBassClearance);
        }

        const float kickLift =
            impl->kickEnvelope *
            kickPumpAmount *
            kickScale;

        if (isFirstBar)
        {
            const float betweenKicks =
                1.0f - smoothstep(
                    0.10f,
                    0.62f,
                    impl->kickEnvelope);

            const float duckScale =
                kFirstBarHighBassDuckScale +
                (1.0f - kFirstBarHighBassDuckScale) *
                    lowBassClearance;

            const float bassDuck =
                impl->kickActivityEnvelope *
                betweenKicks *
                kFirstBarActiveBassDuck *
                duckScale;

            target *= 1.0f - bassDuck;
        }

        target += (1.0f - target) * kickLift;

        if (isSecondBar)
        {
            const float fullResponse = smoothstep(
                kSecondBarFullResponseStart,
                kSecondBarFullResponseEnd,
                impl->secondBarDrive);

            float dominance = 0.0f;
            if (impl->options.bandCount > 2u)
            {
                const float neighbourEnergy =
                    0.5f * (raw[0] + raw[2]);
                dominance = smoothstep(
                    kSecondBarDominanceStart,
                    kSecondBarDominanceEnd,
                    impl->secondBarDrive / std::max(neighbourEnergy, 0.001f));
            }
            else if (impl->options.bandCount > 1u)
            {
                dominance = smoothstep(
                    kSecondBarDominanceStart,
                    kSecondBarDominanceEnd,
                    impl->secondBarDrive / std::max(raw[0], 0.001f));
            }

            const float release =
                fullResponse *
                (0.35f + 0.65f * dominance);

            const float curve =
                kSecondBarCurve +
                (1.0f - kSecondBarCurve) *
                    release;

            target = std::pow(
                std::clamp(target, 0.0f, 1.0f),
                curve);

            const float lowResponse = smoothstep(
                kSecondBarLowResponseStart,
                kSecondBarLowResponseEnd,
                impl->secondBarDrive);

            target +=
                (1.0f - target) *
                kSecondBarLowResponseLift *
                lowResponse *
                (1.0f - release);

            const float upperResistance = smoothstep(
                kSecondBarUpperResistanceStart,
                kSecondBarUpperResistanceEnd,
                target);

            target *=
                1.0f -
                kSecondBarUpperResistance *
                upperResistance *
                (1.0f - release);

            const float activityPresence = smoothstep(
                kSecondBarActivityFloorStart,
                kSecondBarActivityFloorEnd,
                globalEnergy);

            const float activityFloor =
                (kSecondBarActivityFloor +
                    kSecondBarActivityFloorBoost *
                    activityPresence) *
                gate;

            const float dotBlend = smoothstep(
                kSecondBarDotBlendStart,
                kSecondBarDotBlendEnd,
                impl->secondBarDrive);

            const float dotFloor = kSecondBarDotFloor * gate;
            const float lowLevelFloor =
                dotFloor +
                (activityFloor - dotFloor) *
                    dotBlend;

            target = std::max(target, lowLevelFloor);
        }
        else if (band == 2u)
        {
            target = std::pow(
                std::clamp(target, 0.0f, 1.0f),
                kThirdBarCurve);

            const float presenceRelease = smoothstep(
                kThirdBarPresenceReleaseStart,
                kThirdBarPresenceReleaseEnd,
                target);

            const float presenceScale =
                kThirdBarPresenceScale +
                (1.0f - kThirdBarPresenceScale) *
                    presenceRelease;

            target *= presenceScale;
        }
        else if (band == 3u)
        {
            target = std::pow(
                std::clamp(target, 0.0f, 1.0f),
                kFourthBarCurve);

            const float presenceBoost = smoothstep(
                kFourthBarPresenceBoostStart,
                kFourthBarPresenceBoostEnd,
                target);

            target +=
                (1.0f - target) *
                kFourthBarPresenceBoost *
                presenceBoost;
        }
        else if (band == 4u)
        {
            target = std::pow(
                std::clamp(target, 0.0f, 1.0f),
                kFifthBarCurve);

            target +=
                (1.0f - target) *
                kFifthBarPresenceBoost *
                smoothstep(0.12f, 0.76f, target);
        }
        else if (band == 5u)
        {
            target = std::pow(
                std::clamp(target, 0.0f, 1.0f),
                kSixthBarCurve);

            target +=
                (1.0f - target) *
                kSixthBarPresenceBoost *
                smoothstep(0.12f, 0.78f, target);
        }

        targets[band] = std::clamp(target, 0.0f, 1.0f);
    }

    if (impl->options.bandCount > 1u)
    {
        const float crestBlend = std::pow(
            smoothstep(
                0.38f,
                0.86f,
                impl->secondBarCrestEnvelope),
            0.88f);

        const float crestLift =
            kSecondBarCrestLift * crestBlend;

        targets[1] +=
            (1.0f - targets[1]) * crestLift;

        targets[1] = std::clamp(targets[1], 0.0f, 1.0f);
    }

    for (std::size_t band = 0; band < impl->options.bandCount; ++band)
    {
        const float rawTarget = targets[band];
        float& smoothedTarget = impl->smoothedTargetLevels[band];

        const bool targetRising = rawTarget > smoothedTarget;
        float targetRate = targetRising
            ? kTargetAttackSmoothingRate
            : kTargetReleaseSmoothingRate;

        if (band == 1u)
        {
            const float crestBlend = smoothstep(
                0.36f,
                0.84f,
                impl->secondBarCrestEnvelope);

            targetRate = targetRising
                ? kSecondBarIdleTargetAttackRate +
                    (kSecondBarCrestTargetAttackRate -
                        kSecondBarIdleTargetAttackRate) *
                        crestBlend
                : kSecondBarTargetReleaseRate;

        }

        if (band == 0u && !targetRising)
        {
            const float betweenKicks =
                1.0f - smoothstep(
                    0.10f,
                    0.62f,
                    impl->kickEnvelope);

            const float pumpRelease =
                impl->kickActivityEnvelope *
                betweenKicks;

            targetRate += 14.0f * pumpRelease;
        }

        const float targetAlpha =
            1.0f - std::exp(-targetRate * delta);

        smoothedTarget +=
            (rawTarget - smoothedTarget) * targetAlpha;

        float& current = impl->displayedLevels[band];
        float& velocity = impl->displayVelocities[band];

        const bool rising = smoothedTarget > current;
        float springRate = rising
            ? std::max(
                impl->options.attackRate,
                kMinimumAttackSpringRate)
            : std::max(
                impl->options.releaseRate,
                kMinimumReleaseSpringRate);

        if (band == 1u)
        {
            const float crestBlend = smoothstep(
                0.36f,
                0.84f,
                impl->secondBarCrestEnvelope);

            springRate = rising
                ? std::max(
                    impl->options.attackRate,
                    kSecondBarIdleAttackSpringRate +
                        (kSecondBarCrestAttackSpringRate -
                            kSecondBarIdleAttackSpringRate) *
                            crestBlend)
                : std::max(
                    impl->options.releaseRate,
                    kSecondBarReleaseSpringRate);

        }

        if (band == 0u && !rising)
        {
            const float betweenKicks =
                1.0f - smoothstep(
                    0.10f,
                    0.62f,
                    impl->kickEnvelope);

            springRate +=
                kFirstBarPumpReleaseSpringBoost *
                impl->kickActivityEnvelope *
                betweenKicks;
        }

        const float displacement = current - smoothedTarget;
        const float c = velocity + springRate * displacement;
        const float decay = std::exp(-springRate * delta);

        const float nextDisplacement =
            (displacement + c * delta) * decay;

        float nextVelocity =
            (velocity - springRate * c * delta) * decay;

        if (impl->options.maximumChangePerSecond > 0.0f)
        {
            float velocityMultiplier = rising
                ? kVelocityLimitAttackMultiplier
                : kVelocityLimitReleaseMultiplier;


            const float maximumVelocity =
                impl->options.maximumChangePerSecond *
                velocityMultiplier;

            if (maximumVelocity > 0.0f)
            {
                nextVelocity =
                    maximumVelocity *
                    std::tanh(nextVelocity / maximumVelocity);
            }
        }

        float next = smoothedTarget + nextDisplacement;

        if (next <= 0.0f)
        {
            next = 0.0f;
            if (nextVelocity < 0.0f)
            {
                nextVelocity = 0.0f;
            }
        }
        else if (next >= 1.0f)
        {
            next = 1.0f;
            if (nextVelocity > 0.0f)
            {
                nextVelocity = 0.0f;
            }
        }

        current = next;
        velocity = nextVelocity;
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
    impl->samplesCollected = 0u;
    impl->samplesSinceFft = 0u;
    impl->sampleBlock.fill(0.0f);
    impl->normalisedSampleBlock.fill(0.0f);
    impl->normalisationPeak = 0.0f;
    impl->magnitudes.fill(0.0f);
    impl->previousMagnitudes.fill(0.0f);
    impl->hasPreviousMagnitudeSpectrum = false;
    std::fill(
        impl->pendingBandSumSquares.begin(),
        impl->pendingBandSumSquares.end(),
        0.0f);
    std::fill(
        impl->pendingBandPeaks.begin(),
        impl->pendingBandPeaks.end(),
        0.0f);
    std::fill(
        impl->pendingBandPositiveFlux.begin(),
        impl->pendingBandPositiveFlux.end(),
        0.0f);
    impl->clear_levels_unlocked();
}

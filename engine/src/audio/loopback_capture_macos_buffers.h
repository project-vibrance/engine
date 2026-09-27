#pragma once
#include <CoreAudio/CoreAudioTypes.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <cstdint>
#include <span>
#include <vector>

namespace mac_audio
{
    // A single native producer and the existing capture worker are the only
    // users. Audio callbacks never allocate, lock, or run the spectrum analyser.
    struct SampleQueue
    {
        static constexpr std::size_t capacity = 65536;
        std::array<float, capacity> samples {};
        std::atomic<std::size_t> written {0}, read {0};
        std::atomic<float> sampleRate {0};
        std::atomic<bool> active {true}, failed {false};

        void push(std::span<const float> input)
        {
            const auto end = written.load(std::memory_order_relaxed);
            const auto begin = read.load(std::memory_order_acquire);
            if (input.size() > capacity - (end - begin)) return; // Drop, never block the audio thread.
            for (std::size_t i = 0; i < input.size(); ++i) samples[(end + i) % capacity] = input[i];
            written.store(end + input.size(), std::memory_order_release);
        }
        std::size_t pop(std::span<float> output)
        {
            const auto begin = read.load(std::memory_order_relaxed);
            const auto count = std::min(output.size(), written.load(std::memory_order_acquire) - begin);
            for (std::size_t i = 0; i < count; ++i) output[i] = samples[(begin + i) % capacity];
            read.store(begin + count, std::memory_order_release);
            return count;
        }
    };

    inline bool supported_format(const AudioStreamBasicDescription& format)
    {
        const bool floating = (format.mFormatFlags & kAudioFormatFlagIsFloat) != 0;
        const bool signedInteger = (format.mFormatFlags & kAudioFormatFlagIsSignedInteger) != 0;
        const bool planar = (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
        return format.mFormatID == kAudioFormatLinearPCM &&
            !(format.mFormatFlags & (kAudioFormatFlagIsBigEndian | kAudioFormatFlagIsAlignedHigh)) &&
            std::isfinite(format.mSampleRate) && format.mSampleRate > 0 &&
            format.mChannelsPerFrame > 0 && format.mChannelsPerFrame <= 32 &&
            ((floating && (format.mBitsPerChannel == 32 || format.mBitsPerChannel == 64)) ||
             (!floating && signedInteger && (format.mBitsPerChannel == 16 || format.mBitsPerChannel == 32))) &&
            format.mBytesPerFrame >= (format.mBitsPerChannel / 8) * (planar ? 1 : format.mChannelsPerFrame);
    }

    inline void consume(SampleQueue& queue, const AudioStreamBasicDescription& format,
        const AudioBufferList* buffers)
    {
        if (!queue.active.load(std::memory_order_relaxed) || !buffers) return;
        if (!supported_format(format)) { queue.failed = true; return; }
        const bool planar = (format.mFormatFlags & kAudioFormatFlagIsNonInterleaved) != 0;
        const auto channels = format.mChannelsPerFrame;
        if (buffers->mNumberBuffers != (planar ? channels : 1)) { queue.failed = true; return; }
        const auto frames = buffers->mBuffers[0].mDataByteSize / format.mBytesPerFrame;
        for (UInt32 i = 0; i < buffers->mNumberBuffers; ++i)
            if (buffers->mBuffers[i].mNumberChannels != (planar ? 1 : channels) ||
                buffers->mBuffers[i].mDataByteSize / format.mBytesPerFrame != frames)
            { queue.failed = true; return; }
        const float rate = static_cast<float>(format.mSampleRate);
        const float oldRate = queue.sampleRate.load(std::memory_order_relaxed);
        if (oldRate != 0 && oldRate != rate) { queue.failed = true; return; }
        queue.sampleRate.store(rate, std::memory_order_relaxed);
        std::array<float, 512> mono;
        for (UInt32 offset = 0; offset < frames;)
        {
            const auto count = std::min<std::size_t>(mono.size(), frames - offset);
            for (std::size_t i = 0; i < count; ++i)
            {
                double sum = 0;
                for (UInt32 channel = 0; channel < channels; ++channel)
                {
                    const auto& buffer = buffers->mBuffers[planar ? channel : 0];
                    if (!buffer.mData) continue;
                    const auto* bytes = static_cast<const unsigned char*>(buffer.mData) +
                        (offset + i) * format.mBytesPerFrame + (planar ? 0 : channel * (format.mBitsPerChannel / 8));
                    double value = 0;
                    if (format.mFormatFlags & kAudioFormatFlagIsFloat)
                    {
                        if (format.mBitsPerChannel == 32) { float v; std::memcpy(&v, bytes, 4); value = v; }
                        else { double v; std::memcpy(&v, bytes, 8); value = v; }
                    }
                    else if (format.mBitsPerChannel == 16)
                    { std::int16_t v; std::memcpy(&v, bytes, 2); value = v / 32768.0; }
                    else { std::int32_t v; std::memcpy(&v, bytes, 4); value = v / 2147483648.0; }
                    if (std::isfinite(value)) sum += value;
                }
                mono[i] = static_cast<float>(std::clamp(sum / channels, -1.0, 1.0));
            }
            queue.push(std::span(mono.data(), count));
            offset += static_cast<UInt32>(count);
        }
    }

    inline std::vector<int> process_tree(int root, const std::vector<std::pair<int, int>>& parents)
    {
        if (root <= 0) return {};
        std::vector<int> result {root};
        for (std::size_t i = 0; i < result.size(); ++i)
            for (auto [pid, parent] : parents)
                if (pid > 0 && parent == result[i] && std::find(result.begin(), result.end(), pid) == result.end())
                    result.push_back(pid);
        std::sort(result.begin(), result.end());
        return result;
    }
}

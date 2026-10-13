#include "../src/audio/loopback_capture_macos_buffers.h"
#include <iostream>
#include <thread>

int main()
{
    int failures = 0;
    const auto expect = [&](bool value, const char* message) {
        if (!value) { std::cerr << message << '\n'; ++failures; }
    };
    auto format = AudioStreamBasicDescription{48000, kAudioFormatLinearPCM,
        kAudioFormatFlagIsFloat | kAudioFormatFlagIsPacked, 8, 1, 8, 2, 32, 0};
    float stereo[] {1, -1, 0.25f, 0.75f, NAN, 0.5f};
    AudioBufferList interleaved {1, {{2, sizeof(stereo), stereo}}};
    mac_audio::SampleQueue queue;
    mac_audio::consume(queue, format, &interleaved);
    std::array<float, 4> output {};
    expect(queue.pop(output) == 3 && output[0] == 0 && output[1] == 0.5f && output[2] == 0.25f,
        "Interleaved stereo must downmix and sanitise non-finite samples");
    expect(queue.sampleRate == 48000 && !queue.failed, "Capture sample rate must reach the worker");

    float left[] {0.25f, -0.75f}, right[] {0.75f, 0.25f};
    struct { AudioBufferList list; AudioBuffer second; } planar {{2, {{1, sizeof(left), left}}}, {1, sizeof(right), right}};
    format.mFormatFlags |= kAudioFormatFlagIsNonInterleaved;
    format.mBytesPerFrame = format.mBytesPerPacket = 4;
    mac_audio::consume(queue, format, &planar.list);
    expect(queue.pop(output) == 2 && output[0] == 0.5f && output[1] == -0.25f,
        "Planar ScreenCaptureKit samples must mix with correct per-buffer strides");
    planar.second.mDataByteSize = 4;
    mac_audio::consume(queue, format, &planar.list);
    expect(queue.failed && queue.pop(output) == 0, "Truncated channel buffers must be rejected before reading");

    mac_audio::SampleQueue integerQueue;
    std::int16_t pcm[] {16384, -16384};
    AudioBufferList integer {1, {{1, sizeof(pcm), pcm}}};
    format = {44100, kAudioFormatLinearPCM, kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
        2, 1, 2, 1, 16, 0};
    mac_audio::consume(integerQueue, format, &integer);
    expect(integerQueue.pop(output) == 2 && output[0] == 0.5f && output[1] == -0.5f,
        "Signed PCM must normalise correctly");
    integerQueue.active = false;
    mac_audio::consume(integerQueue, format, &integer);
    expect(integerQueue.pop(output) == 0, "Late callbacks after stopping must be discarded");
    format.mFormatID = kAudioFormatMPEG4AAC;
    expect(!mac_audio::supported_format(format), "Compressed data must not be interpreted as PCM");

    expect(mac_audio::process_tree(20, {{10,1}, {20,10}, {21,20}, {22,21}, {30,10}, {31,30}}) ==
        std::vector<int>({20,21,22}), "Process capture must include descendants but never parents or siblings");
    expect(mac_audio::process_tree(0, {{10,1}}).empty(), "An unresolved target must not select any processes");

    mac_audio::SampleQueue concurrent;
    constexpr int count = 100000;
    std::thread producer([&] {
        for (int i = 0; i < count; ++i)
        {
            while (concurrent.written.load() - concurrent.read.load() >= concurrent.capacity)
                std::this_thread::yield();
            float sample = static_cast<float>(i);
            concurrent.push(std::span(&sample, 1));
        }
    });
    bool ordered = true;
    for (int expected = 0; expected < count;)
    {
        auto received = concurrent.pop(output);
        for (std::size_t i = 0; i < received; ++i, ++expected)
            ordered &= output[i] == static_cast<float>(expected);
        if (!received) std::this_thread::yield();
    }
    producer.join();
    expect(ordered, "The callback queue must preserve samples across wraparound and concurrent access");
    std::vector<float> oversized(mac_audio::SampleQueue::capacity + 1, 1);
    concurrent.push(oversized);
    expect(concurrent.pop(output) == 0, "Overflow must drop input instead of blocking or corrupting output");
    return failures ? 1 : 0;
}

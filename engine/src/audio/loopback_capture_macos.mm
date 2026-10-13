#include "loopback_capture_macos.h"
#include "loopback_capture_macos_buffers.h"

#import <Foundation/Foundation.h>
#import <CoreAudio/CoreAudio.h>
#import <CoreAudio/AudioHardwareTapping.h>
#import <CoreAudio/CATapDescription.h>
#import <CoreMedia/CoreMedia.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#include <libproc.h>
#include <chrono>
#include <limits>
#include <memory>
#include <thread>

namespace
{
    using Clock = std::chrono::steady_clock;
    using Queue = mac_audio::SampleQueue;

    template<class T>
    bool property(AudioObjectID object, AudioObjectPropertySelector selector, T& value)
    {
        AudioObjectPropertyAddress address {selector, kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        UInt32 size = sizeof(value);
        return AudioObjectGetPropertyData(object, &address, 0, nullptr, &size, &value) == noErr && size == sizeof(value);
    }

    std::vector<int> process_ids(std::uint32_t target)
    {
        if (!target || target > static_cast<std::uint32_t>(std::numeric_limits<pid_t>::max())) return {};
        proc_bsdinfo root {};
        if (proc_pidinfo(static_cast<int>(target), PROC_PIDTBSDINFO, 0, &root, sizeof(root)) != sizeof(root)) return {};
        const int bytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
        if (bytes <= 0) return {};
        std::vector<pid_t> pids(static_cast<std::size_t>(bytes) / sizeof(pid_t) + 64);
        const int filled = proc_listpids(PROC_ALL_PIDS, 0, pids.data(), static_cast<int>(pids.size() * sizeof(pid_t)));
        if (filled <= 0) return {};
        pids.resize(static_cast<std::size_t>(filled) / sizeof(pid_t));
        std::vector<std::pair<int, int>> parents;
        for (pid_t pid : pids)
        {
            proc_bsdinfo info {};
            if (pid > 0 && proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) == sizeof(info))
                parents.emplace_back(pid, static_cast<int>(info.pbi_ppid));
        }
        return mac_audio::process_tree(static_cast<int>(target), parents);
    }

    std::vector<AudioObjectID> process_objects(const std::vector<int>& pids)
    {
        std::vector<AudioObjectID> result;
        const AudioObjectPropertyAddress address {kAudioHardwarePropertyTranslatePIDToProcessObject,
            kAudioObjectPropertyScopeGlobal, kAudioObjectPropertyElementMain};
        for (pid_t pid : pids)
        {
            AudioObjectID object = kAudioObjectUnknown;
            UInt32 size = sizeof(object);
            if (AudioObjectGetPropertyData(kAudioObjectSystemObject, &address, sizeof(pid), &pid,
                &size, &object) == noErr && object != kAudioObjectUnknown) result.push_back(object);
        }
        std::sort(result.begin(), result.end());
        result.erase(std::unique(result.begin(), result.end()), result.end());
        return result;
    }

    // RAII cleans up every partially initialised path, in reverse ownership order.
    struct TapCapture
    {
        AudioObjectID tap = kAudioObjectUnknown;
        AudioObjectID aggregate = kAudioObjectUnknown;
        AudioDeviceIOProcID io = nullptr;
        AudioStreamBasicDescription format {};
        std::shared_ptr<Queue> queue = std::make_shared<Queue>();

        bool start(bool processOnly, const std::vector<AudioObjectID>& objects)
        {
            if (@available(macOS 14.2, *))
            {
                if (processOnly && objects.empty()) return false;
                NSMutableArray<NSNumber*>* processes = [NSMutableArray array];
                for (auto object : objects) [processes addObject:@(object)];
                CATapDescription* description = processOnly ?
                    [[CATapDescription alloc] initStereoMixdownOfProcesses:processes] :
                    [[CATapDescription alloc] initStereoGlobalTapButExcludeProcesses:@[]];
                description.name = @"Vibrance visualiser";
                description.privateTap = YES;
                description.muteBehavior = CATapUnmuted;
                if (AudioHardwareCreateProcessTap(description, &tap) != noErr) return false;
                if (!property(tap, kAudioTapPropertyFormat, format) || !mac_audio::supported_format(format)) return false;
                NSDictionary* settings = @{
                    @kAudioAggregateDeviceNameKey: @"Vibrance visualiser capture",
                    @kAudioAggregateDeviceUIDKey: NSUUID.UUID.UUIDString,
                    @kAudioAggregateDeviceIsPrivateKey: @YES,
                    @kAudioAggregateDeviceTapAutoStartKey: @YES,
                    @kAudioAggregateDeviceTapListKey: @[@{
                        @kAudioSubTapUIDKey: description.UUID.UUIDString,
                        @kAudioSubTapDriftCompensationKey: @YES}]
                };
                if (AudioHardwareCreateAggregateDevice((__bridge CFDictionaryRef)settings, &aggregate) != noErr) return false;
                const auto sink = queue;
                const auto inputFormat = format;
                if (AudioDeviceCreateIOProcIDWithBlock(&io, aggregate, nullptr,
                    ^(const AudioTimeStamp*, const AudioBufferList* input, const AudioTimeStamp*,
                        AudioBufferList*, const AudioTimeStamp*) {
                        mac_audio::consume(*sink, inputFormat, input);
                    }) != noErr) return false;
                return AudioDeviceStart(aggregate, io) == noErr;
            }
            return false;
        }
        ~TapCapture()
        {
            queue->active = false;
            if (aggregate != kAudioObjectUnknown)
            {
                if (io)
                {
                    AudioDeviceStop(aggregate, io);
                    AudioDeviceDestroyIOProcID(aggregate, io);
                }
                AudioHardwareDestroyAggregateDevice(aggregate);
            }
            if (@available(macOS 14.2, *))
                if (tap != kAudioObjectUnknown) AudioHardwareDestroyProcessTap(tap);
        }
    };

    struct Completion
    {
        std::atomic<bool> done {false};
        bool success = false;
        SCShareableContent* content = nil;
    };

    bool wait_for(const std::shared_ptr<Completion>& completion, const std::atomic_bool& requested,
        const std::atomic<std::uint32_t>& target, std::uint32_t pid, bool processOnly)
    {
        const auto deadline = Clock::now() + std::chrono::seconds(4);
        while (!completion->done.load(std::memory_order_acquire))
        {
            if (!requested || (processOnly && target.load() != pid) || Clock::now() >= deadline) return false;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        return completion->success;
    }
}

// Native callbacks own only the queue. They never retain or dereference the
// AudioLoopbackCapture object, including late ScreenCaptureKit completions.
@interface VibranceAudioStreamOutput : NSObject <SCStreamOutput, SCStreamDelegate>
{
@public
    std::shared_ptr<Queue> samples;
}
@end

@implementation VibranceAudioStreamOutput
- (void)stream:(SCStream*)stream didStopWithError:(NSError*)error
{
    samples->failed = true;
}
- (void)stream:(SCStream*)stream didOutputSampleBuffer:(CMSampleBufferRef)sampleBuffer ofType:(SCStreamOutputType)type
{
    if (type != SCStreamOutputTypeAudio || !samples->active || !CMSampleBufferDataIsReady(sampleBuffer)) return;
    const auto description = CMSampleBufferGetFormatDescription(sampleBuffer);
    const auto* format = description ? CMAudioFormatDescriptionGetStreamBasicDescription(description) : nullptr;
    if (!format) { samples->failed = true; return; }
    // Both requested channels fit on the stack, including planar audio.
    struct { AudioBufferList list; AudioBuffer second; } storage {};
    CMBlockBufferRef retained = nullptr;
    const OSStatus status = CMSampleBufferGetAudioBufferListWithRetainedBlockBuffer(sampleBuffer,
        nullptr, &storage.list, sizeof(storage), kCFAllocatorDefault, kCFAllocatorDefault,
        kCMSampleBufferFlag_AudioBufferList_Assure16ByteAlignment, &retained);
    if (status == noErr) mac_audio::consume(*samples, *format, &storage.list);
    else samples->failed = true;
    if (retained) CFRelease(retained);
}
@end

namespace
{
    struct ScreenCapture
    {
        std::shared_ptr<Queue> queue = std::make_shared<Queue>();
        SCStream* stream = nil;
        VibranceAudioStreamOutput* output = nil;
        dispatch_queue_t delivery = dispatch_queue_create("com.vibrance.audio.screen", DISPATCH_QUEUE_SERIAL);

        bool start(bool processOnly, const std::vector<int>& pids,
            const std::atomic_bool& requested, const std::atomic<std::uint32_t>& target, std::uint32_t pid)
        {
            if (@available(macOS 13.0, *))
            {
                if (processOnly && pids.empty()) return false;
                auto content = std::make_shared<Completion>();
                [SCShareableContent getShareableContentExcludingDesktopWindows:YES onScreenWindowsOnly:NO
                    completionHandler:^(SCShareableContent* value, NSError* error) {
                        content->content = value;
                        content->success = value != nil && error == nil;
                        content->done.store(true, std::memory_order_release);
                    }];
                if (!wait_for(content, requested, target, pid, processOnly) || !content->content.displays.count) return false;
                SCContentFilter* filter = nil;
                SCDisplay* display = content->content.displays.firstObject;
                if (processOnly)
                {
                    NSMutableArray<SCRunningApplication*>* included = [NSMutableArray array];
                    for (SCRunningApplication* app in content->content.applications)
                        if (std::binary_search(pids.begin(), pids.end(), app.processID)) [included addObject:app];
                    // Never broaden a missing helper/PID to its parent or the entire desktop.
                    if (!included.count) return false;
                    filter = [[SCContentFilter alloc] initWithDisplay:display includingApplications:included exceptingWindows:@[]];
                }
                else filter = [[SCContentFilter alloc] initWithDisplay:display excludingApplications:@[] exceptingWindows:@[]];
                SCStreamConfiguration* configuration = [SCStreamConfiguration new];
                configuration.capturesAudio = YES;
                configuration.excludesCurrentProcessAudio = YES;
                configuration.sampleRate = 48000;
                configuration.channelCount = 2;
                // Only register an audio output. No screen frames are consumed or stored.
                configuration.width = 2;
                configuration.height = 2;
                configuration.minimumFrameInterval = CMTimeMake(1, 1);
                configuration.showsCursor = NO;
                output = [VibranceAudioStreamOutput new];
                output->samples = queue;
                stream = [[SCStream alloc] initWithFilter:filter configuration:configuration delegate:output];
                if (![stream addStreamOutput:output type:SCStreamOutputTypeAudio sampleHandlerQueue:delivery error:nil]) return false;
                auto started = std::make_shared<Completion>();
                const auto sink = queue;
                SCStream* startingStream = stream;
                [stream startCaptureWithCompletionHandler:^(NSError* error) {
                    started->success = error == nil;
                    // A timed-out/cancelled start may complete after the owner has gone.
                    if (!sink->active) [startingStream stopCaptureWithCompletionHandler:nil];
                    started->done.store(true, std::memory_order_release);
                }];
                return wait_for(started, requested, target, pid, processOnly);
            }
            return false;
        }
        ~ScreenCapture()
        {
            queue->active = false;
            if (stream)
            {
                [stream stopCaptureWithCompletionHandler:nil];
                [stream removeStreamOutput:output type:SCStreamOutputTypeAudio error:nil];
            }
        }
    };
}

bool mac_audio_capture_supported()
{
    if (@available(macOS 13.0, *)) return true;
    return false;
}

void mac_audio_capture_loop(const AudioLoopbackCaptureCallbacks& callbacks,
    const AudioLoopbackCaptureOptions& options, std::atomic<std::uint32_t>& target,
    std::atomic_bool& requested)
{
    const bool processOnly = options.mode == AudioLoopbackCaptureMode::eProcessTree;
    const auto reset = [&] {
        if (callbacks.onReset) { try { callbacks.onReset(); } catch (...) {} }
    };
    reset();
    try
    {
        if (callbacks.onSourceVolume) callbacks.onSourceVolume(1.0f);
        while (requested)
        {
            @autoreleasepool
            {
                const auto pid = target.load();
                if (processOnly && !pid)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
                    continue;
                }
                const auto pids = processOnly ? process_ids(pid) : std::vector<int>{};
                bool useTap = false;
                if (@available(macOS 14.2, *)) useTap = true;
                const auto objects = useTap && processOnly ? process_objects(pids) : std::vector<AudioObjectID>{};
                AudioObjectID outputDevice = kAudioObjectUnknown;
                property(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, outputDevice);
                {
                    TapCapture tap;
                    ScreenCapture screen;
                    const bool started = useTap ? tap.start(processOnly, objects) :
                        screen.start(processOnly, pids, requested, target, pid);
                    const auto queue = useTap ? tap.queue : screen.queue;
                    auto nextDiscovery = Clock::now() + std::chrono::seconds(1);
                    auto lastSamples = Clock::now();
                    bool silent = false;
                    std::array<float, 2048> mono;
                    while (started && requested && !queue->failed && (!processOnly || target.load() == pid))
                    {
                        const auto count = queue->pop(mono);
                        if (count && requested && (!processOnly || target.load() == pid))
                        {
                            callbacks.onSamples(std::span(mono.data(), count), queue->sampleRate.load());
                            lastSamples = Clock::now();
                            silent = false;
                        }
                        else
                        {
                            if (!silent && Clock::now() - lastSamples >= std::chrono::milliseconds(250))
                            { reset(); silent = true; }
                            std::this_thread::sleep_for(std::chrono::milliseconds(10));
                        }
                        if (Clock::now() >= nextDiscovery)
                        {
                            AudioObjectID currentOutput = kAudioObjectUnknown;
                            property(kAudioObjectSystemObject, kAudioHardwarePropertyDefaultOutputDevice, currentOutput);
                            if (currentOutput != outputDevice) break;
                            if (processOnly)
                            {
                                const auto current = process_ids(pid);
                                if (current.empty() || (useTap ? process_objects(current) != objects : current != pids)) break;
                            }
                            if (useTap)
                            {
                                AudioStreamBasicDescription current {};
                                if (!property(tap.tap, kAudioTapPropertyFormat, current) ||
                                    current.mSampleRate != tap.format.mSampleRate ||
                                    current.mFormatFlags != tap.format.mFormatFlags ||
                                    current.mChannelsPerFrame != tap.format.mChannelsPerFrame ||
                                    current.mBytesPerFrame != tap.format.mBytesPerFrame) break;
                            }
                            nextDiscovery = Clock::now() + std::chrono::seconds(1);
                        }
                    }
                } // Stop native callbacks before resetting/retargeting the consumer.
                reset();
                // Back off on denied permission/unavailable sources; remain cancellable.
                for (int i = 0; i < 50 && requested && (!processOnly || target.load() == pid); ++i)
                    std::this_thread::sleep_for(std::chrono::milliseconds(20));
            }
        }
    }
    catch (...) { requested = false; }
    reset();
}

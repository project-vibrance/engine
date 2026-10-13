#pragma once

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include "playback_quality_detection.h"
#include <vibranceUI/media/session.h>
#include <array>
#include <chrono>
#include <libproc.h>
#include <vector>

namespace mac_media_quality
{
inline std::wstring wide(id value)
{
    if (![value isKindOfClass:NSString.class]) return {};
    NSString* text = value;
    std::wstring result;
    for (NSUInteger index = 0; index < text.length; ++index)
    {
        uint32_t character = [text characterAtIndex:index];
        if (character >= 0xd800 && character <= 0xdbff && index + 1 < text.length)
        {
            const uint32_t low = [text characterAtIndex:index + 1];
            if (low >= 0xdc00 && low <= 0xdfff)
            {
                character = 0x10000 + ((character - 0xd800) << 10) + low - 0xdc00;
                ++index;
            }
        }
        result.push_back(static_cast<wchar_t>(character));
    }
    return result;
}

inline media_quality_detection::Role role(id value)
{
    using media_quality_detection::Role;
    if (![value isKindOfClass:NSString.class]) return Role::eExcluded;
    NSString* name = value;
    if ([name isEqualToString:(__bridge NSString*)kAXButtonRole]) return Role::eButton;
    if ([name isEqualToString:(__bridge NSString*)kAXSliderRole]) return Role::eSlider;
    if ([name isEqualToString:(__bridge NSString*)kAXStaticTextRole] ||
        [name isEqualToString:(__bridge NSString*)kAXImageRole]) return Role::eText;
    if ([name isEqualToString:(__bridge NSString*)kAXWindowRole]) return Role::eWindow;
    for (NSString* excluded in @[@"AXList", @"AXRow", @"AXTable", @"AXOutline",
        @"AXMenu", @"AXMenuItem", @"AXMenuBar", @"AXCheckBox", @"AXRadioButton",
        @"AXPopUpButton", @"AXComboBox", @"AXScrollArea"])
        if ([name isEqualToString:excluded]) return Role::eExcluded;
    return Role::eContainer;
}

// Resolve an audio helper to its owning UI without changing the capture PID.
// A supplied PID must be live; a bundle-only source must resolve unambiguously.
inline uint32_t ui_process(const MediaSessionSnapshot& snapshot)
{
    NSString* bundle = [NSString stringWithUTF8String:snapshot.sourceApp.c_str()];
    if (!snapshot.sourceProcessId)
    {
        if (!bundle.length) return 0;
        NSArray<NSRunningApplication*>* applications =
            [NSRunningApplication runningApplicationsWithBundleIdentifier:bundle];
        return applications.count == 1 && !applications.firstObject.terminated &&
            applications.firstObject.activationPolicy != NSApplicationActivationPolicyProhibited ?
            applications.firstObject.processIdentifier : 0;
    }
    pid_t pid = static_cast<pid_t>(snapshot.sourceProcessId);
    for (unsigned depth = 0; pid > 1 && depth < 16; ++depth)
    {
        proc_bsdinfo info {};
        if (proc_pidinfo(pid, PROC_PIDTBSDINFO, 0, &info, sizeof(info)) != sizeof(info))
            return 0;
        NSRunningApplication* application = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
        if (application && !application.terminated &&
            application.activationPolicy != NSApplicationActivationPolicyProhibited &&
            (!bundle.length || [application.bundleIdentifier isEqualToString:bundle]))
            return static_cast<uint32_t>(pid);
        // With no application identity, never guess that an ancestor owns media.
        if (!bundle.length || info.pbi_ppid == static_cast<uint32_t>(pid)) break;
        pid = static_cast<pid_t>(info.pbi_ppid);
    }
    return 0;
}

inline bool absent_attribute(id value)
{
    if (value == NSNull.null) return true;
    CFTypeRef object = (__bridge CFTypeRef)value;
    if (!object || CFGetTypeID(object) != AXValueGetTypeID() ||
        AXValueGetType((AXValueRef)object) != kAXValueAXErrorType) return false;
    AXError error = kAXErrorFailure;
    return AXValueGetValue((AXValueRef)object, static_cast<AXValueType>(kAXValueAXErrorType), &error) &&
        (error == kAXErrorAttributeUnsupported || error == kAXErrorNoValue);
}

// One native request per element. Preserve optional unsupported attributes, but
// reject failed reads instead of treating a partial tree as playback evidence.
inline bool read_node(NSArray* attributes, int parent, media_quality_detection::Node& node)
{
    if (attributes.count != 8 || ![attributes[0] isKindOfClass:NSString.class]) return false;
    for (id value in attributes)
    {
        CFTypeRef object = (__bridge CFTypeRef)value;
        if (CFGetTypeID(object) == AXValueGetTypeID() && !absent_attribute(value)) return false;
    }
    node = {};
    node.parent = parent;
    node.role = role(attributes[0]);
    node.visible = !([attributes[1] isKindOfClass:NSNumber.class] && [attributes[1] boolValue]) &&
        !([attributes[2] isKindOfClass:NSNumber.class] && [attributes[2] boolValue]);
    // Text fields expose their content in AXValue; buttons/images often expose
    // their spoken label in AXDescription and a numeric AXValue (e.g. zero).
    const bool spokenLabel = node.role == media_quality_detection::Role::eButton ||
        [attributes[0] isEqualToString:(__bridge NSString*)kAXImageRole];
    const auto order = spokenLabel ? std::array{ 3u, 5u, 4u } : std::array{ 4u, 3u, 5u };
    for (unsigned index : order)
        if ([attributes[index] isKindOfClass:NSString.class] && [attributes[index] length])
        {
            node.name = wide(attributes[index]);
            break;
        }
    node.automationId = wide(attributes[6]);
    return true;
}

inline MediaPlaybackQuality detect(std::span<media_quality_detection::Node> nodes,
    std::wstring_view title, std::wstring_view artist, std::wstring_view album)
{
    const auto artistName = media_quality_detection::normalise(artist);
    const auto titleName = media_quality_detection::normalise(title);
    const auto playingPrefix = L"now playing: " + titleName + L" by ";
    const auto playingLabel = playingPrefix + artistName;
    // Web players can split artist links while exposing the complete title and
    // artist in the enclosing playback group's spoken label (e.g. Spotify).
    if (!title.empty() && !artist.empty())
        for (std::size_t scope = 0; scope < nodes.size(); ++scope)
        {
            auto& node = nodes[scope];
            if (node.role == media_quality_detection::Role::eContainer)
            {
                const auto label = media_quality_detection::normalise(node.name);
                if (!label.starts_with(L"now playing: ")) continue;
                bool artistMatches = label == playingLabel;
                // Spotify's MediaRemote artist can contain only the primary
                // credit. Require a complete artist label inside this exact
                // track's playback group, rather than matching a substring.
                if (!artistMatches && label.starts_with(playingPrefix))
                    for (std::size_t child = 0; child < nodes.size(); ++child)
                        if (media_quality_detection::belongs_to(nodes, static_cast<int>(child), static_cast<int>(scope)) &&
                            media_quality_detection::normalise(nodes[child].name) == artistName)
                        {
                            artistMatches = true;
                            break;
                        }
                if (artistMatches) node.name = artistName;
                else node.role = media_quality_detection::Role::eExcluded;
            }
        }
    // Music's Mini Player exposes "Artist — Album" as one subtitle button.
    // Match both complete metadata fields; never accept an artist substring.
    if (!artist.empty() && !album.empty())
    {
        const auto albumName = media_quality_detection::normalise(album);
        for (auto& node : nodes)
        {
            if (node.role != media_quality_detection::Role::eText &&
                node.role != media_quality_detection::Role::eButton) continue;
            const auto label = media_quality_detection::normalise(node.name);
            for (const auto separator : { L" — ", L" – ", L" - " })
                if (label == artistName + separator + albumName)
                {
                    node.name = artistName;
                    break;
                }
        }
    }
    return media_quality_detection::detect<true>(nodes, title, artist, album);
}

inline bool prepare_application(AXUIElementRef element, uint32_t pid)
{
    NSRunningApplication* application = [NSRunningApplication runningApplicationWithProcessIdentifier:pid];
    NSString* frameworks = [application.bundleURL.path stringByAppendingPathComponent:@"Contents/Frameworks"];
    if (!frameworks) return true;
    NSFileManager* files = NSFileManager.defaultManager;
    const bool electron = [files fileExistsAtPath:[frameworks stringByAppendingPathComponent:@"Electron Framework.framework"]];
    const bool cef = [files fileExistsAtPath:[frameworks stringByAppendingPathComponent:@"Chromium Embedded Framework.framework"]];
    if (!electron && !cef) return true;
    // CEF's Spotify adapter initialises the key window only. Wait for the user
    // to foreground it, without changing focus or switching VoiceOver globally.
    if (!application.active) return false;
    if (AXUIElementSetMessagingTimeout(element, 0.1f) != kAXErrorSuccess) return false;
    // Spotify enables CEF, then delegates to AppKit, which can still return
    // kAXErrorNotImplemented. Actual playback labels remain the evidence.
    const AXError error = AXUIElementSetAttributeValue(element,
        electron ? CFSTR("AXManualAccessibility") : CFSTR("AXEnhancedUserInterface"), kCFBooleanTrue);
    return error == kAXErrorSuccess || error == kAXErrorAttributeUnsupported ||
        (cef && error == kAXErrorNotImplemented);
}

}

// Run only on the media worker. Request Accessibility once for a live player,
// never on an idle launch or from a command-line host. Captured PCM is not
// evidence of the track's encoding.
class MacPlaybackQualityProbe
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point sampledAt {};
    uint32_t processId = 0;
    uint32_t sourceProcessId = 0;
    uint32_t preparedProcessId = 0;
    bool requestedAccessibility = false;
    std::string sourceApp, title, artist, album;
    int64_t durationMilliseconds = 0;
    MediaPlaybackQuality quality = MediaPlaybackQuality::eUnknown;

    static bool flatten(AXUIElementRef element, int parent, unsigned depth,
        std::vector<media_quality_detection::Node>& nodes, Clock::time_point deadline)
    {
        const auto now = Clock::now();
        if (depth > 40 || nodes.size() >= 1024 || now >= deadline) return false;
        // A player's first batch can take more than 20 ms, even when subsequent
        // reads are fast. Keep each call inside the remaining traversal budget.
        const float timeout = std::min(0.1f, std::chrono::duration<float>(deadline - now).count());
        if (AXUIElementSetMessagingTimeout(element, timeout) != kAXErrorSuccess) return false;
        static NSArray* const names = @[(__bridge NSString*)kAXRoleAttribute, @"AXHidden",
            (__bridge NSString*)kAXMinimizedAttribute, (__bridge NSString*)kAXTitleAttribute,
            (__bridge NSString*)kAXValueAttribute, (__bridge NSString*)kAXDescriptionAttribute,
            (__bridge NSString*)kAXIdentifierAttribute, (__bridge NSString*)kAXChildrenAttribute];
        CFArrayRef values = nullptr;
        const AXError error = AXUIElementCopyMultipleAttributeValues(element,
            (__bridge CFArrayRef)names, 0, &values);
        NSArray* attributes = CFBridgingRelease(values);
        if (error != kAXErrorSuccess) return false;
        media_quality_detection::Node node;
        if (!mac_media_quality::read_node(attributes, parent, node)) return false;
        const int index = static_cast<int>(nodes.size());
        nodes.push_back(std::move(node));
        if (!nodes.back().visible || nodes.back().role == media_quality_detection::Role::eExcluded)
            return true;
        id children = attributes[7];
        if (![children isKindOfClass:NSArray.class])
            return mac_media_quality::absent_attribute(children) && Clock::now() < deadline;
        for (id child in children)
        {
            CFTypeRef value = (__bridge CFTypeRef)child;
            if (CFGetTypeID(value) != AXUIElementGetTypeID() ||
                !flatten((AXUIElementRef)value, index, depth + 1, nodes, deadline)) return false;
        }
        return Clock::now() < deadline;
    }

public:
    MediaPlaybackQuality refresh(const MediaSessionSnapshot& snapshot)
    {
        if (!snapshot.sessionAvailable || snapshot.title.empty() ||
            snapshot.artist.empty())
        {
            quality = MediaPlaybackQuality::eUnknown;
            sampledAt = {};
            return quality;
        }
        const uint32_t pid = mac_media_quality::ui_process(snapshot);
        if (!pid)
        {
            quality = MediaPlaybackQuality::eUnknown;
            sampledAt = {};
            return quality;
        }
        if (!AXIsProcessTrusted())
        {
            // Audio capture and Accessibility have independent grants. Local
            // rebuilds can invalidate the latter even while Settings lists
            // the previous build. The native prompt identifies this process.
            if (!requestedAccessibility && NSBundle.mainBundle.bundleIdentifier.length)
            {
                requestedAccessibility = true;
                AXIsProcessTrustedWithOptions((__bridge CFDictionaryRef)@{
                    (__bridge NSString*)kAXTrustedCheckOptionPrompt: @YES });
            }
            quality = MediaPlaybackQuality::eUnknown;
            sampledAt = {};
            return quality;
        }
        const bool changed = pid != processId || sourceProcessId != snapshot.sourceProcessId ||
            sourceApp != snapshot.sourceApp || title != snapshot.title ||
            artist != snapshot.artist || album != snapshot.album ||
            durationMilliseconds != snapshot.durationMilliseconds;
        const auto now = Clock::now();
        if (!changed && now - sampledAt < std::chrono::seconds(2)) return quality;
        processId = pid;
        sourceProcessId = snapshot.sourceProcessId;
        if (sourceApp != snapshot.sourceApp) preparedProcessId = 0;
        sourceApp = snapshot.sourceApp;
        title = snapshot.title;
        artist = snapshot.artist;
        album = snapshot.album;
        durationMilliseconds = snapshot.durationMilliseconds;
        sampledAt = now;
        quality = MediaPlaybackQuality::eUnknown;
        AXUIElementRef application = AXUIElementCreateApplication(static_cast<pid_t>(pid));
        if (!application) return quality;
        if (pid != preparedProcessId && mac_media_quality::prepare_application(application, pid))
            preparedProcessId = pid;
        std::vector<media_quality_detection::Node> nodes;
        const bool complete = flatten(application, -1, 0, nodes, now + std::chrono::milliseconds(250));
        CFRelease(application);
        if (complete)
            quality = mac_media_quality::detect(nodes,
                mac_media_quality::wide([NSString stringWithUTF8String:title.c_str()]),
                mac_media_quality::wide([NSString stringWithUTF8String:artist.c_str()]),
                mac_media_quality::wide([NSString stringWithUTF8String:album.c_str()]));
        return quality;
    }
};

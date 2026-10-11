#pragma once

#import <Cocoa/Cocoa.h>
#import <ApplicationServices/ApplicationServices.h>
#include "playback_quality_detection.h"
#include <vibranceUI/media/session.h>
#include <chrono>
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
    if ([name isEqualToString:(__bridge NSString*)kAXStaticTextRole] ||
        [name isEqualToString:(__bridge NSString*)kAXImageRole]) return Role::eText;
    if ([name isEqualToString:(__bridge NSString*)kAXWindowRole]) return Role::eWindow;
    for (NSString* excluded in @[@"AXList", @"AXRow", @"AXTable", @"AXOutline",
        @"AXMenu", @"AXMenuItem", @"AXMenuBar", @"AXCheckBox", @"AXRadioButton",
        @"AXPopUpButton", @"AXComboBox", @"AXScrollArea"])
        if ([name isEqualToString:excluded]) return Role::eExcluded;
    return Role::eContainer;
}

}

// Run only on the media worker, using an existing accessibility grant. Never
// infer the track's encoding from captured PCM or request permission on launch.
class MacPlaybackQualityProbe
{
    using Clock = std::chrono::steady_clock;
    Clock::time_point sampledAt {};
    uint32_t processId = 0;
    std::string title, artist, album;
    MediaPlaybackQuality quality = MediaPlaybackQuality::eUnknown;

    static id attribute(AXUIElementRef element, CFStringRef name)
    {
        CFTypeRef value = nullptr;
        if (AXUIElementCopyAttributeValue(element, name, &value) != kAXErrorSuccess)
            return nil;
        return CFBridgingRelease(value);
    }

    static bool flatten(AXUIElementRef element, int parent, unsigned depth,
        std::vector<media_quality_detection::Node>& nodes, Clock::time_point deadline)
    {
        if (depth > 24 || nodes.size() >= 1024 || Clock::now() >= deadline) return false;
        AXUIElementSetMessagingTimeout(element, 0.02f);
        media_quality_detection::Node node;
        node.parent = parent;
        node.role = mac_media_quality::role(attribute(element, kAXRoleAttribute));
        id hidden = attribute(element, CFSTR("AXHidden"));
        id minimised = attribute(element, kAXMinimizedAttribute);
        node.visible = !([hidden isKindOfClass:NSNumber.class] && [hidden boolValue]) &&
            !([minimised isKindOfClass:NSNumber.class] && [minimised boolValue]);
        id name = attribute(element, kAXTitleAttribute);
        if (![name isKindOfClass:NSString.class] || ![name length])
            name = attribute(element, kAXValueAttribute);
        if (![name isKindOfClass:NSString.class] || ![name length])
            name = attribute(element, kAXDescriptionAttribute);
        node.name = mac_media_quality::wide(name);
        node.automationId = mac_media_quality::wide(attribute(element, kAXIdentifierAttribute));
        const int index = static_cast<int>(nodes.size());
        nodes.push_back(std::move(node));
        if (!nodes.back().visible || nodes.back().role == media_quality_detection::Role::eExcluded)
            return true;
        id children = attribute(element, kAXChildrenAttribute);
        if (![children isKindOfClass:NSArray.class]) return true;
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
        uint32_t pid = snapshot.sourceProcessId;
        if (!pid && !snapshot.sourceApp.empty())
        {
            NSString* bundle = [NSString stringWithUTF8String:snapshot.sourceApp.c_str()];
            pid = [NSRunningApplication runningApplicationsWithBundleIdentifier:bundle].firstObject.processIdentifier;
        }
        if (!snapshot.sessionAvailable || !pid || snapshot.title.empty() ||
            snapshot.artist.empty() || !AXIsProcessTrusted())
        {
            quality = MediaPlaybackQuality::eUnknown;
            sampledAt = {};
            return quality;
        }
        const bool changed = pid != processId || title != snapshot.title ||
            artist != snapshot.artist || album != snapshot.album;
        const auto now = Clock::now();
        if (!changed && now - sampledAt < std::chrono::seconds(2)) return quality;
        processId = pid;
        title = snapshot.title;
        artist = snapshot.artist;
        album = snapshot.album;
        sampledAt = now;
        quality = MediaPlaybackQuality::eUnknown;
        AXUIElementRef application = AXUIElementCreateApplication(static_cast<pid_t>(pid));
        if (!application) return quality;
        std::vector<media_quality_detection::Node> nodes;
        const bool complete = flatten(application, -1, 0, nodes, now + std::chrono::milliseconds(250));
        CFRelease(application);
        if (complete)
            quality = media_quality_detection::detect(nodes,
                mac_media_quality::wide([NSString stringWithUTF8String:title.c_str()]),
                mac_media_quality::wide([NSString stringWithUTF8String:artist.c_str()]),
                mac_media_quality::wide([NSString stringWithUTF8String:album.c_str()]));
        return quality;
    }
};

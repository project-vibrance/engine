// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/desktop_wallpaper.h>
#include <algorithm>
#include <vibranceUI/platform/image_services.h>
#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>

colour_palette::Image native_desktop_wallpaper(glm::ivec2 position, glm::ivec2 size)
{
    @autoreleasepool
    {
        colour_palette::Image image;
        if (size.x <= 0 || size.y <= 0) return image;
        for (NSScreen* screen in [NSScreen screens])
        {
            const auto display = static_cast<CGDirectDisplayID>([screen.deviceDescription[@"NSScreenNumber"] unsignedIntValue]);
            const CGRect bounds = CGDisplayBounds(display);
            if (!CGRectContainsPoint(bounds, CGPointMake(position.x + size.x / 2, position.y + size.y / 2))) continue;
            NSURL* url = [[NSWorkspace sharedWorkspace] desktopImageURLForScreen:screen];
            if (url.fileURL)
            {
                colour_palette::ImageSource source;
                source.path = std::filesystem::path(url.fileSystemRepresentation);
                std::string error;
                return colour_palette::decode_image(source, error);
            }
        }
        return {};
    }
}

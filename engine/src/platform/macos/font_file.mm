// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "platform/font_file_native.h"
#include <cstring>

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

namespace font_files
{
namespace
{
NSURL* font_url(const std::filesystem::path& path)
{
    NSString* value = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:path.c_str()
        length:std::strlen(path.c_str())];
    return value ? [NSURL fileURLWithPath:value] : nil;
}
}

bool native_font_valid(const std::filesystem::path& source)
{
    @autoreleasepool
    {
        NSURL* url = font_url(source);
        if (!url) return false;
        CFArrayRef descriptors = CTFontManagerCreateFontDescriptorsFromURL((__bridge CFURLRef)url);
        const bool valid = descriptors && CFArrayGetCount(descriptors) > 0;
        if (descriptors) CFRelease(descriptors);
        return valid;
    }
}

}

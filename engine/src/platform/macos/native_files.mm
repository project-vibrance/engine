// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/native_dialog.h>
#include <cstring>

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>

namespace
{
NSURL* font_url(const std::filesystem::path& path)
{
    NSString* value = [[NSFileManager defaultManager] stringWithFileSystemRepresentation:path.c_str()
        length:std::strlen(path.c_str())];
    return value ? [NSURL fileURLWithPath:value] : nil;
}
}

std::optional<std::filesystem::path> show_native_file_dialog(const NativeFileDialogOptions& options, std::string& error)
{
    @autoreleasepool
    {
        NSOpenPanel* panel = [NSOpenPanel openPanel];
        panel.title = [NSString stringWithUTF8String:options.title.c_str()];
        if (!options.initialDirectory.empty()) panel.directoryURL = font_url(options.initialDirectory);
        panel.canChooseFiles = YES;
        panel.canChooseDirectories = NO;
        panel.allowsMultipleSelection = NO;
        NSMutableArray<NSString*>* types = [NSMutableArray array];
        for (const auto& filter : options.filters)
            for (const auto& extension : filter.extensions)
                [types addObject:[NSString stringWithUTF8String:extension.c_str()]];
        if (types.count) panel.allowedFileTypes = types;
        panel.allowsOtherFileTypes = options.allowAllFiles;
        if ([panel runModal] != NSModalResponseOK || !panel.URL)
        { error.clear(); return {}; }
        error.clear();
        return std::filesystem::path(panel.URL.fileSystemRepresentation);
    }
}

bool native_open_directory(const std::filesystem::path& directory, std::string& error, bool createIfMissing)
{
    std::error_code fileError;
    if (createIfMissing && !directory.empty()) std::filesystem::create_directories(directory, fileError);
    if (directory.empty() || fileError || !std::filesystem::is_directory(directory, fileError))
    { error = "The requested directory is unavailable."; return false; }
    @autoreleasepool
    {
        NSURL* url = font_url(directory);
        if (!url || ![NSWorkspace.sharedWorkspace openURL:url])
        { error = "The directory could not be opened."; return false; }
    }
    error.clear();
    return true;
}

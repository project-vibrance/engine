// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/image_services.h>

#import <AppKit/AppKit.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace colour_palette
{
namespace
{
constexpr std::size_t maximumInputBytes = 64u * 1024u * 1024u;
constexpr std::uint64_t maximumSourcePixels = 64u * 1024u * 1024u;

NSString* path_string(const std::filesystem::path& path)
{
    return [[NSFileManager defaultManager] stringWithFileSystemRepresentation:path.c_str()
        length:std::strlen(path.c_str())];
}

CGImageRef rgba_image(const Image& image)
{
    if (!image.valid()) return nullptr;
    CGDataProviderRef provider = CGDataProviderCreateWithData(nullptr, image.rgba.data(),
        image.rgba.size(), nullptr);
    CGColorSpaceRef colourSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    CGImageRef result = CGImageCreate(image.width, image.height, 8u, 32u, image.width * 4u,
        colourSpace, kCGBitmapByteOrder32Big | kCGImageAlphaLast, provider, nullptr, false,
        kCGRenderingIntentDefault);
    CGColorSpaceRelease(colourSpace);
    CGDataProviderRelease(provider);
    return result;
}
}

std::optional<ImageSource> clipboard_image(void*, std::string& error)
{
    @autoreleasepool
    {
        NSPasteboard* clipboard = [NSPasteboard generalPasteboard];
        for (NSPasteboardType type in @[ NSPasteboardTypePNG, NSPasteboardTypeTIFF ])
        {
            NSData* data = [clipboard dataForType:type];
            if (!data || data.length == 0u || data.length > maximumInputBytes) continue;
            ImageSource source;
            source.kind = ImageSourceKind::eEncoded;
            source.name = "Clipboard";
            const auto* begin = static_cast<const unsigned char*>(data.bytes);
            source.bytes.assign(begin, begin + data.length);
            error.clear();
            return source;
        }
        NSArray<NSURL*>* urls = [clipboard readObjectsForClasses:@[ [NSURL class] ]
            options:@{ NSPasteboardURLReadingFileURLsOnlyKey: @YES }];
        if (urls.count > 0u)
        {
            ImageSource source;
            source.path = urls.firstObject.fileSystemRepresentation;
            source.name = urls.firstObject.lastPathComponent.stringByDeletingPathExtension.UTF8String;
            error.clear();
            return source;
        }
        error = "The clipboard does not contain an image.";
        return {};
    }
}

Image decode_image(const ImageSource& source, std::string& error,
    std::stop_token cancellation)
{
    @autoreleasepool
    {
        if (cancellation.stop_requested()) { error = "Image loading was cancelled."; return {}; }
        if (source.kind == ImageSourceKind::eRgba)
        {
            if (source.width == 0u || source.height == 0u ||
                static_cast<std::uint64_t>(source.width) * source.height > maximumSourcePixels ||
                source.bytes.size() !=
                    static_cast<std::size_t>(source.width) * source.height * 4u)
            { error = "The clipboard image is invalid or too large."; return {}; }
            const double scale = std::min(1.0, 1024.0 / std::max(source.width, source.height));
            const auto width = std::max(1u, static_cast<unsigned>(std::lround(source.width * scale)));
            const auto height = std::max(1u, static_cast<unsigned>(std::lround(source.height * scale)));
            Image image { source.name, source.path, width, height,
                std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4u) };
            for (std::uint32_t y = 0u; y < height; ++y)
            {
                if (cancellation.stop_requested()) { error = "Image loading was cancelled."; return {}; }
                for (std::uint32_t x = 0u; x < width; ++x)
                {
                    const auto originalX = std::min(source.width - 1u,
                        static_cast<unsigned>((static_cast<std::uint64_t>(x) * source.width) / width));
                    const auto originalY = std::min(source.height - 1u,
                        static_cast<unsigned>((static_cast<std::uint64_t>(y) * source.height) / height));
                    const auto original = (static_cast<std::size_t>(originalY) * source.width + originalX) * 4u;
                    std::copy_n(source.bytes.data() + original, 4u,
                        image.rgba.data() + (static_cast<std::size_t>(y) * width + x) * 4u);
                }
            }
            error.clear();
            return image;
        }
        CGImageSourceRef decoder = nullptr;
        if (source.kind == ImageSourceKind::eFile)
        {
            std::error_code fileError;
            const auto size = std::filesystem::file_size(source.path, fileError);
            if (fileError || size > maximumInputBytes)
            { error = "The image file is unavailable or larger than 64 MB."; return {}; }
            NSURL* url = [NSURL fileURLWithPath:path_string(source.path)];
            decoder = CGImageSourceCreateWithURL((__bridge CFURLRef)url, nullptr);
        }
        else if (source.kind == ImageSourceKind::eEncoded &&
            !source.bytes.empty() && source.bytes.size() <= maximumInputBytes)
        {
            NSData* data = [NSData dataWithBytesNoCopy:const_cast<unsigned char*>(source.bytes.data())
                length:source.bytes.size() freeWhenDone:NO];
            decoder = CGImageSourceCreateWithData((__bridge CFDataRef)data, nullptr);
        }
        if (!decoder) { error = "The image format is unsupported or the image is damaged."; return {}; }
        struct DecoderRelease { CGImageSourceRef value; ~DecoderRelease() { CFRelease(value); } } release { decoder };
        NSDictionary* properties = CFBridgingRelease(CGImageSourceCopyPropertiesAtIndex(decoder, 0u, nullptr));
        const auto sourceWidth = [properties[(__bridge NSString*)kCGImagePropertyPixelWidth] unsignedLongLongValue];
        const auto sourceHeight = [properties[(__bridge NSString*)kCGImagePropertyPixelHeight] unsignedLongLongValue];
        if (sourceWidth == 0u || sourceHeight == 0u || sourceWidth > maximumSourcePixels ||
            sourceHeight > maximumSourcePixels || sourceWidth * sourceHeight > maximumSourcePixels)
        { error = "The image dimensions are invalid or too large."; return {}; }
        NSDictionary* options = @{
            (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways: @YES,
            (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform: @YES,
            (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize: @1024,
            (__bridge NSString*)kCGImageSourceShouldCacheImmediately: @YES
        };
        CGImageRef thumbnail = CGImageSourceCreateThumbnailAtIndex(decoder, 0u,
            (__bridge CFDictionaryRef)options);
        if (!thumbnail) { error = "The image pixels could not be read."; return {}; }
        struct ImageRelease { CGImageRef value; ~ImageRelease() { CGImageRelease(value); } } imageRelease { thumbnail };
        if (cancellation.stop_requested()) { error = "Image loading was cancelled."; return {}; }
        const auto width = static_cast<std::uint32_t>(CGImageGetWidth(thumbnail));
        const auto height = static_cast<std::uint32_t>(CGImageGetHeight(thumbnail));
        if (width == 0u || height == 0u || width > 1024u || height > 1024u)
        { error = "The image dimensions are invalid or too large."; return {}; }
        Image image { source.name, source.path, width, height,
            std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4u) };
        CGColorSpaceRef colourSpace = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
        CGContextRef context = CGBitmapContextCreate(image.rgba.data(), width, height, 8u,
            width * 4u, colourSpace, kCGBitmapByteOrder32Big | kCGImageAlphaPremultipliedLast);
        CGColorSpaceRelease(colourSpace);
        if (!context) { error = "The image colours could not be decoded."; return {}; }
        CGContextTranslateCTM(context, 0.0, height);
        CGContextScaleCTM(context, 1.0, -1.0);
        CGContextDrawImage(context, CGRectMake(0.0, 0.0, width, height), thumbnail);
        CGContextRelease(context);
        for (std::size_t pixel = 0u; pixel < image.rgba.size(); pixel += 4u)
        {
            const auto alpha = image.rgba[pixel + 3u];
            if (alpha == 0u) continue;
            for (unsigned channel = 0u; channel < 3u; ++channel)
                image.rgba[pixel + channel] = static_cast<unsigned char>(std::min(255u,
                    (static_cast<unsigned>(image.rgba[pixel + channel]) * 255u + alpha / 2u) / alpha));
        }
        if (cancellation.stop_requested()) { error = "Image loading was cancelled."; return {}; }
        error.clear();
        return image;
    }
}

bool copy_image_to_clipboard(void*, const Image& image, std::string& error)
{
    @autoreleasepool
    {
        CGImageRef pixels = rgba_image(image);
        if (!pixels) { error = "There is no image palette to copy."; return false; }
        NSBitmapImageRep* bitmap = [[NSBitmapImageRep alloc] initWithCGImage:pixels];
        CGImageRelease(pixels);
        NSData* png = [bitmap representationUsingType:NSBitmapImageFileTypePNG properties:@{}];
        if (!png) { error = "The image could not be copied to the clipboard."; return false; }
        NSPasteboard* clipboard = [NSPasteboard generalPasteboard];
        [clipboard clearContents];
        if (![clipboard setData:png forType:NSPasteboardTypePNG])
        { error = "The image could not be copied to the clipboard."; return false; }
        error.clear();
        return true;
    }
}

std::optional<std::uint32_t> screen_colour_at_cursor(std::string& error)
{
    error = "Use the native macOS screen colour sampler.";
    return {};
}

ScreenPickState screen_pick_state() noexcept
{
    return { CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonLeft),
        CGEventSourceKeyState(kCGEventSourceStateCombinedSessionState, 53u) ||
            CGEventSourceButtonState(kCGEventSourceStateCombinedSessionState, kCGMouseButtonRight) };
}

bool begin_native_screen_pick(std::function<void(std::optional<std::uint32_t>)> completed)
{
    if (@available(macOS 10.15, *))
    {
        __block NSColorSampler* sampler = [[NSColorSampler alloc] init];
        [sampler showSamplerWithSelectionHandler:^(NSColor* colour) {
            NSColor* value = [colour colorUsingColorSpace:[NSColorSpace sRGBColorSpace]];
            std::optional<std::uint32_t> result;
            if (value)
            {
                const auto byte = [](CGFloat channel) {
                    return static_cast<std::uint32_t>(std::lround(std::clamp(channel, 0.0, 1.0) * 255.0));
                };
                result = (byte(value.redComponent) << 16u) |
                    (byte(value.greenComponent) << 8u) | byte(value.blueComponent);
            }
            if (completed) completed(result);
            sampler = nil;
        }];
        return true;
    }
    return false;
}
}

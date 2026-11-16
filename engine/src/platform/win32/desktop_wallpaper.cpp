// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#ifndef WINVER
#define WINVER 0x0602
#endif
#ifndef NTDDI_VERSION
#define NTDDI_VERSION 0x06020000
#endif
#include <vibranceUI/platform/desktop_wallpaper.h>
#include <algorithm>
#include <cmath>
#include <future>


#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shobjidl.h>
#include <wincodec.h>

static colour_palette::Image load_monitor_wallpaper(glm::ivec2 position, glm::ivec2 size)
{
    if (size.x <= 0 || size.y <= 0) return {};
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    IDesktopWallpaper* wallpaper = nullptr;
    const HRESULT created = CoCreateInstance(CLSID_DesktopWallpaper, nullptr, CLSCTX_ALL,
        IID_PPV_ARGS(&wallpaper));
    colour_palette::Image image;
    if (SUCCEEDED(created))
    {
        COLORREF background = RGB(0, 0, 0);
        wallpaper->GetBackgroundColor(&background);
        DESKTOP_WALLPAPER_POSITION mode = DWPOS_FILL;
        wallpaper->GetPosition(&mode);
        UINT count = 0u;
        wallpaper->GetMonitorDevicePathCount(&count);
        LPWSTR selected = nullptr;
        RECT selectedRect { position.x, position.y, position.x + size.x, position.y + size.y };
        RECT desktopRect = selectedRect;
        long long bestArea = -1;
        for (UINT index = 0u; index < count; ++index)
        {
            LPWSTR device = nullptr;
            RECT bounds {};
            if (SUCCEEDED(wallpaper->GetMonitorDevicePathAt(index, &device)) && device &&
                SUCCEEDED(wallpaper->GetMonitorRECT(device, &bounds)))
            {
                desktopRect.left = std::min(desktopRect.left, bounds.left);
                desktopRect.top = std::min(desktopRect.top, bounds.top);
                desktopRect.right = std::max(desktopRect.right, bounds.right);
                desktopRect.bottom = std::max(desktopRect.bottom, bounds.bottom);
                const auto area = static_cast<long long>(std::max(0L, std::min(bounds.right, LONG(position.x + size.x)) -
                    std::max(bounds.left, LONG(position.x)))) * std::max(0L,
                    std::min(bounds.bottom, LONG(position.y + size.y)) - std::max(bounds.top, LONG(position.y)));
                if (area > bestArea)
                {
                    CoTaskMemFree(selected); selected = device; device = nullptr;
                    selectedRect = bounds; bestArea = area;
                }
            }
            CoTaskMemFree(device);
        }
        LPWSTR file = nullptr;
        wallpaper->GetWallpaper(selected, &file);
        colour_palette::Image source;
        if (file && *file)
        {
            IWICImagingFactory* factory = nullptr;
            IWICBitmapDecoder* decoder = nullptr;
            IWICBitmapFrameDecode* frame = nullptr;
            IWICFormatConverter* converter = nullptr;
            if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))))
            {
                if (SUCCEEDED(factory->CreateDecoderFromFilename(file, nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) &&
                    SUCCEEDED(decoder->GetFrame(0u, &frame)) &&
                    SUCCEEDED(frame->GetSize(&source.width, &source.height)) && source.width && source.height &&
                    std::uint64_t(source.width) * source.height <= 64u * 1024u * 1024u &&
                    SUCCEEDED(factory->CreateFormatConverter(&converter)) &&
                    SUCCEEDED(converter->Initialize(frame, GUID_WICPixelFormat32bppRGBA,
                        WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
                {
                    source.rgba.resize(std::size_t(source.width) * source.height * 4u);
                    if (FAILED(converter->CopyPixels(nullptr, source.width * 4u,
                        static_cast<UINT>(source.rgba.size()), source.rgba.data()))) source = {};
                }
            }
            if (converter) converter->Release();
            if (frame) frame->Release();
            if (decoder) decoder->Release();
            if (factory) factory->Release();
        }
        CoTaskMemFree(file); CoTaskMemFree(selected); wallpaper->Release();
        image.width = 1008u; image.height = 450u;
        image.rgba.resize(std::size_t(image.width) * image.height * 4u);
        const bool sourceReady = source.width && source.height &&
            source.rgba.size() == std::size_t(source.width) * source.height * 4u;
        const RECT canvas = mode == DWPOS_SPAN ? desktopRect : selectedRect;
        const double canvasWidth = std::max(1L, canvas.right - canvas.left);
        const double canvasHeight = std::max(1L, canvas.bottom - canvas.top);
        const double sourceWidth = source.width;
        const double sourceHeight = source.height;
        double scaleX = 1.0, scaleY = 1.0;
        if (sourceReady)
        {
            if (mode == DWPOS_FILL || mode == DWPOS_SPAN)
                scaleX = scaleY = std::max(canvasWidth / sourceWidth, canvasHeight / sourceHeight);
            else if (mode == DWPOS_FIT) scaleX = scaleY = std::min(canvasWidth / sourceWidth, canvasHeight / sourceHeight);
            else if (mode == DWPOS_STRETCH) { scaleX = canvasWidth / sourceWidth; scaleY = canvasHeight / sourceHeight; }
        }
        const double left = canvas.left + (canvasWidth - sourceWidth * scaleX) * .5;
        const double top = canvas.top + (canvasHeight - sourceHeight * scaleY) * .5;
        const double patchWidth = std::min(size.x, 1008);
        const double patchHeight = std::min(size.y, 450);
        for (unsigned y = 0u; y < image.height; ++y)
            for (unsigned x = 0u; x < image.width; ++x)
            {
                const auto offset = (std::size_t(y) * image.width + x) * 4u;
                image.rgba[offset] = GetRValue(background);
                image.rgba[offset + 1u] = GetGValue(background);
                image.rgba[offset + 2u] = GetBValue(background);
                image.rgba[offset + 3u] = 255u;
                if (!sourceReady) continue;
                const double screenX = position.x + (size.x - patchWidth) * .5 + (x + .5) * patchWidth / image.width;
                const double screenY = position.y + (y + .5) * patchHeight / image.height;
                double imageX = (screenX - left) / scaleX;
                double imageY = (screenY - top) / scaleY;
                if (mode == DWPOS_TILE)
                {
                    imageX = std::fmod(screenX - desktopRect.left, sourceWidth);
                    imageY = std::fmod(screenY - desktopRect.top, sourceHeight);
                    if (imageX < 0) imageX += sourceWidth;
                    if (imageY < 0) imageY += sourceHeight;
                }
                if (imageX < 0 || imageY < 0 || imageX >= sourceWidth || imageY >= sourceHeight) continue;
                imageX = std::clamp(imageX - .5, 0.0, sourceWidth - 1);
                imageY = std::clamp(imageY - .5, 0.0, sourceHeight - 1);
                const auto sx = unsigned(imageX), sy = unsigned(imageY);
                const auto nx = std::min(sx + 1u, source.width - 1u), ny = std::min(sy + 1u, source.height - 1u);
                const double fx = imageX - sx, fy = imageY - sy;
                const auto sample = [&](unsigned channel) {
                    const auto value = [&](unsigned x, unsigned y) {
                        return source.rgba[(std::size_t(y) * source.width + x) * 4u + channel];
                    };
                    return (value(sx, sy) * (1 - fx) + value(nx, sy) * fx) * (1 - fy) +
                        (value(sx, ny) * (1 - fx) + value(nx, ny) * fx) * fy;
                };
                const double alpha = sample(3u) / 255.0;
                for (unsigned channel = 0u; channel < 3u; ++channel)
                    image.rgba[offset + channel] = static_cast<unsigned char>(std::lround(
                        sample(channel) * alpha + image.rgba[offset + channel] * (1 - alpha)));
            }
    }
    if (SUCCEEDED(apartment)) CoUninitialize();
    return image;
}
colour_palette::Image native_desktop_wallpaper(glm::ivec2 position, glm::ivec2 size)
{
    return std::async(std::launch::async, [position, size] { return load_monitor_wallpaper(position, size); }).get();
}

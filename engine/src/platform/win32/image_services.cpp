// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/platform/image_services.h>
#include <vibranceUI/platform/native_dialog.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>


#ifndef NOMINMAX
#define NOMINMAX
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <wincodec.h>

namespace colour_palette
{
namespace
{
constexpr std::size_t maximumInputBytes = 64u * 1024u * 1024u;
constexpr std::uint64_t maximumSourcePixels = 64u * 1024u * 1024u;

template <typename T>
class ComObject
{
public:
    ~ComObject() { if (value) value->Release(); }
    T* operator->() const { return value; }
    T** put() { return &value; }
    T* get() const { return value; }
private:
    T* value = nullptr;
};

class Apartment
{
public:
    Apartment() : result(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
    ~Apartment() { if (SUCCEEDED(result)) CoUninitialize(); }
    bool valid() const { return SUCCEEDED(result) || result == RPC_E_CHANGED_MODE; }
private:
    HRESULT result;
};

class Clipboard
{
public:
    explicit Clipboard(HWND owner) : opened(OpenClipboard(owner) != FALSE) {}
    ~Clipboard() { if (opened) CloseClipboard(); }
    bool valid() const { return opened; }
private:
    bool opened;
};

std::string utf8(std::wstring_view text)
{
    if (text.empty()) return {};
    const int count = WideCharToMultiByte(CP_UTF8, 0u, text.data(),
        static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    if (count <= 0) return {};
    std::string result(static_cast<std::size_t>(count), '\0');
    WideCharToMultiByte(CP_UTF8, 0u, text.data(), static_cast<int>(text.size()),
        result.data(), count, nullptr, nullptr);
    return result;
}

std::vector<unsigned char> clipboard_bytes(UINT format)
{
    const HANDLE memory = GetClipboardData(format);
    const SIZE_T size = memory ? GlobalSize(memory) : 0u;
    if (size == 0u || size > maximumInputBytes) return {};
    const void* bytes = GlobalLock(memory);
    if (!bytes) return {};
    std::vector<unsigned char> result;
    try
    {
        const auto* begin = static_cast<const unsigned char*>(bytes);
        result.assign(begin, begin + size);
    }
    catch (...)
    {
        GlobalUnlock(memory);
        throw;
    }
    GlobalUnlock(memory);
    return result;
}

std::optional<std::vector<unsigned char>> bitmap_file(std::span<const unsigned char> dib)
{
    if (dib.size() < sizeof(BITMAPINFOHEADER)) return {};
    BITMAPINFOHEADER info {};
    std::memcpy(&info, dib.data(), sizeof(info));
    if (info.biSize < sizeof(info) || info.biSize > dib.size() ||
        info.biWidth <= 0 || info.biHeight == 0 || info.biHeight == std::numeric_limits<LONG>::min() ||
        info.biBitCount == 0u || info.biBitCount > 64u) return {};
    std::size_t offset = info.biSize;
    if (info.biSize == sizeof(info) && (info.biCompression == BI_BITFIELDS || info.biCompression == 6u))
        offset += info.biCompression == 6u ? 16u : 12u;
    const std::uint32_t colours = info.biClrUsed != 0u ? info.biClrUsed :
        (info.biBitCount <= 8u ? 1u << info.biBitCount : 0u);
    if (colours > 65536u) return {};
    offset += static_cast<std::size_t>(colours) * sizeof(RGBQUAD);
    if (offset > dib.size()) return {};
    BITMAPFILEHEADER header {};
    header.bfType = 0x4d42u;
    header.bfOffBits = static_cast<DWORD>(sizeof(header) + offset);
    header.bfSize = static_cast<DWORD>(sizeof(header) + dib.size());
    std::vector<unsigned char> result(sizeof(header) + dib.size());
    std::memcpy(result.data(), &header, sizeof(header));
    std::memcpy(result.data() + sizeof(header), dib.data(), dib.size());
    return result;
}

Image resized_rgba(const ImageSource& source, std::string& error,
    std::stop_token cancellation)
{
    if (source.width == 0u || source.height == 0u ||
        static_cast<std::uint64_t>(source.width) * source.height > maximumSourcePixels ||
        source.bytes.size() != static_cast<std::size_t>(source.width) * source.height * 4u)
    {
        error = "The clipboard image is invalid or too large.";
        return {};
    }
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
}

std::optional<ImageSource> clipboard_image(void* owner, std::string& error)
{
    Clipboard clipboard(static_cast<HWND>(owner));
    if (!clipboard.valid()) { error = "The clipboard is busy. Try again."; return {}; }
    ImageSource source;
    source.name = "Clipboard";
    const UINT png = RegisterClipboardFormatW(L"PNG");
    if (png != 0u && IsClipboardFormatAvailable(png))
    {
        source.kind = ImageSourceKind::eEncoded;
        source.bytes = clipboard_bytes(png);
        if (!source.bytes.empty()) { error.clear(); return source; }
    }
    for (const UINT format : { static_cast<UINT>(CF_DIBV5), static_cast<UINT>(CF_DIB) })
    {
        if (!IsClipboardFormatAvailable(format)) continue;
        source.kind = ImageSourceKind::eDib;
        source.bytes = clipboard_bytes(format);
        if (!source.bytes.empty()) { error.clear(); return source; }
    }
    if (IsClipboardFormatAvailable(CF_HDROP))
    {
        const auto files = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        const UINT length = files ? DragQueryFileW(files, 0u, nullptr, 0u) : 0u;
        if (length > 0u && length < 32768u)
        {
            std::vector<wchar_t> path(length + 1u, L'\0');
            if (DragQueryFileW(files, 0u, path.data(), static_cast<UINT>(path.size())) != 0u)
            {
                source.kind = ImageSourceKind::eFile;
                source.path = path.data();
                source.name = utf8(source.path.stem().wstring());
                error.clear();
                return source;
            }
        }
    }
    error = "The clipboard does not contain an image.";
    return {};
}

Image decode_image(const ImageSource& source, std::string& error,
    std::stop_token cancellation)
{
    if (cancellation.stop_requested()) { error = "Image loading was cancelled."; return {}; }
    if (source.kind == ImageSourceKind::eRgba) return resized_rgba(source, error, cancellation);
    if (source.kind != ImageSourceKind::eFile && source.bytes.size() > maximumInputBytes)
    { error = "The clipboard image is too large."; return {}; }
    if (source.kind == ImageSourceKind::eFile)
    {
        std::error_code fileError;
        const auto size = std::filesystem::file_size(source.path, fileError);
        if (fileError || size > maximumInputBytes)
        { error = "The image file is unavailable or larger than 64 MB."; return {}; }
    }
    Apartment apartment;
    if (!apartment.valid()) { error = "The image decoder could not be initialised."; return {}; }
    ComObject<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
        IID_IWICImagingFactory, reinterpret_cast<void**>(factory.put()))))
    { error = "The Windows image decoder is unavailable."; return {}; }
    ComObject<IWICBitmapDecoder> decoder;
    ComObject<IWICStream> stream;
    std::optional<std::vector<unsigned char>> bitmap;
    HRESULT result = E_FAIL;
    if (source.kind == ImageSourceKind::eFile)
        result = factory->CreateDecoderFromFilename(source.path.c_str(), nullptr,
            GENERIC_READ, WICDecodeMetadataCacheOnDemand, decoder.put());
    else
    {
        std::span<const unsigned char> encoded(source.bytes);
        if (source.kind == ImageSourceKind::eDib)
        {
            bitmap = bitmap_file(source.bytes);
            if (!bitmap) { error = "The clipboard bitmap is invalid."; return {}; }
            encoded = *bitmap;
        }
        if (!encoded.empty() && SUCCEEDED(factory->CreateStream(stream.put())) &&
            SUCCEEDED(stream->InitializeFromMemory(const_cast<BYTE*>(encoded.data()),
                static_cast<DWORD>(encoded.size()))))
            result = factory->CreateDecoderFromStream(stream.get(), nullptr,
                WICDecodeMetadataCacheOnDemand, decoder.put());
    }
    if (FAILED(result)) { error = "The image format is unsupported or the image is damaged."; return {}; }
    ComObject<IWICBitmapFrameDecode> frame;
    UINT sourceWidth = 0u, sourceHeight = 0u;
    if (FAILED(decoder->GetFrame(0u, frame.put())) ||
        FAILED(frame->GetSize(&sourceWidth, &sourceHeight)) || sourceWidth == 0u || sourceHeight == 0u ||
        static_cast<std::uint64_t>(sourceWidth) * sourceHeight > maximumSourcePixels)
    { error = "The image dimensions are invalid or too large."; return {}; }
    const double scale = std::min(1.0, 1024.0 / std::max(sourceWidth, sourceHeight));
    const UINT width = std::max(1u, static_cast<UINT>(std::lround(sourceWidth * scale)));
    const UINT height = std::max(1u, static_cast<UINT>(std::lround(sourceHeight * scale)));
    ComObject<IWICBitmapScaler> scaler;
    IWICBitmapSource* pixels = frame.get();
    if (width != sourceWidth || height != sourceHeight)
    {
        if (FAILED(factory->CreateBitmapScaler(scaler.put())) ||
            FAILED(scaler->Initialize(frame.get(), width, height, WICBitmapInterpolationModeFant)))
        { error = "The image could not be resized."; return {}; }
        pixels = scaler.get();
    }
    ComObject<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(converter.put())) ||
        FAILED(converter->Initialize(pixels, GUID_WICPixelFormat32bppRGBA,
            WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeCustom)))
    { error = "The image colours could not be decoded."; return {}; }
    if (cancellation.stop_requested()) { error = "Image loading was cancelled."; return {}; }
    Image image { source.name, source.path, width, height,
        std::vector<unsigned char>(static_cast<std::size_t>(width) * height * 4u) };
    if (FAILED(converter->CopyPixels(nullptr, width * 4u,
        static_cast<UINT>(image.rgba.size()), image.rgba.data())))
    { error = "The image pixels could not be read."; return {}; }
    if (cancellation.stop_requested()) { error = "Image loading was cancelled."; return {}; }
    error.clear();
    return image;
}

bool copy_image_to_clipboard(void* owner, const Image& image, std::string& error)
{
    if (!image.valid()) { error = "There is no image palette to copy."; return false; }
    const auto size = sizeof(BITMAPV5HEADER) + image.rgba.size();
    const HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!memory) { error = "The clipboard image could not be allocated."; return false; }
    auto* header = static_cast<BITMAPV5HEADER*>(GlobalLock(memory));
    if (!header) { GlobalFree(memory); error = "The clipboard image could not be prepared."; return false; }
    *header = {};
    header->bV5Size = sizeof(*header);
    header->bV5Width = static_cast<LONG>(image.width);
    header->bV5Height = -static_cast<LONG>(image.height);
    header->bV5Planes = 1u;
    header->bV5BitCount = 32u;
    header->bV5Compression = BI_BITFIELDS;
    header->bV5SizeImage = static_cast<DWORD>(image.rgba.size());
    header->bV5RedMask = 0x00ff0000u;
    header->bV5GreenMask = 0x0000ff00u;
    header->bV5BlueMask = 0x000000ffu;
    header->bV5AlphaMask = 0xff000000u;
    header->bV5CSType = LCS_sRGB;
    auto* output = reinterpret_cast<unsigned char*>(header + 1u);
    for (std::size_t pixel = 0u; pixel < image.rgba.size(); pixel += 4u)
    {
        output[pixel] = image.rgba[pixel + 2u];
        output[pixel + 1u] = image.rgba[pixel + 1u];
        output[pixel + 2u] = image.rgba[pixel];
        output[pixel + 3u] = image.rgba[pixel + 3u];
    }
    GlobalUnlock(memory);
    Clipboard clipboard(static_cast<HWND>(owner));
    if (!clipboard.valid() || !EmptyClipboard() || !SetClipboardData(CF_DIBV5, memory))
    { GlobalFree(memory); error = "The image could not be copied to the clipboard."; return false; }
    error.clear();
    return true;
}

std::optional<std::uint32_t> screen_colour_at_cursor(std::string& error)
{
    POINT point {};
    if (!GetCursorPos(&point)) { error = "The pointer position could not be read."; return {}; }
    const HDC screen = GetDC(nullptr);
    if (!screen) { error = "The screen colour could not be read."; return {}; }
    const COLORREF value = GetPixel(screen, point.x, point.y);
    ReleaseDC(nullptr, screen);
    if (value == CLR_INVALID) { error = "The screen colour could not be read."; return {}; }
    error.clear();
    return (static_cast<std::uint32_t>(GetRValue(value)) << 16u) |
        (static_cast<std::uint32_t>(GetGValue(value)) << 8u) | GetBValue(value);
}

ScreenPickState screen_pick_state() noexcept
{
    return { (GetAsyncKeyState(VK_LBUTTON) & 0x8000) != 0,
        (GetAsyncKeyState(VK_ESCAPE) & 0x8000) != 0 ||
            (GetAsyncKeyState(VK_RBUTTON) & 0x8000) != 0 };
}

bool begin_native_screen_pick(std::function<void(std::optional<std::uint32_t>)>) { return false; }
}

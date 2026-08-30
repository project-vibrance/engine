#if defined(_WIN32)

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <appmodel.h>
#include <audiopolicy.h>
#include <mmdeviceapi.h>
#include <shlobj_core.h>
#include <shobjidl_core.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <propvarutil.h>
#include <propkey.h>

#include <vibranceUI/platform/win32_interop_abi.h>

#include <winrt/Windows.Foundation.h>
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.ApplicationModel.h>
#include <winrt/Windows.Graphics.Imaging.h>
#include <winrt/Windows.Media.Control.h>
#include <winrt/Windows.Storage.Streams.h>
#include <winrt/Windows.UI.Notifications.h>
#include <winrt/Windows.UI.Notifications.Management.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <mutex>
#include <new>
#include <optional>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace
{
using namespace std::chrono_literals;

constexpr GUID kClsidMmDeviceEnumerator {
    0xbcde0395, 0xe52f, 0x467c,
    { 0x8e, 0x3d, 0xc4, 0x57, 0x92, 0x91, 0x69, 0x2e }
};
constexpr GUID kIidMmDeviceEnumerator {
    0xa95664d2, 0x9614, 0x4f35,
    { 0xa7, 0x46, 0xde, 0x8d, 0xb6, 0x36, 0x17, 0xe6 }
};
constexpr GUID kIidAudioSessionManager2 {
    0x77aa99a0, 0x1bd6, 0x484f,
    { 0x8b, 0xc7, 0x2c, 0x65, 0x4c, 0x9a, 0x9b, 0x6f }
};
constexpr GUID kIidAudioSessionControl2 {
    0xbfb7ff88, 0x7239, 0x4fc9,
    { 0x8f, 0xa2, 0x07, 0xc9, 0x50, 0xbe, 0x9c, 0x6d }
};

template <typename T>
void release_com(T*& value)
{
    if (value)
    {
        value->Release();
        value = nullptr;
    }
}

using winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSession;
using winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionManager;
using winrt::Windows::Media::Control::
    GlobalSystemMediaTransportControlsSessionPlaybackStatus;
using winrt::Windows::Storage::Streams::Buffer;
using winrt::Windows::Storage::Streams::DataReader;
using winrt::Windows::Storage::Streams::DataWriter;
using winrt::Windows::Storage::Streams::InMemoryRandomAccessStream;
using winrt::Windows::Storage::Streams::InputStreamOptions;
using winrt::Windows::Storage::Streams::IRandomAccessStream;
using winrt::Windows::Graphics::Imaging::BitmapAlphaMode;
using winrt::Windows::Graphics::Imaging::BitmapDecoder;
using winrt::Windows::Graphics::Imaging::BitmapEncoder;
using winrt::Windows::Graphics::Imaging::BitmapPixelFormat;
using winrt::Windows::Graphics::Imaging::SoftwareBitmap;
using winrt::Windows::UI::Notifications::KnownNotificationBindings;
using winrt::Windows::UI::Notifications::NotificationKinds;
using winrt::Windows::UI::Notifications::Management::UserNotificationListener;
using winrt::Windows::UI::Notifications::Management::
    UserNotificationListenerAccessStatus;

template <std::size_t Size>
void copy_utf8(char (&destination)[Size], const winrt::hstring& value)
{
    const std::string utf8 = winrt::to_string(value);
    const std::size_t limit = std::min(utf8.size(), Size - 1u);
    std::size_t validSize = 0u;
    while (validSize < limit)
    {
        const unsigned char lead =
            static_cast<unsigned char>(utf8[validSize]);
        std::size_t sequenceLength = 1u;
        if ((lead & 0xE0u) == 0xC0u)
        {
            sequenceLength = 2u;
        }
        else if ((lead & 0xF0u) == 0xE0u)
        {
            sequenceLength = 3u;
        }
        else if ((lead & 0xF8u) == 0xF0u)
        {
            sequenceLength = 4u;
        }
        if (validSize + sequenceLength > limit)
        {
            break;
        }
        validSize += sequenceLength;
    }
    std::memcpy(destination, utf8.data(), validSize);
    destination[validSize] = '\0';
}

template <std::size_t Size>
void copy_utf8(char (&destination)[Size], const std::string& value)
{
    const std::size_t copySize = std::min(value.size(), Size - 1u);
    std::memcpy(destination, value.data(), copySize);
    destination[copySize] = '\0';
}

std::int64_t milliseconds(
    const winrt::Windows::Foundation::TimeSpan& value)
{
    return value.count() / 10000;
}

std::string utf8_path(const std::filesystem::path& path)
{
    const std::wstring wide = path.wstring();
    if (wide.empty())
    {
        return {};
    }
    const int size = WideCharToMultiByte(
        CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
        nullptr, 0, nullptr, nullptr);
    std::string result(static_cast<std::size_t>(std::max(size, 0)), '\0');
    if (size > 0)
    {
        WideCharToMultiByte(
            CP_UTF8, 0, wide.data(), static_cast<int>(wide.size()),
            result.data(), size, nullptr, nullptr);
    }
    return result;
}

std::uint64_t thumbnail_hash(const std::vector<std::uint8_t>& bytes)
{
    std::uint64_t value = 1469598103934665603ull;
    for (const std::uint8_t byte : bytes)
    {
        value = (value ^ byte) * 1099511628211ull;
    }
    return value;
}

std::wstring thumbnail_extension(const std::vector<std::uint8_t>& bytes)
{
    if (bytes.size() >= 8u &&
        bytes[0] == 0x89u && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G')
    {
        return L".png";
    }
    if (bytes.size() >= 3u && bytes[0] == 0xFFu && bytes[1] == 0xD8u)
    {
        return L".jpg";
    }
    if (bytes.size() >= 12u &&
        std::memcmp(bytes.data(), "RIFF", 4u) == 0 &&
        std::memcmp(bytes.data() + 8u, "WEBP", 4u) == 0)
    {
        return L".webp";
    }
    if (bytes.size() >= 6u &&
        (std::memcmp(bytes.data(), "GIF87a", 6u) == 0 ||
            std::memcmp(bytes.data(), "GIF89a", 6u) == 0))
    {
        return L".gif";
    }
    if (bytes.size() >= 2u && bytes[0] == 'B' && bytes[1] == 'M')
    {
        return L".bmp";
    }
    if (bytes.size() >= 4u && bytes[0] == 0u && bytes[1] == 0u &&
        bytes[2] == 1u && bytes[3] == 0u)
    {
        return L".ico";
    }
    const std::size_t textProbeSize = std::min<std::size_t>(
        bytes.size(),
        1024u);
    const std::string textProbe(
        reinterpret_cast<const char*>(bytes.data()),
        textProbeSize);
    if (textProbe.find("<svg") != std::string::npos)
    {
        return L".svg";
    }
    return L".img";
}

std::vector<std::uint8_t> read_stream_bytes(
    const IRandomAccessStream& stream)
{
    std::vector<std::uint8_t> result;
    stream.Seek(0u);
    const std::uint64_t streamSize = std::min<std::uint64_t>(
        stream.Size(), 32ull * 1024ull * 1024ull);
    if (streamSize == 0u)
    {
        return result;
    }
    Buffer buffer(static_cast<std::uint32_t>(streamSize));
    const auto filled = stream.ReadAsync(
        buffer,
        static_cast<std::uint32_t>(streamSize),
        InputStreamOptions::None).get();
    DataReader reader = DataReader::FromBuffer(filled);
    result.resize(reader.UnconsumedBufferLength());
    reader.ReadBytes(winrt::array_view<std::uint8_t>(result));
    return result;
}

bool cache_encoded_thumbnail(
    const std::vector<std::uint8_t>& bytes,
    std::uint64_t& revision,
    std::string& path,
    std::wstring extension = {})
{
    if (bytes.empty())
    {
        return false;
    }
    revision = thumbnail_hash(bytes);
    std::error_code error;
    const std::filesystem::path directory =
        std::filesystem::temp_directory_path(error) / L"vibranceUI" / L"gsmtc";
    if (error)
    {
        return false;
    }
    std::filesystem::create_directories(directory, error);
    if (error)
    {
        return false;
    }
    if (extension.empty())
    {
        extension = thumbnail_extension(bytes);
    }
    std::wostringstream filename;
    filename << L"thumbnail-" << std::hex << revision << extension;
    const std::filesystem::path destination = directory / filename.str();
    if (!std::filesystem::exists(destination, error))
    {
        const std::filesystem::path temporary = destination.wstring() + L".tmp";
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output)
        {
            std::filesystem::remove(temporary, error);
            return false;
        }
        std::filesystem::rename(temporary, destination, error);
        if (error && !std::filesystem::exists(destination))
        {
            return false;
        }
    }
    path = utf8_path(destination);
    return !path.empty();
}

bool cache_icon(
    HICON icon,
    std::uint64_t& revision,
    std::string& path)
{
    if (!icon)
    {
        return false;
    }
    constexpr int width = 64;
    constexpr int height = 64;
    BITMAPINFO bitmapInfo = {};
    bitmapInfo.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bitmapInfo.bmiHeader.biWidth = width;
    bitmapInfo.bmiHeader.biHeight = -height;
    bitmapInfo.bmiHeader.biPlanes = 1u;
    bitmapInfo.bmiHeader.biBitCount = 32u;
    bitmapInfo.bmiHeader.biCompression = BI_RGB;
    void* pixelMemory = nullptr;
    HDC screen = GetDC(nullptr);
    HDC memory = screen ? CreateCompatibleDC(screen) : nullptr;
    HBITMAP bitmap = memory ? CreateDIBSection(
        memory,
        &bitmapInfo,
        DIB_RGB_COLORS,
        &pixelMemory,
        nullptr,
        0u) : nullptr;
    bool drawn = false;
    if (bitmap && pixelMemory)
    {
        const HGDIOBJ previous = SelectObject(memory, bitmap);
        std::memset(pixelMemory, 0, width * height * 4u);
        drawn = DrawIconEx(
            memory,
            0,
            0,
            icon,
            width,
            height,
            0u,
            nullptr,
            DI_NORMAL) != FALSE;
        SelectObject(memory, previous);
    }
    DestroyIcon(icon);
    if (memory)
    {
        DeleteDC(memory);
    }
    if (screen)
    {
        ReleaseDC(nullptr, screen);
    }
    if (!drawn || !bitmap || !pixelMemory)
    {
        if (bitmap)
        {
            DeleteObject(bitmap);
        }
        return false;
    }

    std::vector<std::uint8_t> pixels(width * height * 4u);
    std::memcpy(pixels.data(), pixelMemory, pixels.size());
    DeleteObject(bitmap);
    bool nonZeroAlpha = false;
    for (std::size_t index = 3u; index < pixels.size(); index += 4u)
    {
        nonZeroAlpha = nonZeroAlpha || pixels[index] != 0u;
    }
    if (!nonZeroAlpha)
    {
        for (std::size_t index = 0u; index < pixels.size(); index += 4u)
        {
            if (pixels[index] != 0u || pixels[index + 1u] != 0u ||
                pixels[index + 2u] != 0u)
            {
                pixels[index + 3u] = 255u;
            }
        }
    }

    try
    {
        DataWriter writer;
        writer.WriteBytes(winrt::array_view<const std::uint8_t>(pixels));
        const auto buffer = writer.DetachBuffer();
        const SoftwareBitmap softwareBitmap =
            SoftwareBitmap::CreateCopyFromBuffer(
                buffer,
                BitmapPixelFormat::Bgra8,
                width,
                height,
                BitmapAlphaMode::Straight);
        InMemoryRandomAccessStream encoded;
        const auto encoder = BitmapEncoder::CreateAsync(
            BitmapEncoder::PngEncoderId(), encoded).get();
        encoder.SetSoftwareBitmap(softwareBitmap);
        encoder.FlushAsync().get();
        return cache_encoded_thumbnail(
            read_stream_bytes(encoded),
            revision,
            path,
            L".png");
    }
    catch (...)
    {
        return false;
    }
}

bool cache_registered_application_icon(
    std::wstring_view appUserModelId,
    std::uint64_t& revision,
    std::string& path)
{
    if (appUserModelId.empty())
    {
        return false;
    }
    const std::wstring parsingName =
        L"shell:AppsFolder\\" + std::wstring(appUserModelId);
    PIDLIST_ABSOLUTE itemIdList = nullptr;
    if (FAILED(SHParseDisplayName(
            parsingName.c_str(),
            nullptr,
            &itemIdList,
            0u,
            nullptr)) || !itemIdList)
    {
        return false;
    }
    SHFILEINFOW fileInfo = {};
    const DWORD_PTR result = SHGetFileInfoW(
        reinterpret_cast<LPCWSTR>(itemIdList),
        0u,
        &fileInfo,
        sizeof(fileInfo),
        SHGFI_PIDL | SHGFI_ICON | SHGFI_LARGEICON);
    CoTaskMemFree(itemIdList);
    return result != 0u && fileInfo.hIcon &&
        cache_icon(fileInfo.hIcon, revision, path);
}

bool cache_window_icon(
    HWND window,
    std::uint64_t& revision,
    std::string& path)
{
    if (!window)
    {
        return false;
    }
    DWORD_PTR messageResult = 0u;
    SendMessageTimeoutW(
        window,
        WM_GETICON,
        ICON_BIG,
        0,
        SMTO_ABORTIFHUNG | SMTO_BLOCK,
        100u,
        &messageResult);
    HICON sourceIcon = reinterpret_cast<HICON>(messageResult);
    if (!sourceIcon)
    {
        sourceIcon = reinterpret_cast<HICON>(
            GetClassLongPtrW(window, GCLP_HICON));
    }
    if (!sourceIcon)
    {
        SendMessageTimeoutW(
            window,
            WM_GETICON,
            ICON_SMALL2,
            0,
            SMTO_ABORTIFHUNG | SMTO_BLOCK,
            100u,
            &messageResult);
        sourceIcon = reinterpret_cast<HICON>(messageResult);
    }
    if (!sourceIcon)
    {
        sourceIcon = reinterpret_cast<HICON>(
            GetClassLongPtrW(window, GCLP_HICONSM));
    }
    HICON icon = sourceIcon ? CopyIcon(sourceIcon) : nullptr;
    if (!icon)
    {
        DWORD processId = 0u;
        GetWindowThreadProcessId(window, &processId);
        HANDLE process = processId != 0u ? OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId) : nullptr;
        std::wstring executablePath(32768u, L'\0');
        DWORD executablePathSize =
            static_cast<DWORD>(executablePath.size());
        if (process && QueryFullProcessImageNameW(
                process,
                0u,
                executablePath.data(),
                &executablePathSize))
        {
            executablePath.resize(executablePathSize);
            SHFILEINFOW fileInfo = {};
            if (SHGetFileInfoW(
                    executablePath.c_str(),
                    0u,
                    &fileInfo,
                    sizeof(fileInfo),
                    SHGFI_ICON | SHGFI_LARGEICON) != 0u)
            {
                icon = fileInfo.hIcon;
            }
        }
        if (process)
        {
            CloseHandle(process);
        }
    }
    return cache_icon(icon, revision, path);
}

bool cache_thumbnail(
    const winrt::Windows::Storage::Streams::IRandomAccessStreamReference& reference,
    std::uint64_t& revision,
    std::string& path,
    bool normalizeToPng = false)
{
    if (!reference)
    {
        return false;
    }
    std::vector<std::uint8_t> bytes;
    bool encodedAsPng = false;
    if (normalizeToPng)
    {
        try
        {
            const auto source = reference.OpenReadAsync().get();
            const auto decoder = BitmapDecoder::CreateAsync(source).get();
            const auto bitmap = decoder.GetSoftwareBitmapAsync(
                BitmapPixelFormat::Bgra8,
                BitmapAlphaMode::Premultiplied).get();
            InMemoryRandomAccessStream encoded;
            const auto encoder = BitmapEncoder::CreateAsync(
                BitmapEncoder::PngEncoderId(),
                encoded).get();
            encoder.SetSoftwareBitmap(bitmap);
            encoder.FlushAsync().get();
            bytes = read_stream_bytes(encoded);
            encodedAsPng = !bytes.empty();
        }
        catch (...)
        {
            // Some package resources are already encoded vector/bitmap data
            // that the imaging codec cannot transcode. Preserve those bytes.
        }
    }
    if (bytes.empty())
    {
        bytes = read_stream_bytes(reference.OpenReadAsync().get());
    }
    if (bytes.empty())
    {
        return false;
    }
    // A package resource stream can be an opaque PRI payload. Caching that as
    // `.img` reports success to the app even though no media decoder can open
    // it, preventing the native window/executable fallback below.
    const std::wstring rawExtension = thumbnail_extension(bytes);
    if (normalizeToPng && !encodedAsPng &&
        (rawExtension == L".img" || rawExtension == L".ico"))
    {
        return false;
    }

    return cache_encoded_thumbnail(
        bytes,
        revision,
        path,
        encodedAsPng ? L".png" : rawExtension);
}

std::uint32_t playback_status(
    GlobalSystemMediaTransportControlsSessionPlaybackStatus status)
{
    switch (status)
    {
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Closed:
            return VIBRANCE_WIN32_MEDIA_CLOSED;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Opened:
            return VIBRANCE_WIN32_MEDIA_OPENED;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Changing:
            return VIBRANCE_WIN32_MEDIA_CHANGING;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Stopped:
            return VIBRANCE_WIN32_MEDIA_STOPPED;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Playing:
            return VIBRANCE_WIN32_MEDIA_PLAYING;
        case GlobalSystemMediaTransportControlsSessionPlaybackStatus::Paused:
            return VIBRANCE_WIN32_MEDIA_PAUSED;
        default:
            return VIBRANCE_WIN32_MEDIA_UNKNOWN;
    }
}

bool same_payload(
    const VibranceWin32MediaSnapshot& left,
    const VibranceWin32MediaSnapshot& right)
{
    return
        left.serviceReady == right.serviceReady &&
        left.sessionAvailable == right.sessionAvailable &&
        left.playbackStatus == right.playbackStatus &&
        left.capabilities == right.capabilities &&
        left.sourceProcessId == right.sourceProcessId &&
        left.thumbnailRevision == right.thumbnailRevision &&
        left.positionMilliseconds == right.positionMilliseconds &&
        left.durationMilliseconds == right.durationMilliseconds &&
        std::strcmp(left.sourceApp, right.sourceApp) == 0 &&
        std::strcmp(left.title, right.title) == 0 &&
        std::strcmp(left.artist, right.artist) == 0 &&
        std::strcmp(left.album, right.album) == 0 &&
        std::strcmp(left.thumbnailPath, right.thumbnailPath) == 0 &&
        std::strcmp(left.diagnostic, right.diagnostic) == 0;
}

std::uint32_t notification_access_status(
    UserNotificationListenerAccessStatus status)
{
    switch (status)
    {
        case UserNotificationListenerAccessStatus::Allowed:
            return VIBRANCE_WIN32_NOTIFICATIONS_ALLOWED;
        case UserNotificationListenerAccessStatus::Denied:
            return VIBRANCE_WIN32_NOTIFICATIONS_DENIED;
        case UserNotificationListenerAccessStatus::Unspecified:
        default:
            return VIBRANCE_WIN32_NOTIFICATIONS_UNSPECIFIED;
    }
}

bool same_notification_item(
    const VibranceWin32NotificationItem& left,
    const VibranceWin32NotificationItem& right)
{
    return left.notificationId == right.notificationId &&
        left.creationUnixMilliseconds == right.creationUnixMilliseconds &&
        left.iconRevision == right.iconRevision &&
        std::strcmp(left.sourceApp, right.sourceApp) == 0 &&
        std::strcmp(left.appUserModelId, right.appUserModelId) == 0 &&
        std::strcmp(left.title, right.title) == 0 &&
        std::strcmp(left.message, right.message) == 0 &&
        std::strcmp(left.iconPath, right.iconPath) == 0;
}

class Win32InteropBridge
{
public:
    explicit Win32InteropBridge(
        std::uint32_t enabledFeatures = VIBRANCE_WIN32_INTEROP_MEDIA) :
        featureMask(enabledFeatures)
    {
        snapshot.structSize = sizeof(snapshot);
        notificationSnapshot.structSize = sizeof(notificationSnapshot);
        worker = std::thread(&Win32InteropBridge::run, this);
    }

    ~Win32InteropBridge()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        if (worker.joinable())
        {
            worker.join();
        }
    }

    bool get_snapshot(
        VibranceWin32MediaSnapshot* destination,
        std::uint32_t destinationSize)
    {
        if (!destination ||
            destinationSize < sizeof(VibranceWin32MediaSnapshot))
        {
            return false;
        }

        std::lock_guard lock(mutex);
        *destination = snapshot;
        return true;
    }

    bool enqueue(std::uint32_t command)
    {
        if (command < VIBRANCE_WIN32_MEDIA_TOGGLE_PLAY_PAUSE ||
            command > VIBRANCE_WIN32_MEDIA_SKIP_PREVIOUS)
        {
            return false;
        }

        {
            std::lock_guard lock(mutex);
            if (stopping)
            {
                return false;
            }
            commands.push_back(command);
        }
        wake.notify_one();
        return true;
    }

    bool enqueue_seek(std::int64_t positionMilliseconds)
    {
        {
            std::lock_guard lock(mutex);
            if (stopping)
            {
                return false;
            }
            pendingSeekPosition = std::max(positionMilliseconds, std::int64_t(0));
        }
        wake.notify_one();
        return true;
    }

    bool request_notification_access()
    {
        if ((featureMask & VIBRANCE_WIN32_INTEROP_NOTIFICATIONS) == 0u)
        {
            return false;
        }
        {
            std::lock_guard lock(mutex);
            if (stopping)
            {
                return false;
            }
            notificationPolling = true;
        }
        wake.notify_one();

        try
        {
            struct CallingThreadApartment
            {
                CallingThreadApartment()
                {
                    winrt::init_apartment(
                        winrt::apartment_type::multi_threaded);
                }
                ~CallingThreadApartment()
                {
                    winrt::uninit_apartment();
                }
            } apartment;
            const UserNotificationListener listener =
                UserNotificationListener::Current();
            VibranceWin32NotificationSnapshot next {};
            next.structSize = sizeof(next);
            next.serviceReady = 1u;
            next.accessStatus = notification_access_status(
                listener.RequestAccessAsync().get());
            publish_notifications(next, {});
            return true;
        }
        catch (const winrt::hresult_error& error)
        {
            publish_notification_error(error.message());
        }
        catch (const std::exception& error)
        {
            publish_notification_error(error.what());
        }
        catch (...)
        {
            publish_notification_error(
                "Windows could not request notification access.");
        }
        return false;
    }

    bool get_notifications(
        VibranceWin32NotificationSnapshot* destination,
        std::uint32_t destinationSize,
        VibranceWin32NotificationItem* items,
        std::uint32_t itemSize,
        std::uint32_t capacity)
    {
        if ((featureMask & VIBRANCE_WIN32_INTEROP_NOTIFICATIONS) == 0u ||
            !destination || destinationSize < sizeof(*destination) ||
            itemSize < sizeof(VibranceWin32NotificationItem) ||
            (capacity > 0u && !items))
        {
            return false;
        }
        {
            std::lock_guard lock(mutex);
            notificationPolling = true;
            *destination = notificationSnapshot;
            const std::uint32_t copied = std::min<std::uint32_t>(
                capacity,
                static_cast<std::uint32_t>(notificationItems.size()));
            destination->itemCount = copied;
            for (std::uint32_t index = 0u; index < copied; ++index)
            {
                items[index] = notificationItems[index];
            }
        }
        return true;
    }

    bool enqueue_notification_dismissal(std::uint32_t notificationId)
    {
        if ((featureMask & VIBRANCE_WIN32_INTEROP_NOTIFICATIONS) == 0u)
        {
            return false;
        }
        {
            std::lock_guard lock(mutex);
            if (stopping)
            {
                return false;
            }
            notificationPolling = true;
            notificationDismissals.push_back(notificationId);
        }
        wake.notify_one();
        return true;
    }

    bool enqueue_notification_activation(const char* appUserModelId)
    {
        if ((featureMask & VIBRANCE_WIN32_INTEROP_NOTIFICATIONS) == 0u ||
            !appUserModelId || appUserModelId[0] == '\0')
        {
            return false;
        }
        {
            std::lock_guard lock(mutex);
            if (stopping)
            {
                return false;
            }
            notificationActivations.emplace_back(appUserModelId);
        }
        wake.notify_one();
        return true;
    }

    bool activate_notification_source(const char* appUserModelId)
    {
        return (featureMask & VIBRANCE_WIN32_INTEROP_NOTIFICATIONS) != 0u &&
            appUserModelId && appUserModelId[0] != '\0' &&
            activate_application(appUserModelId);
    }

    bool resolve_application_icon(
        const char* appUserModelId,
        VibranceWin32ApplicationIcon* destination,
        std::uint32_t destinationSize)
    {
        if ((featureMask & VIBRANCE_WIN32_INTEROP_NOTIFICATIONS) == 0u ||
            !appUserModelId || appUserModelId[0] == '\0' ||
            !destination || destinationSize < sizeof(*destination))
        {
            return false;
        }
        *destination = {};
        destination->structSize = sizeof(*destination);
        const winrt::hstring identity =
            winrt::to_hstring(appUserModelId);
        std::uint64_t revision = 0u;
        std::string path;
        bool loaded = false;
        try
        {
            const auto appInfo = winrt::Windows::ApplicationModel::AppInfo::
                GetFromAppUserModelId(identity);
            if (appInfo)
            {
                loaded = cache_thumbnail(
                    appInfo.DisplayInfo().GetLogo({ 64.0f, 64.0f }),
                    revision,
                    path,
                    true);
            }
        }
        catch (...)
        {
            // Desktop registrations are not guaranteed to expose a WinRT
            // logo. The AppsFolder and live-window fallbacks cover them.
        }
        if (!loaded)
        {
            loaded = cache_registered_application_icon(
                identity.c_str(),
                revision,
                path);
        }
        if (!loaded)
        {
            loaded = cache_window_icon(
                find_source_window(identity.c_str()),
                revision,
                path);
        }
        if (!loaded)
        {
            return false;
        }
        destination->revision = revision;
        copy_utf8(destination->path, path);
        return destination->path[0] != '\0';
    }

private:
    void publish(VibranceWin32MediaSnapshot next)
    {
        std::lock_guard lock(mutex);
        next.structSize = sizeof(next);
        if (same_payload(snapshot, next))
        {
            return;
        }
        next.revision = snapshot.revision + 1u;
        snapshot = next;
    }

    void publish_error(const winrt::hresult_error& error)
    {
        VibranceWin32MediaSnapshot next {};
        next.structSize = sizeof(next);
        copy_utf8(next.diagnostic, error.message());
        publish(next);
    }

    void publish_error(const std::string& message)
    {
        VibranceWin32MediaSnapshot next {};
        next.structSize = sizeof(next);
        copy_utf8(next.diagnostic, message);
        publish(next);
    }

    void publish_notifications(
        VibranceWin32NotificationSnapshot next,
        std::vector<VibranceWin32NotificationItem> nextItems)
    {
        std::lock_guard lock(mutex);
        next.structSize = sizeof(next);
        next.itemCount = static_cast<std::uint32_t>(nextItems.size());
        const bool sameHeader =
            notificationSnapshot.serviceReady == next.serviceReady &&
            notificationSnapshot.accessStatus == next.accessStatus &&
            std::strcmp(
                notificationSnapshot.diagnostic,
                next.diagnostic) == 0;
        const bool sameItems = sameHeader &&
            notificationItems.size() == nextItems.size() &&
            std::equal(
                notificationItems.begin(),
                notificationItems.end(),
                nextItems.begin(),
                same_notification_item);
        if (sameItems)
        {
            return;
        }
        next.revision = notificationSnapshot.revision + 1u;
        notificationSnapshot = next;
        notificationItems = std::move(nextItems);
    }

    void publish_notification_error(const winrt::hstring& message)
    {
        VibranceWin32NotificationSnapshot next {};
        next.structSize = sizeof(next);
        next.accessStatus = VIBRANCE_WIN32_NOTIFICATIONS_ERROR;
        copy_utf8(next.diagnostic, message);
        publish_notifications(next, {});
    }

    void publish_notification_error(const std::string& message)
    {
        VibranceWin32NotificationSnapshot next {};
        next.structSize = sizeof(next);
        next.accessStatus = VIBRANCE_WIN32_NOTIFICATIONS_ERROR;
        copy_utf8(next.diagnostic, message);
        publish_notifications(next, {});
    }

    void poll_notifications(const UserNotificationListener& listener)
    {
        VibranceWin32NotificationSnapshot next {};
        next.structSize = sizeof(next);
        next.serviceReady = 1u;
        const UserNotificationListenerAccessStatus status =
            listener.GetAccessStatus();
        next.accessStatus = notification_access_status(status);
        if (status != UserNotificationListenerAccessStatus::Allowed)
        {
            publish_notifications(next, {});
            return;
        }

        const auto notifications = listener.GetNotificationsAsync(
            NotificationKinds::Toast).get();
        std::vector<VibranceWin32NotificationItem> items;
        items.reserve(notifications.Size());
        for (const auto& notification : notifications)
        {
            try
            {
                VibranceWin32NotificationItem item {};
                item.structSize = sizeof(item);
                item.notificationId = notification.Id();
                constexpr std::int64_t windowsToUnixMilliseconds =
                    11'644'473'600'000ll;
                item.creationUnixMilliseconds =
                    notification.CreationTime().time_since_epoch().count() /
                        10'000ll -
                    windowsToUnixMilliseconds;
                const auto appInfo = notification.AppInfo();
                const auto displayInfo = appInfo.DisplayInfo();
                copy_utf8(item.sourceApp, displayInfo.DisplayName());
                const winrt::hstring appUserModelId =
                    appInfo.AppUserModelId();
                copy_utf8(item.appUserModelId, appUserModelId);

                std::string iconCacheKey = winrt::to_string(
                    appInfo.AppUserModelId());
                if (iconCacheKey.empty())
                {
                    iconCacheKey = item.sourceApp;
                }
                const auto cachedIcon = notificationIconCache.find(
                    iconCacheKey);
                if (cachedIcon != notificationIconCache.end() &&
                    !cachedIcon->second.path.empty() &&
                    std::filesystem::exists(cachedIcon->second.path))
                {
                    item.iconRevision = cachedIcon->second.revision;
                    copy_utf8(item.iconPath, cachedIcon->second.path);
                }
                else
                {
                    if (cachedIcon != notificationIconCache.end())
                    {
                        notificationIconCache.erase(cachedIcon);
                    }
                    CachedNotificationIcon icon = {};
                    bool loadedIcon = false;
                    try
                    {
                        loadedIcon = cache_thumbnail(
                            displayInfo.GetLogo({ 64.0f, 64.0f }),
                            icon.revision,
                            icon.path,
                            true);
                    }
                    catch (...)
                    {
                        // Continue through the window/executable icon path.
                    }
                    if (!loadedIcon && !appUserModelId.empty())
                    {
                        loadedIcon = cache_registered_application_icon(
                            appUserModelId.c_str(),
                            icon.revision,
                            icon.path);
                    }
                    if (!loadedIcon && !appUserModelId.empty())
                    {
                        loadedIcon = cache_window_icon(
                            find_source_window(appUserModelId.c_str()),
                            icon.revision,
                            icon.path);
                    }
                    if (loadedIcon)
                    {
                        item.iconRevision = icon.revision;
                        copy_utf8(item.iconPath, icon.path);
                        notificationIconCache.emplace(
                            std::move(iconCacheKey),
                            std::move(icon));
                    }
                }

                const auto binding = notification.Notification().Visual().
                    GetBinding(KnownNotificationBindings::ToastGeneric());
                if (binding)
                {
                    const auto textElements = binding.GetTextElements();
                    if (textElements.Size() > 0u)
                    {
                        copy_utf8(item.title, textElements.GetAt(0u).Text());
                    }
                    std::string body;
                    for (std::uint32_t index = 1u;
                        index < textElements.Size(); ++index)
                    {
                        if (!body.empty())
                        {
                            body += '\n';
                        }
                        body += winrt::to_string(
                            textElements.GetAt(index).Text());
                    }
                    copy_utf8(item.message, body);
                }
                if (item.title[0] == '\0')
                {
                    copy_utf8(item.title, notification.AppInfo().
                        DisplayInfo().DisplayName());
                }
                items.push_back(item);
            }
            catch (...)
            {
                // One malformed or concurrently removed toast must not hide
                // the other notifications in the same snapshot.
            }
        }
        std::stable_sort(
            items.begin(),
            items.end(),
            [](const auto& left, const auto& right) {
                if (left.creationUnixMilliseconds !=
                    right.creationUnixMilliseconds)
                {
                    return left.creationUnixMilliseconds >
                        right.creationUnixMilliseconds;
                }
                return left.notificationId > right.notificationId;
            });
        publish_notifications(next, std::move(items));
    }

    void poll(
        const GlobalSystemMediaTransportControlsSessionManager& manager)
    {
        VibranceWin32MediaSnapshot next {};
        next.structSize = sizeof(next);
        next.serviceReady = 1u;

        const GlobalSystemMediaTransportControlsSession session =
            manager.GetCurrentSession();
        if (!session)
        {
            publish(next);
            return;
        }

        next.sessionAvailable = 1u;
        const winrt::hstring sourceApp = session.SourceAppUserModelId();
        copy_utf8(next.sourceApp, sourceApp);
        {
            std::lock_guard lock(mutex);
            if (std::strcmp(snapshot.sourceApp, next.sourceApp) == 0 &&
                process_alive(snapshot.sourceProcessId))
            {
                next.sourceProcessId = snapshot.sourceProcessId;
            }
        }
        if (next.sourceProcessId == 0u)
        {
            next.sourceProcessId = resolve_media_process_id(sourceApp.c_str());
        }

        const auto mediaProperties =
            session.TryGetMediaPropertiesAsync().get();
        copy_utf8(next.title, mediaProperties.Title());
        copy_utf8(next.artist, mediaProperties.Artist());
        copy_utf8(next.album, mediaProperties.AlbumTitle());
        bool reuseThumbnail = false;
        {
            std::lock_guard lock(mutex);
            reuseThumbnail =
                std::strcmp(snapshot.sourceApp, next.sourceApp) == 0 &&
                std::strcmp(snapshot.title, next.title) == 0 &&
                std::strcmp(snapshot.artist, next.artist) == 0 &&
                std::strcmp(snapshot.album, next.album) == 0 &&
                snapshot.thumbnailRevision != 0u;
            if (reuseThumbnail)
            {
                next.thumbnailRevision = snapshot.thumbnailRevision;
                std::memcpy(next.thumbnailPath, snapshot.thumbnailPath, sizeof(next.thumbnailPath));
            }
        }
        if (!reuseThumbnail)
        {
            std::uint64_t thumbnailRevision = 0u;
            std::string thumbnailPath;
            if (cache_thumbnail(
                    mediaProperties.Thumbnail(),
                    thumbnailRevision,
                    thumbnailPath))
            {
                next.thumbnailRevision = thumbnailRevision;
                copy_utf8(next.thumbnailPath, thumbnailPath);
            }
        }

        const auto playbackInfo = session.GetPlaybackInfo();
        next.playbackStatus =
            playback_status(playbackInfo.PlaybackStatus());
        const auto controls = playbackInfo.Controls();
        if (controls.IsPlayPauseToggleEnabled())
        {
            next.capabilities |=
                VIBRANCE_WIN32_MEDIA_CAN_PLAY_PAUSE;
        }
        if (controls.IsNextEnabled())
        {
            next.capabilities |= VIBRANCE_WIN32_MEDIA_CAN_SKIP_NEXT;
        }
        if (controls.IsPreviousEnabled())
        {
            next.capabilities |=
                VIBRANCE_WIN32_MEDIA_CAN_SKIP_PREVIOUS;
        }

        const auto timeline = session.GetTimelineProperties();
        const std::int64_t startMilliseconds =
            milliseconds(timeline.StartTime());
        next.positionMilliseconds =
            std::max(
                milliseconds(timeline.Position()) - startMilliseconds,
                std::int64_t(0));
        next.durationMilliseconds =
            std::max(
                milliseconds(timeline.EndTime()) - startMilliseconds,
                std::int64_t(0));
        publish(next);
    }

    static void execute(
        const GlobalSystemMediaTransportControlsSessionManager& manager,
        std::uint32_t command)
    {
        const GlobalSystemMediaTransportControlsSession session =
            manager.GetCurrentSession();
        if (!session)
        {
            return;
        }

        switch (command)
        {
            case VIBRANCE_WIN32_MEDIA_TOGGLE_PLAY_PAUSE:
                session.TryTogglePlayPauseAsync().get();
                break;
            case VIBRANCE_WIN32_MEDIA_SKIP_NEXT:
                session.TrySkipNextAsync().get();
                break;
            case VIBRANCE_WIN32_MEDIA_SKIP_PREVIOUS:
                session.TrySkipPreviousAsync().get();
                break;
            default:
                break;
        }
    }

    static void execute_seek(
        const GlobalSystemMediaTransportControlsSessionManager& manager,
        std::int64_t positionMilliseconds)
    {
        const GlobalSystemMediaTransportControlsSession session =
            manager.GetCurrentSession();
        if (!session)
        {
            return;
        }
        const std::int64_t startMilliseconds =
            milliseconds(session.GetTimelineProperties().StartTime());
        constexpr std::int64_t ticksPerMillisecond = 10'000;
        const std::int64_t targetTicks =
            (startMilliseconds +
                std::max(positionMilliseconds, std::int64_t(0))) *
            ticksPerMillisecond;
        session.TryChangePlaybackPositionAsync(targetTicks).get();
    }

    static bool equal_aumid(
        std::wstring_view left,
        std::wstring_view right)
    {
        return left.size() == right.size() &&
            CompareStringOrdinal(
                left.data(),
                static_cast<int>(left.size()),
                right.data(),
                static_cast<int>(right.size()),
                TRUE) == CSTR_EQUAL;
    }

    static std::wstring process_aumid(DWORD processId)
    {
        const HANDLE process = OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId);
        if (!process)
        {
            return {};
        }
        UINT32 length = 0u;
        const LONG sizeResult = GetApplicationUserModelId(
            process,
            &length,
            nullptr);
        if (sizeResult != ERROR_INSUFFICIENT_BUFFER || length == 0u)
        {
            CloseHandle(process);
            return {};
        }
        std::wstring value(length, L'\0');
        const LONG valueResult = GetApplicationUserModelId(
            process,
            &length,
            value.data());
        CloseHandle(process);
        if (valueResult != ERROR_SUCCESS)
        {
            return {};
        }
        if (!value.empty() && value.back() == L'\0')
        {
            value.pop_back();
        }
        return value;
    }

    static std::wstring process_executable_path(DWORD processId)
    {
        const HANDLE process = processId != 0u ? OpenProcess(
            PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId) : nullptr;
        if (!process)
        {
            return {};
        }
        std::wstring path(32768u, L'\0');
        DWORD length = static_cast<DWORD>(path.size());
        const bool queried = QueryFullProcessImageNameW(
            process,
            0u,
            path.data(),
            &length) != FALSE;
        CloseHandle(process);
        if (!queried)
        {
            return {};
        }
        path.resize(length);
        return path;
    }

    static bool process_alive(DWORD processId)
    {
        if (processId == 0u)
        {
            return false;
        }
        const HANDLE process = OpenProcess(
            SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION,
            FALSE,
            processId);
        if (!process)
        {
            return false;
        }
        const bool alive = WaitForSingleObject(process, 0u) == WAIT_TIMEOUT;
        CloseHandle(process);
        return alive;
    }

    static std::wstring executable_file_name(std::wstring_view path)
    {
        const std::size_t separator = path.find_last_of(L"\\/");
        return std::wstring(separator == std::wstring_view::npos ?
            path : path.substr(separator + 1u));
    }

    static bool source_matches_process(
        std::wstring_view sourceApp,
        DWORD processId)
    {
        if (sourceApp.empty() || processId == 0u)
        {
            return false;
        }
        const std::wstring appUserModelId = process_aumid(processId);
        if (!appUserModelId.empty() &&
            equal_aumid(appUserModelId, sourceApp))
        {
            return true;
        }
        const std::wstring executablePath =
            process_executable_path(processId);
        return !executablePath.empty() &&
            (equal_aumid(executablePath, sourceApp) ||
                equal_aumid(
                    executable_file_name(executablePath),
                    sourceApp));
    }

    struct ProcessEntry
    {
        DWORD processId = 0u;
    };

    static std::vector<ProcessEntry> process_snapshot()
    {
        std::vector<ProcessEntry> entries;
        const HANDLE snapshotHandle = CreateToolhelp32Snapshot(
            TH32CS_SNAPPROCESS,
            0u);
        if (snapshotHandle == INVALID_HANDLE_VALUE)
        {
            return entries;
        }
        PROCESSENTRY32W entry {};
        entry.dwSize = sizeof(entry);
        if (Process32FirstW(snapshotHandle, &entry))
        {
            do
            {
                entries.push_back({ entry.th32ProcessID });
            }
            while (Process32NextW(snapshotHandle, &entry));
        }
        CloseHandle(snapshotHandle);
        return entries;
    }

    static DWORD resolve_media_process_id(std::wstring_view sourceApp)
    {
        if (sourceApp.empty())
        {
            return 0u;
        }

        const std::vector<ProcessEntry> processes = process_snapshot();
        DWORD activeProcessId = 0u;
        DWORD inactiveProcessId = 0u;
        IMMDeviceEnumerator* deviceEnumerator = nullptr;
        IMMDeviceCollection* devices = nullptr;
        if (SUCCEEDED(CoCreateInstance(
                kClsidMmDeviceEnumerator,
                nullptr,
                CLSCTX_ALL,
                kIidMmDeviceEnumerator,
                reinterpret_cast<void**>(&deviceEnumerator))) &&
            SUCCEEDED(deviceEnumerator->EnumAudioEndpoints(
                eRender,
                DEVICE_STATE_ACTIVE,
                &devices)))
        {
            UINT deviceCount = 0u;
            devices->GetCount(&deviceCount);
            for (UINT deviceIndex = 0u;
                deviceIndex < deviceCount && activeProcessId == 0u;
                ++deviceIndex)
            {
                IMMDevice* device = nullptr;
                IAudioSessionManager2* sessionManager = nullptr;
                IAudioSessionEnumerator* sessions = nullptr;
                if (SUCCEEDED(devices->Item(deviceIndex, &device)) &&
                    SUCCEEDED(device->Activate(
                        kIidAudioSessionManager2,
                        CLSCTX_ALL,
                        nullptr,
                        reinterpret_cast<void**>(&sessionManager))) &&
                    SUCCEEDED(sessionManager->GetSessionEnumerator(&sessions)))
                {
                    int sessionCount = 0;
                    sessions->GetCount(&sessionCount);
                    for (int sessionIndex = 0;
                        sessionIndex < sessionCount;
                        ++sessionIndex)
                    {
                        IAudioSessionControl* sessionControl = nullptr;
                        IAudioSessionControl2* sessionControl2 = nullptr;
                        if (SUCCEEDED(sessions->GetSession(
                                sessionIndex,
                                &sessionControl)) &&
                            SUCCEEDED(sessionControl->QueryInterface(
                                kIidAudioSessionControl2,
                                reinterpret_cast<void**>(&sessionControl2))))
                        {
                            DWORD processId = 0u;
                            AudioSessionState state =
                                AudioSessionStateInactive;
                            sessionControl2->GetProcessId(&processId);
                            sessionControl2->GetState(&state);
                            if (source_matches_process(
                                    sourceApp,
                                    processId))
                            {
                                if (state == AudioSessionStateActive)
                                {
                                    activeProcessId = processId;
                                }
                                else if (inactiveProcessId == 0u)
                                {
                                    inactiveProcessId = processId;
                                }
                            }
                        }
                        release_com(sessionControl2);
                        release_com(sessionControl);
                        if (activeProcessId != 0u)
                        {
                            break;
                        }
                    }
                }
                release_com(sessions);
                release_com(sessionManager);
                release_com(device);
            }
        }
        release_com(devices);
        release_com(deviceEnumerator);

        const DWORD sessionProcessId = activeProcessId != 0u ?
            activeProcessId : inactiveProcessId;
        if (sessionProcessId != 0u)
        {
            // Keep the audio-session PID exact. Promoting it to the app's
            // root process would include sibling browser tabs and unrelated
            // helpers in the process-loopback tree.
            return sessionProcessId;
        }

        // A paused source may not retain an audio session. Resolve its root
        // process by the same AUMID/executable identity without broadening
        // capture to unrelated output.
        for (const ProcessEntry& entry : processes)
        {
            if (!source_matches_process(sourceApp, entry.processId))
            {
                continue;
            }
            return entry.processId;
        }
        return 0u;
    }

    static std::wstring registered_application_executable_path(
        std::wstring_view appUserModelId)
    {
        if (appUserModelId.empty())
        {
            return {};
        }
        const std::wstring parsingName =
            L"shell:AppsFolder\\" + std::wstring(appUserModelId);
        winrt::com_ptr<IShellItem2> item;
        if (FAILED(SHCreateItemFromParsingName(
                parsingName.c_str(),
                nullptr,
                IID_PPV_ARGS(item.put()))))
        {
            return {};
        }
        PWSTR rawPath = nullptr;
        if (FAILED(item->GetString(
                PKEY_Link_TargetParsingPath,
                &rawPath)) || !rawPath)
        {
            return {};
        }
        std::wstring path(rawPath);
        CoTaskMemFree(rawPath);
        return path;
    }

    static std::wstring window_aumid(HWND window)
    {
        winrt::com_ptr<IPropertyStore> properties;
        if (SUCCEEDED(SHGetPropertyStoreForWindow(
                window,
                IID_PPV_ARGS(properties.put()))))
        {
            PROPVARIANT value;
            PropVariantInit(&value);
            if (SUCCEEDED(properties->GetValue(
                    PKEY_AppUserModel_ID,
                    &value)) &&
                value.vt == VT_LPWSTR && value.pwszVal)
            {
                std::wstring result(value.pwszVal);
                PropVariantClear(&value);
                return result;
            }
            PropVariantClear(&value);
        }
        DWORD processId = 0u;
        GetWindowThreadProcessId(window, &processId);
        return process_aumid(processId);
    }

    struct SourceWindowSearch
    {
        std::wstring appUserModelId;
        std::wstring executablePath;
        DWORD processId = 0u;
        HWND result = nullptr;
    };

    static BOOL CALLBACK find_source_window_callback(
        HWND window,
        LPARAM parameter)
    {
        auto* search = reinterpret_cast<SourceWindowSearch*>(parameter);
        if (!search || !IsWindowVisible(window) ||
            GetAncestor(window, GA_ROOT) != window)
        {
            return TRUE;
        }
        DWORD processId = 0u;
        GetWindowThreadProcessId(window, &processId);
        const bool processMatches = search->processId != 0u &&
            processId == search->processId;
        const bool identityMatches = !search->appUserModelId.empty() &&
            equal_aumid(window_aumid(window), search->appUserModelId);
        const bool executableMatches = !search->executablePath.empty() &&
            equal_aumid(
                process_executable_path(processId),
                search->executablePath);
        if (!processMatches && !identityMatches && !executableMatches)
        {
            return TRUE;
        }
        search->result = window;
        return FALSE;
    }

    static HWND find_source_window(
        std::wstring_view appUserModelId,
        DWORD processId = 0u,
        std::wstring_view executablePath = {})
    {
        SourceWindowSearch search = {};
        search.appUserModelId = appUserModelId;
        search.executablePath = executablePath;
        search.processId = processId;
        EnumWindows(
            find_source_window_callback,
            reinterpret_cast<LPARAM>(&search));
        return search.result;
    }

    static bool foreground_source_window(HWND window)
    {
        if (!window)
        {
            return false;
        }
        DWORD processId = 0u;
        const DWORD targetThread =
            GetWindowThreadProcessId(window, &processId);
        if (processId != 0u)
        {
            AllowSetForegroundWindow(processId);
        }
        const DWORD callingThread = GetCurrentThreadId();
        const HWND foregroundWindow = GetForegroundWindow();
        const DWORD foregroundThread = foregroundWindow ?
            GetWindowThreadProcessId(foregroundWindow, nullptr) : 0u;
        const bool attachedForeground = foregroundThread != 0u &&
            foregroundThread != callingThread &&
            AttachThreadInput(
                callingThread,
                foregroundThread,
                TRUE) != FALSE;
        const bool attachedTarget = targetThread != 0u &&
            targetThread != callingThread &&
            targetThread != foregroundThread &&
            AttachThreadInput(
                callingThread,
                targetThread,
                TRUE) != FALSE;
        if (IsIconic(window))
        {
            ShowWindow(window, SW_RESTORE);
        }
        else
        {
            ShowWindow(window, SW_SHOW);
        }
        BringWindowToTop(window);
        const bool raised = SetWindowPos(
            window,
            HWND_TOP,
            0,
            0,
            0,
            0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW) != FALSE;
        bool foregrounded = SetForegroundWindow(window) != FALSE ||
            GetForegroundWindow() == window;
        if (!foregrounded)
        {
            // Electron and other multi-process desktop applications can
            // reject SetForegroundWindow even while their input queues are
            // attached. SwitchToThisWindow is the Shell-compatible fallback
            // used for an explicit user selection such as a notification.
            SwitchToThisWindow(window, TRUE);
            foregrounded = GetForegroundWindow() == window;
        }
        bool forcedAbove = false;
        if (!foregrounded)
        {
            // A no-activate notification surface may not receive Windows'
            // foreground privilege even though the user clicked it. Briefly
            // promote and immediately demote the source so it becomes visible
            // without leaving the third-party window always-on-top.
            const bool promoted = SetWindowPos(
                window,
                HWND_TOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW) != FALSE;
            const bool demoted = SetWindowPos(
                window,
                HWND_NOTOPMOST,
                0,
                0,
                0,
                0,
                SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW) != FALSE;
            forcedAbove = promoted && demoted;
        }
        if (attachedTarget)
        {
            AttachThreadInput(callingThread, targetThread, FALSE);
        }
        if (attachedForeground)
        {
            AttachThreadInput(callingThread, foregroundThread, FALSE);
        }
        return foregrounded || forcedAbove || raised;
    }

    static bool activate_application(const std::string& appUserModelId)
    {
        if (appUserModelId.empty())
        {
            return false;
        }
        const winrt::hstring target = winrt::to_hstring(appUserModelId);
        const std::wstring registeredExecutable =
            registered_application_executable_path(target.c_str());
        if (foreground_source_window(find_source_window(
                target.c_str(),
                0u,
                registeredExecutable)))
        {
            return true;
        }

        DWORD processId = 0u;
        bool activationRequested = false;
        try
        {
            winrt::com_ptr<IApplicationActivationManager> activationManager;
            winrt::check_hresult(CoCreateInstance(
                CLSID_ApplicationActivationManager,
                nullptr,
                CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(activationManager.put())));
            (void)CoAllowSetForegroundWindow(
                activationManager.get(),
                nullptr);
            winrt::check_hresult(activationManager->ActivateApplication(
                target.c_str(),
                nullptr,
                AO_NONE,
                &processId));
            activationRequested = true;
        }
        catch (...)
        {
            // Some unpackaged desktop applications register an AppsFolder
            // entry but reject IApplicationActivationManager. ShellExecute
            // is the native fallback for that same AppUserModelId.
        }
        if (!activationRequested)
        {
            const std::wstring shellTarget =
                L"shell:AppsFolder\\" + std::wstring(target.c_str());
            const HINSTANCE result = ShellExecuteW(
                nullptr,
                L"open",
                shellTarget.c_str(),
                nullptr,
                nullptr,
                SW_SHOWNORMAL);
            activationRequested =
                reinterpret_cast<INT_PTR>(result) > 32;
        }
        if (!activationRequested)
        {
            return false;
        }
        std::wstring executablePath = registeredExecutable;
        if (executablePath.empty() && processId != 0u)
        {
            executablePath = process_executable_path(processId);
        }
        for (std::size_t attempt = 0u; attempt < 20u; ++attempt)
        {
            HWND window = find_source_window(
                target.c_str(),
                processId,
                executablePath);
            if (window && foreground_source_window(window))
            {
                return true;
            }
            std::this_thread::sleep_for(40ms);
        }
        // The native activation request was accepted even if the application
        // has not created a discoverable top-level window yet.
        return true;
    }

    bool take_work(
        std::deque<std::uint32_t>& pending,
        std::optional<std::int64_t>& seekPosition,
        std::deque<std::uint32_t>& dismissals,
        std::deque<std::string>& activations,
        bool& shouldPollNotifications)
    {
        std::lock_guard lock(mutex);
        pending.swap(commands);
        seekPosition.swap(pendingSeekPosition);
        dismissals.swap(notificationDismissals);
        activations.swap(notificationActivations);
        shouldPollNotifications = notificationPolling;
        notificationChangePending = false;
        return stopping;
    }

    void signal_notification_change()
    {
        {
            std::lock_guard lock(mutex);
            notificationChangePending = true;
        }
        wake.notify_one();
    }

    bool wait_for_work()
    {
        std::unique_lock lock(mutex);
        wake.wait_for(
            lock,
            500ms,
            [this] {
                return stopping || !commands.empty() ||
                    pendingSeekPosition.has_value() ||
                    !notificationDismissals.empty() ||
                    !notificationActivations.empty() ||
                    notificationChangePending;
            });
        return stopping;
    }

    void run()
    {
        try
        {
            struct ApartmentScope
            {
                ApartmentScope()
                {
                    winrt::init_apartment(
                        winrt::apartment_type::multi_threaded);
                }

                ~ApartmentScope()
                {
                    winrt::uninit_apartment();
                }
            } apartment;

            GlobalSystemMediaTransportControlsSessionManager manager = nullptr;
            UserNotificationListener notificationListener = nullptr;
            winrt::event_token notificationChangedToken {};
            bool notificationChangedSubscribed = false;
            if ((featureMask & VIBRANCE_WIN32_INTEROP_MEDIA) != 0u)
            {
                try
                {
                    manager = GlobalSystemMediaTransportControlsSessionManager::
                        RequestAsync().get();
                }
                catch (const winrt::hresult_error& error)
                {
                    publish_error(error);
                }
            }

            while (true)
            {
                std::deque<std::uint32_t> pending;
                std::optional<std::int64_t> seekPosition;
                std::deque<std::uint32_t> dismissals;
                std::deque<std::string> activations;
                bool shouldPollNotifications = false;
                if (take_work(
                        pending,
                        seekPosition,
                        dismissals,
                        activations,
                        shouldPollNotifications))
                {
                    break;
                }
                if (manager && seekPosition)
                {
                    try
                    {
                        execute_seek(manager, *seekPosition);
                    }
                    catch (const winrt::hresult_error&)
                    {
                    }
                }

                for (const std::uint32_t command : pending)
                {
                    if (!manager)
                    {
                        break;
                    }
                    try
                    {
                        execute(manager, command);
                    }
                    catch (const winrt::hresult_error&)
                    {
                        // A session may disappear between a UI press and the
                        // worker processing it; the next poll publishes truth.
                    }
                }

                for (const std::string& appUserModelId : activations)
                {
                    try
                    {
                        activate_application(appUserModelId);
                    }
                    catch (const winrt::hresult_error&)
                    {
                        // The source may have been uninstalled or lost its
                        // activation registration after the toast was stored.
                    }
                }

                if (manager)
                {
                    try
                    {
                        poll(manager);
                    }
                    catch (const winrt::hresult_error& error)
                    {
                        publish_error(error);
                    }
                }

                if (shouldPollNotifications)
                {
                    try
                    {
                        if (!notificationListener)
                        {
                            notificationListener =
                                UserNotificationListener::Current();
                            notificationChangedToken =
                                notificationListener.NotificationChanged(
                                    [this](const auto&, const auto&) {
                                        signal_notification_change();
                                    });
                            notificationChangedSubscribed = true;
                        }
                        for (const std::uint32_t id : dismissals)
                        {
                            notificationListener.RemoveNotification(id);
                        }
                        poll_notifications(notificationListener);
                    }
                    catch (const winrt::hresult_error& error)
                    {
                        publish_notification_error(error.message());
                    }
                    catch (const std::exception& error)
                    {
                        publish_notification_error(error.what());
                    }
                }

                if (wait_for_work())
                {
                    break;
                }
            }
            if (notificationChangedSubscribed && notificationListener)
            {
                notificationListener.NotificationChanged(
                    notificationChangedToken);
            }
        }
        catch (const winrt::hresult_error& error)
        {
            publish_error(error);
        }
        catch (const std::exception& error)
        {
            publish_error(error.what());
        }
        catch (...)
        {
            publish_error("The GSMTC worker stopped unexpectedly.");
        }
    }

    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::uint32_t> commands;
    std::optional<std::int64_t> pendingSeekPosition;
    std::deque<std::uint32_t> notificationDismissals;
    std::deque<std::string> notificationActivations;
    VibranceWin32MediaSnapshot snapshot {};
    VibranceWin32NotificationSnapshot notificationSnapshot {};
    std::vector<VibranceWin32NotificationItem> notificationItems;
    struct CachedNotificationIcon
    {
        std::uint64_t revision = 0u;
        std::string path;
    };
    std::unordered_map<std::string, CachedNotificationIcon>
        notificationIconCache;
    bool notificationPolling = false;
    bool notificationChangePending = false;
    std::uint32_t featureMask = VIBRANCE_WIN32_INTEROP_MEDIA;
    bool stopping = false;
    std::thread worker;
};
}

#if defined(_M_IX86)
// Keep GetProcAddress names ABI-stable on 32-bit MSVC, where __cdecl symbols
// otherwise receive a leading underscore.
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_abi_version=_vibrance_win32_interop_abi_version")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_create=_vibrance_win32_interop_create")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_create_with_features=_vibrance_win32_interop_create_with_features")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_destroy=_vibrance_win32_interop_destroy")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_get_media_snapshot=_vibrance_win32_interop_get_media_snapshot")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_send_media_command=_vibrance_win32_interop_send_media_command")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_seek_media=_vibrance_win32_interop_seek_media")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_request_notification_access=_vibrance_win32_interop_request_notification_access")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_get_notifications=_vibrance_win32_interop_get_notifications")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_dismiss_notification=_vibrance_win32_interop_dismiss_notification")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_activate_notification_source=_vibrance_win32_interop_activate_notification_source")
#pragma comment(linker, "/EXPORT:vibrance_win32_interop_resolve_application_icon=_vibrance_win32_interop_resolve_application_icon")
#endif

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_win32_interop_abi_version()
{
    return VIBRANCE_WIN32_INTEROP_ABI_VERSION;
}

extern "C" __declspec(dllexport) VibranceWin32InteropHandle __cdecl
vibrance_win32_interop_create()
{
    try
    {
        return new (std::nothrow) Win32InteropBridge(
            VIBRANCE_WIN32_INTEROP_MEDIA);
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" __declspec(dllexport) VibranceWin32InteropHandle __cdecl
vibrance_win32_interop_create_with_features(std::uint32_t features)
{
    try
    {
        const std::uint32_t supported = features &
            (VIBRANCE_WIN32_INTEROP_MEDIA |
                VIBRANCE_WIN32_INTEROP_NOTIFICATIONS);
        return supported == 0u ? nullptr :
            new (std::nothrow) Win32InteropBridge(supported);
    }
    catch (...)
    {
        return nullptr;
    }
}

extern "C" __declspec(dllexport) void __cdecl
vibrance_win32_interop_destroy(VibranceWin32InteropHandle handle)
{
    delete static_cast<Win32InteropBridge*>(handle);
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_win32_interop_get_media_snapshot(
    VibranceWin32InteropHandle handle,
    VibranceWin32MediaSnapshot* snapshot,
    std::uint32_t snapshotSize)
{
    try
    {
        Win32InteropBridge* bridge =
            static_cast<Win32InteropBridge*>(handle);
        return bridge &&
            bridge->get_snapshot(snapshot, snapshotSize) ? 1u : 0u;
    }
    catch (...)
    {
        return 0u;
    }
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_win32_interop_send_media_command(
    VibranceWin32InteropHandle handle,
    std::uint32_t command)
{
    try
    {
        Win32InteropBridge* bridge =
            static_cast<Win32InteropBridge*>(handle);
        return bridge && bridge->enqueue(command) ? 1u : 0u;
    }
    catch (...)
    {
        return 0u;
    }
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_win32_interop_seek_media(
    VibranceWin32InteropHandle handle,
    std::int64_t positionMilliseconds)
{
    try
    {
        Win32InteropBridge* bridge =
            static_cast<Win32InteropBridge*>(handle);
        return bridge && bridge->enqueue_seek(positionMilliseconds) ? 1u : 0u;
    }
    catch (...)
    {
        return 0u;
    }
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_win32_interop_request_notification_access(
    VibranceWin32InteropHandle handle)
{
    try
    {
        Win32InteropBridge* bridge =
            static_cast<Win32InteropBridge*>(handle);
        return bridge && bridge->request_notification_access() ? 1u : 0u;
    }
    catch (...)
    {
        return 0u;
    }
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_win32_interop_get_notifications(
    VibranceWin32InteropHandle handle,
    VibranceWin32NotificationSnapshot* snapshot,
    std::uint32_t snapshotSize,
    VibranceWin32NotificationItem* items,
    std::uint32_t itemSize,
    std::uint32_t capacity)
{
    try
    {
        Win32InteropBridge* bridge =
            static_cast<Win32InteropBridge*>(handle);
        return bridge && bridge->get_notifications(
            snapshot,
            snapshotSize,
            items,
            itemSize,
            capacity) ? 1u : 0u;
    }
    catch (...)
    {
        return 0u;
    }
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_win32_interop_dismiss_notification(
    VibranceWin32InteropHandle handle,
    std::uint32_t notificationId)
{
    try
    {
        Win32InteropBridge* bridge =
            static_cast<Win32InteropBridge*>(handle);
        return bridge && bridge->enqueue_notification_dismissal(
            notificationId) ? 1u : 0u;
    }
    catch (...)
    {
        return 0u;
    }
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_win32_interop_activate_notification_source(
    VibranceWin32InteropHandle handle,
    const char* appUserModelId)
{
    const HRESULT apartmentResult = CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED);
    if (FAILED(apartmentResult) &&
        apartmentResult != RPC_E_CHANGED_MODE)
    {
        return 0u;
    }
    try
    {
        Win32InteropBridge* bridge =
            static_cast<Win32InteropBridge*>(handle);
        const std::uint32_t result = bridge &&
            bridge->activate_notification_source(appUserModelId) ? 1u : 0u;
        if (SUCCEEDED(apartmentResult))
        {
            CoUninitialize();
        }
        return result;
    }
    catch (...)
    {
        if (SUCCEEDED(apartmentResult))
        {
            CoUninitialize();
        }
        return 0u;
    }
}

extern "C" __declspec(dllexport) std::uint32_t __cdecl
vibrance_win32_interop_resolve_application_icon(
    VibranceWin32InteropHandle handle,
    const char* appUserModelId,
    VibranceWin32ApplicationIcon* destination,
    std::uint32_t destinationSize)
{
    const HRESULT apartmentResult = CoInitializeEx(
        nullptr,
        COINIT_MULTITHREADED);
    if (FAILED(apartmentResult) &&
        apartmentResult != RPC_E_CHANGED_MODE)
    {
        return 0u;
    }
    try
    {
        Win32InteropBridge* bridge =
            static_cast<Win32InteropBridge*>(handle);
        const std::uint32_t result = bridge &&
            bridge->resolve_application_icon(
                appUserModelId,
                destination,
                destinationSize) ? 1u : 0u;
        if (SUCCEEDED(apartmentResult))
        {
            CoUninitialize();
        }
        return result;
    }
    catch (...)
    {
        if (SUCCEEDED(apartmentResult))
        {
            CoUninitialize();
        }
        return 0u;
    }
}

#endif // defined(_WIN32)

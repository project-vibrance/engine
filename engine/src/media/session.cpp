#include <vibranceUI/media/session.h>

#include <algorithm>
#include <bit>
#include <utility>

#if defined(_WIN32) && !defined(VIBRANCE_FORCE_PORTABLE_MEDIA_SESSION)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vibranceUI/platform/win32_interop_abi.h>

#include <array>
#include <cstring>
#include <string_view>
#endif

struct GlobalMediaSession::Impl
{
    MediaSessionSnapshot snapshot;

#if defined(_WIN32) && !defined(VIBRANCE_FORCE_PORTABLE_MEDIA_SESSION)
    HMODULE module = nullptr;
    VibranceWin32InteropHandle handle = nullptr;
    VibranceWin32InteropDestroyFn destroy = nullptr;
    VibranceWin32InteropGetMediaSnapshotFn getMediaSnapshot = nullptr;
    VibranceWin32InteropSendMediaCommandFn sendMediaCommand = nullptr;
    VibranceWin32InteropSeekMediaFn seekMedia = nullptr;

    static std::wstring bridge_path()
    {
        std::array<wchar_t, 32768u> executablePath {};
        const DWORD length = GetModuleFileNameW(
            nullptr,
            executablePath.data(),
            static_cast<DWORD>(executablePath.size()));
        if (length == 0u || length >= executablePath.size())
        {
            return L"vibrance_win32_interop.dll";
        }

        std::wstring path(executablePath.data(), length);
        const std::size_t separator = path.find_last_of(L"\\/");
        if (separator != std::wstring::npos)
        {
            path.resize(separator + 1u);
        }
        else
        {
            path.clear();
        }
        path += L"vibrance_win32_interop.dll";
        return path;
    }

    template <typename Function>
    static Function load_function(HMODULE library, const char* symbol)
    {
        const FARPROC address = GetProcAddress(library, symbol);
        static_assert(sizeof(Function) == sizeof(address));
        return address ? std::bit_cast<Function>(address) : Function {};
    }

    Impl()
    {
        module = LoadLibraryW(bridge_path().c_str());
        if (!module)
        {
            snapshot.diagnostic =
                "vibrance_win32_interop.dll was not found beside the "
                "current executable.";
            return;
        }

        const auto abiVersion = load_function<VibranceWin32InteropAbiVersionFn>(
            module,
            VIBRANCE_WIN32_INTEROP_ABI_VERSION_SYMBOL);
        const auto create = load_function<VibranceWin32InteropCreateFn>(
            module,
            VIBRANCE_WIN32_INTEROP_CREATE_SYMBOL);
        destroy = load_function<VibranceWin32InteropDestroyFn>(
            module,
            VIBRANCE_WIN32_INTEROP_DESTROY_SYMBOL);
        getMediaSnapshot =
            load_function<VibranceWin32InteropGetMediaSnapshotFn>(
                module,
                VIBRANCE_WIN32_INTEROP_GET_MEDIA_SNAPSHOT_SYMBOL);
        sendMediaCommand =
            load_function<VibranceWin32InteropSendMediaCommandFn>(
                module,
                VIBRANCE_WIN32_INTEROP_SEND_MEDIA_COMMAND_SYMBOL);
        seekMedia = load_function<VibranceWin32InteropSeekMediaFn>(
            module,
            VIBRANCE_WIN32_INTEROP_SEEK_MEDIA_SYMBOL);

        if (!abiVersion ||
            !create ||
            !destroy ||
            !getMediaSnapshot ||
            !sendMediaCommand ||
            !seekMedia ||
            abiVersion() != VIBRANCE_WIN32_INTEROP_ABI_VERSION)
        {
            snapshot.diagnostic =
                "The Win32 interop DLL has an incompatible ABI.";
            FreeLibrary(module);
            module = nullptr;
            destroy = nullptr;
            getMediaSnapshot = nullptr;
            sendMediaCommand = nullptr;
            seekMedia = nullptr;
            return;
        }

        handle = create();
        if (!handle)
        {
            snapshot.diagnostic =
                "The Win32 interop bridge could not be started.";
            FreeLibrary(module);
            module = nullptr;
            destroy = nullptr;
            getMediaSnapshot = nullptr;
            sendMediaCommand = nullptr;
            return;
        }

        snapshot.bridgeLoaded = true;
        refresh();
    }

    ~Impl()
    {
        if (handle && destroy)
        {
            destroy(handle);
            handle = nullptr;
        }
        if (module)
        {
            FreeLibrary(module);
            module = nullptr;
        }
    }

    bool refresh()
    {
        if (!handle || !getMediaSnapshot)
        {
            return false;
        }

        VibranceWin32MediaSnapshot bridgeSnapshot {};
        if (getMediaSnapshot(
                handle,
                &bridgeSnapshot,
                sizeof(bridgeSnapshot)) == 0u ||
            bridgeSnapshot.structSize != sizeof(bridgeSnapshot))
        {
            return false;
        }

        const std::uint64_t previousRevision = snapshot.revision;
        snapshot.bridgeLoaded = true;
        snapshot.serviceReady = bridgeSnapshot.serviceReady != 0u;
        snapshot.sessionAvailable = bridgeSnapshot.sessionAvailable != 0u;
        snapshot.canTogglePlayPause =
            (bridgeSnapshot.capabilities &
                VIBRANCE_WIN32_MEDIA_CAN_PLAY_PAUSE) != 0u;
        snapshot.canSkipNext =
            (bridgeSnapshot.capabilities &
                VIBRANCE_WIN32_MEDIA_CAN_SKIP_NEXT) != 0u;
        snapshot.canSkipPrevious =
            (bridgeSnapshot.capabilities &
                VIBRANCE_WIN32_MEDIA_CAN_SKIP_PREVIOUS) != 0u;
        snapshot.revision = bridgeSnapshot.revision;
        snapshot.thumbnailRevision = bridgeSnapshot.thumbnailRevision;
        snapshot.positionMilliseconds =
            bridgeSnapshot.positionMilliseconds;
        snapshot.durationMilliseconds =
            bridgeSnapshot.durationMilliseconds;
        snapshot.sourceProcessId = bridgeSnapshot.sourceProcessId;
        snapshot.sourceApp = bridgeSnapshot.sourceApp;
        snapshot.title = bridgeSnapshot.title;
        snapshot.artist = bridgeSnapshot.artist;
        snapshot.album = bridgeSnapshot.album;
        snapshot.thumbnailPath = bridgeSnapshot.thumbnailPath;
        snapshot.diagnostic = bridgeSnapshot.diagnostic;

        switch (bridgeSnapshot.playbackStatus)
        {
            case VIBRANCE_WIN32_MEDIA_CLOSED:
                snapshot.playbackStatus =
                    MediaSessionPlaybackStatus::eClosed;
                break;
            case VIBRANCE_WIN32_MEDIA_OPENED:
                snapshot.playbackStatus =
                    MediaSessionPlaybackStatus::eOpened;
                break;
            case VIBRANCE_WIN32_MEDIA_CHANGING:
                snapshot.playbackStatus =
                    MediaSessionPlaybackStatus::eChanging;
                break;
            case VIBRANCE_WIN32_MEDIA_STOPPED:
                snapshot.playbackStatus =
                    MediaSessionPlaybackStatus::eStopped;
                break;
            case VIBRANCE_WIN32_MEDIA_PLAYING:
                snapshot.playbackStatus =
                    MediaSessionPlaybackStatus::ePlaying;
                break;
            case VIBRANCE_WIN32_MEDIA_PAUSED:
                snapshot.playbackStatus =
                    MediaSessionPlaybackStatus::ePaused;
                break;
            default:
                snapshot.playbackStatus =
                    MediaSessionPlaybackStatus::eUnknown;
                break;
        }

        return snapshot.revision != previousRevision;
    }

    bool send(MediaSessionCommand command)
    {
        if (!handle || !sendMediaCommand)
        {
            return false;
        }

        std::uint32_t bridgeCommand = 0u;
        switch (command)
        {
            case MediaSessionCommand::eTogglePlayPause:
                bridgeCommand =
                    VIBRANCE_WIN32_MEDIA_TOGGLE_PLAY_PAUSE;
                break;
            case MediaSessionCommand::eSkipNext:
                bridgeCommand = VIBRANCE_WIN32_MEDIA_SKIP_NEXT;
                break;
            case MediaSessionCommand::eSkipPrevious:
                bridgeCommand = VIBRANCE_WIN32_MEDIA_SKIP_PREVIOUS;
                break;
        }
        return sendMediaCommand(handle, bridgeCommand) != 0u;
    }

    bool seek(std::int64_t positionMilliseconds)
    {
        return handle && seekMedia &&
            seekMedia(handle, std::max(positionMilliseconds, std::int64_t(0))) != 0u;
    }
#else
    Impl()
    {
        snapshot.diagnostic =
            "Global media sessions are available on Win32 only.";
    }

    bool refresh()
    {
        return false;
    }

    bool send(MediaSessionCommand)
    {
        return false;
    }

    bool seek(std::int64_t)
    {
        return false;
    }
#endif
};

GlobalMediaSession::GlobalMediaSession() :
    impl(std::make_unique<Impl>())
{
}

GlobalMediaSession::~GlobalMediaSession() = default;
GlobalMediaSession::GlobalMediaSession(GlobalMediaSession&&) noexcept = default;
GlobalMediaSession& GlobalMediaSession::operator=(
    GlobalMediaSession&&) noexcept = default;

bool GlobalMediaSession::platform_supported()
{
#if defined(_WIN32) && !defined(VIBRANCE_FORCE_PORTABLE_MEDIA_SESSION)
    return true;
#else
    return false;
#endif
}

bool GlobalMediaSession::bridge_loaded() const
{
    return impl && impl->snapshot.bridgeLoaded;
}

bool GlobalMediaSession::refresh()
{
    return impl && impl->refresh();
}

bool GlobalMediaSession::send(MediaSessionCommand command)
{
    return impl && impl->send(command);
}

bool GlobalMediaSession::seek(std::int64_t positionMilliseconds)
{
    return impl && impl->seek(positionMilliseconds);
}

const MediaSessionSnapshot& GlobalMediaSession::snapshot() const
{
    static const MediaSessionSnapshot emptySnapshot {};
    return impl ? impl->snapshot : emptySnapshot;
}

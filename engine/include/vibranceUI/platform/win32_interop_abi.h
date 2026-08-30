#pragma once

#include <cstdint>

#if defined(_WIN32) && !defined(VIBRANCE_FORCE_PORTABLE_WIN32_INTEROP_ABI)
#define VIBRANCE_WIN32_INTEROP_CALL __cdecl
#else
// Keeps this POD contract parseable for cross-platform tooling. The function
// pointers are loaded and called only by _WIN32 implementation blocks.
#define VIBRANCE_WIN32_INTEROP_CALL
#endif

// This header is the only contract shared by SDK consumers and the MSVC
// C++/WinRT companion. Keep it POD-only: no C++ standard-library objects,
// COM pointers, or allocations may cross the DLL boundary.
constexpr std::uint32_t VIBRANCE_WIN32_INTEROP_ABI_VERSION = 7u;

enum VibranceWin32MediaPlaybackStatus : std::uint32_t
{
    VIBRANCE_WIN32_MEDIA_UNKNOWN = 0u,
    VIBRANCE_WIN32_MEDIA_CLOSED = 1u,
    VIBRANCE_WIN32_MEDIA_OPENED = 2u,
    VIBRANCE_WIN32_MEDIA_CHANGING = 3u,
    VIBRANCE_WIN32_MEDIA_STOPPED = 4u,
    VIBRANCE_WIN32_MEDIA_PLAYING = 5u,
    VIBRANCE_WIN32_MEDIA_PAUSED = 6u
};

enum VibranceWin32MediaCapability : std::uint32_t
{
    VIBRANCE_WIN32_MEDIA_CAN_PLAY_PAUSE = 1u << 0u,
    VIBRANCE_WIN32_MEDIA_CAN_SKIP_NEXT = 1u << 1u,
    VIBRANCE_WIN32_MEDIA_CAN_SKIP_PREVIOUS = 1u << 2u
};

enum VibranceWin32MediaCommand : std::uint32_t
{
    VIBRANCE_WIN32_MEDIA_TOGGLE_PLAY_PAUSE = 1u,
    VIBRANCE_WIN32_MEDIA_SKIP_NEXT = 2u,
    VIBRANCE_WIN32_MEDIA_SKIP_PREVIOUS = 3u
};

enum VibranceWin32NotificationAccessStatus : std::uint32_t
{
    VIBRANCE_WIN32_NOTIFICATIONS_UNSPECIFIED = 0u,
    VIBRANCE_WIN32_NOTIFICATIONS_ALLOWED = 1u,
    VIBRANCE_WIN32_NOTIFICATIONS_DENIED = 2u,
    VIBRANCE_WIN32_NOTIFICATIONS_UNAVAILABLE = 3u,
    VIBRANCE_WIN32_NOTIFICATIONS_ERROR = 4u
};

enum VibranceWin32InteropFeature : std::uint32_t
{
    VIBRANCE_WIN32_INTEROP_MEDIA = 1u << 0u,
    VIBRANCE_WIN32_INTEROP_NOTIFICATIONS = 1u << 1u
};

#pragma pack(push, 8)
struct VibranceWin32MediaSnapshot
{
    std::uint32_t structSize = 0u;
    std::uint32_t serviceReady = 0u;
    std::uint32_t sessionAvailable = 0u;
    std::uint32_t playbackStatus = VIBRANCE_WIN32_MEDIA_UNKNOWN;
    std::uint32_t capabilities = 0u;
    std::uint32_t sourceProcessId = 0u;
    std::uint64_t revision = 0u;
    std::uint64_t thumbnailRevision = 0u;
    std::int64_t positionMilliseconds = 0;
    std::int64_t durationMilliseconds = 0;
    char sourceApp[192] {};
    char title[384] {};
    char artist[256] {};
    char album[256] {};
    char thumbnailPath[768] {};
    char diagnostic[384] {};
};

struct VibranceWin32NotificationSnapshot
{
    std::uint32_t structSize = 0u;
    std::uint32_t serviceReady = 0u;
    std::uint32_t accessStatus =
        VIBRANCE_WIN32_NOTIFICATIONS_UNSPECIFIED;
    std::uint32_t itemCount = 0u;
    std::uint64_t revision = 0u;
    char diagnostic[384] {};
};

struct VibranceWin32NotificationItem
{
    std::uint32_t structSize = 0u;
    std::uint32_t notificationId = 0u;
    std::int64_t creationUnixMilliseconds = 0;
    std::uint64_t iconRevision = 0u;
    char sourceApp[192] {};
    char appUserModelId[256] {};
    char title[384] {};
    char message[768] {};
    char iconPath[768] {};
};

struct VibranceWin32ApplicationIcon
{
    std::uint32_t structSize = 0u;
    std::uint32_t reserved = 0u;
    std::uint64_t revision = 0u;
    char path[768] {};
};
#pragma pack(pop)

using VibranceWin32InteropHandle = void*;

using VibranceWin32InteropAbiVersionFn =
    std::uint32_t(VIBRANCE_WIN32_INTEROP_CALL*)();
using VibranceWin32InteropCreateFn =
    VibranceWin32InteropHandle(VIBRANCE_WIN32_INTEROP_CALL*)();
using VibranceWin32InteropCreateWithFeaturesFn =
    VibranceWin32InteropHandle(VIBRANCE_WIN32_INTEROP_CALL*)(std::uint32_t);
using VibranceWin32InteropDestroyFn =
    void(VIBRANCE_WIN32_INTEROP_CALL*)(VibranceWin32InteropHandle);
using VibranceWin32InteropGetMediaSnapshotFn =
    std::uint32_t(VIBRANCE_WIN32_INTEROP_CALL*)(
    VibranceWin32InteropHandle,
    VibranceWin32MediaSnapshot*,
    std::uint32_t);
using VibranceWin32InteropSendMediaCommandFn =
    std::uint32_t(VIBRANCE_WIN32_INTEROP_CALL*)(
    VibranceWin32InteropHandle,
    std::uint32_t);
using VibranceWin32InteropSeekMediaFn =
    std::uint32_t(VIBRANCE_WIN32_INTEROP_CALL*)(
    VibranceWin32InteropHandle,
    std::int64_t);
using VibranceWin32InteropRequestNotificationAccessFn =
    std::uint32_t(VIBRANCE_WIN32_INTEROP_CALL*)(
    VibranceWin32InteropHandle);
using VibranceWin32InteropGetNotificationsFn =
    std::uint32_t(VIBRANCE_WIN32_INTEROP_CALL*)(
    VibranceWin32InteropHandle,
    VibranceWin32NotificationSnapshot*,
    std::uint32_t,
    VibranceWin32NotificationItem*,
    std::uint32_t,
    std::uint32_t);
using VibranceWin32InteropDismissNotificationFn =
    std::uint32_t(VIBRANCE_WIN32_INTEROP_CALL*)(
    VibranceWin32InteropHandle,
    std::uint32_t);
using VibranceWin32InteropActivateNotificationSourceFn =
    std::uint32_t(VIBRANCE_WIN32_INTEROP_CALL*)(
        VibranceWin32InteropHandle,
        const char*);
using VibranceWin32InteropResolveApplicationIconFn =
    std::uint32_t(VIBRANCE_WIN32_INTEROP_CALL*)(
        VibranceWin32InteropHandle,
        const char*,
        VibranceWin32ApplicationIcon*,
        std::uint32_t);

constexpr const char* VIBRANCE_WIN32_INTEROP_ABI_VERSION_SYMBOL =
    "vibrance_win32_interop_abi_version";
constexpr const char* VIBRANCE_WIN32_INTEROP_CREATE_SYMBOL =
    "vibrance_win32_interop_create";
constexpr const char* VIBRANCE_WIN32_INTEROP_CREATE_WITH_FEATURES_SYMBOL =
    "vibrance_win32_interop_create_with_features";
constexpr const char* VIBRANCE_WIN32_INTEROP_DESTROY_SYMBOL =
    "vibrance_win32_interop_destroy";
constexpr const char* VIBRANCE_WIN32_INTEROP_GET_MEDIA_SNAPSHOT_SYMBOL =
    "vibrance_win32_interop_get_media_snapshot";
constexpr const char* VIBRANCE_WIN32_INTEROP_SEND_MEDIA_COMMAND_SYMBOL =
    "vibrance_win32_interop_send_media_command";
constexpr const char* VIBRANCE_WIN32_INTEROP_SEEK_MEDIA_SYMBOL =
    "vibrance_win32_interop_seek_media";
constexpr const char* VIBRANCE_WIN32_INTEROP_REQUEST_NOTIFICATION_ACCESS_SYMBOL =
    "vibrance_win32_interop_request_notification_access";
constexpr const char* VIBRANCE_WIN32_INTEROP_GET_NOTIFICATIONS_SYMBOL =
    "vibrance_win32_interop_get_notifications";
constexpr const char* VIBRANCE_WIN32_INTEROP_DISMISS_NOTIFICATION_SYMBOL =
    "vibrance_win32_interop_dismiss_notification";
constexpr const char*
VIBRANCE_WIN32_INTEROP_ACTIVATE_NOTIFICATION_SOURCE_SYMBOL =
    "vibrance_win32_interop_activate_notification_source";
constexpr const char*
VIBRANCE_WIN32_INTEROP_RESOLVE_APPLICATION_ICON_SYMBOL =
    "vibrance_win32_interop_resolve_application_icon";

#undef VIBRANCE_WIN32_INTEROP_CALL

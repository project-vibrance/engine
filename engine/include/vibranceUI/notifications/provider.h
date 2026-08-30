#pragma once

#include <vibranceUI/export.h>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

enum class SystemNotificationAccess : std::uint8_t
{
    eUnsupported,
    eUnspecified,
    eAllowed,
    eDenied,
    eUnavailable,
    eError
};

// One platform notification, independent from any application's inbox IDs,
// actions, persistence schema, or presentation model.
struct SystemNotificationItem
{
    std::uint32_t providerId = 0u;
    std::int64_t creationUnixMilliseconds = 0;
    std::uint64_t iconRevision = 0u;
    std::string source;
    std::string title;
    std::string message;
    std::string activationTarget;
    std::string iconPath;

    bool operator==(const SystemNotificationItem&) const = default;
};

struct SystemNotificationSnapshot
{
    // True only when the optional native companion was loaded and its ABI was
    // accepted. platform_supported() can still be true when this is false.
    bool bridgeLoaded = false;
    // Provider-owned monotonic revision. Consumers may keep their own applied
    // revision when persistence or another downstream commit must be retried.
    std::uint64_t revision = 0u;
    SystemNotificationAccess access = SystemNotificationAccess::eUnsupported;
    std::vector<SystemNotificationItem> items;
    std::string diagnostic;
};

struct SystemApplicationIcon
{
    std::string path;
    std::uint64_t revision = 0u;
};

// Portable facade for the optional operating-system notification listener.
// It never persists, claims, groups, filters, or displays notifications.
class VIBRANCE_ENGINE_API SystemNotificationProvider
{
public:
    SystemNotificationProvider();
    ~SystemNotificationProvider();

    SystemNotificationProvider(const SystemNotificationProvider&) = delete;
    SystemNotificationProvider& operator=(
        const SystemNotificationProvider&) = delete;

    SystemNotificationProvider(SystemNotificationProvider&&) noexcept;
    SystemNotificationProvider& operator=(
        SystemNotificationProvider&&) noexcept;

    // Compile-time provider support; this does not promise runtime service or
    // companion availability.
    static bool platform_supported();
    // Runtime companion/ABI readiness. See snapshot().diagnostic on failure.
    bool bridge_loaded() const;
    // Performs the platform permission request. The resulting allowed/denied
    // state is reported by snapshot().access.
    bool request_access();
    // Polls the provider and returns true only when observable snapshot state
    // changed. False can mean "unchanged" as well as "unavailable"; inspect
    // bridge_loaded(), access, and diagnostic when that distinction matters.
    bool refresh();
    // Commands are explicit and best-effort. Reading or refreshing a snapshot
    // never claims, dismisses, activates, or persists a notification.
    bool dismiss(std::uint32_t providerId);
    bool activate_source(std::string_view activationTarget);
    SystemApplicationIcon resolve_application_icon(
        std::string_view activationTarget);
    // The reference remains valid until the next non-const provider call,
    // move, or destruction. Copy it when it must outlive that boundary.
    const SystemNotificationSnapshot& snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

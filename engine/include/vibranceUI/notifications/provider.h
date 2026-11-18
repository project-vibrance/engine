// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

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



enum class SystemNotificationActivationKind : std::uint32_t
{
    eApplication, eForeground, eProtocol, eBackground
};

struct SystemNotificationItem
{
    std::uint32_t providerId = 0u;
    std::int64_t creationUnixMilliseconds = 0;
    std::uint64_t iconRevision = 0u;
    std::string source;
    std::string title;
    std::string message;
    std::string activationTarget;
    std::string activationArguments;
    SystemNotificationActivationKind activationKind = SystemNotificationActivationKind::eApplication;
    std::string iconPath;

    bool operator==(const SystemNotificationItem&) const = default;
};

struct SystemNotificationSnapshot
{


    bool bridgeLoaded = false;


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



    static bool platform_supported();

    bool bridge_loaded() const;


    bool request_access();



    bool refresh();


    bool dismiss(std::uint32_t providerId);
    bool activate_source(std::string_view activationTarget);
    bool activate_notification(std::string_view activationTarget,
        std::string_view arguments, SystemNotificationActivationKind kind);

    SystemApplicationIcon resolve_application_icon(
        std::string_view activationTarget);


    const SystemNotificationSnapshot& snapshot() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

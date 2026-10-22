#pragma once

#include <vibranceUI/platform/win32_interop_abi.h>
#include <chrono>
#include <cstring>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace vibrance::win32
{
inline bool same_notification_record(const VibranceWin32NotificationItem& left,
    const VibranceWin32NotificationItem& right)
{
    return left.notificationId == right.notificationId &&
        left.creationUnixMilliseconds == right.creationUnixMilliseconds &&
        left.iconRevision == right.iconRevision &&
        left.activationKind == right.activationKind &&
        std::strcmp(left.activationArguments, right.activationArguments) == 0 &&
        std::strcmp(left.sourceApp, right.sourceApp) == 0 &&
        std::strcmp(left.appUserModelId, right.appUserModelId) == 0 &&
        std::strcmp(left.title, right.title) == 0 &&
        std::strcmp(left.message, right.message) == 0 &&
        std::strcmp(left.iconPath, right.iconPath) == 0;
}

// Only the current native snapshot is retained. IDs alone are insufficient:
// Windows can update a toast or reuse an ID after its earlier toast disappears.
class NotificationRecordCache
{
public:
    using Clock = std::chrono::steady_clock;
    struct Entry
    {
        VibranceWin32NotificationItem item;
        Clock::time_point refreshed;
        Clock::time_point payloadChecked;
        std::string nativeTitle;
        std::string nativeMessage;
    };

    Entry* find(const VibranceWin32NotificationItem& candidate, Clock::time_point now)
    {
        const auto found = entries.find(candidate.notificationId);
        if (found == entries.end()) return nullptr;
        auto& cached = found->second;
        if (cached.item.creationUnixMilliseconds != candidate.creationUnixMilliseconds ||
            cached.nativeTitle != candidate.title || cached.nativeMessage != candidate.message ||
            now - cached.refreshed >= std::chrono::seconds(30)) return nullptr;
        return &cached;
    }

    void store(const VibranceWin32NotificationItem& item, Clock::time_point now)
    {
        entries.insert_or_assign(item.notificationId,
            Entry { item, now, now, item.title, item.message });
    }

    void retain(const std::unordered_set<std::uint32_t>& liveIds)
    {
        for (auto it = entries.begin(); it != entries.end();)
            if (!liveIds.contains(it->first)) it = entries.erase(it);
            else ++it;
    }

    void clear() { entries.clear(); }
    std::size_t size() const { return entries.size(); }

private:
    std::unordered_map<std::uint32_t, Entry> entries;
};
}

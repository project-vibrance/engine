#include "../src/platform/win32/notification_record_cache.h"
#include <iostream>
#include <cstring>

int main()
{
    using vibrance::win32::NotificationRecordCache;
    using namespace std::chrono_literals;
    NotificationRecordCache cache;
    const auto now = NotificationRecordCache::Clock::time_point {};
    int failed = 0;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; ++failed; }
    };
    VibranceWin32NotificationItem item {};
    item.notificationId = 42;
    item.creationUnixMilliseconds = 1000;
    std::strcpy(item.title, "A message");
    std::strcpy(item.message, "Original body");
    std::strcpy(item.appUserModelId, "test.application");
    item.activationKind = 1;
    std::strcpy(item.activationArguments, "conversation=correct");
    cache.store(item, now);
    expect(vibrance::win32::same_notification_record(item, item),
        "An unchanged native snapshot must not publish another revision");
    auto activation = item;
    activation.activationKind = 0;
    expect(!vibrance::win32::same_notification_record(item, activation),
        "A newly resolved activation kind must publish even when its visuals are unchanged");
    activation = item;
    std::strcpy(activation.activationArguments, "conversation=updated");
    expect(!vibrance::win32::same_notification_record(item, activation),
        "Updated activation arguments must publish without needing a visual change");
    for (int tick = 0; tick < 60; ++tick)
    {
        const auto* entry = cache.find(item, now + tick * 499ms);
        expect(entry && entry->item.activationKind == 1 &&
            std::strcmp(entry->item.activationArguments, "conversation=correct") == 0,
            "Unchanged native items must retain their exact activation target between refreshes");
    }
    expect(!cache.find(item, now + 30s), "Metadata must refresh at its bounded expiry");
    auto changed = item;
    std::strcpy(changed.message, "Updated body");
    expect(!cache.find(changed, now + 1s), "A changed toast body must be observed immediately");
    changed = item;
    std::strcpy(changed.title, "Updated title");
    expect(!cache.find(changed, now + 1s), "A changed toast title must be observed immediately");
    changed = item;
    ++changed.creationUnixMilliseconds;
    expect(!cache.find(changed, now + 1s), "A reused Windows ID must not inherit another notification's destination");
    changed = item;
    changed.notificationId = 43;
    changed.title[0] = '\0';
    cache.store(changed, now);
    auto* fallback = cache.find(changed, now);
    expect(fallback != nullptr, "An empty native title must be cacheable");
    if (fallback) std::strcpy(fallback->item.title, "Application display name");
    expect(cache.find(changed, now + 1s) != nullptr,
        "A published display-name fallback must not invalidate unchanged empty native text");
    cache.retain({43});
    expect(cache.size() == 1 && !cache.find(item, now), "Removed native notifications must release cached records");
    cache.clear();
    expect(cache.size() == 0, "Revoked access must release the previous native snapshot");
    return failed ? 1 : 0;
}

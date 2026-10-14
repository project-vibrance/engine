#pragma once

#include "notification_activation.h"
#include <shlobj.h>
#include <filesystem>

namespace vibrance::win32
{
// The listener exposes visuals, not launch arguments. This optional, read-only
// adapter recovers the payload by notification ID, sender and arrival time.
// The Windows store is private: reject absent/changed schemas and retain the
// original Windows action when it cannot be read. Never carve deleted records.
class NotificationPayloadStore
{
public:
    using ElectronRegistrationQuery = bool (*)(const std::string&);
    explicit NotificationPayloadStore(std::filesystem::path path = {},
        ElectronRegistrationQuery registrationQuery = registered_electron_toast_sender) :
        electronRegistrationQuery(registrationQuery)
    {
        library = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!library) return;
#define VIBRANCE_SQLITE_ENTRY(member, name) member = reinterpret_cast<decltype(member)>(GetProcAddress(library, name))
        VIBRANCE_SQLITE_ENTRY(open, "sqlite3_open_v2");
        VIBRANCE_SQLITE_ENTRY(close, "sqlite3_close");
        VIBRANCE_SQLITE_ENTRY(prepare, "sqlite3_prepare_v2");
        VIBRANCE_SQLITE_ENTRY(finalise, "sqlite3_finalize");
        VIBRANCE_SQLITE_ENTRY(bindInteger, "sqlite3_bind_int64");
        VIBRANCE_SQLITE_ENTRY(bindText, "sqlite3_bind_text");
        VIBRANCE_SQLITE_ENTRY(step, "sqlite3_step");
        VIBRANCE_SQLITE_ENTRY(columnBlob, "sqlite3_column_blob");
        VIBRANCE_SQLITE_ENTRY(columnBytes, "sqlite3_column_bytes");
#undef VIBRANCE_SQLITE_ENTRY
        if (!open || !close || !prepare || !finalise || !bindInteger || !bindText ||
            !step || !columnBlob || !columnBytes) return;
        if (path.empty())
        {
            PWSTR folder = nullptr;
            if (FAILED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &folder))) return;
            path = std::filesystem::path(folder) / L"Microsoft/Windows/Notifications/wpndatabase.db";
            CoTaskMemFree(folder);
        }
        const auto encoded = path.u8string();
        // READONLY includes committed WAL records; immutable would lose recent
        // notifications. No busy waits on the notification delivery worker.
        if (open(reinterpret_cast<const char*>(encoded.c_str()), &database, 1, nullptr) != 0 && database)
        { close(database); database = nullptr; }
    }
    ~NotificationPayloadStore()
    {
        if (database && close) close(database);
        if (library) FreeLibrary(library);
    }
    NotificationPayloadStore(const NotificationPayloadStore&) = delete;
    NotificationPayloadStore& operator=(const NotificationPayloadStore&) = delete;

    std::optional<ToastDestination> read(std::uint32_t id, const std::string& sender,
        std::int64_t createdUnixMilliseconds) const
    {
        if (!database || sender.empty() || !id || createdUnixMilliseconds <= 0) return {};
        void* statement = nullptr;
        constexpr char query[] =
            "SELECT n.Payload,n.Tag FROM Notification n JOIN NotificationHandler h ON n.HandlerId=h.RecordId "
            "WHERE n.Id=?1 AND h.PrimaryId=?2 AND n.Type='toast' AND n.ArrivalTime BETWEEN ?3 AND ?4 LIMIT 2";
        if (prepare(database, query, -1, &statement, nullptr) != 0) return {};
        struct Statement { void* value; decltype(finalise) release; ~Statement() { release(value); } } guard { statement, finalise };
        constexpr std::int64_t epochOffset = 11'644'473'600'000ll;
        // Windows stores FILETIME. Bound arithmetic before accepting input
        // originating outside the listener (including persisted test fixtures).
        if (createdUnixMilliseconds > INT64_MAX / 10'000ll - epochOffset - 1'000ll) return {};
        const auto time = (createdUnixMilliseconds + epochOffset) * 10'000ll;
        if (bindInteger(statement, 1, id) != 0 ||
            bindText(statement, 2, sender.c_str(), static_cast<int>(sender.size()), nullptr) != 0 ||
            bindInteger(statement, 3, time - 10'000'000ll) != 0 ||
            bindInteger(statement, 4, time + 10'000'000ll) != 0 || step(statement) != 100) return {};
        const auto bytes = columnBytes(statement, 0);
        const auto blob = static_cast<const char*>(columnBlob(statement, 0));
        if (!blob || bytes <= 0 || bytes > 128 * 1024) return {};
        std::optional<ToastDestination> result;
        try { result = parse_toast_destination(winrt::to_hstring(std::string(blob, bytes))); }
        catch (...) { return {}; }
        if (result && result->kind == 1u && result->arguments.empty() &&
            electronRegistrationQuery && electronRegistrationQuery(sender))
        {
            const auto tagBytes = columnBytes(statement, 1);
            const auto tag = static_cast<const char*>(columnBlob(statement, 1));
            result = electron_tagged_toast_destination(std::move(*result),
                tag && tagBytes > 0 && tagBytes <= 128 ? std::string_view(tag, tagBytes) : std::string_view {});
        }
        // Reject duplicate IDs rather than choosing another conversation.
        return step(statement) == 101 ? result : std::nullopt;
    }

private:
    ElectronRegistrationQuery electronRegistrationQuery;
    HMODULE library = nullptr;
    void* database = nullptr;
    int (__cdecl* open)(const char*, void**, int, const char*) = nullptr;
    int (__cdecl* close)(void*) = nullptr;
    int (__cdecl* prepare)(void*, const char*, int, void**, const char**) = nullptr;
    int (__cdecl* finalise)(void*) = nullptr;
    int (__cdecl* bindInteger)(void*, int, std::int64_t) = nullptr;
    int (__cdecl* bindText)(void*, int, const char*, int, void (__cdecl*)(void*)) = nullptr;
    int (__cdecl* step)(void*) = nullptr;
    const void* (__cdecl* columnBlob)(void*, int) = nullptr;
    int (__cdecl* columnBytes)(void*, int) = nullptr;
};
}

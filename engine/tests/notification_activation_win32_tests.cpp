#define NOMINMAX
#include <initguid.h>
#include "../src/platform/win32/notification_activation.h"
#include "../src/platform/win32/notification_payload_store.h"
#include <iostream>

struct ActivationSpy final : INotificationActivationCallback
{
    std::wstring identity, payload;
    HRESULT result = S_OK;
    ULONG calls = 0u;
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) override
    {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (id != IID_IUnknown && id != __uuidof(INotificationActivationCallback)) return E_NOINTERFACE;
        *value = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1u; }
    ULONG STDMETHODCALLTYPE Release() override { return 1u; }
    HRESULT STDMETHODCALLTYPE Activate(LPCWSTR appId, LPCWSTR arguments,
        const NOTIFICATION_USER_INPUT_DATA* data, ULONG count) override
    {
        if (data || count) return E_INVALIDARG;
        ++calls; identity = appId; payload = arguments; return result;
    }
};

struct ActivationFactory final : IClassFactory
{
    ActivationSpy& receiver;
    explicit ActivationFactory(ActivationSpy& value) : receiver(value) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** value) override
    {
        if (!value) return E_POINTER;
        *value = nullptr;
        if (id != IID_IUnknown && id != IID_IClassFactory) return E_NOINTERFACE;
        *value = this; AddRef(); return S_OK;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return 1u; }
    ULONG STDMETHODCALLTYPE Release() override { return 1u; }
    HRESULT STDMETHODCALLTYPE CreateInstance(IUnknown* outer, REFIID id, void** value) override
    {
        if (outer) return CLASS_E_NOAGGREGATION;
        return receiver.QueryInterface(id, value);
    }
    HRESULT STDMETHODCALLTYPE LockServer(BOOL) override { return S_OK; }
};

// All registrations belong to this fixture. No real application is started,
// and no production notification preference or callback registration is edited.
struct RegisteredActivationFixture
{
    ActivationSpy receiver;
    ActivationFactory factory { receiver };
    std::wstring identity, appKey, classKey;
    CLSID clsid {};
    DWORD cookie = 0u;
    bool ownsAppKey = false, ownsClassKey = false, ready = false;

    RegisteredActivationFixture(const wchar_t* suffix, bool electron)
    {
        identity = L"Vibrance.Tests.NotificationRouting." + std::to_wstring(GetCurrentProcessId()) + L"." + suffix;
        if (FAILED(CoCreateGuid(&clsid))) return;
        wchar_t encoded[40] {};
        if (!StringFromGUID2(clsid, encoded, 40)) return;
        appKey = L"Software\\Classes\\AppUserModelId\\" + identity;
        classKey = L"Software\\Classes\\CLSID\\" + std::wstring(encoded);
        const auto create = [](const std::wstring& path, bool& owned, HKEY& key) {
            DWORD disposition = 0u;
            if (RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0u, nullptr, 0u,
                KEY_SET_VALUE, nullptr, &key, &disposition) != ERROR_SUCCESS) return false;
            owned = disposition == REG_CREATED_NEW_KEY;
            if (!owned) { RegCloseKey(key); key = nullptr; }
            return owned;
        };
        HKEY key = nullptr;
        if (!create(appKey, ownsAppKey, key)) return;
        const auto registered = RegSetValueExW(key, L"CustomActivator", 0u, REG_SZ,
            reinterpret_cast<const BYTE*>(encoded), static_cast<DWORD>((wcslen(encoded) + 1u) * sizeof(wchar_t)));
        RegCloseKey(key);
        if (registered != ERROR_SUCCESS || !create(classKey, ownsClassKey, key)) return;
        const std::wstring name = electron ? L"Electron Notification Activator" : L"Vibrance callback fixture";
        const DWORD marker = electron ? 1u : 0u;
        const auto named = RegSetValueExW(key, nullptr, 0u, REG_SZ,
            reinterpret_cast<const BYTE*>(name.c_str()), static_cast<DWORD>((name.size() + 1u) * sizeof(wchar_t)));
        const auto marked = RegSetValueExW(key, L"CustomActivator", 0u, REG_DWORD,
            reinterpret_cast<const BYTE*>(&marker), sizeof(marker));
        RegCloseKey(key);
        if (named != ERROR_SUCCESS || marked != ERROR_SUCCESS) return;
        ready = SUCCEEDED(CoRegisterClassObject(clsid, &factory, CLSCTX_LOCAL_SERVER,
            REGCLS_MULTIPLEUSE, &cookie));
    }
    ~RegisteredActivationFixture()
    {
        if (cookie) CoRevokeClassObject(cookie);
        if (ownsClassKey) RegDeleteTreeW(HKEY_CURRENT_USER, classKey.c_str());
        if (ownsAppKey) RegDeleteTreeW(HKEY_CURRENT_USER, appKey.c_str());
    }
};

int main()
{
    winrt::init_apartment();
    using namespace vibrance::win32;
    int failures = 0;
    const auto check = [&](bool value, const char* message) { if (!value) { std::cerr << message << '\n'; ++failures; } };
    const auto chat = parse_toast_destination(LR"(<toast launch="conversation=42&amp;message=17"><visual><binding template="ToastGeneric"><text>Sender</text><text>Message</text></binding></visual></toast>)");
    check(chat && chat->kind == 1u && chat->arguments == "conversation=42&message=17", "Capture must decode the original toast arguments");
    const auto protocol = parse_toast_destination(LR"(<toast activationType="protocol" launch="discord://channels/1/42"><visual><binding><text>Sender</text><text>Message</text></binding></visual></toast>)");
    check(protocol && protocol->kind == 2u && protocol->arguments == "discord://channels/1/42", "Protocol destinations must retain their URI and activation type");
    check(!parse_toast_destination(L"<toast") && !parse_toast_destination(L"<toast activationType='unknown'/>") &&
        !parse_toast_destination(L"<!DOCTYPE toast [<!ENTITY x SYSTEM 'file:///missing'>]><toast launch='&x;'/>") &&
        !parse_toast_destination(L"<toast activationType='protocol'/>"), "Malformed, external or unknown payloads must be rejected");
    const std::wstring oversized = L"<toast launch='" + std::wstring(4096u, L'x') + L"'/>";
    check(!parse_toast_destination(winrt::hstring(oversized)), "Oversized launch arguments must not be silently truncated");
    ActivationSpy spy;
    check(dispatch_toast_callback(&spy, L"Tests.App", L"conversation=42&message=17") &&
        spy.calls == 1u && spy.identity == L"Tests.App" && spy.payload == L"conversation=42&message=17",
        "Card activation must invoke the real notification callback with the original identity and arguments");
    check(dispatch_toast_callback(&spy, L"Tests.App", L"") && spy.calls == 2u && spy.payload.empty(),
        "A registered notification callback must receive valid empty launch arguments");
    spy.result = E_FAIL;
    check(!dispatch_toast_callback(&spy, L"Tests.App", L"retry"), "Failed destination invocation must be reported to the model");
    check(!dispatch_toast_callback(nullptr, L"Tests.App", L""), "Missing callbacks must not count as successful activation");
    check(!invoke_toast_destination("Tests.App", "", 0u), "A generic application launch must not count as notification activation");
    check(!parse_toast_destination(L"<toast activationType='protocol' launch='test://chat' protocolActivationTargetApplicationPfn='Other.App'/>") ,
        "Targeted protocols must not launch a different registered application");

    const auto tagged = electron_tagged_toast_destination({ "", 1u }, "original-toast-42");
    check(tagged && tagged->kind == 1u && tagged->arguments == "type=click&tag=original-toast-42",
        "Electron body clicks must identify the exact original notification object by its native tag");
    const auto explicitDestination = electron_tagged_toast_destination({ "conversation=42", 1u }, "other-tag");
    check(explicitDestination && explicitDestination->arguments == "conversation=42",
        "Native launch arguments must take precedence over the Electron tag adapter");
    check(!electron_tagged_toast_destination({ "", 1u }, "") &&
        !electron_tagged_toast_destination({ "", 1u }, "one&tag=two") &&
        !electron_tagged_toast_destination({ "", 1u }, std::string(129u, 'a')),
        "Absent, ambiguous and oversized native tags must not select a notification object");

    const auto manifest = LR"(<Package xmlns="http://schemas.microsoft.com/appx/manifest/foundation/windows10"
        xmlns:desktop="http://schemas.microsoft.com/appx/manifest/desktop/windows10">
        <Applications><Application Id="Other"><Extensions>
        <desktop:Extension Category="windows.toastNotificationActivation"><desktop:ToastNotificationActivation
        ToastActivatorCLSID="11111111-1111-1111-1111-111111111111"/></desktop:Extension>
        </Extensions></Application><Application Id="Chat"><Extensions>
        <desktop:Extension Category="windows.toastNotificationActivation"><desktop:ToastNotificationActivation
        ToastActivatorCLSID="6AE9F7DC-D879-4970-B996-842A638B9A98"/></desktop:Extension>
        </Extensions></Application></Applications></Package>)";
    CLSID activator {}, expected {};
    CLSIDFromString(L"{6AE9F7DC-D879-4970-B996-842A638B9A98}", &expected);
    check(parse_packaged_toast_activator(manifest, L"Chat", activator) && activator == expected,
        "Package manifest discovery must select the exact application and tolerate namespace prefixes");
    check(!parse_packaged_toast_activator(manifest, L"Missing", activator),
        "Another application in the same package must not supply the notification callback");
    check(!parse_packaged_toast_activator(LR"(<Package><Applications><Application Id="Chat"><Extensions>
        <Extension Category="windows.protocol"><ToastNotificationActivation
        ToastActivatorCLSID="6AE9F7DC-D879-4970-B996-842A638B9A98"/></Extension>
        </Extensions></Application></Applications></Package>)", L"Chat", activator),
        "Only a toast activation extension may supply the callback");
    check(!parse_packaged_toast_activator(LR"(<Package><Applications><Application Id="Chat"><Extensions>
        <Extension Category="windows.toastNotificationActivation"><ToastNotificationActivation
        ToastActivatorCLSID="invalid"/></Extension></Extensions></Application></Applications></Package>)", L"Chat", activator) &&
        !parse_packaged_toast_activator(L"<!DOCTYPE Package [<!ENTITY x SYSTEM 'file:///missing'>]><Package/>", L"Chat", activator),
        "Invalid identifiers and external entities must not be accepted from package manifests");

    // Exercise the real Windows SQLite adapter using only synthetic records.
    HMODULE sqlite = LoadLibraryExW(L"winsqlite3.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    check(sqlite != nullptr, "Windows SQLite must be available for the payload fixture");
    if (sqlite)
    {
        const auto open = reinterpret_cast<int(__cdecl*)(const char*, void**, int, const char*)>(GetProcAddress(sqlite, "sqlite3_open_v2"));
        const auto execute = reinterpret_cast<int(__cdecl*)(void*, const char*, void*, void*, void*)>(GetProcAddress(sqlite, "sqlite3_exec"));
        const auto close = reinterpret_cast<int(__cdecl*)(void*)>(GetProcAddress(sqlite, "sqlite3_close"));
        const auto path = std::filesystem::temp_directory_path() /
            (L"vibrance-notification-\u00e9-" + std::to_wstring(GetCurrentProcessId()) + L".db");
        const auto encoded = path.u8string();
        void* database = nullptr;
        check(open && execute && close && open(reinterpret_cast<const char*>(encoded.c_str()), &database, 6, nullptr) == 0,
            "The synthetic payload store must be created");
        if (database && execute && close)
        {
            constexpr std::int64_t created = 1'723'456'789'000ll;
            const auto time = std::to_string((created + 11'644'473'600'000ll) * 10'000ll);
            const std::string sql = "CREATE TABLE NotificationHandler(RecordId INTEGER, PrimaryId TEXT);"
                "CREATE TABLE Notification(Id INTEGER,HandlerId INTEGER,Type TEXT,ArrivalTime INTEGER,Payload BLOB,Tag TEXT);"
                "INSERT INTO NotificationHandler VALUES(1,'Tests.App');"
                "INSERT INTO NotificationHandler VALUES(2,'Tests.Electron');"
                "INSERT INTO Notification VALUES(42,1,'toast'," + time + ",'<toast launch=\"chat=one\"><visual><binding><text>Same</text></binding></visual></toast>','first-tag');"
                "INSERT INTO Notification VALUES(43,1,'toast'," + time + ",'<toast launch=\"chat=two\"><visual><binding><text>Same</text></binding></visual></toast>','second-tag');"
                "INSERT INTO Notification VALUES(44,2,'toast'," + time + ",'<toast/>','electron-one');"
                "INSERT INTO Notification VALUES(45,2,'toast'," + time + ",'<toast/>','electron-two');"
                "INSERT INTO Notification VALUES(46,2,'toast'," + time + ",'<toast/>',NULL);"
                "INSERT INTO Notification VALUES(47,1,'toast'," + time + ",'<toast/>','other-framework-tag');";
            check(execute(database, sql.c_str(), nullptr, nullptr, nullptr) == 0, "The payload fixture must contain two identical visual notifications");
            {
                NotificationPayloadStore store(path);
                const auto first = store.read(42, "Tests.App", created);
                const auto second = store.read(43, "Tests.App", created);
                check(first && second && first->arguments == "chat=one" && second->arguments == "chat=two",
                    "Identical visuals must route by native ID to the correct conversation");
                check(!store.read(42, "Other.App", created) && !store.read(42, "Tests.App", created + 60'000) &&
                    !store.read(99, "Tests.App", created), "Wrong senders, reused IDs and absent notifications must be rejected");
            }
            {
                NotificationPayloadStore store(path, [](const std::string& sender) { return sender == "Tests.Electron"; });
                const auto first = store.read(44, "Tests.Electron", created);
                const auto second = store.read(45, "Tests.Electron", created);
                const auto otherFramework = store.read(47, "Tests.App", created);
                check(first && second && first->arguments == "type=click&tag=electron-one" &&
                    second->arguments == "type=click&tag=electron-two",
                    "Empty Electron body arguments must be completed from each record's exact native tag");
                check(!store.read(46, "Tests.Electron", created) && otherFramework && otherFramework->arguments.empty(),
                    "Missing Electron tags must fail safely without rewriting other frameworks' valid empty arguments");
            }
            {
                RegisteredActivationFixture chat(L"ChatAlpha", true), mail(L"MailBeta", true), tasks(L"Tasks", false);
                check(chat.ready && mail.ready && tasks.ready, "Isolated COM callback fixtures must register successfully");
                if (chat.ready && mail.ready && tasks.ready)
                {
                    const auto chatId = winrt::to_string(chat.identity), mailId = winrt::to_string(mail.identity), tasksId = winrt::to_string(tasks.identity);
                    const std::string additional =
                        "INSERT INTO NotificationHandler VALUES(3,'" + chatId + "');"
                        "INSERT INTO NotificationHandler VALUES(4,'" + mailId + "');"
                        "INSERT INTO NotificationHandler VALUES(5,'" + tasksId + "');"
                        "INSERT INTO Notification VALUES(48,3,'toast'," + time + ",'<toast/>','shared-tag');"
                        "INSERT INTO Notification VALUES(49,4,'toast'," + time + ",'<toast/>','shared-tag');"
                        "INSERT INTO Notification VALUES(50,5,'toast'," + time + ",'<toast launch=\"task=42\"/>','shared-tag');"
                        "INSERT INTO Notification VALUES(51,5,'toast'," + time + ",'<toast/>','shared-tag');";
                    check(execute(database, additional.c_str(), nullptr, nullptr, nullptr) == 0,
                        "Independent senders sharing the same native tag must be created");
                    NotificationPayloadStore store(path);
                    const auto chatDestination = store.read(48, chatId, created), mailDestination = store.read(49, mailId, created);
                    check(registered_electron_toast_sender(chatId) && registered_electron_toast_sender(mailId) &&
                        !registered_electron_toast_sender(tasksId),
                        "Framework detection must depend on each sender's actual registration, without an application-name allowlist");
                    check(chatDestination && mailDestination &&
                        invoke_toast_destination(chatId, chatDestination->arguments, chatDestination->kind) &&
                        invoke_toast_destination(mailId, mailDestination->arguments, mailDestination->kind) &&
                        chat.receiver.calls == 1u && mail.receiver.calls == 1u &&
                        chat.receiver.identity == chat.identity && mail.receiver.identity == mail.identity &&
                        chat.receiver.payload == L"type=click&tag=shared-tag" && mail.receiver.payload == L"type=click&tag=shared-tag",
                        "Identical tags in unrelated Electron senders must reach their respective registered callbacks");
                    const auto taskDestination = store.read(50, tasksId, created), emptyDestination = store.read(51, tasksId, created);
                    check(taskDestination && taskDestination->arguments == "task=42" &&
                        invoke_toast_destination(tasksId, taskDestination->arguments, taskDestination->kind) &&
                        tasks.receiver.identity == tasks.identity && tasks.receiver.payload == L"task=42",
                        "Non-Electron apps must receive their own original event arguments through the real COM callback");
                    check(emptyDestination && emptyDestination->arguments.empty() &&
                        invoke_toast_destination(tasksId, emptyDestination->arguments, emptyDestination->kind) &&
                        tasks.receiver.calls == 2u && tasks.receiver.payload.empty(),
                        "Valid empty non-Electron callback arguments must not be replaced with Electron's tag protocol");
                    check(!store.read(48, mailId, created) && !invoke_toast_destination(chatId, "", 1u) &&
                        chat.receiver.calls == 1u,
                        "Cross-sender identities and empty Electron calls must not invoke an unrelated or unidentified notification");
                }
            }
            check(execute(database, "INSERT INTO Notification SELECT * FROM Notification WHERE Id=42;", nullptr, nullptr, nullptr) == 0,
                "A duplicate native ID fixture must be created");
            {
                NotificationPayloadStore store(path);
                check(!store.read(42, "Tests.App", created), "An ambiguous native identity must never select a conversation");
            }
            close(database);
            std::error_code error;
            std::filesystem::remove(path, error);
        }
        FreeLibrary(sqlite);
    }
    return failures ? 1 : 0;
}

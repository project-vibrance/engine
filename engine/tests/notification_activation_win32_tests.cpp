#define NOMINMAX
#include <initguid.h>
#include "../src/platform/win32/notification_activation.h"
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
    if (chat && protocol)
    {
        check(match_toast_destination({ *chat, *chat }, "Sender", "Message").has_value(), "Repeated identical destinations are unambiguous");
        check(!match_toast_destination({ *chat, *protocol }, "Sender", "Message"), "Identical text for different destinations must never select the wrong conversation");
        check(!match_toast_destination({ *chat }, "Other sender", "Message"), "A card must match its own toast visual content");
    }
    ActivationSpy spy;
    check(dispatch_toast_callback(&spy, L"Tests.App", L"conversation=42&message=17") &&
        spy.calls == 1u && spy.identity == L"Tests.App" && spy.payload == L"conversation=42&message=17",
        "Card activation must invoke the real notification callback with the original identity and arguments");
    spy.result = E_FAIL;
    check(!dispatch_toast_callback(&spy, L"Tests.App", L"retry"), "Failed destination invocation must be reported to the model");
    check(!dispatch_toast_callback(nullptr, L"Tests.App", L""), "Missing callbacks must not count as successful activation");
    return failures ? 1 : 0;
}

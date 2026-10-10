#pragma once

#include <objbase.h>
#include <oleauto.h>
#include <UIAutomation.h>
#include <winrt/base.h>
#include "../../media/playback_quality_detection.h"
#include <chrono>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

// Native COM objects stay on a dedicated MTA worker. Audio capture, transport
// commands and renderer snapshot reads never wait for an accessibility provider.
class Win32PlaybackQualityProbe
{
public:
    struct Request
    {
        std::uint32_t processId = 0u;
        std::wstring sourceApp;
        std::wstring title;
        std::wstring artist;
        bool active = false;
        std::wstring album;
        std::int64_t durationMilliseconds = 0;

        bool operator==(const Request&) const = default;
    };
    using WindowResolver = std::function<HWND(const Request&)>;

    explicit Win32PlaybackQualityProbe(WindowResolver resolver) :
        resolveWindow(std::move(resolver)), worker([this] { run(); }) {}

    ~Win32PlaybackQualityProbe()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        worker.join();
    }

    MediaPlaybackQuality refresh(Request next)
    {
        std::lock_guard lock(mutex);
        if (request != next)
        {
            request = std::move(next);
            ++generation;
            quality = MediaPlaybackQuality::eUnknown;
            wake.notify_all();
        }
        if (!request.active || request.processId == 0u ||
            std::chrono::steady_clock::now() - sampledAt > std::chrono::milliseconds(2500))
            return MediaPlaybackQuality::eUnknown;
        return quality;
    }

private:
    static media_quality_detection::Role role(CONTROLTYPEID type)
    {
        using media_quality_detection::Role;
        switch (type)
        {
            case UIA_ButtonControlTypeId: return Role::eButton;
            case UIA_TextControlTypeId:
            case UIA_ImageControlTypeId: return Role::eText;
            case UIA_WindowControlTypeId: return Role::eWindow;
            case UIA_ListControlTypeId:
            case UIA_ListItemControlTypeId:
            case UIA_DataGridControlTypeId:
            case UIA_DataItemControlTypeId:
            case UIA_TableControlTypeId:
            case UIA_TreeControlTypeId:
            case UIA_TreeItemControlTypeId:
            case UIA_MenuControlTypeId:
            case UIA_MenuItemControlTypeId:
            case UIA_CheckBoxControlTypeId:
            case UIA_RadioButtonControlTypeId:
            case UIA_ComboBoxControlTypeId: return Role::eExcluded;
            default: return Role::eContainer;
        }
    }

    static bool flatten(IUIAutomationElement* element, int parent, unsigned depth,
        std::vector<media_quality_detection::Node>& nodes)
    {
        if (!element || depth > 40u || nodes.size() >= 4096u) return false;
        media_quality_detection::Node node;
        node.parent = parent;
        CONTROLTYPEID type = 0;
        BOOL offscreen = TRUE;
        if (FAILED(element->get_CachedControlType(&type)) ||
            FAILED(element->get_CachedIsOffscreen(&offscreen))) return false;
        node.role = role(type);
        node.visible = offscreen == FALSE;
        BSTR text = nullptr;
        if (SUCCEEDED(element->get_CachedName(&text)) && text)
            node.name.assign(text, SysStringLen(text));
        SysFreeString(text);
        text = nullptr;
        if (SUCCEEDED(element->get_CachedAutomationId(&text)) && text)
            node.automationId.assign(text, SysStringLen(text));
        SysFreeString(text);
        const int index = static_cast<int>(nodes.size());
        nodes.push_back(std::move(node));
        winrt::com_ptr<IUIAutomationElementArray> children;
        if (FAILED(element->GetCachedChildren(children.put()))) return false;
        if (!children) return true;
        int count = 0;
        if (FAILED(children->get_Length(&count))) return false;
        for (int i = 0; i < count; ++i)
        {
            winrt::com_ptr<IUIAutomationElement> child;
            if (FAILED(children->GetElement(i, child.put())) ||
                !flatten(child.get(), index, depth + 1u, nodes)) return false;
        }
        return true;
    }

    static MediaPlaybackQuality sample(IUIAutomation2* automation,
        IUIAutomationCacheRequest* cache, HWND window, const Request& request)
    {
        if (!window || !IsWindowVisible(window) || IsIconic(window))
            return MediaPlaybackQuality::eUnknown;
        winrt::com_ptr<IUIAutomationElement> root;
        if (FAILED(automation->ElementFromHandleBuildCache(window, cache, root.put())))
            return MediaPlaybackQuality::eUnknown;
        std::vector<media_quality_detection::Node> nodes;
        if (!flatten(root.get(), -1, 0u, nodes)) return MediaPlaybackQuality::eUnknown;
        return media_quality_detection::detect(nodes, request.title, request.artist, request.album);
    }

    void run() noexcept
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct ApartmentExit { ~ApartmentExit() { winrt::uninit_apartment(); } } apartment;
            winrt::com_ptr<IUIAutomation2> automation;
            if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(automation.put())))) return;
            // Hung/inaccessible players fail closed instead of blocking shutdown.
            if (FAILED(automation->put_ConnectionTimeout(250u)) ||
                FAILED(automation->put_TransactionTimeout(500u)) ||
                FAILED(automation->put_AutoSetFocus(FALSE))) return;
            winrt::com_ptr<IUIAutomationCacheRequest> cache;
            winrt::com_ptr<IUIAutomationCondition> all;
            if (FAILED(automation->CreateCacheRequest(cache.put())) ||
                FAILED(automation->CreateTrueCondition(all.put())) ||
                FAILED(cache->put_TreeScope(TreeScope_Subtree)) ||
                FAILED(cache->put_TreeFilter(all.get())) ||
                FAILED(cache->put_AutomationElementMode(AutomationElementMode_None))) return;
            for (const PROPERTYID property : { UIA_NamePropertyId, UIA_AutomationIdPropertyId,
                UIA_ControlTypePropertyId, UIA_IsOffscreenPropertyId })
                if (FAILED(cache->AddProperty(property))) return;

            std::unique_lock lock(mutex);
            while (!stopping)
            {
                wake.wait(lock, [this] { return stopping ||
                    (request.active && request.processId != 0u); });
                if (stopping) break;
                const Request current = request;
                const auto version = generation;
                lock.unlock();
                const auto detected = sample(automation.get(), cache.get(),
                    resolveWindow(current), current);
                lock.lock();
                if (generation == version)
                {
                    quality = detected;
                    sampledAt = std::chrono::steady_clock::now();
                }
                wake.wait_for(lock, std::chrono::seconds(1),
                    [this, version] { return stopping || generation != version; });
            }
        }
        catch (...) { /* Missing accessibility providers leave quality unknown. */ }
    }

    WindowResolver resolveWindow;
    std::mutex mutex;
    std::condition_variable wake;
    Request request;
    MediaPlaybackQuality quality = MediaPlaybackQuality::eUnknown;
    std::chrono::steady_clock::time_point sampledAt {};
    std::uint64_t generation = 0u;
    bool stopping = false;
    std::thread worker;
};

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "playback_quality_probe.h"
#include "../../media/playback_seek_policy.h"
#include <winrt/Windows.Media.Control.h>
#include <optional>

class Win32MediaSeekFallback
{
public:
    struct Request
    {
        Win32PlaybackQualityProbe::Request source;
        std::int64_t positionMilliseconds = 0;
        std::uint64_t generation = 0u;
    };

    explicit Win32MediaSeekFallback(Win32PlaybackQualityProbe::WindowResolver resolver) :
        resolveWindow(std::move(resolver)), worker([this] { run(); }) {}

    ~Win32MediaSeekFallback()
    {
        {
            std::lock_guard lock(mutex);
            stopping = true;
        }
        wake.notify_all();
        worker.join();
    }

    void enqueue(Request request)
    {
        {
            std::lock_guard lock(mutex);
            pending = std::move(request);
            ++generation;
        }
        wake.notify_one();
    }

    void cancel()
    {
        std::lock_guard lock(mutex);
        pending.reset();
        ++generation;
    }

    static bool matches_session(
        const winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSession& session,
        const Request& request)
    {
        if (!session) return false;
        const auto sourceApp = session.SourceAppUserModelId();
        const auto properties = session.TryGetMediaPropertiesAsync().get();
        const auto title = properties.Title();
        const auto artist = properties.Artist();
        const auto album = properties.AlbumTitle();
        return vibrance::media::PlaybackSeekIdentity { sourceApp, title, artist, album } ==
            vibrance::media::PlaybackSeekIdentity { request.source.sourceApp,
                request.source.title, request.source.artist, request.source.album };
    }

private:
    static bool same_media(
        const winrt::Windows::Media::Control::GlobalSystemMediaTransportControlsSessionManager& manager,
        const Request& request)
    {
        return matches_session(manager.GetCurrentSession(), request);
    }

    static bool excluded(CONTROLTYPEID type)
    {
        switch (type)
        {
            case UIA_ListControlTypeId:
            case UIA_ListItemControlTypeId:
            case UIA_DataGridControlTypeId:
            case UIA_DataItemControlTypeId:
            case UIA_TableControlTypeId:
            case UIA_TreeControlTypeId:
            case UIA_TreeItemControlTypeId:
            case UIA_MenuControlTypeId:
            case UIA_MenuItemControlTypeId: return true;
            default: return false;
        }
    }

    static bool collect(IUIAutomationElement* element, unsigned depth,
        const Request& request, bool& titleFound,
        std::vector<vibrance::media::PlaybackSlider>& sliders,
        std::vector<winrt::com_ptr<IUIAutomationRangeValuePattern>>& patterns,
        unsigned& nodeCount, bool minimised)
    {
        if (!element || depth > 40u || ++nodeCount > 4096u) return false;
        CONTROLTYPEID type = 0;
        BOOL offscreen = TRUE;
        if (FAILED(element->get_CachedControlType(&type)) ||
            FAILED(element->get_CachedIsOffscreen(&offscreen))) return false;
        if ((offscreen && !minimised) || excluded(type)) return true;
        BSTR text = nullptr;
        std::wstring name, automationId;
        if (SUCCEEDED(element->get_CachedName(&text)) && text)
            name.assign(text, SysStringLen(text));
        SysFreeString(text);
        text = nullptr;
        if (SUCCEEDED(element->get_CachedAutomationId(&text)) && text)
            automationId.assign(text, SysStringLen(text));
        SysFreeString(text);
        titleFound |= !request.source.title.empty() &&
            media_quality_detection::normalise(name) ==
                media_quality_detection::normalise(request.source.title);
        if (type == UIA_SliderControlTypeId)
        {
            vibrance::media::PlaybackSlider slider;
            slider.name = std::move(name);
            slider.automationId = std::move(automationId);
            BOOL enabled = FALSE;
            winrt::com_ptr<IUIAutomationRangeValuePattern> pattern;
            if (SUCCEEDED(element->get_CachedIsEnabled(&enabled)) && enabled &&
                SUCCEEDED(element->GetCachedPatternAs(UIA_RangeValuePatternId,
                    IID_PPV_ARGS(pattern.put()))) && pattern)
            {
                BOOL readOnly = TRUE;
                if (SUCCEEDED(pattern->get_CachedIsReadOnly(&readOnly)) &&
                    SUCCEEDED(pattern->get_CachedMinimum(&slider.minimum)) &&
                    SUCCEEDED(pattern->get_CachedMaximum(&slider.maximum)))
                {
                    slider.enabled = true;
                    slider.visible = true;
                    slider.readOnly = readOnly != FALSE;
                    sliders.push_back(std::move(slider));
                    patterns.push_back(std::move(pattern));
                }
            }
        }
        winrt::com_ptr<IUIAutomationElementArray> children;
        if (FAILED(element->GetCachedChildren(children.put()))) return false;
        if (!children) return true;
        int count = 0;
        if (FAILED(children->get_Length(&count))) return false;
        for (int index = 0; index < count; ++index)
        {
            winrt::com_ptr<IUIAutomationElement> child;
            if (FAILED(children->GetElement(index, child.put())) ||
                !collect(child.get(), depth + 1u, request, titleFound,
                    sliders, patterns, nodeCount, minimised)) return false;
        }
        return true;
    }

    void run() noexcept
    {
        try
        {
            winrt::init_apartment(winrt::apartment_type::multi_threaded);
            struct ApartmentExit { ~ApartmentExit() { winrt::uninit_apartment(); } } apartment;
            const auto manager = winrt::Windows::Media::Control::
                GlobalSystemMediaTransportControlsSessionManager::RequestAsync().get();
            winrt::com_ptr<IUIAutomation2> automation;
            if (FAILED(CoCreateInstance(CLSID_CUIAutomation8, nullptr, CLSCTX_INPROC_SERVER,
                IID_PPV_ARGS(automation.put()))) ||
                FAILED(automation->put_ConnectionTimeout(250u)) ||
                FAILED(automation->put_TransactionTimeout(500u)) ||
                FAILED(automation->put_AutoSetFocus(FALSE))) return;
            winrt::com_ptr<IUIAutomationCacheRequest> cache;
            winrt::com_ptr<IUIAutomationCondition> all;
            if (FAILED(automation->CreateCacheRequest(cache.put())) ||
                FAILED(automation->CreateTrueCondition(all.put())) ||
                FAILED(cache->put_TreeScope(TreeScope_Subtree)) ||
                FAILED(cache->put_TreeFilter(all.get())) ||
                FAILED(cache->put_AutomationElementMode(AutomationElementMode_Full))) return;
            for (const PROPERTYID property : { UIA_NamePropertyId, UIA_AutomationIdPropertyId,
                UIA_ControlTypePropertyId, UIA_IsOffscreenPropertyId, UIA_IsEnabledPropertyId,
                UIA_RangeValueIsReadOnlyPropertyId, UIA_RangeValueMinimumPropertyId,
                UIA_RangeValueMaximumPropertyId })
                if (FAILED(cache->AddProperty(property))) return;
            if (FAILED(cache->AddPattern(UIA_RangeValuePatternId))) return;
            for (;;)
            {
                Request request;
                std::uint64_t version = 0u;
                {
                    std::unique_lock lock(mutex);
                    wake.wait(lock, [this] { return stopping || pending.has_value(); });
                    if (stopping) return;
                    request = std::move(*pending);
                    pending.reset();
                    version = generation;
                }
                try
                {
                    if (!same_media(manager, request)) continue;
                    const HWND window = resolveWindow(request.source);
                    if (!window || !IsWindowVisible(window)) continue;
                    winrt::com_ptr<IUIAutomationElement> root;
                    if (FAILED(automation->ElementFromHandleBuildCache(window, cache.get(), root.put()))) continue;
                    bool titleFound = false;
                    unsigned nodeCount = 0u;
                    std::vector<vibrance::media::PlaybackSlider> sliders;
                    std::vector<winrt::com_ptr<IUIAutomationRangeValuePattern>> patterns;
                    if (!collect(root.get(), 0u, request, titleFound, sliders, patterns, nodeCount,
                            IsIconic(window) != FALSE) ||
                        !titleFound) continue;
                    const auto index = vibrance::media::playback_slider_index(
                        sliders, request.source.durationMilliseconds);
                    if (!index || !same_media(manager, request)) continue;
                    {
                        std::lock_guard lock(mutex);
                        if (stopping || version != generation) continue;
                    }
                    patterns[*index]->SetValue(vibrance::media::playback_slider_value(
                        sliders[*index], request.positionMilliseconds, request.source.durationMilliseconds));
                }
                catch (...) {}
            }
        }
        catch (...) {}
    }

    Win32PlaybackQualityProbe::WindowResolver resolveWindow;
    std::mutex mutex;
    std::condition_variable wake;
    std::optional<Request> pending;
    std::uint64_t generation = 0u;
    bool stopping = false;
    std::thread worker;
};

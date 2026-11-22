// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/engine.h>
#include <vibranceUI/ui/colour_palette_panel.h>
#include <array>
#include <memory>

class StarterApplication
{
public:
    int run(bool smokeTest = false);

private:
    UiTheme theme() const;
    std::string tr(std::string_view key, std::string_view fallback) const;
    void build(UiWindowContext& view);
    void build_controls(UiWindowContext& view, entt::entity page, float width);
    void build_graphics(UiWindowContext& view, entt::entity page, float width);
    void build_services(UiWindowContext& view, entt::entity page, float width);
    entt::entity label(UiWindowContext& view, entt::entity parent, std::string value,
        glm::vec2 position, float width, float size = 14.0f, bool muted = false);
    entt::entity card(UiWindowContext& view, entt::entity parent, glm::vec2 position, glm::vec2 size);
    UiControlHandle button(UiWindowContext& view, entt::entity parent, std::string caption,
        glm::vec2 position, float width, std::function<void()> clicked, bool primary = false);
    void set_label(entt::entity entity, std::string value);
    void set_status(std::string value);
    void apply_material();
    void update_level();
    void update_timer();
    void choose_colour();
    void choose_media();
    void refresh_theme();
    void smoke_test();

    std::filesystem::path assets;
    std::filesystem::path font;
    std::unique_ptr<UiWindow> window;
    std::unique_ptr<UiColourPalettePanel> palette;
    std::unique_ptr<GlobalMediaSession> mediaSession;
    Engine* engine = nullptr;
    Media2DHandle sampleImage;
    Media2DHandle displayedImage;
    AudioClipHandle sampleAudio;
    AudioVoiceHandle voice;
    CountdownTimer timer { 30u };
    UiPageStackHandle pages;
    std::array<entt::entity, 3> pageRoots {};
    entt::entity statusLabel = entt::null;
    entt::entity echoLabel = entt::null;
    entt::entity levelLabel = entt::null;
    entt::entity levelRing = entt::null;
    entt::entity timerLabel = entt::null;
    entt::entity timerRing = entt::null;
    entt::entity glassPreview = entt::null;
    entt::entity colourSwatch = entt::null;
    entt::entity mediaPreview = entt::null;
    UiControlHandle actionButton;
    UiControlHandle levelSlider;
    UiControlHandle inputField;
    UiControlHandle optionCheckbox;
    std::string inputValue;
    std::string status;
    std::string locale = "en_gb";
    std::size_t pageIndex = 0u;
    int selectedFormat = 0;
    unsigned clicks = 0u;
    bool dark = true;
    bool actionEnabled = true;
    bool checked = false;
    bool rebuildPending = false;
    bool liquid = true;
    float level = 55.0f;
    float blur = 15.0f;
    float refraction = 70.0f;
    std::uint32_t tint = 0x6699ffu;
};

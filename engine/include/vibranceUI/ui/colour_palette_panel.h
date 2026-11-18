// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/glfw/panel_window.h>
#include <vibranceUI/ui/resources.h>
#include <vibranceUI/ui/glass.h>
#include <array>
#include <cstdint>
#include <vector>
#include <functional>
#include <memory>
#include <optional>
#include <string>

namespace colour_palette { struct ImageSource; }

struct UiColourPalettePanelCreateInfo
{
    UiResourceDirectories resources;
    UiTheme theme = ui_light_theme();
    std::array<std::filesystem::path, 5> tabIconPaths {};
    std::filesystem::path chevronIconPath;
    std::filesystem::path checkIconPath;
    std::filesystem::path samplerIconPath;
    std::filesystem::path pencilArtworkPath;
    std::string pencilColourMarker = "#FF00FF";
    std::vector<std::filesystem::path> localisationDirectories;
    std::function<void(EngineCreateInfo&, const UiResourceDirectories&, std::string_view)> configureEngine;
    std::function<void(GlfwPanelWindowTemplateOptions&, const UiResourceDirectories&, std::string_view)> configurePanel;
    std::function<UiGlassOptions(const UiTheme&)> controlMaterial;
    std::string locale = "en_gb";
    float uiScale = 0.85f;
    double previewIntervalSeconds = 1.0 / 30.0;
    bool livePreview = true;
    bool focusOnShow = true;
    std::optional<GlfwWindowPositionOptions> positioning = GlfwWindowPositionOptions {};
};

class VIBRANCE_ENGINE_API UiColourPalettePanel
{
public:
    static constexpr glm::ivec2 logicalSize { 416, 736 };
    using Change = std::function<void(std::uint32_t)>;
    explicit UiColourPalettePanel(UiColourPalettePanelCreateInfo createInfo = {});
    ~UiColourPalettePanel();
    UiColourPalettePanel(const UiColourPalettePanel&) = delete;
    UiColourPalettePanel& operator=(const UiColourPalettePanel&) = delete;
    void request_open(std::uint32_t colour, std::string targetLabel,
        Change onPreview, Change onCommit = {});
    void tick();
    void close(bool immediately = false);
    bool is_open() const;
    bool has_pointer_capture() const;
    bool is_focused() const;
    bool is_hovered() const;
    bool busy() const;
    std::uint32_t colour() const;
    std::size_t active_tab() const;
    void select_tab(std::size_t tab);
    void set_appearance(UiTheme theme, std::string locale);
    void import_image(colour_palette::ImageSource source);
    GlfwPanelWindowHost* host();
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

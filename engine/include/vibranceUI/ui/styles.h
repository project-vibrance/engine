#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>
#include <glm/glm.hpp>
#include <vibranceUI/export.h>
#include <vibranceUI/localisation/localisation.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <vibranceUI/ui/layout.h>

inline ShapeStyleComponent make_solid_style(
    glm::vec4 color,
    glm::vec4 outlineColor = glm::vec4(0.0f),
    float outlineWidth = 0.0f,
    float opacity = 1.0f)
{
    // Solid styles are the base for controls, cards, masks and clear surfaces
    ShapeStyleComponent style = {};
    style.fill = Renderer2DFill::eSolid;
    style.color0 = color;
    style.color1 = color;
    style.outlineColor = outlineColor;
    style.outlineWidth = outlineWidth;
    style.edgeSoftness = 1.25f;
    style.opacity = opacity;
    return style;
}

inline ShapeStyleComponent make_solid_style(
    std::string_view color,
    std::string_view outlineColor = "#00000000",
    float outlineWidth = 0.0f,
    float opacity = 1.0f)
{
    return make_solid_style(
        renderer2d_hex_color(color),
        renderer2d_hex_color(outlineColor, glm::vec4(0.0f)),
        outlineWidth,
        opacity);
}

inline ShapeStyleComponent make_gradient_style(
    glm::vec4 color0,
    glm::vec4 color1,
    glm::vec4 outlineColor = glm::vec4(0.0f),
    float outlineWidth = 0.0f,
    float opacity = 1.0f)
{
    // Gradients default to diagonal; callers can override gradientStart and gradientEnd
    ShapeStyleComponent style = make_solid_style(color0, outlineColor, outlineWidth, opacity);
    style.fill = Renderer2DFill::eLinearGradient;
    style.color1 = color1;
    style.gradientStart = { 0.0f, 0.0f };
    style.gradientEnd = { 1.0f, 1.0f };
    return style;
}

inline ShapeStyleComponent make_gradient_style(
    std::string_view color0,
    std::string_view color1,
    std::string_view outlineColor = "#00000000",
    float outlineWidth = 0.0f,
    float opacity = 1.0f)
{
    return make_gradient_style(
        renderer2d_hex_color(color0),
        renderer2d_hex_color(color1),
        renderer2d_hex_color(outlineColor, glm::vec4(0.0f)),
        outlineWidth,
        opacity);
}

inline ShapeStyleComponent make_frosted_panel_style(
    glm::vec4 topColor,
    glm::vec4 bottomColor,
    glm::vec4 outlineColor,
    float outlineWidth,
    float opacity,
    float backdropBlurRadius,
    uint32_t backdropBlurPasses,
    float backdropBlurOpacity)
{
    // Frosted panels combine a vertical gradient with backdrop blur settings
    ShapeStyleComponent style = make_gradient_style(topColor, bottomColor, outlineColor, outlineWidth, opacity);
    style.gradientStart = { 0.0f, 0.0f };
    style.gradientEnd = { 0.0f, 1.0f };
    style.set_backdrop_blur(backdropBlurRadius, backdropBlurPasses, backdropBlurOpacity);
    return style;
}

inline ShapeStyleComponent make_frosted_panel_style(
    std::string_view topColor,
    std::string_view bottomColor,
    std::string_view outlineColor,
    float outlineWidth,
    float opacity,
    float backdropBlurRadius,
    uint32_t backdropBlurPasses,
    float backdropBlurOpacity)
{
    return make_frosted_panel_style(
        renderer2d_hex_color(topColor),
        renderer2d_hex_color(bottomColor),
        renderer2d_hex_color(outlineColor, glm::vec4(0.0f)),
        outlineWidth,
        opacity,
        backdropBlurRadius,
        backdropBlurPasses,
        backdropBlurOpacity);
}

inline TextStyleComponent make_text_style(glm::vec4 color0, glm::vec4 color1, glm::vec4 effectColor)
{
    TextStyleComponent style = {};
    style.fill = Renderer2DFill::eLinearGradient;
    style.color0 = color0;
    style.color1 = color1;
    style.effectColor = effectColor;
    style.set_shadow_color("#000A0FB8");
    style.gradientStart = { 0.0f, 0.0f };
    style.gradientEnd = { 1.0f, 0.7f };
    style.outlineWidth = 1.5f;
    style.shadowOffset = { 3.0f, 4.0f };
    style.shadowBlur = 8.0f;
    style.glowRadius = 9.0f;
    style.opacity = 1.0f;
    return style;
}

inline TextStyleComponent make_text_style(
    std::string_view color0,
    std::string_view color1,
    std::string_view effectColor)
{
    return make_text_style(
        renderer2d_hex_color(color0),
        renderer2d_hex_color(color1),
        renderer2d_hex_color(effectColor, glm::vec4(0.0f)));
}

inline TextStyleComponent make_flat_text_style(std::string_view color)
{
    // Flat text deliberately has no glow, outline, or shadow
    TextStyleComponent style = {};
    style.set_color(color);
    style.shadowColor = glm::vec4(0.0f);
    style.effectColor = glm::vec4(0.0f);
    return style;
}

inline TextStyleComponent make_label_text_style(
    std::string_view color,
    const LayoutScale& scale,
    bool glow = false)
{
    TextStyleComponent style = {};
    style.set_color(color);
    style.set_shadow_color("#000000A0");
    style.shadowOffset = scaled_offset(1.0f, 2.0f, scale);
    style.shadowBlur = scaled_scalar(5.0f, scale);
    if (glow)
    {
        style.set_effect_color("#69F5D699");
        style.glowRadius = scaled_scalar(7.0f, scale);
    }
    return style;
}

struct UiSystemAccentPalette
{
    // The native accent is preserved verbatim; derived tones provide
    // consistent hover, press, and vertical-gradient states to controls.
    std::string accent = "#007AFFFF";
    std::string hovered = "#1487FFFF";
    std::string pressed = "#006BE0FF";
    std::string gradientBottom = "#0066D6FF";
};

VIBRANCE_ENGINE_API UiSystemAccentPalette ui_system_accent_palette(
    float lowerTone = 0.84f);

struct UiTheme
{
    // Theme values are strings so JSON can use hex, rgba, or future colour formats
    struct SettingsPanel
    {
        std::string sidebarSurface = "rgba(242, 243, 246, 0.88)";
        std::string contentSurface = "rgba(255, 255, 255, 0.99)";
        std::string cardSurface = "rgba(250, 250, 251, 1)";
        std::string cardOutline = "rgba(0, 0, 0, 0.07)";
        std::string separator = "rgba(0, 0, 0, 0.08)";
        std::string sidebarFadeSolid = "rgba(242, 243, 246, 0.96)";
        std::string sidebarFadeTransparent = "rgba(242, 243, 246, 0)";
        std::string scrollbarThumb = "rgba(70, 74, 82, 0.32)";
        std::string scrollbarThumbOutline = "rgba(255, 255, 255, 0.27)";
        std::string searchPlaceholder = "rgba(110, 110, 115, 0.91)";
        std::string searchBackground = "rgba(255, 255, 255, 0.36)";
        std::string searchBackgroundBottom = "rgba(230, 233, 239, 0.28)";
        std::string searchFocusedBackground = "rgba(255, 255, 255, 0.58)";
        std::string searchFocusedBackgroundBottom = "rgba(242, 244, 248, 0.42)";
        std::string searchOutline = "rgba(0, 0, 0, 0.2)";
        std::string navBackground = "rgba(255, 255, 255, 0.42)";
        std::string navBackgroundBottom = "rgba(235, 238, 244, 0.32)";
        std::string navOutline = "rgba(255, 255, 255, 0.45)";
        std::string navDivider = "rgba(0, 0, 0, 0.08)";
        std::string navIcon = "rgba(85, 88, 94, 1)";
        std::string navIconDisabled = "rgba(177, 180, 186, 1)";
        std::string sidebarRowSelected = "rgba(0, 113, 227, 1)";
        std::string sidebarRowSelectedHover = "rgba(0, 103, 207, 1)";
        std::string sidebarRowSelectedPressed = "rgba(0, 92, 190, 1)";
        std::string sidebarRowHover = "rgba(255, 255, 255, 0.55)";
        std::string sidebarRowPressed = "rgba(230, 230, 232, 0.75)";
        std::string sidebarLabel = "rgba(44, 45, 48, 1)";
        std::string sidebarSubLabel = "rgba(23, 24, 26, 1)";
        std::string sidebarSelectedLabel = "rgba(255, 255, 255, 1)";
        std::string avatarBackground = "rgba(217, 161, 132, 1)";
        std::string avatarText = "rgba(91, 52, 29, 1)";
        std::string previewLabel = "rgba(115, 119, 126, 1)";
        std::string selectedPreviewLabel = "rgba(29, 29, 31, 1)";
    };

    std::string surface = "rgba(255, 255, 255, 1)";
    std::string surfaceElevated = "rgba(248, 248, 248, 0.94)";
    std::string surfaceMuted = "rgba(231, 231, 234, 0.94)";
    std::string outline = "rgba(0, 0, 0, 0.12)";
    std::string outlineStrong = "rgba(0, 0, 0, 0.22)";
    std::string text = "rgba(29, 29, 31, 1)";
    std::string textMuted = "rgba(112, 112, 117, 0.91)";
    std::string icon = "rgba(29, 29, 31, 0.85)";
    std::string accent = "rgba(0, 122, 255, 1)";
    std::string accentHover = "rgba(19, 135, 255, 1)";
    std::string accentPressed = "rgba(0, 104, 221, 1)";
    std::string danger = "rgba(255, 90, 98, 1)";
    std::string warning = "rgba(255, 189, 46, 1)";
    std::string success = "rgba(41, 200, 64, 1)";
    std::string disabled = "rgba(89, 99, 109, 0.58)";
    SettingsPanel settingsPanel {};

    ShapeStyleComponent panel_style(float outlineWidth = 1.0f, float opacity = 1.0f) const
    {
        return make_solid_style(surface, outline, outlineWidth, opacity);
    }

    ShapeStyleComponent elevated_style(float outlineWidth = 0.8f, float opacity = 1.0f) const
    {
        return make_solid_style(surfaceElevated, outline, outlineWidth, opacity);
    }

    ShapeStyleComponent muted_style(float outlineWidth = 0.0f, float opacity = 1.0f) const
    {
        return make_solid_style(surfaceMuted, "rgba(0, 0, 0, 0)", outlineWidth, opacity);
    }

    ShapeStyleComponent settings_card_style(float outlineWidth = 0.8f, float opacity = 1.0f) const
    {
        // Settings cards use the dedicated panel palette rather than generic surfaces
        return make_solid_style(settingsPanel.cardSurface, settingsPanel.cardOutline, outlineWidth, opacity);
    }

    ShapeStyleComponent settings_sidebar_style(float opacity = 1.0f) const
    {
        return make_solid_style(settingsPanel.sidebarSurface, "rgba(0, 0, 0, 0)", 0.0f, opacity);
    }

    ShapeStyleComponent settings_content_style(float opacity = 1.0f) const
    {
        return make_solid_style(settingsPanel.contentSurface, "rgba(0, 0, 0, 0)", 0.0f, opacity);
    }

    TextStyleComponent text_style(float fontWeight = 400.0f) const
    {
        TextStyleComponent style = make_flat_text_style(text);
        style.set_font_weight(fontWeight);
        return style;
    }

    TextStyleComponent text_style(std::string_view color, float fontWeight) const
    {
        TextStyleComponent style = make_flat_text_style(color);
        style.set_font_weight(fontWeight);
        return style;
    }

    TextStyleComponent muted_text_style(float fontWeight = 400.0f) const
    {
        TextStyleComponent style = make_flat_text_style(textMuted);
        style.set_font_weight(fontWeight);
        return style;
    }
};

inline UiTheme ui_light_theme()
{
    // The default constructed theme is the light baseline
    return {};
}

inline UiTheme ui_dark_theme()
{
    // Dark theme overrides only values that differ from the light baseline
    UiTheme theme = {};
    theme.surface = "rgba(15, 17, 20, 0.96)";
    theme.surfaceElevated = "rgba(28, 32, 38, 0.94)";
    theme.surfaceMuted = "rgba(42, 47, 54, 0.9)";
    theme.outline = "rgba(255, 255, 255, 0.14)";
    theme.outlineStrong = "rgba(255, 255, 255, 0.26)";
    theme.text = "rgba(246, 247, 249, 1)";
    theme.textMuted = "rgba(178, 184, 193, 0.86)";
    theme.icon = "rgba(246, 247, 249, 0.86)";
    theme.accent = "rgba(51, 153, 255, 1)";
    theme.accentHover = "rgba(83, 170, 255, 1)";
    theme.accentPressed = "rgba(28, 130, 225, 1)";
    theme.disabled = "rgba(123, 132, 144, 0.52)";
    theme.settingsPanel.sidebarSurface = "rgba(22, 25, 30, 0.86)";
    theme.settingsPanel.contentSurface = "rgba(13, 15, 18, 0.96)";
    theme.settingsPanel.cardSurface = "rgba(28, 32, 38, 0.94)";
    theme.settingsPanel.cardOutline = "rgba(255, 255, 255, 0.11)";
    theme.settingsPanel.separator = "rgba(255, 255, 255, 0.09)";
    theme.settingsPanel.sidebarFadeSolid = "rgba(22, 25, 30, 0.96)";
    theme.settingsPanel.sidebarFadeTransparent = "rgba(22, 25, 30, 0)";
    theme.settingsPanel.scrollbarThumb = "rgba(218, 224, 233, 0.32)";
    theme.settingsPanel.scrollbarThumbOutline = "rgba(0, 0, 0, 0.22)";
    theme.settingsPanel.searchPlaceholder = "rgba(203, 208, 216, 0.78)";
    theme.settingsPanel.searchBackground = "rgba(255, 255, 255, 0.13)";
    theme.settingsPanel.searchBackgroundBottom = "rgba(255, 255, 255, 0.08)";
    theme.settingsPanel.searchFocusedBackground = "rgba(255, 255, 255, 0.18)";
    theme.settingsPanel.searchFocusedBackgroundBottom = "rgba(255, 255, 255, 0.12)";
    theme.settingsPanel.searchOutline = "rgba(255, 255, 255, 0.18)";
    theme.settingsPanel.navBackground = "rgba(255, 255, 255, 0.12)";
    theme.settingsPanel.navBackgroundBottom = "rgba(255, 255, 255, 0.07)";
    theme.settingsPanel.navOutline = "rgba(255, 255, 255, 0.13)";
    theme.settingsPanel.navDivider = "rgba(255, 255, 255, 0.12)";
    theme.settingsPanel.navIcon = "rgba(220, 225, 233, 0.88)";
    theme.settingsPanel.navIconDisabled = "rgba(141, 149, 160, 0.58)";
    theme.settingsPanel.sidebarRowSelected = theme.accent;
    theme.settingsPanel.sidebarRowSelectedHover = theme.accentHover;
    theme.settingsPanel.sidebarRowSelectedPressed = theme.accentPressed;
    theme.settingsPanel.sidebarRowHover = "rgba(255, 255, 255, 0.10)";
    theme.settingsPanel.sidebarRowPressed = "rgba(255, 255, 255, 0.15)";
    theme.settingsPanel.sidebarLabel = theme.text;
    theme.settingsPanel.sidebarSubLabel = theme.textMuted;
    theme.settingsPanel.sidebarSelectedLabel = "rgba(255, 255, 255, 1)";
    theme.settingsPanel.avatarBackground = "rgba(139, 104, 88, 1)";
    theme.settingsPanel.avatarText = "rgba(255, 232, 214, 1)";
    theme.settingsPanel.previewLabel = theme.textMuted;
    theme.settingsPanel.selectedPreviewLabel = theme.text;
    return theme;
}

VIBRANCE_ENGINE_API bool ui_load_theme_file(const std::filesystem::path& path, UiTheme& theme);

VIBRANCE_ENGINE_API UiTheme ui_theme_from_file(
    const std::filesystem::path& path,
    UiTheme fallback = ui_light_theme());

inline UiTheme ui_theme_from_files(
    const std::vector<std::filesystem::path>& paths,
    UiTheme fallback = ui_light_theme())
{
    // Layers are applied from packaged to user priority. A parser failure
    // leaves the last valid layer intact; partial themes inherit missing keys.
    UiTheme theme = std::move(fallback);
    for (const std::filesystem::path& path : paths)
    {
        ui_load_theme_file(path, theme);
    }
    return theme;
}

inline std::string ui_theme_label_fallback(std::string_view themeName)
{
    // Common theme names get friendly labels while custom files keep their stem
    if (themeName == "theme" || themeName == "light")
    {
        return "Light";
    }
    if (themeName == "dark")
    {
        return "Dark";
    }
    if (themeName.empty())
    {
        return "Theme";
    }

    std::string label(themeName);
    for (char& c : label)
    {
        if (c == '_' || c == '-')
        {
            c = ' ';
        }
    }
    if (!label.empty())
    {
        label[0] = static_cast<char>(std::toupper(static_cast<unsigned char>(label[0])));
    }
    return label;
}

inline Text ui_theme_label_text(std::string_view themeName)
{
    // Theme files can provide labels through settings.theme.<name>
    if (themeName == "theme")
    {
        return ui_tr("settings.theme.light", "Light");
    }
    return ui_tr(
        std::string("settings.theme.") + std::string(themeName),
        ui_theme_label_fallback(themeName));
}

inline std::vector<UiOptionChoice> ui_theme_choices_from_directories(
    const Localisation& localisation,
    const std::vector<std::filesystem::path>& directories,
    std::string_view selectedTheme = {},
    int* selectedIndex = nullptr)
{
    // JSON files from every resource layer become reusable dropdown choices.
    std::vector<std::string> themeCodes;
    const auto addCode = [&themeCodes](std::string code) {
        if (code.empty())
        {
            return;
        }
        if (std::find(themeCodes.begin(), themeCodes.end(), code) == themeCodes.end())
        {
            themeCodes.push_back(std::move(code));
        }
    };

    // "theme" was the original built-in light-theme identifier. Keep
    // accepting it as a saved preference below, but expose only the canonical
    // file-backed name so light.json is not listed as a second Light option.
    addCode("light");
    addCode("dark");

    for (const std::filesystem::path& directory : directories)
    {
        std::error_code error;
        if (!std::filesystem::exists(directory, error) || !std::filesystem::is_directory(directory, error))
        {
            continue;
        }
        for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory, error))
        {
            if (error)
            {
                break;
            }
            if (entry.is_regular_file(error) && entry.path().extension() == ".json")
            {
                addCode(entry.path().stem().string());
            }
        }
    }

    std::vector<UiOptionChoice> choices;
    choices.reserve(themeCodes.size());
    if (selectedIndex)
    {
        *selectedIndex = 0;
    }

    const std::string_view canonicalSelectedTheme =
        selectedTheme == "theme" ? std::string_view("light") : selectedTheme;
    for (std::size_t i = 0; i < themeCodes.size(); ++i)
    {
        choices.push_back({
            themeCodes[i],
            localisation.resolve(ui_theme_label_text(themeCodes[i]))
        });
        if (selectedIndex && themeCodes[i] == canonicalSelectedTheme)
        {
            *selectedIndex = static_cast<int>(i);
        }
    }
    return choices;
}

inline std::vector<UiOptionChoice> ui_theme_choices_from_directory(
    const Localisation& localisation,
    const std::filesystem::path& directory,
    std::string_view selectedTheme = {},
    int* selectedIndex = nullptr)
{
    return ui_theme_choices_from_directories(
        localisation,
        directory.empty()
            ? std::vector<std::filesystem::path> {}
            : std::vector<std::filesystem::path> { directory },
        selectedTheme,
        selectedIndex);
}

inline bool ui_set_media_tint(Renderer2DScene& scene, entt::entity entity, std::string_view color)
{
    // Applies a colour multiplier while preserving the media alpha
    if (entity == entt::null)
    {
        return false;
    }
    Media2DComponent* media = scene.registry().try_get<Media2DComponent>(entity);
    if (!media || !media->set_tint(color))
    {
        return false;
    }
    scene.mark_dirty(entity);
    return true;
}

inline bool ui_set_media_mask_tint(Renderer2DScene& scene, entt::entity entity, std::string_view color)
{
    // Recolours media as a mask, useful for monochrome SVG icons
    if (entity == entt::null)
    {
        return false;
    }
    Media2DComponent* media = scene.registry().try_get<Media2DComponent>(entity);
    if (!media || !media->set_mask_tint(color))
    {
        return false;
    }
    scene.mark_dirty(entity);
    return true;
}

inline bool ui_set_media_tint_as_mask(Renderer2DScene& scene, entt::entity entity, bool enabled = true)
{
    if (entity == entt::null)
    {
        return false;
    }
    Media2DComponent* media = scene.registry().try_get<Media2DComponent>(entity);
    if (!media)
    {
        return false;
    }
    media->set_tint_as_mask(enabled);
    scene.mark_dirty(entity);
    return true;
}

inline bool ui_set_media_rgb_multiplier(Renderer2DScene& scene, entt::entity entity, std::string_view color)
{
    if (entity == entt::null)
    {
        return false;
    }
    Media2DComponent* media = scene.registry().try_get<Media2DComponent>(entity);
    if (!media || !media->set_rgb_multiplier(color))
    {
        return false;
    }
    scene.mark_dirty(entity);
    return true;
}

inline bool ui_set_media_tint_gradient(
    Renderer2DScene& scene,
    entt::entity entity,
    std::string_view startColor,
    std::string_view endColor,
    glm::vec2 start,
    glm::vec2 end)
{
    if (entity == entt::null)
    {
        return false;
    }
    Media2DComponent* media = scene.registry().try_get<Media2DComponent>(entity);
    if (!media || !media->set_tint_gradient(startColor, endColor, start, end))
    {
        return false;
    }
    scene.mark_dirty(entity);
    return true;
}

inline bool ui_set_media_color_adjustment(
    Renderer2DScene& scene,
    entt::entity entity,
    float brightness = 0.0f,
    float contrast = 1.0f,
    float exposureStops = 0.0f,
    float invertAmount = 0.0f)
{
    if (entity == entt::null)
    {
        return false;
    }
    Media2DComponent* media = scene.registry().try_get<Media2DComponent>(entity);
    if (!media)
    {
        return false;
    }
    media->set_color_adjustment(brightness, contrast, exposureStops, invertAmount);
    scene.mark_dirty(entity);
    return true;
}

inline bool ui_clear_media_tint(Renderer2DScene& scene, entt::entity entity)
{
    if (entity == entt::null)
    {
        return false;
    }
    Media2DComponent* media = scene.registry().try_get<Media2DComponent>(entity);
    if (!media)
    {
        return false;
    }
    media->set_tint(glm::vec4(1.0f));
    media->set_tint_as_mask(false);
    scene.mark_dirty(entity);
    return true;
}

#include <vibranceUI/ui/styles.h>

#include <vibranceUI/core/logger.h>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace
{
    using ThemeEntries = std::unordered_map<std::string, std::string>;

    std::string read_text_file(const std::filesystem::path& path)
    {
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            return {};
        }

        std::ostringstream out;
        out << file.rdbuf();
        return out.str();
    }

    class ThemeJsonParser
    {
    public:
        // Flatten theme JSON into dotted keys so files can be organised freely
        explicit ThemeJsonParser(std::string_view source) : source_(source) {}

        bool parse(ThemeEntries& entries)
        {
            skip_ws();
            if (!parse_object("", entries))
            {
                return false;
            }
            skip_ws();
            return done();
        }

    private:
        bool done() const
        {
            return position_ >= source_.size();
        }

        char peek() const
        {
            return done() ? '\0' : source_[position_];
        }

        void skip_ws()
        {
            while (!done() && std::isspace(static_cast<unsigned char>(source_[position_])))
            {
                ++position_;
            }
        }

        bool consume(char expected)
        {
            if (peek() != expected)
            {
                return false;
            }
            ++position_;
            return true;
        }

        bool parse_object(const std::string& prefix, ThemeEntries& entries)
        {
            if (!consume('{'))
            {
                return false;
            }

            skip_ws();
            if (consume('}'))
            {
                return true;
            }

            while (!done())
            {
                std::string key;
                if (!parse_string(key))
                {
                    return false;
                }

                skip_ws();
                if (!consume(':'))
                {
                    return false;
                }

                const std::string path = prefix.empty() ? key : prefix + "." + key;
                skip_ws();
                if (!parse_value(path, entries))
                {
                    return false;
                }

                skip_ws();
                if (consume('}'))
                {
                    return true;
                }
                if (!consume(','))
                {
                    return false;
                }
                skip_ws();
            }
            return false;
        }

        bool parse_value(const std::string& path, ThemeEntries& entries)
        {
            skip_ws();
            if (peek() == '{')
            {
                return parse_object(path, entries);
            }
            if (peek() == '"')
            {
                std::string value;
                if (!parse_string(value))
                {
                    return false;
                }
                entries[path] = std::move(value);
                return true;
            }
            return parse_scalar_or_array(path, entries);
        }

        bool parse_string(std::string& out)
        {
            if (!consume('"'))
            {
                return false;
            }

            while (!done())
            {
                const char ch = source_[position_++];
                if (ch == '"')
                {
                    return true;
                }
                if (ch != '\\')
                {
                    out.push_back(ch);
                    continue;
                }
                if (done())
                {
                    return false;
                }

                const char escaped = source_[position_++];
                switch (escaped)
                {
                case '"':
                case '\\':
                case '/':
                    out.push_back(escaped);
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                default:
                    return false;
                }
            }
            return false;
        }

        bool parse_scalar_or_array(const std::string& path, ThemeEntries& entries)
        {
            // Keep scalar and array values as raw text because colours may use several formats
            const std::size_t start = position_;
            int depth = 0;
            bool inString = false;
            bool escaping = false;

            while (!done())
            {
                const char ch = source_[position_];
                if (inString)
                {
                    if (escaping)
                    {
                        escaping = false;
                    }
                    else if (ch == '\\')
                    {
                        escaping = true;
                    }
                    else if (ch == '"')
                    {
                        inString = false;
                    }
                    ++position_;
                    continue;
                }

                if (ch == '"')
                {
                    inString = true;
                }
                else if (ch == '[' || ch == '{')
                {
                    ++depth;
                }
                else if (ch == ']' || ch == '}')
                {
                    if (depth == 0)
                    {
                        break;
                    }
                    --depth;
                }
                else if (ch == ',' && depth == 0)
                {
                    break;
                }
                ++position_;
            }

            std::string value(source_.substr(start, position_ - start));
            trim(value);
            if (!value.empty() && value.front() != '[')
            {
                entries[path] = std::move(value);
            }
            return true;
        }

        static void trim(std::string& value)
        {
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
            {
                value.erase(value.begin());
            }
            while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
            {
                value.pop_back();
            }
        }

        std::string_view source_;
        std::size_t position_ = 0u;
    };

    std::string lower_ascii(std::string value)
    {
        for (char& ch : value)
        {
            if (ch >= 'A' && ch <= 'Z')
            {
                ch = static_cast<char>(ch - 'A' + 'a');
            }
        }
        return value;
    }

    std::optional<std::string> find_entry(
        const ThemeEntries& entries,
        std::initializer_list<std::string_view> paths)
    {
        for (std::string_view path : paths)
        {
            const auto it = entries.find(std::string(path));
            if (it != entries.end())
            {
                return it->second;
            }
        }
        return std::nullopt;
    }

    bool assign_theme_colour(
        const ThemeEntries& entries,
        std::string& target,
        std::initializer_list<std::string_view> paths)
    {
        // Each colour supports several aliases so app theme files can stay readable
        const std::optional<std::string> value = find_entry(entries, paths);
        if (!value)
        {
            return false;
        }

        if (!renderer2d_parse_color(*value))
        {
            if (Logger* logger = Logger::fetch_logger())
            {
                logger->warning("Ignored invalid theme color: " + *value);
            }
            return false;
        }

        target = *value;
        return true;
    }

    void apply_theme_entries(const ThemeEntries& entries, UiTheme& theme)
    {
        // Base selects the built-in palette first, then explicit JSON values override it
        if (const std::optional<std::string> base = find_entry(entries, { "base", "theme.base", "mode", "theme.mode" }))
        {
            const std::string normalised = lower_ascii(*base);
            if (normalised == "dark")
            {
                theme = ui_dark_theme();
            }
            else if (normalised == "light")
            {
                theme = ui_light_theme();
            }
        }

        assign_theme_colour(entries, theme.surface, {
            "colors.surface", "colours.surface", "theme.colors.surface", "theme.colours.surface", "surface", "background", "panel.surface"
        });
        assign_theme_colour(entries, theme.surfaceElevated, {
            "colors.surfaceElevated", "colours.surfaceElevated", "theme.colors.surfaceElevated", "theme.colours.surfaceElevated", "surfaceElevated", "panel.surfaceElevated"
        });
        assign_theme_colour(entries, theme.surfaceMuted, {
            "colors.surfaceMuted", "colours.surfaceMuted", "theme.colors.surfaceMuted", "theme.colours.surfaceMuted", "surfaceMuted", "muted", "panel.surfaceMuted"
        });
        assign_theme_colour(entries, theme.outline, {
            "colors.outline", "colours.outline", "theme.colors.outline", "theme.colours.outline", "outline", "panel.outline"
        });
        assign_theme_colour(entries, theme.outlineStrong, {
            "colors.outlineStrong", "colours.outlineStrong", "theme.colors.outlineStrong", "theme.colours.outlineStrong", "outlineStrong", "panel.outlineStrong"
        });
        assign_theme_colour(entries, theme.text, {
            "colors.text", "colours.text", "theme.colors.text", "theme.colours.text", "text", "foreground"
        });
        assign_theme_colour(entries, theme.textMuted, {
            "colors.textMuted", "colours.textMuted", "theme.colors.textMuted", "theme.colours.textMuted", "textMuted", "mutedText"
        });
        assign_theme_colour(entries, theme.icon, {
            "colors.icon", "colours.icon", "theme.colors.icon", "theme.colours.icon", "icon", "trafficLights.icon", "panelWindow.trafficLights.icon"
        });
        assign_theme_colour(entries, theme.accent, {
            "colors.accent", "colours.accent", "theme.colors.accent", "theme.colours.accent", "accent", "primary"
        });
        assign_theme_colour(entries, theme.accentHover, {
            "colors.accentHover", "colours.accentHover", "theme.colors.accentHover", "theme.colours.accentHover", "accentHover", "primaryHover"
        });
        assign_theme_colour(entries, theme.accentPressed, {
            "colors.accentPressed", "colours.accentPressed", "theme.colors.accentPressed", "theme.colours.accentPressed", "accentPressed", "primaryPressed"
        });
        assign_theme_colour(entries, theme.danger, {
            "colors.danger", "colours.danger", "theme.colors.danger", "theme.colours.danger", "danger", "trafficLights.close", "panelWindow.trafficLights.close"
        });
        assign_theme_colour(entries, theme.warning, {
            "colors.warning", "colours.warning", "theme.colors.warning", "theme.colours.warning", "warning", "trafficLights.minimise", "panelWindow.trafficLights.minimise"
        });
        assign_theme_colour(entries, theme.success, {
            "colors.success", "colours.success", "theme.colors.success", "theme.colours.success", "success", "trafficLights.maximise", "panelWindow.trafficLights.maximise"
        });
        assign_theme_colour(entries, theme.disabled, {
            "colors.disabled", "colours.disabled", "theme.colors.disabled", "theme.colours.disabled", "disabled", "trafficLights.disabled", "panelWindow.trafficLights.disabled"
        });

        auto& settings = theme.settingsPanel;
        assign_theme_colour(entries, settings.sidebarSurface, {
            "settingsPanel.sidebarSurface", "settings.sidebarSurface", "panelWindow.settings.sidebarSurface"
        });
        assign_theme_colour(entries, settings.contentSurface, {
            "settingsPanel.contentSurface", "settings.contentSurface", "panelWindow.settings.contentSurface"
        });
        assign_theme_colour(entries, settings.cardSurface, {
            "settingsPanel.cardSurface", "settings.cardSurface", "panelWindow.settings.cardSurface"
        });
        assign_theme_colour(entries, settings.cardOutline, {
            "settingsPanel.cardOutline", "settings.cardOutline", "panelWindow.settings.cardOutline"
        });
        assign_theme_colour(entries, settings.separator, {
            "settingsPanel.separator", "settings.separator", "panelWindow.settings.separator"
        });
        assign_theme_colour(entries, settings.sidebarFadeSolid, {
            "settingsPanel.sidebarFadeSolid", "settings.sidebarFadeSolid", "panelWindow.settings.sidebarFadeSolid"
        });
        assign_theme_colour(entries, settings.sidebarFadeTransparent, {
            "settingsPanel.sidebarFadeTransparent", "settings.sidebarFadeTransparent", "panelWindow.settings.sidebarFadeTransparent"
        });
        assign_theme_colour(entries, settings.scrollbarThumb, {
            "settingsPanel.scrollbarThumb", "settings.scrollbarThumb", "panelWindow.settings.scrollbarThumb"
        });
        assign_theme_colour(entries, settings.scrollbarThumbOutline, {
            "settingsPanel.scrollbarThumbOutline", "settings.scrollbarThumbOutline", "panelWindow.settings.scrollbarThumbOutline"
        });
        assign_theme_colour(entries, settings.searchPlaceholder, {
            "settingsPanel.search.placeholder", "settings.search.placeholder", "panelWindow.settings.search.placeholder"
        });
        assign_theme_colour(entries, settings.searchBackground, {
            "settingsPanel.search.background", "settings.search.background", "panelWindow.settings.search.background"
        });
        assign_theme_colour(entries, settings.searchBackgroundBottom, {
            "settingsPanel.search.backgroundBottom", "settings.search.backgroundBottom", "panelWindow.settings.search.backgroundBottom"
        });
        assign_theme_colour(entries, settings.searchFocusedBackground, {
            "settingsPanel.search.focusedBackground", "settings.search.focusedBackground", "panelWindow.settings.search.focusedBackground"
        });
        assign_theme_colour(entries, settings.searchFocusedBackgroundBottom, {
            "settingsPanel.search.focusedBackgroundBottom", "settings.search.focusedBackgroundBottom", "panelWindow.settings.search.focusedBackgroundBottom"
        });
        assign_theme_colour(entries, settings.searchOutline, {
            "settingsPanel.search.outline", "settings.search.outline", "panelWindow.settings.search.outline"
        });
        assign_theme_colour(entries, settings.navBackground, {
            "settingsPanel.nav.background", "settings.nav.background", "panelWindow.settings.nav.background"
        });
        assign_theme_colour(entries, settings.navBackgroundBottom, {
            "settingsPanel.nav.backgroundBottom", "settings.nav.backgroundBottom", "panelWindow.settings.nav.backgroundBottom"
        });
        assign_theme_colour(entries, settings.navOutline, {
            "settingsPanel.nav.outline", "settings.nav.outline", "panelWindow.settings.nav.outline"
        });
        assign_theme_colour(entries, settings.navDivider, {
            "settingsPanel.nav.divider", "settings.nav.divider", "panelWindow.settings.nav.divider"
        });
        assign_theme_colour(entries, settings.navIcon, {
            "settingsPanel.nav.icon", "settings.nav.icon", "panelWindow.settings.nav.icon"
        });
        assign_theme_colour(entries, settings.navIconDisabled, {
            "settingsPanel.nav.iconDisabled", "settings.nav.iconDisabled", "panelWindow.settings.nav.iconDisabled"
        });
        assign_theme_colour(entries, settings.sidebarRowSelected, {
            "settingsPanel.sidebar.selected", "settings.sidebar.selected", "panelWindow.settings.sidebar.selected"
        });
        assign_theme_colour(entries, settings.sidebarRowSelectedHover, {
            "settingsPanel.sidebar.selectedHover", "settings.sidebar.selectedHover", "panelWindow.settings.sidebar.selectedHover"
        });
        assign_theme_colour(entries, settings.sidebarRowSelectedPressed, {
            "settingsPanel.sidebar.selectedPressed", "settings.sidebar.selectedPressed", "panelWindow.settings.sidebar.selectedPressed"
        });
        assign_theme_colour(entries, settings.sidebarRowHover, {
            "settingsPanel.sidebar.hover", "settings.sidebar.hover", "panelWindow.settings.sidebar.hover"
        });
        assign_theme_colour(entries, settings.sidebarRowPressed, {
            "settingsPanel.sidebar.pressed", "settings.sidebar.pressed", "panelWindow.settings.sidebar.pressed"
        });
        assign_theme_colour(entries, settings.sidebarLabel, {
            "settingsPanel.sidebar.label", "settings.sidebar.label", "panelWindow.settings.sidebar.label"
        });
        assign_theme_colour(entries, settings.sidebarSelectedLabel, {
            "settingsPanel.sidebar.selectedLabel", "settings.sidebar.selectedLabel", "panelWindow.settings.sidebar.selectedLabel"
        });
        assign_theme_colour(entries, settings.avatarBackground, {
            "settingsPanel.avatar.background", "settings.avatar.background", "panelWindow.settings.avatar.background"
        });
        assign_theme_colour(entries, settings.avatarText, {
            "settingsPanel.avatar.text", "settings.avatar.text", "panelWindow.settings.avatar.text"
        });
        assign_theme_colour(entries, settings.previewLabel, {
            "settingsPanel.preview.label", "settings.preview.label", "panelWindow.settings.preview.label"
        });
        assign_theme_colour(entries, settings.selectedPreviewLabel, {
            "settingsPanel.preview.selectedLabel", "settings.preview.selectedLabel", "panelWindow.settings.preview.selectedLabel"
        });
    }

}
bool ui_load_theme_file(const std::filesystem::path& path, UiTheme& theme)
{
    const std::string source = read_text_file(path);
    if (source.empty())
    {
        if (Logger* logger = Logger::fetch_logger())
        {
            logger->warning("Theme file missing or empty: " + path.string());
        }
        return false;
    }

    ThemeEntries entries;
    ThemeJsonParser parser(source);
    if (!parser.parse(entries))
    {
        if (Logger* logger = Logger::fetch_logger())
        {
            logger->error("Failed to parse theme file: " + path.string());
        }
        return false;
    }

    apply_theme_entries(entries, theme);
    if (Logger* logger = Logger::fetch_logger())
    {
        logger->info("Loaded UI theme from " + path.string() + ".");
    }
    return true;
}

UiTheme ui_theme_from_file(const std::filesystem::path& path, UiTheme fallback)
{
    UiTheme theme = std::move(fallback);
    ui_load_theme_file(path, theme);
    return theme;
}

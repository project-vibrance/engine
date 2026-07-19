#pragma once
#include "vibranceUI/export.h"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

enum class TextKind
{
    eEmpty,
    eLiteral,
    eTranslatable,
    eFormatted,
    ePlural,
    eNumber,
    eJoin
};

struct VIBRANCE_ENGINE_API Text
{
    // Lightweight text value that can be literal, translated, formatted, or joined
    TextKind kind = TextKind::eEmpty;
    std::string value;
    std::string fallback;
    std::string pluralValue;
    std::string pluralFallback;
    std::string separator;
    std::vector<Text> arguments;
    int64_t count = 0;
    double numericValue = 0.0;
    int precision = 0;

    static Text literal(std::string value);
    static Text translatable(std::string key, std::string fallback = {});
    static Text formatted(std::string key, std::vector<Text> arguments, std::string fallback = {});
    static Text plural(
        int64_t count,
        std::string singularKey,
        std::string pluralKey,
        std::string singularFallback = {},
        std::string pluralFallback = {});
    static Text number(double value, int precision = 0);
    static Text join(std::vector<Text> values, std::string separator = {});
};

struct LocalisedTextComponent
{
    Text value;
};

class VIBRANCE_ENGINE_API Localisation
{
public:
    // Locales are loaded from flat JSON dictionaries and fall back to defaultLocale_
    bool load_directory(
        const std::filesystem::path& directory,
        bool mergeExisting = false);
    bool load_file(
        const std::filesystem::path& path,
        std::string locale = {},
        bool mergeExisting = false);
    void clear();

    bool set_locale(std::string locale);
    const std::string& locale() const;

    void set_default_locale(std::string locale);
    const std::string& default_locale() const;

    std::vector<std::string> available_locales() const;
    bool has_locale(std::string_view locale) const;
    bool has_translation(std::string_view key, std::string_view locale = {}) const;

    std::string translate(std::string_view key, std::string_view fallback = {}) const;
    std::string resolve(const Text& text) const;
    std::string locale_text(std::string_view locale = {}) const;
    std::string all_text() const;

private:
    using Dictionary = std::unordered_map<std::string, std::string>;

    const Dictionary* dictionary_for(std::string_view locale) const;
    const Dictionary* language_dictionary_for(std::string_view locale) const;
    const std::string* find_translation(std::string_view key, std::string_view locale) const;
    std::string translate_for_locale(std::string_view key, std::string_view locale, std::string_view fallback) const;

    std::unordered_map<std::string, Dictionary> dictionaries_;
    std::string currentLocale_ = "en_us";
    std::string defaultLocale_ = "en_us";
};

inline Text ui_tr(std::string_view key, std::string_view fallback = {})
{
    // Short helper for UI code that wants a translatable value without noisy construction
    return Text::translatable(std::string(key), std::string(fallback));
}

inline std::string ui_locale_label_fallback(std::string_view locale)
{
    // Known locale names stay readable before their language files have loaded
    if (locale == "en_us")
    {
        return "English (US)";
    }
    if (locale == "en_gb")
    {
        return "English (UK)";
    }
    if (locale == "es_es")
    {
        return "Spanish";
    }
    if (locale == "zh_cn")
    {
        return "Mandarin Chinese";
    }
    if (locale == "ja_jp")
    {
        return "Japanese";
    }
    if (locale == "de_de")
    {
        return "German";
    }
    if (locale == "fr_fr")
    {
        return "French";
    }
    if (locale == "pt_br")
    {
        return "Portuguese";
    }
    if (locale == "hi_in")
    {
        return "Hindi";
    }
    if (locale == "ar_sa")
    {
        return "Arabic";
    }
    if (locale == "ko_kr")
    {
        return "Korean";
    }
    if (locale == "ru_ru")
    {
        return "Russian";
    }
    return std::string(locale);
}

inline Text ui_locale_label_text(std::string_view locale)
{
    // Locale files can override their display name through settings.language.<locale>
    return ui_tr(
        std::string("settings.language.") + std::string(locale),
        ui_locale_label_fallback(locale));
}

inline std::string ui_resolve_locale_label(const Localisation& localisation, std::string_view locale)
{
    return localisation.resolve(ui_locale_label_text(locale));
}

struct UiOptionChoice
{
    // Generic code and label pair for dropdowns, segmented choices, or command menus
    std::string code;
    std::string label;
};

inline std::vector<UiOptionChoice> ui_locale_choices(
    const Localisation& localisation,
    std::string_view selectedLocale = {},
    int* selectedIndex = nullptr)
{
    // Locales come from loaded dictionaries and keep the current selection index in sync
    std::vector<std::string> locales = localisation.available_locales();
    if (locales.empty())
    {
        locales = { "en_us" };
    }

    std::vector<UiOptionChoice> choices;
    choices.reserve(locales.size());
    if (selectedIndex)
    {
        *selectedIndex = 0;
    }

    for (std::size_t i = 0; i < locales.size(); ++i)
    {
        choices.push_back({ locales[i], ui_resolve_locale_label(localisation, locales[i]) });
        if (selectedIndex && locales[i] == selectedLocale)
        {
            *selectedIndex = static_cast<int>(i);
        }
    }
    return choices;
}

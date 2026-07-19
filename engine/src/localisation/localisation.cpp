#include <vibranceUI/localisation/localisation.h>
#include <vibranceUI/core/logger.h>
#include <algorithm>
#include <cctype>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <optional>
#include <sstream>

namespace
{
    std::string normalise_locale(std::string_view locale)
    {
        // Match locale names to files such as en_gb.json and ko_kr.json
        std::string out;
        out.reserve(locale.size());
        for (char ch : locale)
        {
            out.push_back(ch == '-' ? '_' : static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        }
        return out;
    }

    void append_utf8(std::string& out, uint32_t codepoint)
    {
        // Decode JSON unicode escapes into UTF-8 for the text renderer
        if (codepoint <= 0x7Fu)
        {
            out.push_back(static_cast<char>(codepoint));
        }
        else if (codepoint <= 0x7FFu)
        {
            out.push_back(static_cast<char>(0xC0u | (codepoint >> 6u)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
        else if (codepoint <= 0xFFFFu)
        {
            out.push_back(static_cast<char>(0xE0u | (codepoint >> 12u)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0u | (codepoint >> 18u)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 12u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | ((codepoint >> 6u) & 0x3Fu)));
            out.push_back(static_cast<char>(0x80u | (codepoint & 0x3Fu)));
        }
    }

    std::optional<uint32_t> parse_hex4(std::string_view value, std::size_t offset)
    {
        // JSON unicode escapes always provide exactly four hexadecimal digits
        if (offset + 4u > value.size())
        {
            return std::nullopt;
        }

        uint32_t out = 0u;
        for (std::size_t i = 0u; i < 4u; ++i)
        {
            const char ch = value[offset + i];
            out <<= 4u;
            if (ch >= '0' && ch <= '9')
            {
                out += static_cast<uint32_t>(ch - '0');
            }
            else if (ch >= 'a' && ch <= 'f')
            {
                out += static_cast<uint32_t>(ch - 'a' + 10);
            }
            else if (ch >= 'A' && ch <= 'F')
            {
                out += static_cast<uint32_t>(ch - 'A' + 10);
            }
            else
            {
                return std::nullopt;
            }
        }
        return out;
    }

    class FlatJsonObjectParser
    {
    public:
        // Translation files are flat key/value maps, avoiding a heavier JSON dependency
        explicit FlatJsonObjectParser(std::string_view source) : source_(source) {}

        bool parse(std::unordered_map<std::string, std::string>& out)
        {
            // Only string values are stored; other JSON values are skipped for forwards compatibility
            skip_ws();
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

                skip_ws();
                std::string value;
                if (peek() == '"')
                {
                    if (!parse_string(value))
                    {
                        return false;
                    }
                    out[std::move(key)] = std::move(value);
                }
                else if (!skip_value())
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
                case 'u':
                {
                    // Surrogate pairs are combined so emoji and supplementary scripts survive
                    std::optional<uint32_t> codepoint = parse_hex4(source_, position_);
                    if (!codepoint)
                    {
                        return false;
                    }
                    position_ += 4u;
                    if (*codepoint >= 0xD800u && *codepoint <= 0xDBFFu &&
                        position_ + 6u <= source_.size() &&
                        source_[position_] == '\\' &&
                        source_[position_ + 1u] == 'u')
                    {
                        std::optional<uint32_t> low = parse_hex4(source_, position_ + 2u);
                        if (low && *low >= 0xDC00u && *low <= 0xDFFFu)
                        {
                            position_ += 6u;
                            codepoint = 0x10000u + ((*codepoint - 0xD800u) << 10u) + (*low - 0xDC00u);
                        }
                    }
                    append_utf8(out, *codepoint);
                    break;
                }
                default:
                    return false;
                }
            }
            return false;
        }

        bool skip_value()
        {
            // Unknown values may contain nested arrays or objects, so depth must be tracked
            if (peek() == '"')
            {
                std::string ignored;
                return parse_string(ignored);
            }

            int depth = 0;
            bool inString = false;
            bool escaping = false;
            while (!done())
            {
                const char ch = source_[position_];
                if (inString)
                {
                    escaping = !escaping && ch == '\\';
                    if (!escaping && ch == '"')
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
                else if (ch == '{' || ch == '[')
                {
                    ++depth;
                }
                else if (ch == '}' || ch == ']')
                {
                    if (depth == 0)
                    {
                        return true;
                    }
                    --depth;
                }
                else if (ch == ',' && depth == 0)
                {
                    return true;
                }
                ++position_;
            }
            return true;
        }

        std::string_view source_;
        std::size_t position_ = 0u;
    };

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

    std::string format_pattern(std::string pattern, const std::vector<std::string>& arguments)
    {
        // Simple positional replacement keeps formatting deterministic for translated strings
        for (std::size_t i = 0u; i < arguments.size(); ++i)
        {
            const std::string token = "{" + std::to_string(i) + "}";
            std::size_t at = 0u;
            while ((at = pattern.find(token, at)) != std::string::npos)
            {
                pattern.replace(at, token.size(), arguments[i]);
                at += arguments[i].size();
            }
        }
        return pattern;
    }
}

Text Text::literal(std::string literalValue)
{
    Text text = {};
    text.kind = TextKind::eLiteral;
    text.value = std::move(literalValue);
    return text;
}

Text Text::translatable(std::string key, std::string fallbackValue)
{
    Text text = {};
    text.kind = TextKind::eTranslatable;
    text.value = std::move(key);
    text.fallback = std::move(fallbackValue);
    return text;
}

Text Text::formatted(std::string key, std::vector<Text> textArguments, std::string fallbackValue)
{
    Text text = {};
    text.kind = TextKind::eFormatted;
    text.value = std::move(key);
    text.fallback = std::move(fallbackValue);
    text.arguments = std::move(textArguments);
    return text;
}

Text Text::plural(
    int64_t textCount,
    std::string singularKey,
    std::string pluralKey,
    std::string singularFallback,
    std::string textPluralFallback)
{
    Text text = {};
    text.kind = TextKind::ePlural;
    text.count = textCount;
    text.value = std::move(singularKey);
    text.pluralValue = std::move(pluralKey);
    text.fallback = std::move(singularFallback);
    text.pluralFallback = std::move(textPluralFallback);
    return text;
}

Text Text::number(double numberValue, int numberPrecision)
{
    Text text = {};
    text.kind = TextKind::eNumber;
    text.numericValue = numberValue;
    text.precision = numberPrecision;
    return text;
}

Text Text::join(std::vector<Text> values, std::string textSeparator)
{
    Text text = {};
    text.kind = TextKind::eJoin;
    text.arguments = std::move(values);
    text.separator = std::move(textSeparator);
    return text;
}

bool Localisation::load_directory(
    const std::filesystem::path& directory,
    bool mergeExisting)
{
    // Each JSON file is treated as one locale named after the file stem
    if (!std::filesystem::exists(directory))
    {
        Logger::fetch_logger()->print("Localisation directory missing: " + directory.string());
        return false;
    }

    bool loadedAny = false;
    for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(directory))
    {
        if (!entry.is_regular_file() || entry.path().extension() != ".json")
        {
            continue;
        }
        loadedAny = load_file(entry.path(), {}, mergeExisting) || loadedAny;
    }
    return loadedAny;
}

bool Localisation::load_file(
    const std::filesystem::path& path,
    std::string locale,
    bool mergeExisting)
{
    if (locale.empty())
    {
        locale = path.stem().string();
    }
    locale = normalise_locale(locale);
    if (locale.empty())
    {
        return false;
    }

    const std::string source = read_text_file(path);
    if (source.empty())
    {
        Logger::fetch_logger()->print("Failed to read localisation file: " + path.string());
        return false;
    }

    Dictionary parsed;
    FlatJsonObjectParser parser(source);
    if (!parser.parse(parsed))
    {
        Logger::fetch_logger()->print("Failed to parse localisation file: " + path.string());
        return false;
    }

    if (mergeExisting)
    {
        Dictionary& dictionary = dictionaries_[locale];
        for (auto& [key, value] : parsed)
        {
            dictionary[std::move(key)] = std::move(value);
        }
    }
    else
    {
        dictionaries_[locale] = std::move(parsed);
    }
    Logger::fetch_logger()->print(
        std::string(mergeExisting ? "Merged localisation " : "Loaded localisation ") +
        locale +
        " from " +
        path.string() +
        ".");
    return true;
}

void Localisation::clear()
{
    dictionaries_.clear();
}

bool Localisation::set_locale(std::string locale)
{
    locale = normalise_locale(locale);
    if (locale.empty() || !has_locale(locale))
    {
        return false;
    }
    currentLocale_ = std::move(locale);
    return true;
}

const std::string& Localisation::locale() const
{
    return currentLocale_;
}

void Localisation::set_default_locale(std::string locale)
{
    locale = normalise_locale(locale);
    if (!locale.empty())
    {
        defaultLocale_ = std::move(locale);
    }
}

const std::string& Localisation::default_locale() const
{
    return defaultLocale_;
}

std::vector<std::string> Localisation::available_locales() const
{
    std::vector<std::string> locales;
    locales.reserve(dictionaries_.size());
    for (const auto& [locale, _] : dictionaries_)
    {
        locales.push_back(locale);
    }
    std::sort(locales.begin(), locales.end());
    return locales;
}

bool Localisation::has_locale(std::string_view locale) const
{
    return dictionary_for(locale) != nullptr;
}

bool Localisation::has_translation(std::string_view key, std::string_view locale) const
{
    return find_translation(key, locale.empty() ? currentLocale_ : locale) != nullptr;
}

std::string Localisation::translate(std::string_view key, std::string_view fallback) const
{
    // Current locale wins, then language-only, then default locale, then fallback
    return translate_for_locale(key, currentLocale_, fallback);
}

std::string Localisation::resolve(const Text& text) const
{
    switch (text.kind)
    {
    case TextKind::eLiteral:
        return text.value;
    case TextKind::eTranslatable:
        return translate(text.value, text.fallback);
    case TextKind::eFormatted:
    {
        std::vector<std::string> arguments;
        arguments.reserve(text.arguments.size());
        for (const Text& argument : text.arguments)
        {
            arguments.push_back(resolve(argument));
        }
        return format_pattern(translate(text.value, text.fallback), arguments);
    }
    case TextKind::ePlural:
    {
        const bool singular = text.count == 1 || text.count == -1;
        std::vector<std::string> arguments;
        arguments.push_back(resolve(Text::number(static_cast<double>(text.count), 0)));
        for (const Text& argument : text.arguments)
        {
            arguments.push_back(resolve(argument));
        }
        return format_pattern(
            translate(singular ? text.value : text.pluralValue, singular ? text.fallback : text.pluralFallback),
            arguments);
    }
    case TextKind::eNumber:
    {
        std::ostringstream out;
        if (text.precision >= 0)
        {
            out << std::fixed << std::setprecision(text.precision);
        }
        out << text.numericValue;
        return out.str();
    }
    case TextKind::eJoin:
    {
        std::string out;
        for (std::size_t i = 0u; i < text.arguments.size(); ++i)
        {
            if (i > 0u)
            {
                out += text.separator;
            }
            out += resolve(text.arguments[i]);
        }
        return out;
    }
    case TextKind::eEmpty:
    default:
        return {};
    }
}

std::string Localisation::all_text() const
{
    std::string out;
    for (const auto& [locale, dictionary] : dictionaries_)
    {
        out += locale;
        out.push_back('\n');
        for (const auto& [key, value] : dictionary)
        {
            out += key;
            out.push_back('\n');
            out += value;
            out.push_back('\n');
        }
    }
    return out;
}

std::string Localisation::locale_text(std::string_view locale) const
{
    const Dictionary* dictionary = dictionary_for(locale.empty() ? currentLocale_ : locale);
    if (!dictionary)
    {
        return {};
    }

    std::string out;
    for (const auto& [_, value] : *dictionary)
    {
        out += value;
        out.push_back('\n');
    }
    return out;
}

const Localisation::Dictionary* Localisation::dictionary_for(std::string_view locale) const
{
    const auto it = dictionaries_.find(normalise_locale(locale));
    return it != dictionaries_.end() ? &it->second : nullptr;
}

const Localisation::Dictionary* Localisation::language_dictionary_for(std::string_view locale) const
{
    const std::string normalised = normalise_locale(locale);
    const std::size_t underscore = normalised.find('_');
    if (underscore == std::string::npos)
    {
        return nullptr;
    }

    const std::string prefix = normalised.substr(0u, underscore + 1u);
    for (const auto& [candidate, dictionary] : dictionaries_)
    {
        if (candidate.rfind(prefix, 0u) == 0u)
        {
            return &dictionary;
        }
    }
    return nullptr;
}

const std::string* Localisation::find_translation(std::string_view key, std::string_view locale) const
{
    const Dictionary* dictionary = dictionary_for(locale);
    if (!dictionary)
    {
        return nullptr;
    }

    const auto it = dictionary->find(std::string(key));
    return it != dictionary->end() ? &it->second : nullptr;
}

std::string Localisation::translate_for_locale(std::string_view key, std::string_view locale, std::string_view fallback) const
{
    if (const std::string* value = find_translation(key, locale))
    {
        return *value;
    }
    if (const Dictionary* languageDictionary = language_dictionary_for(locale))
    {
        if (const auto it = languageDictionary->find(std::string(key)); it != languageDictionary->end())
        {
            return it->second;
        }
    }
    if (locale != defaultLocale_)
    {
        if (const std::string* value = find_translation(key, defaultLocale_))
        {
            return *value;
        }
    }
    if (defaultLocale_ != "en_us")
    {
        if (const std::string* value = find_translation(key, "en_us"))
        {
            return *value;
        }
    }
    if (!fallback.empty())
    {
        return std::string(fallback);
    }
    return std::string(key);
}

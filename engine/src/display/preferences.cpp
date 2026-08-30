#include <vibranceUI/display/preferences.h>

#include <fstream>
#include <iterator>
#include <optional>
#include <string_view>
#include <system_error>

namespace
{
    std::optional<std::string> json_string_value(
        std::string_view contents,
        std::string_view key)
    {
        const std::string quotedKey = "\"" + std::string(key) + "\"";
        const std::size_t keyPosition = contents.find(quotedKey);
        if (keyPosition == std::string_view::npos)
        {
            return std::nullopt;
        }
        const std::size_t colon = contents.find(
            ':',
            keyPosition + quotedKey.size());
        if (colon == std::string_view::npos)
        {
            return std::nullopt;
        }
        const std::size_t quote = contents.find('"', colon + 1u);
        if (quote == std::string_view::npos)
        {
            return std::nullopt;
        }

        std::string result;
        bool escaped = false;
        for (std::size_t index = quote + 1u; index < contents.size(); ++index)
        {
            const char value = contents[index];
            if (escaped)
            {
                switch (value)
                {
                    case 'n': result.push_back('\n'); break;
                    case 'r': result.push_back('\r'); break;
                    case 't': result.push_back('\t'); break;
                    default: result.push_back(value); break;
                }
                escaped = false;
            }
            else if (value == '\\')
            {
                escaped = true;
            }
            else if (value == '"')
            {
                return result;
            }
            else
            {
                result.push_back(value);
            }
        }
        return std::nullopt;
    }

    std::string json_escape(std::string_view value)
    {
        std::string result;
        result.reserve(value.size());
        for (const char character : value)
        {
            switch (character)
            {
                case '\\': result += "\\\\"; break;
                case '"': result += "\\\""; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default: result.push_back(character); break;
            }
        }
        return result;
    }
}

DisplayTargetPreference load_display_target_preference(
    const DisplayPreferenceStorage& storage,
    DisplayTargetPreference fallback)
{
    std::ifstream stream(storage.path, std::ios::binary);
    if (storage.path.empty() || !stream)
    {
        return fallback;
    }
    const std::string contents {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>()
    };
    const std::optional<std::string> mode = json_string_value(
        contents,
        storage.modeKey);
    if (!mode)
    {
        return fallback;
    }

    DisplayTargetPreference result = fallback;
    result.mode = storage.migrateLegacyAnyToMonitor && *mode == "any" ?
        DisplayTargetMode::eMonitor :
        display_target_mode_from_code(*mode, fallback.mode);
    if (const std::optional<std::string> monitor = json_string_value(
            contents,
            storage.monitorKey))
    {
        result.monitorId = *monitor;
    }
    return result;
}

bool save_display_target_preference(
    const DisplayPreferenceStorage& storage,
    const DisplayTargetPreference& preference)
{
    if (storage.path.empty() || storage.modeKey.empty() ||
        storage.monitorKey.empty())
    {
        return false;
    }
    std::error_code error;
    const std::filesystem::path parent = storage.path.parent_path();
    if (!parent.empty())
    {
        std::filesystem::create_directories(parent, error);
        if (error)
        {
            return false;
        }
    }
    std::ofstream stream(
        storage.path,
        std::ios::binary | std::ios::trunc);
    if (!stream)
    {
        return false;
    }
    stream << "{\n  \"" << json_escape(storage.modeKey) << "\": \""
        << display_target_mode_code(preference.mode)
        << "\",\n  \"" << json_escape(storage.monitorKey) << "\": \""
        << json_escape(preference.monitorId)
        << "\"\n}\n";
    return static_cast<bool>(stream);
}

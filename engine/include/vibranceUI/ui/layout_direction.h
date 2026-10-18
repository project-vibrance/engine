#pragma once

#include <vibranceUI/renderer/renderer2d_components.h>
#include <cctype>

// A scene context supplies the default. An entity override controls its
// contents; its own placement continues to follow its parent's direction.
enum class UiLayoutDirection : std::uint8_t { eLeftToRight, eRightToLeft };

struct UiLayoutDirectionComponent
{
    UiLayoutDirection direction = UiLayoutDirection::eLeftToRight;
};

// Opt in for arrows whose meaning follows reading order. Artwork, logos and
// playback icons retain their original pixels.
struct UiDirectionalIconComponent {};

inline UiLayoutDirection ui_locale_layout_direction(std::string_view locale)
{
    std::string language;
    std::string script;
    for (std::size_t begin = 0; begin < locale.size();)
    {
        const auto end = locale.find_first_of("_-@.", begin);
        std::string part(locale.substr(begin, end == std::string_view::npos ? end : end - begin));
        for (char& character : part)
            character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
        if (begin == 0) language = part;
        else if (part.size() == 4)
        {
            script = part;
            break;
        }
        // Region, variant and Unicode extension subtags cannot select the
        // interface script (for example ar-u-nu-latn remains RTL).
        else if (part.size() != 3 || !std::all_of(part.begin(), part.end(),
            [](unsigned char character) { return std::isalpha(character) != 0; })) break;
        if (end == std::string_view::npos || locale[end] == '@' || locale[end] == '.') break;
        begin = end + 1;
    }
    const bool rtl = !script.empty() ?
        (script == "arab" || script == "hebr" || script == "thaa" || script == "nkoo" ||
         script == "adlm" || script == "syrc" || script == "rohg" || script == "mand" || script == "samr") :
        (language == "ar" || language == "he" || language == "iw" || language == "fa" ||
         language == "ur" || language == "ps" || language == "dv" || language == "yi" ||
         language == "ji" || language == "ug" || language == "sd" || language == "ckb" ||
         language == "syr" || language == "nqo" || language == "arc" || language == "bal" ||
         language == "bgn" || language == "pnb" || language == "rhg");
    return rtl ? UiLayoutDirection::eRightToLeft : UiLayoutDirection::eLeftToRight;
}

inline bool ui_layout_is_rtl(const entt::registry& registry, entt::entity entity = entt::null)
{
    for (unsigned depth = 0; depth < 64 && registry.valid(entity); ++depth)
    {
        if (const auto* direction = registry.try_get<UiLayoutDirectionComponent>(entity))
            return direction->direction == UiLayoutDirection::eRightToLeft;
        const auto* parent = registry.try_get<Parent2DComponent>(entity);
        if (!parent || parent->parent == entity) break;
        entity = parent->parent;
    }
    const auto* direction = registry.ctx().find<UiLayoutDirectionComponent>();
    return direction && direction->direction == UiLayoutDirection::eRightToLeft;
}

inline float ui_mirror_x(float x, glm::vec4 bounds)
{
    return bounds.x * 2.0f + bounds.z - x;
}

inline glm::vec4 ui_layout_corner_radii(const entt::registry& registry,
    entt::entity entity, glm::vec4 radii, float padding = 0.0f)
{
    const auto* parent = registry.try_get<Parent2DComponent>(entity);
    if (ui_layout_is_rtl(registry, parent ? parent->parent : entt::null))
        radii = { radii.y, radii.x, radii.w, radii.z };
    return glm::max(radii + glm::vec4(std::max(padding, 0.0f)), glm::vec4(0.0f));
}

inline glm::vec4 ui_layout_corner_radii(const entt::registry& registry,
    entt::entity entity, const ShapeComponent& shape, float padding = 0.0f)
{
    return ui_layout_corner_radii(registry, entity, shape.effective_corner_radii(), padding);
}

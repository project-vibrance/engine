#include <vibranceUI/ui/animated_text_view.h>

#include <vibranceUI/renderer/renderer.h>
#include <vibranceUI/ui/styles.h>
#include <vibranceUI/ui/text.h>

#include <algorithm>
#include <cctype>
#include <utility>

namespace
{
    void set_visible(
        Renderer2DScene& scene,
        entt::entity entity,
        bool visible)
    {
        if (RenderLayer2DComponent* layer =
            scene.registry().try_get<RenderLayer2DComponent>(entity))
        {
            if (layer->visible != visible)
            {
                layer->visible = visible;
                scene.mark_dirty(entity);
            }
        }
    }
}

void UiAnimatedCharacterTextView::reset()
{
    root = entt::null;
    characters.clear();
    value.clear();
}

bool UiAnimatedCharacterTextView::valid(const Renderer2DScene& scene) const
{
    return root != entt::null && scene.registry().valid(root);
}

void ui_set_animated_text(
    Engine& engine,
    entt::entity entity,
    std::string value,
    double currentTimeSeconds,
    bool animated,
    const UiAnimatedTextTransition& transitionOptions)
{
    Renderer2DScene& scene = engine.renderer2d_scene();
    if (entity == entt::null || !scene.registry().valid(entity))
    {
        return;
    }
    set_text_entity(
        scene,
        engine.renderer2d_font_atlas(),
        entity,
        value);
    if (!animated)
    {
        return;
    }

    DisplayTransition2DComponent transition = {};
    transition.delaySeconds = std::max(
        transitionOptions.delaySeconds,
        0.0f);
    transition.durationSeconds = transitionOptions.durationSeconds;
    transition.fromOpacity = transitionOptions.fromOpacity;
    transition.toOpacity = 1.0f;
    transition.fromBlurRadius = transitionOptions.fromBlurRadius;
    transition.toBlurRadius = 0.0f;
    transition.fromScale = transitionOptions.fromScale;
    transition.toScale = { 1.0f, 1.0f };
    transition.set_interactive_spring(
        transitionOptions.springResponse,
        transitionOptions.springDamping,
        transitionOptions.springInitialVelocity);
    scene.play_display_transition(entity, transition, currentTimeSeconds);
}

UiAnimatedCharacterTextView ui_create_animated_character_text(
    UiBuilder& ui,
    const Renderer2DFontAtlas& fontAtlas,
    entt::entity parent,
    std::string value,
    const UiAnimatedCharacterTextOptions& options)
{
    UiAnimatedCharacterTextView view = {};
    const std::size_t characterCount =
        std::max<std::size_t>(options.characterCount, 1u);
    if (value.size() < characterCount)
    {
        value.insert(value.begin(), characterCount - value.size(), ' ');
    }
    else if (value.size() > characterCount)
    {
        value = value.substr(value.size() - characterCount);
    }

    const float cellWidth = options.fontSize * options.cellWidthFactor;
    const glm::vec2 rootSize {
        cellWidth * static_cast<float>(characterCount),
        options.fontSize * options.lineHeightFactor
    };
    view.root = ui.scene().create_shape(
        { 0.0f, 0.0f },
        scaled_size(rootSize.x, rootSize.y, ui.scale()),
        make_solid_style(
            "rgba(0, 0, 0, 0)",
            "rgba(0, 0, 0, 0)",
            0.0f,
            0.0f),
        Renderer2DPrimitive::eRectangle);
    ui.set_layer(view.root, options.layer, options.order);
    ui.attach_aligned(
        view.root,
        parent,
        options.alignment,
        scaled_offset(options.offset.x, options.offset.y, ui.scale()),
        scaled_size(rootSize.x, rootSize.y, ui.scale()));

    view.value = std::move(value);
    view.characters.reserve(characterCount);
    for (std::size_t index = 0u; index < characterCount; ++index)
    {
        const bool whitespace = view.value[index] == ' ';
        const entt::entity character = ui.create_layout_text(
            whitespace ? std::string() : std::string(1u, view.value[index]),
            fontAtlas,
            view.root,
            { 0.0f, 0.5f },
            { 0.5f, 0.5f },
            scaled_offset(
                (static_cast<float>(index) + 0.5f) * cellWidth,
                0.0f,
                ui.scale()),
            options.fontSize,
            options.style,
            options.layer + 1,
            options.order + static_cast<std::uint32_t>(index),
            scaled_size(cellWidth, rootSize.y, ui.scale()));
        view.characters.push_back(character);
        if (options.hideWhitespace && whitespace)
        {
            ui.set_layer(
                character,
                options.layer + 1,
                options.order + static_cast<std::uint32_t>(index)).visible =
                    false;
        }
    }
    return view;
}

void ui_update_animated_character_text(
    Engine& engine,
    UiAnimatedCharacterTextView& view,
    std::string value,
    double currentTimeSeconds,
    bool animated,
    bool animateDigitsOnly,
    const UiAnimatedTextTransition& transition)
{
    const std::size_t characterCount = view.characters.size();
    if (characterCount == 0u)
    {
        return;
    }
    if (value.size() < characterCount)
    {
        value.insert(value.begin(), characterCount - value.size(), ' ');
    }
    else if (value.size() > characterCount)
    {
        value = value.substr(value.size() - characterCount);
    }
    if (view.value.size() != characterCount)
    {
        view.value.assign(characterCount, ' ');
    }

    Renderer2DScene& scene = engine.renderer2d_scene();
    const auto characterWillAnimate = [
        &view,
        &value,
        animated,
        animateDigitsOnly](std::size_t index) {
        if (!animated || index >= value.size() ||
            index >= view.value.size() ||
            view.value[index] == value[index] || value[index] == ' ')
        {
            return false;
        }
        return !animateDigitsOnly || std::isdigit(
            static_cast<unsigned char>(value[index])) != 0;
    };
    for (std::size_t index = 0u; index < characterCount; ++index)
    {
        if (view.value[index] == value[index])
        {
            continue;
        }
        const bool visible = value[index] != ' ';
        set_visible(scene, view.characters[index], visible);
        if (!visible)
        {
            continue;
        }
        const bool animateCharacter = animated &&
            (!animateDigitsOnly || std::isdigit(
                static_cast<unsigned char>(value[index])) != 0);
        UiAnimatedTextTransition characterTransition = transition;
        if (animateCharacter && transition.characterStaggerSeconds > 0.0f)
        {
            std::size_t changedCharactersToRight = 0u;
            for (std::size_t right = index + 1u;
                 right < characterCount;
                 ++right)
            {
                if (characterWillAnimate(right))
                {
                    ++changedCharactersToRight;
                }
            }
            characterTransition.delaySeconds +=
                transition.characterStaggerSeconds *
                static_cast<float>(changedCharactersToRight);
        }
        ui_set_animated_text(
            engine,
            view.characters[index],
            std::string(1u, value[index]),
            currentTimeSeconds,
            animateCharacter,
            characterTransition);
    }
    view.value = std::move(value);
}

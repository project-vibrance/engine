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

    void layout_animated_characters(
        Renderer2DScene& scene,
        const Renderer2DFontAtlas& fontAtlas,
        UiAnimatedCharacterTextView& view)
    {
        if (view.root == entt::null || !scene.registry().valid(view.root))
        {
            return;
        }

        const std::size_t first = view.value.find_first_not_of(' ');
        if (first == std::string::npos)
        {
            return;
        }
        const TextComponent* sample = nullptr;
        for (const entt::entity character : view.characters)
        {
            sample = scene.registry().try_get<TextComponent>(character);
            if (sample) break;
        }
        const ShapeComponent* rootShape =
            scene.registry().try_get<ShapeComponent>(view.root);
        if (!sample || !rootShape)
        {
            return;
        }

        TextLayout2DOptions options = {};
        options.characterSpacing = view.characterSpacing;
        const Renderer2DTextLayout run = fontAtlas.layout_text(
            std::string_view(view.value).substr(first),
            sample->fontSize,
            options);
        if (run.glyphs.empty())
        {
            return;
        }
        const float runLeft = std::max(rootShape->size.x - run.bounds.x, 0.0f);
        float inkTop = run.glyphs.front().position.y;
        float inkBottom = inkTop + run.glyphs.front().size.y;
        for (const MSDFGlyph& glyph : run.glyphs)
        {
            inkTop = std::min(inkTop, glyph.position.y);
            inkBottom = std::max(inkBottom, glyph.position.y + glyph.size.y);
        }
        const float inkCenter = (inkTop + inkBottom) * 0.5f;
        std::size_t glyphIndex = 0u;
        for (std::size_t index = first;
             index < view.value.size() && index < view.characters.size();
             ++index)
        {
            if (view.value[index] == ' ')
            {
                continue;
            }
            if (glyphIndex >= run.glyphs.size())
            {
                break;
            }
            Layout2DComponent* layout = scene.registry().try_get<Layout2DComponent>(
                view.characters[index]);
            const TextComponent* characterText =
                scene.registry().try_get<TextComponent>(view.characters[index]);
            if (!layout || !characterText || characterText->glyphs.empty())
            {
                ++glyphIndex;
                continue;
            }
            const MSDFGlyph& glyph = run.glyphs[glyphIndex++];
            const MSDFGlyph& characterGlyph = characterText->glyphs.front();
            // Reconstruct the complete shaped run while each character stays
            // in its own entity. Using the isolated glyph's bounds alone
            // loses the shared baseline, which makes lowercase letters and
            // punctuation drift relative to digits.
            if (view.proportionalSpacing)
            {
                layout->offset.x = runLeft + glyph.position.x -
                    characterGlyph.position.x + characterText->bounds.x * 0.5f;
                layout->size.x = std::max(characterText->bounds.x, 1.0f);
            }
            const unsigned char value = static_cast<unsigned char>(view.value[index]);
            const bool punctuation = std::ispunct(value) != 0;
            layout->offset.y = punctuation && view.centrePunctuationInCell ?
                0.0f :
                glyph.position.y - inkCenter - characterGlyph.position.y +
                    characterText->bounds.y * 0.5f +
                    (punctuation ? view.punctuationBaselineOffset : 0.0f);
            layout->pivot = { 0.5f, 0.5f };
            scene.mark_dirty(view.characters[index]);
        }
    }
}

void UiAnimatedCharacterTextView::reset()
{
    root = entt::null;
    characters.clear();
    value.clear();
    proportionalSpacing = false;
    characterSpacing = 0.0f;
    punctuationBaselineOffset = 0.0f;
    centrePunctuationInCell = false;
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
    view.proportionalSpacing = options.proportionalSpacing;
    view.characterSpacing = scaled_scalar(
        options.characterSpacing, ui.scale());
    view.punctuationBaselineOffset = scaled_scalar(
        options.punctuationBaselineOffset, ui.scale());
    view.centrePunctuationInCell = options.centrePunctuationInCell;
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
    layout_animated_characters(ui.scene(), fontAtlas, view);
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
    layout_animated_characters(
        scene,
        engine.renderer2d_font_atlas(),
        view);
}

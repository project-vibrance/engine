#include <vibranceUI/ui/text_block.h>

#include <cmath>
#include <iostream>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "text block test failed: " << message << '\n';
        }
        return condition;
    }

    bool close(float left, float right)
    {
        return std::abs(left - right) <= 0.001f;
    }
}

int main()
{
    Renderer2DScene scene;
    LayoutScale scale = {};
    scale.factor = { 2.0f, 2.0f };
    scale.contentScale = scale.factor;
    scale.logicalSize = { 640.0f, 480.0f };
    UiBuilder ui(scene, scale);
    Renderer2DFontAtlas unloadedAtlas;
    const entt::entity root = ui.root();

    TextStyleComponent style = {};
    style.set_color("#223344FF");
    UiTextBlockOptions options = {};
    options.width = 240.0f;
    options.fontSize = 15.0f;
    options.wrapMode = TextWrapMode2D::eWord;
    options.textAlignment = TextHorizontalAlignment2D::eCenter;
    options.lineHeightMultiplier = 0.9f;
    options.lineSpacing = -1.0f;
    options.paragraphSpacing = 6.0f;
    options.characterSpacing = 0.5f;
    options.wordSpacing = 2.0f;
    options.style = style;
    options.placement = UiAlignment::eCenter;
    options.offset = { 4.0f, -3.0f };
    options.layer = 4;
    options.order = 7u;

    UiTextBlockHandle block = ui_create_text_block(
        ui,
        unloadedAtlas,
        root,
        "First paragraph\nSecond paragraph",
        options);

    bool passed = true;
    const TextComponent& text =
        scene.registry().get<TextComponent>(block.entity);
    const TextLayout2DComponent& textLayout =
        scene.registry().get<TextLayout2DComponent>(block.entity);
    const Layout2DComponent& placement =
        scene.registry().get<Layout2DComponent>(block.entity);
    const RenderLayer2DComponent& layer =
        scene.registry().get<RenderLayer2DComponent>(block.entity);
    passed &= expect(
        close(text.fontSize, 30.0f) &&
            close(textLayout.options.maximumWidth, 480.0f),
        "font size and wrapping width should use logical-pixel scaling");
    passed &= expect(
        textLayout.options.wrapMode == TextWrapMode2D::eWord &&
            textLayout.options.horizontalAlignment ==
                TextHorizontalAlignment2D::eCenter &&
            close(textLayout.options.lineHeightMultiplier, 0.9f) &&
            close(textLayout.options.lineSpacing, -2.0f) &&
            close(textLayout.options.paragraphSpacing, 12.0f) &&
            close(textLayout.options.characterSpacing, 1.0f) &&
            close(textLayout.options.wordSpacing, 4.0f),
        "all editorial spacing options should reach the renderer component");
    passed &= expect(
        textLayout.lineCount == 2u && close(text.bounds.x, 480.0f),
        "explicit paragraphs should update line metadata before a font loads");
    passed &= expect(
        placement.anchorMin == glm::vec2(0.5f) &&
            placement.pivot == glm::vec2(0.5f) &&
            placement.offset == glm::vec2(8.0f, -6.0f) &&
            layer.layer == 4 && layer.order == 7u,
        "block placement should remain independent from per-line alignment");

    set_text_entity(
        scene,
        unloadedAtlas,
        block.entity,
        "One\nTwo\nThree");
    passed &= expect(
        scene.registry().get<TextLayout2DComponent>(block.entity).lineCount ==
            3u,
        "changing text should automatically reflow the retained text block");

    return passed ? 0 : 1;
}

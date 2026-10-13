#include <vibranceUI/ui/text_block.h>
#include <vibranceUI/ui/controls.h>

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

    for (float dpi : { 1.0f, 1.5f, 2.0f, 3.0f })
    {
        Renderer2DScene fitScene;
        LayoutScale fitScale {};
        fitScale.factor = glm::vec2(dpi);
        fitScale.logicalSize = { 800.0f, 200.0f };
        UiBuilder fitUi(fitScene, fitScale);
        UiButtonOptions buttonOptions {};
        buttonOptions.fitContentWidth = true;
        buttonOptions.padding = { 24.0f, 0.0f, 24.0f, 0.0f };
        const auto button = ui_create_aligned_text_button(fitUi, unloadedAtlas,
            fitUi.root(), UiAlignment::eBottomLeft, { 2.0f, -2.0f },
            "Start", { 144.0f, 42.0f }, 2, 0u, buttonOptions);
        auto& fitRegistry = fitScene.registry();
        fitScene.build_render_plan(0.0, 0u);
        passed &= expect(close(fitRegistry.get<ShapeComponent>(button.root).size.x, 144.0f * dpi),
            "short labels must retain the requested minimum button width at every DPI");
        set_text_entity(fitScene, unloadedAtlas, button.label,
            "D\xC3\xA9marrer une minuterie pour la dur\xC3\xA9" "e choisie");
        // This renderer-only fixture has no glyph atlas. Supply the measured
        // run bounds that a loaded font publishes, then exercise live reflow.
        fitRegistry.get<TextComponent>(button.label).bounds = { 280.0f * dpi, 13.0f * dpi };
        fitScene.build_render_plan(1.0, 0u);
        const auto longBounds = fitRegistry.get<TextComponent>(button.label).bounds;
        const auto buttonSize = fitRegistry.get<ShapeComponent>(button.root).size;
        passed &= expect(buttonSize.x >= longBounds.x + 48.0f * dpi - 0.001f &&
            buttonSize.x > 144.0f * dpi && close(buttonSize.y, 42.0f * dpi),
            "a longer UTF-8 label must grow the existing button without changing its height");
        const auto buttonRect = ui_entity_framebuffer_rect(fitRegistry, button.root);
        passed &= expect(close(buttonRect.z, buttonSize.x) &&
            fitScene.entity_at({ buttonRect.x + buttonRect.z - 8.0f * dpi,
                buttonRect.y + buttonRect.w * 0.5f }) == button.root,
            "the resized button must retain its full hit region beyond the original width");
        set_text_entity(fitScene, unloadedAtlas, button.label, "Go");
        fitScene.build_render_plan(2.0, 0u);
        passed &= expect(close(fitRegistry.get<ShapeComponent>(button.root).size.x, 144.0f * dpi),
            "returning to a short label must shrink the button back to its minimum");
    }

    return passed ? 0 : 1;
}

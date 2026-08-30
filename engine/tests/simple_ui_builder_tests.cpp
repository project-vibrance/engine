#include <vibranceUI/engine.h>

#include <algorithm>
#include <cmath>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "simple UI builder test failed: "
                      << message << '\n';
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
    scale.factor = { 2.0f, 3.0f };
    scale.contentScale = { 2.0f, 3.0f };
    scale.logicalSize = { 400.0f, 240.0f };
    UiBuilder ui(scene, scale);

    bool passed = true;
    const entt::entity root = ui.root(-2, 4u);
    passed &= expect(
        root != entt::null && scene.registry().valid(root),
        "root should create a valid transparent layout entity");

    const entt::entity card = ui.block("#223344FF", 12.0f);
    ui.place(card)
        .inside(root)
        .at(UiAlignment::eCenter)
        .size(100.0f, 50.0f)
        .offset(10.0f, -5.0f)
        .padding(glm::vec4(4.0f, 5.0f, 6.0f, 7.0f))
        .layer(3, 8u, true);

    const entt::registry& registry = scene.registry();
    const Parent2DComponent& parent =
        registry.get<Parent2DComponent>(card);
    const Layout2DComponent& layout =
        registry.get<Layout2DComponent>(card);
    const LayoutRect2DComponent& content =
        registry.get<LayoutRect2DComponent>(card);
    const ShapeComponent& shape = registry.get<ShapeComponent>(card);
    const RenderLayer2DComponent& layer =
        registry.get<RenderLayer2DComponent>(card);

    passed &= expect(parent.parent == root, "inside should assign the parent");
    passed &= expect(
        close(layout.anchorMin.x, 0.5f) &&
            close(layout.anchorMin.y, 0.5f) &&
            close(layout.pivot.x, 0.5f) &&
            close(layout.pivot.y, 0.5f),
        "at should assign matching center anchor and pivot");
    passed &= expect(
        close(layout.size.x, 200.0f) &&
            close(layout.size.y, 150.0f),
        "size should convert logical dimensions once");
    passed &= expect(
        close(layout.offset.x, 20.0f) &&
            close(layout.offset.y, -15.0f),
        "offset should convert logical coordinates once");
    passed &= expect(
        close(content.padding.x, 8.0f) &&
            close(content.padding.y, 15.0f) &&
            close(content.padding.z, 12.0f) &&
            close(content.padding.w, 21.0f),
        "padding should preserve per-edge DPI scaling");
    passed &= expect(
        close(shape.cornerRadius, 24.0f),
        "block corner radius should use logical pixels");
    passed &= expect(
        layer.layer == 3 && layer.order == 8u && layer.alwaysOnTop,
        "layer should set stacking in one call");

    const entt::entity group = ui.layout_group(1, 6u);
    ui.place(group)
        .inside(root)
        .at(UiAlignment::eCenter)
        .size(120.0f, 90.0f);
    const entt::entity groupedChild = ui.block("#FFFFFFFF");
    ui.place(groupedChild)
        .inside(group)
        .at(UiAlignment::eBottomCenter)
        .size(100.0f, 60.0f);
    const Layout2DComponent& groupLayout =
        registry.get<Layout2DComponent>(group);
    const Layout2DComponent& groupedChildLayout =
        registry.get<Layout2DComponent>(groupedChild);
    passed &= expect(
        registry.all_of<InputTransparent2DComponent>(group) &&
            registry.get<Parent2DComponent>(group).parent == root &&
            registry.get<Parent2DComponent>(groupedChild).parent == group &&
            close(groupLayout.size.x, 240.0f) &&
            close(groupLayout.size.y, 270.0f) &&
            groupedChildLayout.anchorMin == glm::vec2(0.5f, 1.0f) &&
            groupedChildLayout.pivot == glm::vec2(0.5f, 1.0f),
        "a layout group should centre measured bounds without constraining child placement");

    const entt::entity background = ui.block("#000000FF");
    ui.place(background).inside(root).fill(6.0f).grid(1u, 2u, 2u, 3u);
    const Layout2DComponent& fill =
        registry.get<Layout2DComponent>(background);
    const GridCell2DComponent& cell =
        registry.get<GridCell2DComponent>(background);
    passed &= expect(
        fill.anchorMin == glm::vec2(0.0f) &&
            fill.anchorMax == glm::vec2(1.0f) &&
            close(fill.margin.x, 12.0f) &&
            close(fill.margin.y, 18.0f),
        "fill should stretch and scale margins");
    passed &= expect(
        cell.column == 1u && cell.row == 2u &&
            cell.columnSpan == 2u && cell.rowSpan == 3u,
        "grid should assign cell and span");

    UiStackLayout vertical({
        .axis = UiStackAxis::eVertical,
        .gap = 6.0f,
        .padding = { 10.0f, 20.0f, 0.0f, 0.0f }
    });
    const UiStackPlacement first = vertical.append({ 80.0f, 20.0f });
    const UiStackPlacement second = vertical.append({ 100.0f, 30.0f });
    passed &= expect(
        close(first.offset.x, 10.0f) && close(first.offset.y, 20.0f) &&
            close(second.offset.x, 10.0f) && close(second.offset.y, 46.0f),
        "vertical stack should derive each row offset from measured content");
    passed &= expect(
        close(vertical.content_size().x, 110.0f) &&
            close(vertical.content_size().y, 76.0f),
        "vertical stack should expose its measured content size");

    UiStackLayout horizontal({
        .axis = UiStackAxis::eHorizontal,
        .gap = 4.0f
    });
    horizontal.append({ 12.0f, 8.0f });
    const UiStackPlacement horizontalSecond = horizontal.append({ 20.0f, 10.0f });
    passed &= expect(
        close(horizontalSecond.offset.x, 16.0f) &&
            close(horizontal.content_size().x, 36.0f) &&
            close(horizontal.content_size().y, 10.0f),
        "horizontal stack should advance and measure on the horizontal axis");

    const Renderer2DFontAtlas unloadedAtlas;
    const std::string arabicWithNumber =
        "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7 123";
    const entt::entity rtlText = ui.text(
        arabicWithNumber,
        unloadedAtlas,
        14.0f);
    ui.place(rtlText)
        .inside(root)
        .at(UiAlignment::eTopLeft);
    passed &= expect(
        unloadedAtlas.layout_text(
            arabicWithNumber,
            14.0f).rightToLeft &&
            registry.get<Layout2DComponent>(rtlText).anchorMin ==
                glm::vec2(0.0f) &&
            registry.get<Layout2DComponent>(rtlText).pivot ==
                glm::vec2(0.0f) &&
            !unloadedAtlas.layout_text(
                "Latin 123",
                14.0f).rightToLeft,
        "RTL shaping should preserve explicitly authored UI placement");

    const std::string multilingualLiteral =
        "Espa\xC3\xB1ol \xE7\xAE\x80\xE4\xBD\x93\xE4\xB8\xAD\xE6\x96\x87 "
        "\xE0\xA4\xB9\xE0\xA4\xBF\xE0\xA4\xA8\xE0\xA5\x8D\xE0\xA4\xA6\xE0\xA5\x80";
    const std::vector<uint32_t> missing =
        unloadedAtlas.missing_codepoints(multilingualLiteral);
    passed &= expect(
        !unloadedAtlas.covers_text(multilingualLiteral) &&
            std::find(missing.begin(), missing.end(), 0x00F1u) != missing.end() &&
            std::find(missing.begin(), missing.end(), 0x7B80u) != missing.end() &&
            std::find(missing.begin(), missing.end(), 0x0939u) != missing.end(),
        "font coverage should report missing literal UTF-8 code points without losing them to question marks");

    return passed ? 0 : 1;
}

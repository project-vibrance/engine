// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include "../src/renderer/common/font_metrics.h"
#include <vibranceUI/glfw/window.h>
#include <vibranceUI/ui/text.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
void expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

struct Face
{
    std::vector<unsigned char> bytes;
    FT_Face face = nullptr;

    Face(FT_Library library, const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
        expect(!bytes.empty() && FT_New_Memory_Face(library, bytes.data(), FT_Long(bytes.size()), 0, &face) == 0,
            "The metric fixture must open a real font");
        expect(FT_Set_Pixel_Sizes(face, 0u, 96u) == 0, "The metric fixture must use the renderer's nominal size");
    }

    ~Face() { if (face) FT_Done_Face(face); }
};

float advance(FT_Face face)
{
    expect(FT_Load_Char(face, 'H', FT_LOAD_NO_BITMAP | FT_LOAD_NO_HINTING) == 0, "The fixture must supply Latin capitals");
    return float(face->glyph->advance.x) / 64.0f;
}

void weight_expansion()
{
    expect(TextComponent {}.bakedFontWeight == 400.0f,
        "Text without loaded font metadata must retain a regular-weight default");
    TextStyleComponent regular, selected;
    regular.set_font_weight(400.0f);
    selected.set_font_weight(700.0f);
    expect(std::abs(regular.resolved_font_weight_expansion(400.0f)) < 0.00001f &&
        std::abs(regular.resolved_font_weight_expansion(500.0f) + 0.17f) < 0.00001f,
        "Regular labels must preserve regular outlines and compensate a genuinely medium-weight atlas");
    expect(std::abs(selected.resolved_font_weight_expansion(400.0f) - 0.51f) < 0.00001f &&
        std::abs(selected.resolved_font_weight_expansion(500.0f) - 0.34f) < 0.00001f,
        "Selected labels must resolve their requested weight from the face actually baked");
}

void renderer(const std::filesystem::path& reference, const std::filesystem::path& candidate,
    const std::filesystem::path& script)
{
    GlfwWindowHostOptions options;
    options.title = "Vibrance font metric consistency";
    options.size = { 240, 120 };
    options.focusOnShow = options.showInTaskbar = options.showInAltTab = options.enableAudio = false;
    options.configureEngine = [&](EngineCreateInfo& info) { info.defaultRenderer2DFontPath = reference; };
    GlfwWindowHost host(options);
    expect(host.open(), "The font metric renderer fixture must open");
    set_glfw_window_visible(host.window(), false);
    auto& engine = *host.engine();
    Renderer2DFontAtlasLoadOptions fonts;
    fonts.fontWeight = 400;
    fonts.fallbackFontPaths = { script };
    fonts.emojiFontPaths = { reference.parent_path() / "AppleColorEmoji.ttf" };
    fonts.preloadText = "\xe6\x97\xa5\xe6\x9c\xac\xe8\xaa\x9e \xf0\x9f\x98\x80";
    fonts.requireRequestedGlyphs = true;
    expect(engine.load_renderer2d_font(reference, fonts), "The renderer must load the reference face and script support");
    expect(engine.renderer2d_font_atlas().font_weight() == 400,
        "Inter requested at regular weight must record a regular baked face");
    TextStyleComponent regular, selected;
    regular.set_font_weight(400.0f);
    selected.set_font_weight(700.0f);
    auto& scene = engine.renderer2d_scene();
    const auto label = scene.create_text("General", {}, 13.0f, regular);
    apply_font_layout(engine.renderer2d_font_atlas(), scene.registry(), label);
    expect(scene.registry().get<TextComponent>(label).bakedFontWeight == 400.0f,
        "Applying a font layout must record its actual baked weight on rendered text");
    const auto refX = engine.renderer2d_font_atlas().layout_text("x", 16.0f);
    const auto refH = engine.renderer2d_font_atlas().layout_text("H", 16.0f);
    expect(engine.load_renderer2d_font(candidate, fonts), "The renderer must load an unnormalised primary font");
    const auto originalScript = engine.renderer2d_font_atlas().layout_text("\xe6\x97\xa5", 16.0f);
    const auto originalEmoji = engine.renderer2d_font_atlas().layout_text("\xf0\x9f\x98\x80", 16.0f);
    fonts.fallbackFontPaths.insert(fonts.fallbackFontPaths.begin(), reference);
    expect(engine.load_renderer2d_font(candidate, fonts), "The renderer must load a normalised primary font");
    const auto& atlas = engine.renderer2d_font_atlas();
    const auto normalX = atlas.layout_text("x", 16.0f), normalH = atlas.layout_text("H", 16.0f);
    const auto normalScript = atlas.layout_text("\xe6\x97\xa5", 16.0f);
    const auto normalEmoji = atlas.layout_text("\xf0\x9f\x98\x80", 16.0f);
    const auto metric = [](const auto& x, const auto& h) { return x.bounds.y * 0.65f + h.bounds.y * 0.35f; };
    expect(std::abs(metric(refX, refH) - metric(normalX, normalH)) < 0.3f,
        "The rendered primary font must match the reference's visible letter size");
    expect(!normalScript.glyphs.empty() && glm::length(normalScript.bounds - originalScript.bounds) < 0.001f &&
        std::abs(normalScript.glyphs.front().advance - originalScript.glyphs.front().advance) < 0.001f,
        "Primary normalisation must not resize locale fallback glyphs");
    std::cout << "Emoji bounds before/after: " << originalEmoji.bounds.x << '/' << originalEmoji.bounds.y << " / " <<
        normalEmoji.bounds.x << '/' << normalEmoji.bounds.y << "; coloured: " <<
        (!normalEmoji.glyphs.empty() && normalEmoji.glyphs.front().coloured) << '\n';
    expect(!normalEmoji.glyphs.empty() && glm::length(normalEmoji.bounds - originalEmoji.bounds) < 0.001f && normalEmoji.glyphs.front().coloured,
        "Primary normalisation must retain emoji size and colour");
    expect(engine.load_renderer2d_font(reference, fonts), "The renderer must reload the Inter reference with its existing fallback chain");
    const auto retainedX = engine.renderer2d_font_atlas().layout_text("x", 16.0f);
    const auto retainedH = engine.renderer2d_font_atlas().layout_text("H", 16.0f);
    expect(retainedX.bounds == refX.bounds && retainedH.bounds == refH.bounds &&
        retainedX.glyphs.front().advance == refX.glyphs.front().advance,
        "Inter's rendered size and advances must remain exactly unchanged");
    fonts.fontWeight = 500;
    expect(engine.load_renderer2d_font(reference, fonts) && engine.renderer2d_font_atlas().font_weight() == 500,
        "A variable Inter face must record the medium weight that was actually baked");
    expect(set_text_entity(scene, engine.renderer2d_font_atlas(), label, "General"),
        "Text must accept the newly loaded medium-weight atlas");
    const auto& mediumText = scene.registry().get<TextComponent>(label);
    expect(mediumText.bakedFontWeight == 500.0f &&
        std::abs(regular.resolved_font_weight_expansion(mediumText.bakedFontWeight) + 0.17f) < 0.00001f &&
        std::abs(selected.resolved_font_weight_expansion(mediumText.bakedFontWeight) - 0.34f) < 0.00001f,
        "Relayout must update the baked weight and preserve medium-atlas regular and selected compensation");
    fonts.preloadText += " \xef\xa3\xbf";
    expect(engine.load_renderer2d_font(candidate, fonts) && engine.renderer2d_font_atlas().font_weight() == 400,
        "Requesting medium from a static Regular-only font must still record the actual regular face");
    expect(set_text_entity(scene, engine.renderer2d_font_atlas(), label, "Appearance"),
        "Text must accept an imported static regular atlas");
    const auto& regularText = scene.registry().get<TextComponent>(label);
    expect(regularText.bakedFontWeight == 400.0f &&
        std::abs(regular.resolved_font_weight_expansion(regularText.bakedFontWeight)) < 0.00001f &&
        std::abs(selected.resolved_font_weight_expansion(regularText.bakedFontWeight) - 0.51f) < 0.00001f,
        "Static Regular-only fonts must retain their outlines without an assumed medium-weight thinning");
    const auto plan = scene.build_render_plan(0.0, 0u);
    const auto containsLabel = [&](const auto& batches) {
        return std::any_of(batches.begin(), batches.end(), [&](const auto& batch) { return batch.entity == label; });
    };
    expect(!regularText.glyphs.empty() && (containsLabel(plan.texts) || containsLabel(plan.cachedTexts)),
        "The actual-weight metadata must remain attached to text that produces render batches");
    expect(set_text_entity(scene, engine.renderer2d_font_atlas(), label, "\xef\xa3\xbf"),
        "The static font must provide its distinctive symbol for mixed-face rendering");
    const auto packedWeight = [&] {
        scene.mark_dirty(label);
        const auto batches = scene.build_render_plan(0.0, 0u);
        for (const auto& batch : batches.texts) if (batch.entity == label) return batch.packedData;
        for (const auto& batch : batches.cachedTexts) if (batch.entity == label) return batch.packedData;
        throw std::runtime_error("The mixed-face symbol must produce a text batch");
    };
    const auto nativeSymbolWeight = packedWeight();
    fonts.fallbackFontPaths = { candidate, reference };
    expect(engine.load_renderer2d_font(script, fonts) && engine.renderer2d_font_atlas().font_weight() == 500,
        "The mixed-face fixture must bake its variable script primary at medium weight");
    expect(set_text_entity(scene, engine.renderer2d_font_atlas(), label, "\xef\xa3\xbf"),
        "A script font must retain the global font's fallback symbol");
    const auto& mixedText = scene.registry().get<TextComponent>(label);
    expect(mixedText.bakedFontWeight == 500.0f && mixedText.glyphs.size() == 1u &&
        mixedText.glyphs.front().bakedFontWeight == 400.0f && packedWeight() == nativeSymbolWeight,
        "Fallback glyphs must use their own baked weight rather than the primary face's weight");
    host.close();
}
}

int main(int argc, char** argv)
{
    if (argc < 4 || !std::filesystem::exists(argv[1]) || !std::filesystem::exists(argv[2]) ||
        !std::filesystem::exists(argv[3])) return 77;
    FT_Library library = nullptr;
    if (FT_Init_FreeType(&library) != 0) return 1;
    try
    {
        weight_expansion();
        {
            Face reference(library, argv[1]), candidate(library, argv[2]), script(library, argv[3]);
            const auto ref = vibrance_text::ui_font_metrics(reference.face);
            const auto original = vibrance_text::ui_font_metrics(candidate.face);
            const float originalAdvance = advance(candidate.face);
            expect(vibrance_text::normalise_ui_font(reference.face, reference.face, 96.0f) == 1.0f &&
                vibrance_text::ui_font_metrics(reference.face).visible_height() == ref.visible_height(),
                "The default Inter font must retain its exact original size");
            const float scale = vibrance_text::normalise_ui_font(candidate.face, reference.face, 96.0f);
            const auto normal = vibrance_text::ui_font_metrics(candidate.face);
            expect(std::abs(normal.visible_height() - ref.visible_height()) < 0.05f,
                "A selected font must match Inter's measured visible size");
            expect(std::abs(advance(candidate.face) / originalAdvance - scale) < 0.003f,
                "Glyph advances must scale uniformly with the font outlines");
            const auto scriptMetrics = script.face->size->metrics;
            expect(vibrance_text::normalise_ui_font(script.face, reference.face, 96.0f) == 1.0f &&
                script.face->size->metrics.y_scale == scriptMetrics.y_scale,
                "Locale-specific primary fonts must preserve their native script sizing");
            std::cout << "Reference x/cap: " << ref.xHeight << '/' << ref.capHeight << "; selected: " <<
                original.xHeight << '/' << original.capHeight << "; scale: " << scale << "; normalised: " <<
                normal.xHeight << '/' << normal.capHeight << '\n';
        }
        FT_Done_FreeType(library);
        library = nullptr;
        if (argc > 4 && std::string_view(argv[4]) == "--renderer") renderer(argv[1], argv[2], argv[3]);
        return 0;
    }
    catch (const std::exception& error)
    {
        if (library) FT_Done_FreeType(library);
        std::cerr << error.what() << '\n';
        return 1;
    }
}

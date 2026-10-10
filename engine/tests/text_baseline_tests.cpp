#include <vibranceUI/glfw/window.h>
#include <vibranceUI/ui/builder.h>
#include <vibranceUI/ui/text.h>
#include <cmath>
#include <filesystem>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 5 || !std::filesystem::exists(argv[1])) return 77;
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    const auto near = [](float a, float b) { return std::abs(a - b) < 0.003f; };
    GlfwWindowHostOptions options;
    options.title = "Vibrance text baseline regression";
    options.size = { 240, 120 };
    options.focusOnShow = false;
    options.showInTaskbar = options.showInAltTab = false;
    options.enableAudio = false;
    options.configureEngine = [&](EngineCreateInfo& info) { info.defaultRenderer2DFontPath = argv[1]; };
    GlfwWindowHost host(options);
    if (!host.open()) return 77;
    set_glfw_window_visible(host.window(), false);
    auto& engine = *host.engine();
    bool exercisedDescender = false;
    for (int index = 1; index <= 2; ++index)
    {
        if (!std::filesystem::exists(argv[index])) continue;
        if (!engine.load_renderer2d_font(argv[index])) { passed = false; continue; }
        const auto& font = engine.renderer2d_font_atlas();
        for (const float dpi : { 1.0f, 1.5f, 2.0f })
        for (const bool flow : { false, true })
        {
            auto& scene = engine.renderer2d_scene();
            scene.clear();
            LayoutScale scale;
            scale.factor = glm::vec2(dpi);
            UiBuilder ui(scene, scale);
            const auto row = scene.create_shape({}, { 200.0f * dpi, 40.0f * dpi });
            const auto text = ui.create_aligned_text("Dolb Atmos", font, row,
                UiAlignment::eMiddleLeft, 12.0f, TextStyleComponent {}, 1, 0u);
            if (flow)
            {
                auto& textLayout = scene.registry().emplace<TextLayout2DComponent>(text);
                textLayout.options.characterSpacing = 0.25f * dpi;
            }
            ui_align_text_cap_height(scene, font, text);
            const auto rect = [&] {
                const auto plan = scene.build_render_plan(0.0, 0u);
                const auto& glyphs = scene.registry().get<TextComponent>(text).glyphs;
                if (glyphs.empty()) return glm::vec4(0.0f);
                for (const auto* batches : { &plan.texts, &plan.cachedTexts })
                    for (const auto& batch : *batches)
                        if (batch.entity == text && glm::vec2(batch.uvRect) == glyphs.front().uvMin)
                            return glm::vec4(batch.rect.x - glyphs.front().position.x,
                                batch.rect.y - glyphs.front().position.y, batch.rect.z, batch.rect.w);
                return glm::vec4(0.0f);
            };
            const auto before = rect();
            const auto& original = scene.registry().get<TextComponent>(text);
            expect(!original.glyphs.empty() && before.z > 0.0f, "The real font must produce rendered glyph bounds");
            if (original.glyphs.empty()) continue;
            const float capitalY = before.y + original.glyphs.front().position.y;
            const float height = original.bounds.y;
            set_text_entity(scene, font, text, "Dolby Atmos");
            const auto after = rect();
            const auto& changed = scene.registry().get<TextComponent>(text);
            exercisedDescender |= !near(height, changed.bounds.y);
            expect(near(capitalY, after.y + changed.glyphs.front().position.y),
                "Adding a descending y must not move the capital D or the rest of the label");
            const float expectedBaseline = 20.0f * dpi + font.layout_text("H", 12.0f * dpi).bounds.y * 0.5f;
            for (const char* label : { "Lossless", "Lossless y", "Dolby Atmos", "gpqy", "H" })
            {
                set_text_entity(scene, font, text, label);
                const auto labelRect = rect();
                const auto* layout = scene.registry().try_get<TextLayout2DComponent>(text);
                const auto measured = layout ? font.layout_text(label, 12.0f * dpi, layout->options) :
                    font.layout_text(label, 12.0f * dpi);
                expect(near(labelRect.y + measured.baseline, expectedBaseline),
                    "Labels with and without capitals and descenders must share one fixed baseline at every DPI");
            }
        }
    }
    expect(exercisedDescender, "The regression must cover a genuine difference in visible glyph height");
    for (int index = 3; index <= 4; ++index)
    {
        Media2DLoadOptions mediaOptions;
        mediaOptions.rasterWidth = mediaOptions.rasterHeight = 256u;
        mediaOptions.premultiplyAlpha = true;
        const auto original = engine.load_media_2d(argv[index], mediaOptions);
        mediaOptions.trimTransparentBorders = true;
        const auto trimmed = engine.load_media_2d(argv[index], mediaOptions);
        expect(original.drawable && trimmed.drawable && original.id != trimmed.id,
            "Original and trimmed SVGs must both render and use distinct cache entries");
        expect(original.pixelSize == glm::uvec2(256u) && trimmed.pixelSize.x < 256u && trimmed.pixelSize.y < 256u,
            "Trimming must remove invisible SVG padding while preserving the original asset");
        const float aspect = static_cast<float>(trimmed.pixelSize.x) / std::max(trimmed.pixelSize.y, 1u);
        expect(index == 3 ? aspect > 1.3f && aspect < 1.6f : aspect > 1.7f && aspect < 2.1f,
            "Badge logos must retain the aspect ratio of their visible artwork instead of the square canvas");
    }
    host.close();
    return passed ? 0 : 1;
}

#include <vibranceUI/glfw/window.h>
#include <vibranceUI/ui/animated_text_view.h>

#include <array>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <string>

int main(int argc, char** argv)
{
    if (argc < 2 || !std::filesystem::exists(argv[1])) return 77;
    bool passed = true;
    bool testedProportionalDigits = false;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    const auto near = [](float a, float b) { return std::abs(a - b) < 0.002f; };
    GlfwWindowHostOptions hostOptions;
    hostOptions.title = "Vibrance digit spacing regression";
    hostOptions.size = { 240, 120 };
    hostOptions.focusOnShow = false;
    hostOptions.showInTaskbar = hostOptions.showInAltTab = false;
    hostOptions.enableAudio = false;
    hostOptions.configureEngine = [&](EngineCreateInfo& info) {
        info.defaultRenderer2DFontPath = argv[1];
    };
    GlfwWindowHost host(hostOptions);
    if (!host.open()) return 77;
    set_glfw_window_visible(host.window(), false);
    auto& engine = *host.engine();
    for (int fontIndex = 1; fontIndex < argc; ++fontIndex)
    {
        if (!std::filesystem::exists(argv[fontIndex])) continue;
        Renderer2DFontAtlasLoadOptions fontOptions;
        fontOptions.requireRequestedGlyphs = true;
        if (!engine.load_renderer2d_font(argv[fontIndex], fontOptions))
        {
            expect(false, "The digit fixture must load its real primary or override font");
            continue;
        }
        const auto& atlas = engine.renderer2d_font_atlas();
        for (const float fontSize : { 14.0f, 21.0f, 28.0f, 46.0f })
        {
            const auto zero = atlas.layout_text("0", fontSize);
            expect(zero.glyphs.size() == 1, "A loaded font must supply numeric glyphs");
            if (zero.glyphs.empty()) continue;
            TextLayout2DOptions natural;
            natural.tabularDigits = false;
            for (char digit = '0'; digit <= '9'; ++digit)
            {
                const std::string value(1, digit);
                const auto cell = atlas.layout_text(value, fontSize);
                const auto proportional = atlas.layout_text(value, fontSize, natural);
                expect(cell.glyphs.size() == 1 && proportional.glyphs.size() == 1,
                    "Both digit spacing modes must retain their original glyphs");
                if (cell.glyphs.empty() || proportional.glyphs.empty()) continue;
                expect(near(cell.bounds.x, zero.bounds.x) &&
                    near(cell.glyphs.front().advance, zero.glyphs.front().advance),
                    "All ten digits must have equal measured widths and advances, including narrow one");
                testedProportionalDigits = testedProportionalDigits ||
                    !near(cell.glyphs.front().advance, proportional.glyphs.front().advance);
            }
            for (const std::string format : { "00:00", "-00:00", "00:00:00", "0hr 00m" })
            {
                const auto baseline = atlas.layout_text(format, fontSize);
                TextLayout2DOptions flow;
                flow.characterSpacing = 0.3f;
                const auto baselineFlow = atlas.layout_text(format, fontSize, flow);
                TextLayout2DOptions wrap = flow;
                wrap.maximumWidth = fontSize * 3.0f;
                wrap.wrapMode = TextWrapMode2D::eCharacter;
                const auto baselineWrap = atlas.layout_text(format, fontSize, wrap);
                for (char digit = '0'; digit <= '9'; ++digit)
                {
                    auto value = format;
                    for (char& character : value) if (character == '0') character = digit;
                    expect(near(atlas.layout_text(value, fontSize).bounds.x, baseline.bounds.x),
                        "Timestamps, signed remaining times and compact timers must retain their width as digits change");
                    expect(near(atlas.layout_text(value, fontSize, flow).bounds.x, baselineFlow.bounds.x),
                        "Explicit text spacing must preserve the same tabular measurement as ordinary text");
                    expect(atlas.layout_text(value, fontSize, wrap).lineCount == baselineWrap.lineCount,
                        "Changing numeric values must not alter wrapping decisions");
                }
            }
            const auto letters = atlas.layout_text("AVATAR", fontSize);
            const auto naturalLetters = atlas.layout_text("AVATAR", fontSize, natural);
            expect(near(letters.bounds.x, naturalLetters.bounds.x) &&
                !near(atlas.layout_text("i", fontSize).bounds.x, atlas.layout_text("W", fontSize).bounds.x),
                "Letters must retain their natural spacing and kerning");
        }
        auto& scene = engine.renderer2d_scene();
        scene.clear();
        UiBuilder ui(scene);
        UiAnimatedCharacterTextOptions options;
        options.fontSize = 46.0f;
        options.characterCount = 5;
        options.proportionalSpacing = true;
        auto view = ui_create_animated_character_text(ui, atlas, ui.root(), "00:00", options);
        std::array<float, 5> offsets;
        for (std::size_t index = 0; index < offsets.size(); ++index)
            offsets[index] = scene.registry().get<Layout2DComponent>(view.characters[index]).offset.x;
        ui_update_animated_character_text(engine, view, "11:11", 1.0, true);
        for (std::size_t index = 0; index < offsets.size(); ++index)
            expect(near(offsets[index], scene.registry().get<Layout2DComponent>(view.characters[index]).offset.x),
                "Animated timers must keep every digit and colon in the same horizontal cell");
    }
    expect(testedProportionalDigits, "The regression must exercise a font whose native digits have unequal advances");
    host.close();
    return passed ? 0 : 1;
}

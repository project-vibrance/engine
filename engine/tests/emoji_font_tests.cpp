#define STBI_ONLY_PNG
#define STB_IMAGE_IMPLEMENTATION
#include "../src/renderer/common/emoji_shaper.h"
#include "../src/renderer/common/colour_bitmap.h"
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/glfw/window.h>
#include <vibranceUI/ui/text.h>
#include <vibranceUI/notifications/card.h>
#include <chrono>
#include <fstream>
#include <iostream>

int main(int argc, char** argv)
{
    if (argc < 2 || !std::filesystem::exists(argv[1])) return 77;
    std::ifstream file(argv[1], std::ios::binary);
    auto bytes = std::make_shared<std::vector<unsigned char>>(
        std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    Renderer2DEmojiShaper shaper;
    shaper.add(*bytes, std::shared_ptr<const unsigned char>(bytes, bytes->data()), 0, 0);
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    const std::vector<std::string> sequences {
        "\xf0\x9f\x98\x80", // Grinning face.
        "\xf0\x9f\x87\xac\xf0\x9f\x87\xa7", // United Kingdom flag.
        "\xf0\x9f\x91\x8d\xf0\x9f\x8f\xbd", // Thumb and skin tone.
        "\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x92\xbb", // Woman technologist.
        "\xf0\x9f\x91\xa8\xe2\x80\x8d\xf0\x9f\x91\xa9\xe2\x80\x8d\xf0\x9f\x91\xa7", // Family.
        "1\xef\xb8\x8f\xe2\x83\xa3", // Keycap.
        "\xe2\x9d\xa4\xef\xb8\x8f" // Emoji heart.
    };
    for (const auto& sequence : sequences)
    {
        const auto run = shaper.visual(sequence);
        expect(run.size() == 1 && run.front() >= 0x110000, "Emoji sequence must form one glyph");
        if (run.size() != 1) continue;
        vibrance_text::ColourBitmap bitmap;
        expect(vibrance_text::load_cbdt_bitmap(*bytes, 0, run.front() - 0x110000, 96, bitmap),
            "Shaped glyph must decode from the colour bitmap table");
        bool chromatic = false, opaque = false;
        for (std::size_t p = 0; p + 3 < bitmap.rgba.size(); p += 4)
        {
            opaque |= bitmap.rgba[p + 3] > 200;
            chromatic |= bitmap.rgba[p] != bitmap.rgba[p + 1] || bitmap.rgba[p + 1] != bitmap.rgba[p + 2];
            expect(bitmap.rgba[p] <= bitmap.rgba[p + 3] && bitmap.rgba[p + 1] <= bitmap.rgba[p + 3] &&
                bitmap.rgba[p + 2] <= bitmap.rgba[p + 3], "Colour atlas pixels must be premultiplied");
        }
        expect(chromatic && opaque, "Emoji must retain visible colour and alpha");
        expect(bitmap.advance > 0 && bitmap.width > 0, "Emoji must have usable layout metrics");
    }
    expect(shaper.visual("2026").size() == 4 && shaper.visual("2026").front() == '2', "Digits must remain ordinary text");
    expect(shaper.visual("\xe2\x9d\xa4\xef\xb8\x8e") == std::vector<uint32_t>{0x2764},
        "Text presentation must bypass the emoji font without a selector glyph");
    const auto mixed = shaper.visual("A " + sequences[3] + " B");
    expect(mixed.size() == 5 && mixed[0] == 'A' && mixed[2] >= 0x110000 && mixed[4] == 'B',
        "Joined emoji must occupy one cell in mixed text");
    const auto rtl = shaper.visual("\xd8\xa7 " + sequences[3]);
    expect(std::count_if(rtl.begin(), rtl.end(), [](auto cp) { return cp >= 0x110000; }) == 1,
        "Joined emoji must remain atomic in right-to-left text");
    std::vector<unsigned char> broken(32, 0xff);
    vibrance_text::ColourBitmap bitmap;
    expect(!vibrance_text::load_cbdt_bitmap(broken, 0, 1, 96, bitmap), "Malformed font offsets must fail safely");
    const auto temporary = std::filesystem::temp_directory_path() /
        ("vibrance-emoji-profile-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(temporary / "packaged");
    std::filesystem::create_directories(temporary / "user");
    const auto packaged = temporary / "packaged";
    const auto user = temporary / "user";
    std::ofstream(packaged / "AppleColorEmoji.ttf") << "packaged";
    std::ofstream(user / "CustomEmoji.ttf") << "custom";
    std::ofstream(temporary / "default.json") << "{\"emoji\":\"AppleColorEmoji.ttf\"}";
    std::ofstream(temporary / "override.json") << "{\"emoji\":\"CustomEmoji.ttf\"}";
    Renderer2DFontProfile profile;
    expect(renderer2d_load_font_profile_layers({ temporary / "default.json", temporary / "override.json" },
        { packaged, user }, profile), "Layered emoji profiles must load");
    auto candidates = renderer2d_emoji_font_candidates(profile, { packaged, user });
    expect(candidates.size() == 2 && candidates.front() == user / "CustomEmoji.ttf" &&
        candidates.back() == packaged / "AppleColorEmoji.ttf", "Named emoji overrides must precede packaged fallback");
    std::ofstream(user / "AppleColorEmoji.ttf") << "same-name override";
    candidates = renderer2d_emoji_font_candidates({}, { packaged, user });
    expect(candidates.size() == 2 && candidates.front() == user / "AppleColorEmoji.ttf",
        "Same-name emoji assets must use the user layer first");
    std::ofstream(temporary / "override.json") << "{\"emoji\":\"Missing.ttf\"}";
    renderer2d_load_font_profile_layers({ temporary / "default.json", temporary / "override.json" },
        { packaged, user }, profile);
    expect(!profile.emojiFontCandidates.empty(), "A missing override must retain the valid default profile");
    std::filesystem::remove_all(temporary);

    if (argc >= 3 && std::string_view(argv[2]) == "--renderer")
    {
        GlfwWindowHostOptions options;
        options.title = "Vibrance emoji renderer regression";
        options.size = { 240, 120 };
        options.enableAudio = false;
        options.focusOnShow = false;
        options.showInTaskbar = false;
        options.showInAltTab = false;
        options.configureEngine = [&](EngineCreateInfo& info) {
            info.defaultRenderer2DFontPath = std::filesystem::path(argv[1]).parent_path() / "Inter.ttc";
            auto& fonts = info.defaultRenderer2DFontOptions;
            fonts.emojiFontPaths = { argv[1] };
            fonts.requireRequestedGlyphs = true;
            for (const auto& sequence : sequences) fonts.preloadText += sequence + '\n';
            fonts.preloadText += "\xf0\x9f\x94\xa5"; // Fire, before the joined heart arrives.
        };
        GlfwWindowHost host(options);
        if (!host.open()) return 77;
        auto& engine = *host.engine();
        auto& atlas = engine.renderer2d_font_atlas();
        expect(atlas.loaded(), "Strict mixed text and emoji preload must create a usable GPU atlas");
        const auto verifySdfMetadata = [&](const Renderer2DTextLayout& layout) {
            expect(!layout.glyphs.empty(), "SDF metadata regression needs visible ordinary glyphs");
            for (const auto& glyph : layout.glyphs)
            {
                expect(!glyph.coloured && glyph.sdfPadding == 24.0f &&
                    glyph.sdfAtlasSize.x > 48.0f && glyph.sdfAtlasSize.y > 48.0f &&
                    glyph.sdfUvRect == glm::vec4(glyph.uvMin.x, glyph.uvMin.y, glyph.uvMax.x, glyph.uvMax.y),
                    "Generated SDF layout must retain its unscaled padding, source dimensions and UV provenance");
            }
        };
        verifySdfMetadata(atlas.layout_text("AMW", 18.0f));
        TextLayout2DOptions ordinaryFlow;
        ordinaryFlow.maximumWidth = 60.0f;
        ordinaryFlow.wrapMode = TextWrapMode2D::eCharacter;
        verifySdfMetadata(atlas.layout_text("AMW", 18.0f, ordinaryFlow));
        for (const auto& sequence : sequences)
        {
            auto layout = atlas.layout_text(sequence, 18.0f);
            expect(layout.glyphs.size() == 1 && layout.glyphs.front().coloured,
                "Renderer layout must emit one coloured glyph for each emoji cluster");
            expect(layout.glyphs.size() == 1 && layout.glyphs.front().sdfPadding == 0.0f &&
                layout.glyphs.front().sdfAtlasSize == glm::vec2(0.0f),
                "Colour bitmap glyphs must retain complete sampling bounds without SDF padding metadata");
            expect(ui_supported_notification_text(atlas, sequence, 18.0f) == sequence,
                "Notification filtering must preserve emoji selectors and joiners");
            TextLayout2DOptions flow;
            flow.maximumWidth = 28.0f;
            flow.wrapMode = TextWrapMode2D::eCharacter;
            layout = atlas.layout_text(sequence + sequence, 18.0f, flow);
            expect(layout.glyphs.size() == 2 && layout.glyphs.front().coloured && layout.lineCount == 2,
                "Wrapping must retain colour and keep emoji clusters intact");
        }
        const std::string joined = "\xe2\x9d\xa4\xef\xb8\x8f\xe2\x80\x8d\xf0\x9f\x94\xa5";
        expect(!atlas.covers_text(joined), "An unseen joined sequence must be discovered despite known constituent characters");
        auto& scene = engine.renderer2d_scene();
        const auto text = scene.create_text(joined, { 8, 8 }, 18.0f);
        apply_font_layout(atlas, scene.registry(), text);
        engine.draw();
        expect(atlas.covers_text(joined), "Scene text must expand the atlas for a newly joined emoji");
        const auto& glyphs = scene.registry().get<TextComponent>(text).glyphs;
        expect(glyphs.size() == 1 && glyphs.front().coloured,
            "Dynamic emoji expansion must relayout the existing text entity");
        engine.draw();
        host.close();
    }
    return passed ? 0 : 1;
}

// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/glfw/window.h>
#include <vibranceUI/renderer/font_atlas.h>
#include <vibranceUI/renderer/renderer.h>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace
{
void expect(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

float gap(const Renderer2DTextLayout& layout)
{
    expect(layout.glyphs.size() == 2u, "The positioning fixture must contain two visible glyphs");
    return layout.glyphs[1].position.x - layout.glyphs[0].position.x;
}

void check(Engine& engine, const std::filesystem::path& candidate,
    const std::filesystem::path& reference, bool authoredTracking)
{
    Renderer2DFontAtlasLoadOptions load;
    load.fontWeight = 400;
    load.preloadText = "nnAVTo12:34";
    load.fallbackFontPaths = { reference };
    expect(engine.load_renderer2d_font(candidate, load), "The positioning fixture must load its font");
    auto& atlas = engine.renderer2d_font_atlas();
    atlas.set_pixel_scale(1.0f);
    const auto ordinary = atlas.layout_text("nn", 13.0f);
    const float advance = atlas.layout_text("n", 13.0f).glyphs.front().advance;
    const float tracking = gap(ordinary) - advance;
    if (authoredTracking)
    {
        expect(tracking > 0.3f && tracking < 0.8f,
            "Small SF Pro Display text must use its authored optical tracking");
        const auto large = atlas.layout_text("nn", 80.0f);
        const float largeAdvance = atlas.layout_text("n", 80.0f).glyphs.front().advance;
        expect(std::abs(gap(large) - largeAdvance) < 0.001f,
            "Display-size SF text must use its zero-tracking optical endpoint");
    }
    atlas.set_pixel_scale(2.0f);
    const auto retina = atlas.layout_text("nn", 26.0f);
    expect(std::abs(gap(retina) - gap(ordinary) * 2.0f) < 0.001f,
        "Doubling display scale must retain the same optical spacing in points");
    atlas.set_pixel_scale(1.0f);
    TextLayout2DOptions explicitSize;
    explicitSize.opticalPointSize = 13.0f;
    const auto explicitPoints = atlas.layout_text("nn", 26.0f, explicitSize);
    expect(std::abs(gap(explicitPoints) - gap(retina)) < 0.001f,
        "An explicit optical point size must match the corresponding display scale");
    if (authoredTracking)
    {
        TextLayout2DOptions flow;
        flow.wrapMode = TextWrapMode2D::eCharacter;
        flow.maximumWidth = advance * 2.0f + tracking * 0.5f;
        expect(atlas.layout_text("nn", 13.0f, flow).lineCount == 2u,
            "Wrapping must measure the same authored spacing as glyph placement");
        flow.maximumWidth = advance * 2.0f + tracking * 1.5f;
        expect(atlas.layout_text("nn", 13.0f, flow).lineCount == 1u,
            "Wrapping must not add tracking beyond the final glyph");
        const auto pair = atlas.layout_text("AV", 13.0f);
        const float unkerned = atlas.layout_text("A", 13.0f).glyphs.front().advance;
        expect(gap(pair) < unkerned,
            "SF Pro Display's GPOS pair adjustment must apply even without a legacy kern table");
        TextLayout2DOptions timer;
        timer.opticalPointSize = 13.0f;
        const auto smallOptical = atlas.layout_text("12:34", 13.0f, timer);
        timer.opticalPointSize = 80.0f;
        const auto largeOptical = atlas.layout_text("12:34", 13.0f, timer);
        expect(smallOptical.glyphs.size() == largeOptical.glyphs.size(),
            "Tabular timer glyphs must be retained across optical sizes");
        for (std::size_t index = 0u; index < smallOptical.glyphs.size(); ++index)
            expect(smallOptical.glyphs[index].position == largeOptical.glyphs[index].position,
                "Optical tracking must not alter tabular timer digit spacing");
    }
    std::cout << candidate.filename().string() << ": small-text inter-letter tracking=" << tracking << " px\n";
}
}

int main(int argc, char** argv)
{
    if (argc < 2 || !std::filesystem::exists(argv[1])) return 77;
    try
    {
        const std::filesystem::path reference = argv[1];
        GlfwWindowHostOptions options;
        options.title = "Vibrance font positioning";
        options.size = { 240, 120 };
        options.focusOnShow = options.showInTaskbar = options.showInAltTab = options.enableAudio = false;
        options.configureEngine = [&](EngineCreateInfo& info) { info.defaultRenderer2DFontPath = reference; };
        GlfwWindowHost host(options);
        expect(host.open(), "The font positioning renderer fixture must open");
        set_glfw_window_visible(host.window(), false);
        check(*host.engine(), reference, reference, false);
        if (argc > 2 && std::filesystem::exists(argv[2])) check(*host.engine(), argv[2], reference, true);
        host.close();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

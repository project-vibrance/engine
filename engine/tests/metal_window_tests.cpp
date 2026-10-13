#include <vibranceUI/glfw/window.h>
#include <vibranceUI/renderer/metal/capabilities.h>
#include <vibranceUI/ui/text.h>
#include <vibranceUI/ui/liquid_controls.h>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <chrono>
#include <ctime>
#include <string_view>
#include <thread>
#include <cmath>

int main(int argc, char **argv)
{
    if (vibrance_metal_version() < 3)
        return 77;
    try
    {
        const std::string_view mode = argc >= 2 ? argv[1] : "";
        const bool countdown = mode == "--benchmark-countdown";
        const bool metal3Expansion = mode == "--benchmark-expansion-metal3";
        const bool metal3BlurExpansion = mode == "--benchmark-expansion-blur-metal3";
        const bool blurExpansion = mode == "--benchmark-expansion-blur" || metal3BlurExpansion;
        const bool expansion = mode == "--benchmark-expansion" || metal3Expansion || blurExpansion;
        const bool interfaceAnimation = countdown || expansion;
        GlfwWindowHostOptions options;
        options.title = "Vibrance Metal renderer test";
        options.size = interfaceAnimation ? glm::ivec2(2056, 300) : glm::ivec2(160, 160);
        options.enableAudio = false;
        options.focusOnShow = false;
        options.transparentFramebuffer = true;
        options.configureEngine = [](EngineCreateInfo &info) {
            info.defaultRenderer2DFontPath = "/System/Library/Fonts/Helvetica.ttc";
        };
        GlfwWindowHost host(options);
        if (!host.open())
            throw std::runtime_error("Cannot open Metal window");
        auto &engine = *host.engine();
        auto &scene = engine.renderer2d_scene();
        if (engine.render_backend() != RenderBackend::eMetal || !engine.renderer2d_font_atlas().loaded())
            throw std::runtime_error("Renderer/font initialisation failed");
        ShapeStyleComponent style;
        style.color0 = {0.2f, 0.5f, 0.8f, 0.5f};
        auto shape = scene.create_shape({0, 0}, {140, 140}, style);
        scene.registry().emplace<ShadowComponent>(shape);
        if (!interfaceAnimation || blurExpansion)
            scene.registry().emplace<BlurComponent>(shape);
        auto text = scene.create_text("Metal 3 & 4", {8, 8}, 16);
        apply_font_layout(engine.renderer2d_font_atlas(), scene.registry(), text);
        if (!interfaceAnimation)
            scene.create_model({20, 35}, {90, 90});
        const auto path = std::filesystem::temp_directory_path() / "vibrance-metal-test.ppm";
        {
            std::ofstream image(path);
            image << "P3\n2 1\n255\n255 0 0 0 255 0\n";
        }
        auto media = engine.load_media_2d(path);
        if (!media.drawable || engine.load_media_2d(path).id != media.id)
            throw std::runtime_error("Media upload/cache failed");
        scene.create_media({8, 130}, {64, 16}, media);
        if (argc >= 2 && (std::string_view(argv[1]) == "--benchmark-idle" ||
                          std::string_view(argv[1]) == "--benchmark-idle-uncapped" ||
                          std::string_view(argv[1]) == "--benchmark-animated" || interfaceAnimation))
        {
            const bool animated = mode == "--benchmark-animated" || interfaceAnimation;
            for (int i = 0; i < 24; ++i)
            {
                auto label =
                    scene.create_text("Metal performance",
                                      interfaceAnimation ? glm::vec2(1200 + (i % 4) * 170, 70 + (i / 4) * 30)
                                                         : glm::vec2(8, float(8 + i * 5)),
                                      12);
                apply_font_layout(engine.renderer2d_font_atlas(), scene.registry(), label);
            }
            // Match hosts which report their framebuffer size on every UI tick.
            // The explicit rate makes before/after CPU measurements comparable.
            const bool uncapped = std::string_view(argv[1]) == "--benchmark-idle-uncapped";
            engine.set_target_frame_rate(uncapped ? 0 : (argc >= 3 ? std::stoul(argv[2]) : 120));
            using Clock = std::chrono::steady_clock;
            const uint32_t width = interfaceAnimation ? engine.render_width() : 160;
            const uint32_t height = interfaceAnimation ? engine.render_height() : 160;
            const auto animationStart = Clock::now();
            unsigned displayedSecond = UINT32_MAX;
            if (interfaceAnimation)
            {
                scene.registry().get<Transform2DComponent>(shape).position = {900, 20};
                scene.registry().get<ShapeComponent>(shape).size = {300, 70};
                scene.registry().get<Transform2DComponent>(text).position = {1000, 45};
                scene.registry().get<TextComponent>(text).fontSize = 32;
            }
            ShapeStyleComponent handStyle;
            handStyle.color0 = {1, 0.6f, 0.2f, 1};
            const auto hand =
                interfaceAnimation ? scene.create_shape({930, 48}, {12, 5}, handStyle) : entt::null;
            auto tick = [&] {
                engine.resize(width, height);
                if (interfaceAnimation)
                {
                    const double time = std::chrono::duration<double>(Clock::now() - animationStart).count();
                    const unsigned second = unsigned(time);
                    if (second != displayedSecond)
                    {
                        scene.registry().get<TextComponent>(text).text =
                            "00:" + std::to_string(59 - second % 60);
                        apply_font_layout(engine.renderer2d_font_atlas(), scene.registry(), text);
                        scene.mark_dirty(text);
                        displayedSecond = second;
                    }
                    auto &transform = scene.registry().get<Transform2DComponent>(hand);
                    transform.position = {930 + float(std::cos(time) * 10), 48 + float(std::sin(time) * 10)};
                    transform.rotationRadians = float(time);
                    scene.mark_dirty(hand);
                    if (expansion)
                    {
                        scene.registry().get<ShapeComponent>(shape).size = {
                            300 + float(150 * (1 + std::sin(time * 3))),
                            70 + float(60 * (1 + std::sin(time * 3)))};
                        scene.mark_dirty(shape);
                    }
                }
                else if (animated)
                    scene.mark_dirty(shape);
                engine.draw();
                glfwPollEvents();
            };
            auto start = Clock::now();
            while (Clock::now() - start < std::chrono::seconds(2))
                tick();
            start = Clock::now();
            const auto cpu = std::clock();
            unsigned ticks = 0;
            while (Clock::now() - start < std::chrono::seconds(5))
            {
                tick();
                ++ticks;
            }
            const double elapsed = std::chrono::duration<double>(Clock::now() - start).count();
            std::cout << (interfaceAnimation
                              ? (countdown ? "countdown" : (metal3BlurExpansion ? "expansion blur Metal 3" :
                                 metal3Expansion ? "expansion Metal 3" :
                                 blurExpansion ? "expansion blur" : "expansion"))
                              : (animated ? "animated" : (uncapped ? "idle uncapped" : "idle")))
                      << ": " << ticks / elapsed << " ticks/s, "
                      << 100.0 * (std::clock() - cpu) / CLOCKS_PER_SEC / elapsed << "% CPU (one core)\n";
            const bool ready = engine.ready();
            host.close();
            terminate_glfw();
            std::filesystem::remove(path);
            return ready ? 0 : 1;
        }
        for (int i = 0; i < 3; ++i)
        {
            engine.draw();
            glfwPollEvents();
            if (!engine.ready())
                throw std::runtime_error("Metal frame failed");
        }
        const std::filesystem::path fontPath = "/System/Library/Fonts/Helvetica.ttc";
        const auto retainedAtlas = engine.renderer2d_font_atlas().image()->native;
        const auto retainedGeneration = scene.frame_generation();
        const auto fontReuseStart = std::chrono::steady_clock::now();
        Renderer2DFontAtlasLoadOptions coveredFont;
        coveredFont.preloadText = "Metal 3 & 4";
        if (!engine.load_renderer2d_font(fontPath) ||
            !engine.load_renderer2d_font(fontPath, coveredFont) ||
            engine.renderer2d_font_atlas().image()->native != retainedAtlas ||
            scene.frame_generation() != retainedGeneration)
            throw std::runtime_error("An unchanged panel font rebuilt its atlas or scene");
        std::cout << "Retained font requests: " <<
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - fontReuseStart).count()
            << " ms\n";
        auto expandedFont = coveredFont;
        expandedFont.preloadText += " \u03a9";
        if (engine.renderer2d_font_atlas().missing_codepoints(expandedFont.preloadText).empty() ||
            !engine.load_renderer2d_font(fontPath, expandedFont) ||
            engine.renderer2d_font_atlas().image()->native == retainedAtlas)
            throw std::runtime_error("A new panel glyph did not expand the font atlas");
        const auto expandedAtlas = engine.renderer2d_font_atlas().image()->native;
        if (!engine.load_renderer2d_font(fontPath) ||
            engine.renderer2d_font_atlas().image()->native != expandedAtlas ||
            !engine.renderer2d_font_atlas().missing_codepoints("\u03a9").empty())
            throw std::runtime_error("A panel refresh discarded previously loaded glyphs");
        auto differentFont = expandedFont;
        differentFont.fontWeight = 700;
        if (!engine.load_renderer2d_font(fontPath, differentFont) ||
            engine.renderer2d_font_atlas().image()->native == expandedAtlas)
            throw std::runtime_error("A changed font request incorrectly reused its old atlas");
        engine.draw();
        const auto generation = scene.frame_generation();
        for (int i = 0; i < 10; ++i)
            engine.resize(engine.render_width(), engine.render_height());
        if (scene.frame_generation() != generation)
            throw std::runtime_error("Unchanged framebuffer size invalidated the idle scene");
        std::this_thread::sleep_for(std::chrono::milliseconds(260));
        if (Engine::idle_event_wait_seconds() <= 0.0)
            throw std::runtime_error("Settled windows did not permit an idle event wait");
        scene.mark_dirty(shape);
        if (Engine::idle_event_wait_seconds() != 0.0)
            throw std::runtime_error("A changed scene was incorrectly treated as idle");
        engine.draw();
        if (Engine::idle_event_wait_seconds() != 0.0)
            throw std::runtime_error("Recent animation did not preserve the active cadence");
        engine.resize(128, 128);
        engine.draw();
        engine.resize(0, 0);
        engine.draw();
        engine.resize(96, 96);
        engine.draw();
        if (!engine.ready())
            throw std::runtime_error("Metal resize failed");
        // Settings navigation/search lenses use the shared control material
        // with native AppKit material on macOS, including live tint.
        const auto lens = scene.create_shape({20, 20}, {72, 48}, style);
        scene.registry().get<ShapeComponent>(lens).cornerRadius = 19;
        scene.registry().emplace<UiLiquidControlSurface>(lens);
        auto optics = ui_liquid_glass_options();
        ui_update_liquid_controls(scene, true, optics);
        const auto* liquid = scene.registry().try_get<LiquidGlassComponent>(lens);
        if (!liquid || liquid->backend != LiquidGlassBackend::eMacOSNative || liquid->sceneBackdrop)
            throw std::runtime_error("macOS controls did not select native AppKit glass");
        const auto glassPlan = scene.build_render_plan(0.0, 0);
        if (std::any_of(glassPlan.blurs.begin(), glassPlan.blurs.end(), [lens](const auto& batch) {
            return batch.entity == lens;
        })) throw std::runtime_error("Native control recorded a shader optical pass");
        engine.draw();
        optics.liquidRefraction = 0.85f;
        optics.liquidBlur = 0.30f;
        optics.liquidRim = 0.25f;
        optics.liquidTint = {0.2f, 0.4f, 0.8f, 0.35f};
        ui_update_liquid_controls(scene, true, optics);
        engine.draw();
        if (scene.registry().get<SystemBackdropComponent>(lens).region.liquidTint.alpha != 0.35f)
            throw std::runtime_error("Live native control tint was not applied");
        ui_update_liquid_controls(scene, false, optics);
        engine.draw();
        if (scene.registry().any_of<LiquidGlassComponent>(lens) || !engine.ready())
            throw std::runtime_error("Liquid control material did not restore cleanly");
        host.close();
        terminate_glfw();
        std::filesystem::remove(path);
        std::cout << "Metal window, text, blur, media, MSAA 3D and resize checks passed\n";
    }
    catch (const std::exception &error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

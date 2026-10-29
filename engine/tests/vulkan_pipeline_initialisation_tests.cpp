#include <vibranceUI/glfw/panel_window.h>
#include <vibranceUI/core/logger.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void expect(bool condition, const char* message)
{
    if (!condition) throw std::runtime_error(message);
}

std::string log_text(Logger& logger)
{
    logger.flush_file_output();
    std::ifstream file(logger.output_path());
    return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
}

std::size_t occurrences(const std::string& text, const std::string& needle)
{
    std::size_t count = 0u;
    for (auto index = text.find(needle); index != text.npos; index = text.find(needle, index + needle.size())) ++count;
    return count;
}
}

int main(int argc, char** argv)
{
    if (argc < 2 || argc > 3) return 2;
    const auto directory = std::filesystem::absolute(argv[1]);
    std::filesystem::create_directories(directory);
    LoggerOutputOptions logging;
    logging.directory = directory / "logs";
    Logger::configure_output(logging);
    auto& logger = *Logger::fetch_logger();
    try
    {
        GlfwPanelWindowHostOptions options;
        options.width = options.height = 80;
        options.enableAudio = false;
        options.msaaSamples = 1u;
        options.transparentFramebuffer = false;
        options.focusOnShow = options.showInTaskbar = options.showInAltTab = false;
        options.configureEngine = [](EngineCreateInfo& info) {
            set_glfw_window_visible(static_cast<GLFWwindow*>(info.surfaceUserData), false);
        };
        options.makeTemplateOptions = [](Engine&) {
            GlfwPanelWindowTemplateOptions panel;
            panel.title.clear();
            panel.trafficLights.visible = false;
            panel.buildContent = [](GlfwPanelWindowTemplateContext&) {};
            return panel;
        };
        GlfwPanelWindowHost host(options);
        if (!host.open()) return 77;
        expect(host.engine() && host.engine()->ready(), "The 2D renderer must become ready before any model is requested");
        const auto draw = [&] {
            // The application normally supplies monitor-paced draw calls.
            // Avoid a test-only back-to-back call being skipped by cadence.
            Engine::begin_externally_paced_frame(60u);
            host.engine()->draw();
            Engine::end_externally_paced_frame();
        };
        draw();
        const auto startupLog = log_text(logger);
        expect(startupLog.find("pipeline creation: rasterise_small.") == startupLog.npos &&
            startupLog.find("pipeline creation: hosted_3d_composite.") == startupLog.npos &&
            startupLog.find("pipeline creation: model_3d.") == startupLog.npos,
            "A pure 2D window must not invoke the 3D shader compiler during startup");

        if (argc == 3)
        {
            // Directly authored model entities do not pass through the asset
            // loader. Their first draw must initialise the same pipelines.
            host.engine()->renderer2d_scene().create_model({ 40.0f, 40.0f }, { 60.0f, 60.0f });
            draw();
            expect(log_text(logger).find("pipeline creation: rasterise_small.") != std::string::npos,
                "A scene-authored model must initialise its pipelines before command recording");
        }

        // A real glTF upload exercises deferred creation and the retained
        // graphics/fallback pipelines, rather than only inspecting a flag.
        const std::array<float, 9> positions { -0.5f, -0.5f, 0.0f, 0.5f, -0.5f, 0.0f, 0.0f, 0.5f, 0.0f };
        const std::array<std::uint16_t, 3> indices { 0u, 1u, 2u };
        {
            std::ofstream buffer(directory / "triangle.bin", std::ios::binary);
            buffer.write(reinterpret_cast<const char*>(positions.data()), sizeof(positions));
            buffer.write(reinterpret_cast<const char*>(indices.data()), sizeof(indices));
            std::ofstream model(directory / "triangle.gltf");
            model << R"({"asset":{"version":"2.0"},"buffers":[{"uri":"triangle.bin","byteLength":42}],"bufferViews":[{"buffer":0,"byteOffset":0,"byteLength":36},{"buffer":0,"byteOffset":36,"byteLength":6}],"accessors":[{"bufferView":0,"componentType":5126,"count":3,"type":"VEC3","min":[-0.5,-0.5,0],"max":[0.5,0.5,0]},{"bufferView":1,"componentType":5123,"count":3,"type":"SCALAR"}],"meshes":[{"primitives":[{"attributes":{"POSITION":0},"indices":1}]}],"nodes":[{"mesh":0}],"scenes":[{"nodes":[0]}],"scene":0})";
        }
        const auto model = host.engine()->load_model_3d(directory / "triangle.gltf");
        expect(model.valid() && model.triangleCount == 1u, "The first model request must still upload valid geometry");
        Model3DComponent component;
        component.set_model(model);
        host.engine()->renderer2d_scene().create_model({ 40.0f, 40.0f }, { 60.0f, 60.0f }, component);
        for (unsigned frame = 0u; frame < 3u; ++frame) draw();
        const auto again = host.engine()->load_model_3d(directory / "triangle.gltf");
        expect(again.id == model.id, "Repeated model requests must retain the existing asset cache");
        const auto modelLog = log_text(logger);
        for (const auto* name : { "rasterise_small", "hosted_3d_composite", "model_3d" })
            expect(occurrences(modelLog, std::string("pipeline creation: ") + name + ".") == 1u,
                "Deferred 3D pipelines must compile once, including after repeated model loads");
        host.close();
        terminate_glfw();
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        terminate_glfw();
        return 1;
    }
}

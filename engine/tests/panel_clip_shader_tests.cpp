// Reuse the existing offscreen readback harness. These checks examine actual
// pixels, since a correct CPU clip rectangle alone cannot detect spare lanes.
#define VIBRANCE_SHADER_HARNESS_NO_MAIN
#include "blur_shader_performance.cpp"

int main(int argc, char** argv)
{
    try
    {
        if (argc != 4) throw std::runtime_error("Expected shape, shadow and blur SPIR-V paths");
        constexpr uint32_t side = 96;
        unsigned cases = 0;
        for (unsigned shader = 0; shader < 3; ++shader)
        {
            const auto code = load_spirv(argv[shader + 1]);
            Harness gpu(side, side, code, code);
            Case empty;
            const auto untouched = gpu.run(empty, false, 1, false, false, true);
            for (uint32_t width = 25; width <= 32; ++width)
            for (uint32_t height = 25; height <= 32; ++height)
            {
                Case draw;
                draw.originX = 13;
                draw.originY = 11;
                draw.dispatchWidth = width;
                draw.dispatchHeight = height;
                draw.constants.rect = { 0, 0, 90, 90 };
                draw.constants.color0 = { 1, 0, 0, 1 };
                draw.constants.color1 = draw.constants.color0;
                draw.constants.effect0 = { 0, shader == 1 ? 4.0f : 0.0f, 0.75f, 1 };
                if (shader == 2)
                {
                    draw.constants.effect0[2] = 0;
                    draw.constants.data[0] = 1;
                    draw.constants.data[1] = renderer2d_blur_dispatch_flags(compositeFlag, width, height);
                }
                else draw.constants.data[0] = renderer2d_primitive_dispatch_data(1, width, height);
                const auto rendered = gpu.run(draw, false, 1, false, false, true);
                unsigned changedInside = 0;
                for (uint32_t y = 0; y < side; ++y)
                for (uint32_t x = 0; x < side; ++x)
                {
                    const auto offset = (y * side + x) * 4u;
                    const bool changed = !std::equal(rendered.bytes.begin() + offset,
                        rendered.bytes.begin() + offset + 4, untouched.bytes.begin() + offset);
                    const bool inside = x >= draw.originX && y >= draw.originY &&
                        x < draw.originX + width && y < draw.originY + height;
                    if (changed && !inside)
                        throw std::runtime_error("GPU drawing escaped its panel clip: shader " + std::to_string(shader) +
                            ", extent " + std::to_string(width) + "x" + std::to_string(height) +
                            ", pixel " + std::to_string(x) + "," + std::to_string(y));
                    changedInside += changed;
                }
                if (!changedInside) throw std::runtime_error("The clipped GPU fixture did not draw any visible pixels");
                ++cases;
            }
        }
        std::cout << "GPU panel clipping: " << cases << " shape, shadow and blur cases passed\n";
        return validationErrors.load() ? 1 : 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}

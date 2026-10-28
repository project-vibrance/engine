#define VIBRANCE_SHADER_HARNESS_NO_MAIN
#include "blur_shader_performance.cpp"

int main(int argc, char** argv)
try
{
    if (argc != 3) return 2;
    const auto shader = load_spirv(argv[1]);
    unsigned checks = 0;
    for (const unsigned scale : {1u, 2u})
    {
        const unsigned width = 224u * scale, height = 128u * scale;
        Harness gpu(width, height, shader, shader);
        Case lens;
        lens.name = "static coloured rim";
        lens.constants.rect = {32.0f*scale,32.0f*scale,160.0f*scale,48.0f*scale};
        lens.constants.effect0 = {0.0f,14.0f*scale,1.0f,1.0f};
        lens.constants.effect1[1] = 0.75f;
        lens.constants.data[0] = 2u;
        lens.constants.data[1] = liquidFlag | (1u << 13u);
        lens.constants.data[2] = 127u << 14u;
        lens.dispatchWidth = width; lens.dispatchHeight = height;
        const auto pixel = [&](const Result& result, unsigned x, unsigned y) {
            const auto index = (y * width + x) * 4u;
            return std::array<unsigned,3>{result.bytes[index],result.bytes[index+1],result.bytes[index+2]};
        };
        for (const std::array<unsigned,3> colour : {
            std::array<unsigned,3>{255,0,0}, {0,255,0}, {0,0,255},
            {255,255,0}, {0,255,255}, {255,0,255}, {255,255,255}})
        {
            std::vector<uint8_t> input(width*height*4u, 0u);
            for (unsigned y=0; y<height; ++y)
                for (unsigned x=0; x<width; ++x)
                {
                    const auto i=(y*width+x)*4u;
                    input[i+3]=255u;
                    if ((y>=16u*scale && y<24u*scale) || (y>=88u*scale && y<100u*scale))
                        for (unsigned c=0;c<3;++c) input[i+c]=colour[c];
                }
            for (unsigned image=0;image<5;++image) gpu.set_input_pixels(image, input);
            const auto still = gpu.run(lens, true);
            for (const auto y : {32u*scale,80u*scale-1u})
            {
                const auto edge=pixel(still,112u*scale,y);
                for (unsigned c=0;c<3;++c)
                    if ((colour[c] && edge[c]<20u) || (!colour[c] && edge[c]>12u))
                        throw std::runtime_error("A top/bottom rim failed to reflect the nearby colour");
                ++checks;
            }
            const auto side=pixel(still,32u*scale,56u*scale);
            if (side[0]>12u || side[1]>12u || side[2]>12u)
                throw std::runtime_error("The vertical contour must remain a faint outer stroke");
            lens.constants.data[2] |= 255u << 21u;
            const auto later=gpu.run(lens,true);
            if (still.bytes != later.bytes)
                throw std::runtime_error("A static rim changed when only the animation clock advanced");
            lens.constants.data[2] &= ~(255u << 21u);
            lens.constants.data[1] &= ~(1u << 13u);
            const auto moving=gpu.run(lens,true);
            lens.constants.data[2] |= 128u << 21u;
            const auto moved=gpu.run(lens,true);
            if (moving.bytes == moved.bytes)
                throw std::runtime_error("Enabling rim animation did not animate its light");
            lens.constants.data[1] |= 1u << 13u;
            lens.constants.data[2] &= ~(255u << 21u);
        }
    }
    // Distant colour lies well outside the former 0.4-short-side halo. Sharp
    // red/blue source boundaries must become a mixed reflection at the rim.
    for (const bool splitColour : { false, true })
    {
        constexpr unsigned width = 256, height = 256;
        Harness gpu(width, height, shader, shader);
        Case lens;
        lens.constants.rect = {48.0f,96.0f,160.0f,48.0f};
        lens.constants.effect0 = {0.0f,14.0f,1.0f,1.0f};
        lens.constants.effect1[1] = 0.75f;
        lens.constants.data[0] = 2u;
        lens.constants.data[1] = liquidFlag | (1u << 13u);
        lens.constants.data[2] = 127u << 14u;
        lens.dispatchWidth = width; lens.dispatchHeight = height;
        std::vector<uint8_t> input(width*height*4u,0u);
        for (unsigned y=0;y<height;++y)
            for (unsigned x=0;x<width;++x)
            {
                const auto i=(y*width+x)*4u;
                input[i+3]=255u;
                if ((y>=24 && y<56) || (y>=190 && y<224))
                    input[i+(splitColour ? (x<128 ? 0 : 2) : 1)]=255u;
            }
        for (unsigned image=0;image<5;++image) gpu.set_input_pixels(image,input);
        const auto result=gpu.run(lens,true);
        for (const auto y : {96u,143u})
        {
            const auto i=(y*width+128u)*4u;
            if (splitColour ? (result.bytes[i]<15 || result.bytes[i+2]<15 || result.bytes[i+1]>12) :
                (result.bytes[i+1]<20 || result.bytes[i]>12 || result.bytes[i+2]>12))
                throw std::runtime_error(splitColour ? "Rim reflections must blur adjacent source colours" :
                    "Rim reflections must reach distant content outside the former halo");
            ++checks;
        }
    }
    const auto shapeShader = load_spirv(argv[2]);
    Harness shapeGpu(256,128,shapeShader,shapeShader);
    std::vector<uint8_t> black(256u*128u*4u,0u);
    for (unsigned image=0;image<5;++image) shapeGpu.set_input_pixels(image,black);
    // The UI hairline must not alter any pixel of the optical inner rim.
    for (const float scale : { 0.65f, 0.75f, 1.0f, 1.25f, 1.5f })
    for (const float fraction : { 0.0f, 0.25f, 0.5f })
    for (const float softness : { 0.5f, 1.25f, 2.0f })
    {
        Harness gpu(320,160,shapeShader,shapeShader);
        std::vector<uint8_t> background(320u*160u*4u,160u);
        for (unsigned i=3;i<background.size();i+=4) background[i]=255u;
        for (unsigned image=0;image<5;++image) gpu.set_input_pixels(image,background);
        Case glass;
        glass.constants.rect={32.0f+fraction,32.0f+fraction,160.0f*scale,48.0f*scale};
        glass.constants.color0=glass.constants.color1={0,0,0,0};
        glass.constants.effect0={24.0f*scale,0,softness,1};
        glass.constants.data={2u,(1u<<27u)|4u,0,0};
        glass.dispatchWidth=320;glass.dispatchHeight=160;
        const auto x=unsigned(glass.constants.rect[0]+glass.constants.rect[2]*0.5f);
        const auto y=unsigned(std::ceil(glass.constants.rect[1]));
        unsigned previousMass=256u;
        for (const float tone : { 0.0f, 0.5f, 1.0f })
        {
            glass.constants.color2[3]=tone;
            const auto result=gpu.run(glass,true,1u,false,false,true);
            const auto value=[&](unsigned row){return result.bytes[(row*320u+x)*4u];};
            unsigned darkMass=0;
            for(unsigned row=y-3;row<=y+2;++row) darkMass+=160u-value(row);
            const float t=std::clamp((tone-0.12f)/0.70f,0.0f,1.0f);
            const float expectedMass=160.0f*(0.20f-0.12f*t*t*(3.0f-2.0f*t));
            if (value(y+2)!=160u || value(y-3)!=160u || value(y-1)>=160u ||
                std::abs(float(darkMass)-expectedMass)>4.0f || darkMass>previousMass)
                throw std::runtime_error("Outside stroke scale="+std::to_string(scale)+" phase="+std::to_string(fraction)+
                    " tone="+std::to_string(tone)+" coverage="+std::to_string(darkMass));
            previousMass=darkMass;
            ++checks;
        }
    }
    // Exercise the shared bridge shader as well as the native D3D11 contour.
    // A low merge amount must expose a real gap, without fading the caps.
    {
        Case bridge;
        bridge.constants.rect = { 32, 32, 132, 64 };
        bridge.constants.color0 = bridge.constants.color1 = { 1, 1, 1, 1 };
        bridge.constants.effect0 = { 0, 0, 0.5f, 1 };
        bridge.constants.data = { 9u, 0u, 0u, 0u }; // Renderer2DPrimitive::eLiquidBridge
        bridge.dispatchWidth = 256;
        bridge.dispatchHeight = 128;
        unsigned previousCoverage = 0u;
        for (unsigned step = 0; step <= 40; ++step)
        {
            bridge.constants.uvRect[0] = float(step) / 40.0f;
            const auto result = shapeGpu.run(bridge, true, 1u, false, false, true);
            unsigned coverage = 0u;
            for (unsigned y = 32; y < 96; ++y)
                coverage += result.bytes[(y * 256u + 98u) * 4u + 3u];
            if (coverage < previousCoverage ||
                (step <= 4 && result.bytes[(64u * 256u + 98u) * 4u + 3u] != 0u))
                throw std::runtime_error("The liquid neck must pinch off continuously without a residual thread");
            for (const unsigned x : { 64u, 132u })
                if (result.bytes[(64u * 256u + x) * 4u + 3u] != 255u)
                    throw std::runtime_error("Separating a liquid neck must retain opaque, sharp caps");
            previousCoverage = coverage;
            ++checks;
        }
        if (previousCoverage == 0u)
            throw std::runtime_error("A complete liquid merge must still join the caps");
        unsigned previousNeck = 255u;
        for (unsigned distance = 0; distance <= 104; distance += 2)
        {
            bridge.constants.rect[2] = 64.0f + distance;
            bridge.constants.uvRect[0] = 1.0f;
            const auto result = shapeGpu.run(bridge, true, 1u, false, false, true);
            const auto alpha = [&](unsigned x, unsigned y) {
                return result.bytes[(y * 256u + x) * 4u + 3u];
            };
            const unsigned neck = alpha(64u + distance / 2u, 64u);
            if (neck > previousNeck || alpha(64u, 64u) != 255u ||
                alpha(64u + distance, 64u) != 255u ||
                alpha(32u, 32u) != 0u || alpha(95u + distance, 32u) != 0u)
                throw std::runtime_error("Separating surfaces must narrow their neck and keep both outer caps round and opaque");
            if (distance >= 94u && neck != 0u)
                throw std::runtime_error("Separated surfaces must not retain a stretched liquid thread");
            previousNeck = neck;
            if (const auto directory = std::getenv("VIBRANCE_MERGE_PREVIEW"))
            {
                std::ofstream frame(std::string(directory) + "/merge-" +
                    std::to_string(distance) + ".rgba", std::ios::binary);
                frame.write(reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size());
                if (!frame) throw std::runtime_error("Cannot write merge preview frame");
            }
            ++checks;
        }
    }
    for (const std::array<float,2> opacity : {std::array<float,2>{1,0},{0,0},{1,1}})
    {
        Case cap;
        cap.constants.rect={32,24,160,80};
        cap.constants.uvRect={0,0.5f,0,1};
        cap.constants.color0={0,0,0,opacity[0]};
        cap.constants.color1={0,0,0,opacity[1]};
        cap.constants.color2={1,1,1,0.12f};
        cap.constants.effect0={14,1,0.5f,1};
        cap.constants.effect1={1,4,0,0};
        cap.constants.data={2u,1u|2u|4u|(1u<<30u),0,0};
        cap.dispatchWidth=256; cap.dispatchHeight=128;
        const auto result=shapeGpu.run(cap,true,1,false,false,true);
        auto unoutlined=cap;
        unoutlined.constants.data[1]=1u|4u;
        unoutlined.constants.effect0[1]=0.0f;
        const auto reference=shapeGpu.run(unoutlined,true,1,false,false,true);
        for (unsigned edge=0;edge<2;++edge)
        {
            const unsigned y=edge ? 103u : 24u;
            const auto i=(y*256u+112u)*4u;
            if (opacity[edge]==1.0f ? result.bytes[i]>1u :
                (result.bytes[i]<15u || result.bytes[i+3]<15u))
                throw std::runtime_error("The sharp UI outline must respect the solid/glass cap boundary");
            if (opacity[edge]==1.0f && result.bytes[i+3]!=reference.bytes[i+3])
                throw std::runtime_error("A solid cap must retain the coverage of its unoutlined antialiased edge");
            ++checks;
        }
    }
    // The white content column must reach the same top/bottom pixel boundary
    // as the native sidebar, including fractional window positions and DPI.
    for (const float scale : { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f })
    {
        constexpr unsigned width = 384, height = 256;
        Harness gpu(width, height, shapeShader, shapeShader);
        std::vector<uint8_t> transparent(width * height * 4u, 0u);
        for (unsigned image = 0; image < 5; ++image) gpu.set_input_pixels(image, transparent);
        Case column;
        column.constants.rect = { 88.25f * scale, 20.25f * scale, 72.0f * scale, 88.0f * scale };
        column.constants.color0 = column.constants.color1 = { 1, 1, 1, 1 };
        column.constants.effect0 = { 0, 0, 0.5f, 1 };
        column.constants.effect1 = { 16.25f * scale, 20.25f * scale, 144.0f * scale, 88.0f * scale };
        const auto radius = static_cast<unsigned>(std::round(24.0f * scale));
        column.constants.data = { 1u, (1u << 15u) | (1u << 14u),
            radius | (6u << 8u) | (15u << 12u) | (6u << 16u), 0u };
        column.dispatchWidth = width;
        column.dispatchHeight = height;
        const auto result = gpu.run(column, true, 1u, false, false, true);
        const float top = column.constants.rect[1];
        const float bottom = top + column.constants.rect[3];
        for (const float boundary : { top, bottom })
            for (int y = static_cast<int>(std::floor(boundary)) - 1;
                y <= static_cast<int>(std::floor(boundary)) + 1; ++y)
            {
                unsigned covered = 0u;
                for (const float offset : { -0.125f, -0.375f, 0.375f, 0.125f })
                {
                    const float sample = y + 0.5f + offset;
                    if (sample >= top && sample <= bottom) ++covered;
                }
                const auto x = static_cast<unsigned>(112.0f * scale);
                const auto alpha = result.bytes[(y * width + x) * 4u + 3u];
                if (std::abs(static_cast<int>(alpha) - static_cast<int>(std::round(covered * 255.0f / 4.0f))) > 1)
                    throw std::runtime_error("The content column acquired an inward one-pixel inset at a native panel edge");
                ++checks;
            }
    }
    if (validationErrors.load() != 0u) return 1;
    std::cout << checks << " GPU top/bottom colour checks, static phase and animated phase checks passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return std::string_view(error.what()).starts_with("No Vulkan") ? 77 : 1;
}

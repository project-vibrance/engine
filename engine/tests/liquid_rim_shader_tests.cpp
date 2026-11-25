// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

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
            if (*std::max_element(side.begin(), side.end()) > 12u)
                throw std::runtime_error("The vertical contour must retain a soft dark rim");
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
    for (const unsigned diameter : { 14u, 28u, 80u })
    {
        const unsigned size = diameter + 64u;
        Harness gpu(size, size, shader, shader);
        Case lens;
        lens.constants.rect = { 32.0f, 32.0f, float(diameter), float(diameter) };
        lens.constants.effect0 = { 0.0f, float(diameter) * 0.5f, 1.0f, 1.0f };
        lens.constants.effect1[1] = 0.5f;
        lens.constants.data = { 3u, liquidFlag | (1u << 13u), 127u << 14u, 0u };
        lens.dispatchWidth = lens.dispatchHeight = size;
        for (const uint8_t tone : { 0u, 16u, 128u, 240u })
        {
            std::vector<uint8_t> input(size * size * 4u, tone);
            for (std::size_t index = 3u; index < input.size(); index += 4u) input[index] = 255u;
            for (unsigned image = 0; image < 5; ++image) gpu.set_input_pixels(image, input);
            const auto result = gpu.run(lens, true);
            const auto sample = [&](unsigned x, unsigned y) { return result.bytes[(y * size + x) * 4u]; };
            const unsigned centre = 32u + diameter / 2u;
            const unsigned top = sample(centre, 32u), side = sample(32u, centre);
            if (top <= side + 8u || (tone >= 128u && side >= tone))
                throw std::runtime_error("Circular glass rim diameter=" + std::to_string(diameter) +
                    " tone=" + std::to_string(tone) + " top=" + std::to_string(top) + " side=" + std::to_string(side));
            if (sample(centre, centre) != tone)
                throw std::runtime_error("The rim must leave the centre of a glass control unchanged");
            ++checks;
        }
    }
    {
        constexpr unsigned width = 224u, height = 128u;
        Harness gpu(width, height, shader, shader);
        Case header;
        header.constants.rect = { 32.25f, 20.5f, 160.0f, 50.0f };
        header.constants.effect0 = { 12.0f, 1.0f, 1.0f, 1.0f };
        header.constants.effect1 = { 12.0f, 0.5f, 0.0f, 0.0f };
        header.constants.data[0] = 2u;
        header.constants.data[1] = 1u << 25u;
        header.dispatchWidth = width;
        header.dispatchHeight = height;
        for (const uint8_t opacity : { 32u, 128u, 240u })
        {
            std::vector<uint8_t> input(width * height * 4u, 0u);
            for (unsigned y = 20u; y < 100u; ++y)
                for (unsigned x = 32u; x < 192u; ++x)
                {
                    const auto index = (y * width + x) * 4u;
                    input[index] = input[index + 1u] = input[index + 2u] = ((x / 4u) % 2u) ? 255u : 16u;
                    input[index + 3u] = opacity;
                }
            for (unsigned image = 0; image < 5; ++image) gpu.set_input_pixels(image, input);
            const auto result = gpu.run(header, true);
            for (std::size_t index = 3u; index < input.size(); index += 4u)
                if (result.bytes[index] != input[index])
                    throw std::runtime_error("A replacement header blur must preserve the fading panel silhouette");
            if (result.bytes[(40u * width + 80u) * 4u] == input[(40u * width + 80u) * 4u])
                throw std::runtime_error("A fading header must still blur its source content");
            ++checks;
        }
    }
    const auto shapeShader = load_spirv(argv[2]);
    {
        constexpr unsigned size = 96u;
        Harness gpu(size, size, shader, shader, nullptr, false, &shapeShader);
        for (const uint8_t backdrop : { 24u, 235u })
        for (const float diameter : { 11.0f, 14.0f, 28.0f })
        for (const std::array<float, 3> colour : {
            std::array<float, 3>{ 252.0f/255.0f, 111.0f/255.0f, 95.0f/255.0f },
            { 1.0f, 199.0f/255.0f, 63.0f/255.0f }, { 136.0f/255.0f, 212.0f/255.0f, 84.0f/255.0f } })
        {
            std::vector<uint8_t> input(size * size * 4u, backdrop);
            for (std::size_t index = 3u; index < input.size(); index += 4u) input[index] = 255u;
            for (unsigned image = 0; image < 5; ++image) gpu.set_input_pixels(image, input);
            Case paint;
            paint.constants.rect = { 32.0f, 32.0f, diameter, diameter };
            for (unsigned channel = 0; channel < 3u; ++channel)
            {
                paint.constants.color0[channel] = colour[channel] * 0.944f;
                paint.constants.color1[channel] = colour[channel] * 0.97f + 0.03f;
            }
            paint.constants.color0[3] = paint.constants.color1[3] = 0.98f;
            paint.constants.color2[3] = 0.4f;
            paint.constants.effect0 = { diameter * 0.5f, 0.0f, 0.5f, 1.0f };
            paint.constants.effect1 = { 0.0f, 0.05f, 0.0f, 0.95f };
            paint.constants.data = { 3u, 1u | 4u | (1u << 27u) | (1u << 19u), 0u, 0u };
            paint.constants.uvRect = { 0.0f, 0.05f, 0.0f, 0.95f };
            paint.dispatchWidth = paint.dispatchHeight = size;
            Case lens;
            lens.constants.rect = paint.constants.rect;
            lens.constants.effect0 = { 0.0f, diameter * 0.5f, 1.0f, 1.0f };
            lens.constants.effect1[1] = 0.5f;
            lens.constants.data = { 3u, liquidFlag | (1u << 13u) | (1u << 19u),
                89u | (44u << 7u) | (76u << 14u), 0u };
            lens.dispatchWidth = lens.dispatchHeight = size;
            const auto result = gpu.run(lens, true, 1u, false, false, false, false,
                nullptr, true, 0u, nullptr, nullptr, &paint);
            if (const auto directory = std::getenv("VIBRANCE_GLASS_SWATCH_PREVIEW"))
            {
                std::ofstream output(std::string(directory) + "/painted-" + std::to_string(int(diameter)) +
                    "-" + std::to_string(int(colour[1] * 255.0f)) + "-" + std::to_string(backdrop) + ".rgba", std::ios::binary);
                output.write(reinterpret_cast<const char*>(result.bytes.data()), size * size * 4u);
                if (!output) throw std::runtime_error("Cannot save the glass material preview");
            }
            const auto limits = std::minmax_element(colour.begin(), colour.end());
            const float minimumChroma = (*limits.second - *limits.first) * 255.0f * 0.70f;
            const float centre = 32.0f + diameter * 0.5f;
            const auto centreIndex = (unsigned(centre) * size + unsigned(centre)) * 4u;
            for (unsigned channel = 0u; channel < 3u; ++channel)
                if (std::abs(float(result.bytes[centreIndex + channel]) - colour[channel] * 255.0f) > 22.0f)
                    throw std::runtime_error("Traffic-light faces must retain their intended colour over light and dark panels");
            for (unsigned y = 32u; y < 32u + unsigned(diameter); ++y)
                for (unsigned x = 32u; x < 32u + unsigned(diameter); ++x)
                {
                    const float dx = x + 0.5f - centre, dy = y + 0.5f - centre;
                    if (std::hypot(dx, dy) > diameter * 0.5f - 0.9f) continue;
                    const auto index = (y * size + x) * 4u;
                    const auto low = std::min({ result.bytes[index], result.bytes[index + 1u], result.bytes[index + 2u] });
                    const auto high = std::max({ result.bytes[index], result.bytes[index + 1u], result.bytes[index + 2u] });
                    if (float(high - low) < minimumChroma)
                        throw std::runtime_error("Painted glass reflections must preserve colour without clipped white bands");
                }
            ++checks;
        }
    }
    {
        constexpr unsigned size = 96u;
        Harness gpu(size, size, shapeShader, shapeShader);
        for (const float diameter : { 14.0f, 28.0f })
        for (const uint8_t tone : { 32u, 240u })
        for (const float opacity : { 0.96f, 1.0f })
        for (const std::array<float, 3> colour : {
            std::array<float, 3>{ 1.0f, 0.35f, 0.38f }, { 1.0f, 0.74f, 0.18f }, { 0.16f, 0.78f, 0.25f } })
        {
            std::vector<uint8_t> input(size * size * 4u, tone);
            for (std::size_t index = 3u; index < input.size(); index += 4u) input[index] = 255u;
            for (unsigned image = 0; image < 5; ++image) gpu.set_input_pixels(image, input);
            Case control;
            control.constants.rect = { 32.0f, 32.0f, diameter, diameter };
            control.constants.color0 = control.constants.color1 = { colour[0], colour[1], colour[2], opacity };
            control.constants.color2[3] = 0.4f;
            control.constants.effect0 = { diameter * 0.5f, 0.0f, 0.5f, 1.0f };
            control.constants.data = { 3u, 4u | (1u << 27u) | (1u << 19u), 0u, 0u };
            control.dispatchWidth = control.dispatchHeight = size;
            const auto outlined = gpu.run(control, true, 1u, false, false, true);
            control.constants.data[1] &= ~(1u << 27u);
            const auto plain = gpu.run(control, true, 1u, false, false, true);
            const auto centre = 32u + unsigned(diameter) / 2u;
            const auto interior = (centre * size + centre) * 4u;
            if (plain.bytes != outlined.bytes)
                throw std::runtime_error("Painted glass must not acquire a separate outside stroke");
            if (!std::equal(plain.bytes.begin() + interior, plain.bytes.begin() + interior + 4u,
                    outlined.bytes.begin() + interior))
                throw std::runtime_error("The glass contour must preserve its coloured interior");
            ++checks;
        }
    }
    Harness shapeGpu(256,128,shapeShader,shapeShader);
    Harness strokeGpu(320,160,shapeShader,shapeShader);
    std::vector<uint8_t> black(256u*128u*4u,0u);
    for (unsigned image=0;image<5;++image) shapeGpu.set_input_pixels(image,black);
    {
        Case glass;
        glass.constants.rect = { 32, 24, 160, 80 };
        glass.constants.color0 = { 0.4f, 0.4f, 0.4f, 0.65f };
        glass.constants.effect0 = { 14, 0, 0.5f, 1 };
        glass.constants.data = { 2u, 4u, 0u, 0u };
        glass.dispatchWidth = 256;
        glass.dispatchHeight = 128;
        const auto render = [&] { return shapeGpu.run(glass, true, 1u, false, false, true); };
        const auto sample = [](const Result& result, unsigned x, unsigned y) {
            return static_cast<int>(result.bytes[(y * 256u + x) * 4u]);
        };
        const auto disabled = render();
        glass.constants.data[1] |= 1u << 13u;
        glass.constants.effect0[1] = -255.0f;
        const auto still = render();
        if (sample(still, 112, 24) <= sample(disabled, 112, 24) + 15 ||
            sample(still, 112, 103) <= sample(disabled, 112, 103) + 15 ||
            sample(still, 32, 64) >= sample(disabled, 32, 64) - 5 ||
            sample(still, 112, 64) != sample(disabled, 112, 64))
            throw std::runtime_error("The desktop rim must light the horizontal edges, shade the sides and leave the centre intact");
        glass.constants.effect0[1] = -static_cast<float>((1u << 8u) | 255u);
        const auto first = render();
        glass.constants.effect0[1] = -static_cast<float>((128u << 8u) | 255u);
        if (first.bytes == render().bytes)
            throw std::runtime_error("The desktop rim light must follow the animation phase");
        checks += 5;
    }
    for (const float scale : { 0.65f, 0.75f, 1.0f, 1.25f, 1.5f })
    for (const float fraction : { 0.0f, 0.25f, 0.5f })
    for (const float softness : { 0.5f, 1.25f, 2.0f })
    {
        auto& gpu = strokeGpu;
        std::vector<uint8_t> background(320u*160u*4u,160u);
        for (unsigned i=3;i<background.size();i+=4) background[i]=255u;
        for (unsigned image=0;image<5;++image) gpu.set_input_pixels(image,background);
        Case glass;
        glass.constants.rect={32.0f+fraction,32.0f+fraction,160.0f*scale,48.0f*scale};
        glass.constants.color0=glass.constants.color1={0,0,0,0};
        glass.constants.effect0={24.0f*scale,0,softness,1};
        glass.constants.data={2u,(1u<<27u)|4u,0,0};
        glass.dispatchWidth=320;glass.dispatchHeight=160;
        for (const float tone : { 0.0f, 0.5f, 1.0f })
        {
            glass.constants.color2[3]=tone;
            const auto result=gpu.run(glass,true,1u,false,false,true);
            if (!std::equal(background.begin(), background.end(), result.bytes.begin()))
                throw std::runtime_error("Clear glass must not paint a separate hairline at any scale or pixel alignment");
            ++checks;
        }
    }


    {
        Case bridge;
        bridge.constants.rect = { 32, 32, 132, 64 };
        bridge.constants.color0 = bridge.constants.color1 = { 1, 1, 1, 1 };
        bridge.constants.effect0 = { 0, 0, 0.5f, 1 };
        bridge.constants.data = { 9u, 0u, 0u, 0u };
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
        if (result.bytes != reference.bytes)
            throw std::runtime_error("Glass paint must retain its fill and coverage without an authored outline");
        ++checks;
    }


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
    std::cout << checks << " GPU reflection, directional rim, header coverage and animation checks passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return std::string_view(error.what()).starts_with("No Vulkan") ? 77 : 1;
}

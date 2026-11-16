// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#define VIBRANCE_SHADER_HARNESS_NO_MAIN
#include "blur_shader_performance.cpp"
#include <vibranceUI/graphics/liquid_glass_profile.h>
#include <filesystem>

namespace
{
std::vector<uint8_t> background(unsigned width, unsigned height, bool detailed)
{
    std::vector<uint8_t> pixels(width * height * 4u, 255u);
    for (unsigned y = 0; y < height; ++y)
        for (unsigned x = 0; x < width; ++x)
        {
            const float u = float(x) / width, v = float(y) / height;
            std::array<float, 3> colour { 0.40f + v * 0.18f, 0.57f + v * 0.12f, 0.66f + v * 0.06f };
            if (std::hypot(u - 0.48f, v - 0.16f) < 0.15f) colour = { 0.98f, 0.86f, 0.65f };
            if (v > 0.20f + 0.10f * std::sin(u * 6.0f + 0.3f)) colour = { 0.14f, 0.27f, 0.31f };
            if (v > 0.40f - u * 0.13f) colour = { 0.18f, 0.31f, 0.36f };
            if (v > 0.53f + u * 0.13f) colour = { 0.23f, 0.37f, 0.41f };
            if (v > 0.73f - u * 0.06f) colour = { 0.26f, 0.41f, 0.45f };
            if (detailed)
            {
                colour = v < 0.58f ? std::array<float, 3>{ 0.35f, 0.44f, 0.21f } :
                    std::array<float, 3>{ 0.53f, 0.48f, 0.39f };
                if (v < 0.50f && u < 0.55f)
                    colour = ((x / 24u) % 2u) ? std::array<float, 3>{ 0.39f, 0.28f, 0.21f } :
                        std::array<float, 3>{ 0.54f, 0.40f, 0.31f };
                if (std::pow((u - 0.53f) / 0.39f, 2.0f) + std::pow((v - 0.55f) / 0.22f, 2.0f) < 1.0f)
                    colour = { 0.77f, 0.19f + 0.10f * u, 0.10f };
                for (const float wheel : { 0.33f, 0.73f })
                {
                    const float radius = std::hypot((u - wheel) * 1.5f, v - 0.65f);
                    if (radius < 0.13f) colour = { 0.06f, 0.06f, 0.05f };
                    if (radius < 0.085f) colour = { 0.77f, 0.76f, 0.58f };
                    if (radius < 0.018f) colour = { 0.55f, 0.08f, 0.17f };
                }
                const float texture = ((x / 3u + y / 3u) % 2u) ? 0.055f : -0.055f;
                for (auto& channel : colour) channel = std::clamp(channel + texture, 0.0f, 1.0f);
            }
            const auto index = (y * width + x) * 4u;
            for (unsigned channel = 0; channel < 3u; ++channel)
                pixels[index + channel] = uint8_t(std::round(colour[channel] * 255.0f));
        }
    return pixels;
}
}

int main(int argc, char** argv)
try
{
    if (argc == 15 && std::string_view(argv[1]) == "--reference")
    {
        const unsigned width = std::stoul(argv[6]), height = std::stoul(argv[7]);
        const auto shader = load_spirv(argv[2]), shape = load_spirv(argv[3]);
        Harness gpu(width, height, shader, shader, nullptr, false, &shape);
        std::ifstream input(argv[4], std::ios::binary);
        std::vector<uint8_t> pixels(std::istreambuf_iterator<char>(input), {});
        if (pixels.size() != size_t(width) * height * 4u) throw std::runtime_error("Invalid reference backdrop");
        for (unsigned image = 0u; image < 5u; ++image) gpu.set_input_pixels(image, pixels);
        Case lens;
        lens.constants.rect = { std::stof(argv[8]), std::stof(argv[9]), std::stof(argv[10]), std::stof(argv[11]) };
        lens.constants.effect0 = { vibrance::glass::blur_radius(std::stof(argv[12])),
            lens.constants.rect[3] * 0.5f, 1.0f, 1.0f };
        lens.constants.effect1[1] = 0.5f;
        lens.constants.data = { 2u, liquidFlag | (1u << 13u),
            uint32_t(std::round(std::stof(argv[13]) * 127.0f)) |
                (20u << 7u) | (uint32_t(std::round(std::stof(argv[14]) * 127.0f)) << 14u), 0u };
        lens.dispatchWidth = width;
        lens.dispatchHeight = height;
        const auto result = gpu.run(lens, true, 1u, false, false, false, false, nullptr, true, 0u);
        std::ofstream output(argv[5], std::ios::binary);
        output.write(reinterpret_cast<const char*>(result.bytes.data()), result.bytes.size());
        return output && validationErrors.load() == 0u ? 0 : 1;
    }
    if (argc < 3 || argc > 4) return 2;
    constexpr unsigned width = 768u, height = 512u;
    const auto shader = load_spirv(argv[1]), shape = load_spirv(argv[2]);
    Harness gpu(width, height, shader, shader, nullptr, false, &shape);
    unsigned checks = 0u;
    {
        std::vector<uint8_t> pixels(width * height * 4u, 0u);
        for (unsigned y = 43u; y < 55u; ++y)
            for (unsigned x = 80u; x < 170u; ++x)
                for (unsigned channel = 0u; channel < 4u; ++channel)
                    pixels[(y * width + x) * 4u + channel] = 255u;
        for (unsigned image = 0u; image < 5u; ++image) gpu.set_input_pixels(image, pixels);
        Case header;
        header.constants.rect = { 40.75f, 40.75f, 280.0f, 50.0f };
        header.constants.effect0 = { 18.0f, 1.0f, 1.0f, 1.0f };
        header.constants.effect1 = { 40.75f, 40.75f, 280.0f, 160.0f };
        header.constants.color2 = { 6.0f, 18.0f, 1.0f, 3.2f };
        header.constants.data = { 1u, (1u << 14u) | (1u << 21u) | (1u << 23u), 0u, 0u };
        header.dispatchWidth = width;
        header.dispatchHeight = height;
        const auto result = gpu.run(header, true);
        if (result.bytes[(40u * width + 100u) * 4u + 3u] != 0u)
            throw std::runtime_error("Scrolling header blur must not grow alpha above its fractional parent boundary");
        if (result.bytes[(48u * width + 100u) * 4u + 3u] == 0u)
            throw std::runtime_error("Clipping the header must retain its visible interior blur");
        ++checks;
    }
    {
        std::vector<uint8_t> pixels(width * height * 4u, 242u);
        for (std::size_t index = 3u; index < pixels.size(); index += 4u) pixels[index] = 255u;
        for (unsigned image = 0u; image < 5u; ++image) gpu.set_input_pixels(image, pixels);
        Case track;
        track.constants.rect = { 312.0f, 224.0f, 52.0f, 24.0f };
        track.constants.color0 = track.constants.color1 = { 0.0f, 122.0f / 255.0f, 1.0f, 1.0f };
        track.constants.effect0 = { 12.0f, 0.0f, 0.5f, 1.0f };
        track.constants.data = { 2u, 4u, 0u, 0u };
        track.dispatchWidth = width;
        track.dispatchHeight = height;
        Case thumb;
        thumb.constants.rect = { 320.0f, 220.0f, 48.0f, 32.0f };
        thumb.constants.effect0 = { 0.0f, 16.0f, 1.0f, 1.0f };
        thumb.constants.data = { 2u, liquidFlag | (1u << 13u) | (1u << 28u),
            18u | (44u << 7u) | (76u << 14u), 0u };
        thumb.dispatchWidth = width;
        thumb.dispatchHeight = height;
        const auto result = gpu.run(thumb, true, 1u, false, false, false, false, nullptr, true, 0u,
            nullptr, nullptr, &track);
        const auto upper = (221u * width + 344u) * 4u;
        const auto centre = (236u * width + 344u) * 4u;
        if (result.bytes[upper] < 210u || result.bytes[upper + 1u] < 210u ||
            result.bytes[centre] > 1u || std::abs(int(result.bytes[centre + 1u]) - 122) > 1)
            throw std::runtime_error("An expanded clear thumb must keep a clear collar around its coloured track");
        ++checks;
    }
    for (const float scale : { 1.0f, 2.0f })
    for (const auto colour : { std::array<uint8_t, 3>{ 0, 0, 0 }, { 32, 32, 32 },
        { 128, 128, 128 }, { 224, 224, 224 }, { 255, 255, 255 }, { 24, 122, 255 }, { 204, 92, 48 } })
    {
        std::vector<uint8_t> pixels(width * height * 4u, 255u);
        for (std::size_t index = 0u; index < pixels.size(); index += 4u)
            std::copy(colour.begin(), colour.end(), pixels.begin() + index);
        for (unsigned image = 0u; image < 5u; ++image) gpu.set_input_pixels(image, pixels);
        Case thumb;
        thumb.constants.rect = { 320.0f, 224.0f, 40.0f * scale, 24.0f * scale };
        thumb.constants.effect0 = { 0.0f, 12.0f * scale, 1.0f, 1.0f };
        thumb.constants.data = { 2u, liquidFlag | (1u << 13u) | (1u << 28u),
            89u | (44u << 7u) | (76u << 14u), 0u };
        thumb.dispatchWidth = width;
        thumb.dispatchHeight = height;
        const auto result = gpu.run(thumb, true, 1u, false, false, false, false, nullptr, true, 0u);
        const auto centre = (unsigned(224.0f + 12.0f * scale) * width + unsigned(320.0f + 20.0f * scale)) * 4u;
        for (unsigned channel = 0u; channel < 3u; ++channel)
            if (std::abs(int(result.bytes[centre + channel]) - int(colour[channel])) > 1)
                throw std::runtime_error("A clear slider thumb must preserve backdrop brightness and RGB without material toning");
        ++checks;
    }
    {
        std::vector<uint8_t> pixels(width * height * 4u, 255u);
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x)
                for (unsigned channel = 0u; channel < 3u; ++channel)
                    pixels[(y * width + x) * 4u + channel] = ((x + y) & 1u) ? 224u : 32u;
        for (unsigned image = 0u; image < 5u; ++image) gpu.set_input_pixels(image, pixels);
        Case island;
        island.constants.rect = { 160.0f, 160.0f, 400.0f, 200.0f };
        island.constants.effect0 = { 0.0f, 48.0f, 1.0f, 1.0f };
        island.constants.data = { 2u, liquidFlag | (1u << 13u) | (1u << 28u), 89u, 0u };
        island.dispatchWidth = width;
        island.dispatchHeight = height;
        const auto clear = gpu.run(island, true, 1u, false, false, false, false, nullptr, true, 0u);
        for (unsigned y = 216u; y < 304u; ++y)
            for (unsigned x = 216u; x < 504u; ++x)
                for (unsigned channel = 0u; channel < 3u; ++channel)
                {
                    const auto index = (y * width + x) * 4u + channel;
                    if (std::abs(int(clear.bytes[index]) - int(pixels[index])) > 1)
                        throw std::runtime_error("Zero blur must preserve fine backdrop detail with refraction enabled");
                }
        unsigned bentPixels = 0u;
        for (unsigned y = 216u; y < 304u; ++y)
            for (unsigned x = 168u; x < 192u; ++x)
            {
                const auto index = (y * width + x) * 4u;
                bentPixels += std::abs(int(clear.bytes[index]) - int(pixels[index])) > 8;
            }
        if (bentPixels < 100u)
            throw std::runtime_error("Zero blur must retain edge refraction");
        island.constants.effect0[0] = 7.0f;
        const auto blurred = gpu.run(island, true, 1u, false, false, false, false, nullptr, true, 0u);
        const auto centre = (260u * width + 360u) * 4u;
        if (std::abs(int(blurred.bytes[centre]) - int(pixels[centre])) < 40)
            throw std::runtime_error("Increasing blur from zero must still soften fine backdrop detail");
        ++checks;
    }
    for (const float blur : { 0.0f, 7.0f, 30.0f })
    for (const auto colour : { std::array<uint8_t, 3>{ 0, 0, 0 }, { 128, 128, 128 }, { 24, 122, 255 } })
    {
        std::vector<uint8_t> pixels(width * height * 4u, 255u);
        for (std::size_t index = 0u; index < pixels.size(); index += 4u)
            std::copy(colour.begin(), colour.end(), pixels.begin() + index);
        for (unsigned image = 0u; image < 5u; ++image) gpu.set_input_pixels(image, pixels);
        Case island;
        island.constants.rect = { 160.0f, 160.0f, 400.0f, 200.0f };
        island.constants.effect0 = { blur, 48.0f, 1.0f, 1.0f };
        island.constants.data = { 2u, liquidFlag | (1u << 13u) | (1u << 28u), 89u, 0u };
        island.dispatchWidth = width;
        island.dispatchHeight = height;
        const auto result = gpu.run(island, true, 1u, false, false, false, false, nullptr, true, 0u);
        for (unsigned channel = 0u; channel < 3u; ++channel)
            if (std::abs(int(result.bytes[(260u * width + 360u) * 4u + channel]) - int(colour[channel])) > 1)
                throw std::runtime_error("Untinted island glass must preserve backdrop RGB independently of blur and refraction");
        ++checks;
    }
    {
        std::vector<uint8_t> pixels(width * height * 4u, 255u);
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x)
                if ((x / 8u) % 2u == 0u)
                {
                    const auto index = (y * width + x) * 4u;
                    pixels[index] = 16u;
                    pixels[index + 1u] = 96u;
                }
        for (unsigned image = 0u; image < 5u; ++image) gpu.set_input_pixels(image, pixels);
        Case thumb;
        thumb.constants.rect = { 288.0f, 224.0f, 104.0f, 32.0f };
        thumb.constants.effect0 = { 0.0f, 16.0f, 1.0f, 1.0f };
        thumb.constants.data = { 2u, liquidFlag | (1u << 13u) | (1u << 28u),
            89u | (44u << 7u) | (76u << 14u), 0u };
        thumb.dispatchWidth = width;
        thumb.dispatchHeight = height;
        const auto result = gpu.run(thumb, true, 1u, false, false, false, false, nullptr, true, 0u);
        for (const unsigned x : { 324u, 332u, 340u, 348u, 356u })
            for (unsigned channel = 0u; channel < 3u; ++channel)
            {
                const auto index = (240u * width + x) * 4u + channel;
                if (std::abs(int(result.bytes[index]) - int(pixels[index])) > 1)
                    throw std::runtime_error("A clear thumb must retain bright track detail without local contrast darkening");
            }
        ++checks;
    }
    {
        const std::array<uint8_t, 9> greys { 0, 32, 64, 96, 128, 160, 192, 224, 255 };
        const std::array<uint8_t, 9> nativeLevels { 32, 62, 89, 113, 134, 152, 166, 177, 185 };
        std::vector<uint8_t> ramp(width * height * 4u, 255u);
        for (unsigned y = 0u; y < height; ++y)
            for (unsigned x = 0u; x < width; ++x)
                for (unsigned channel = 0u; channel < 3u; ++channel)
                    ramp[(y * width + x) * 4u + channel] = greys[std::min(x / 85u, 8u)];
        for (unsigned image = 0u; image < 5u; ++image) gpu.set_input_pixels(image, ramp);
        Case lens;
        lens.constants.rect = { 16.0f, 160.0f, 736.0f, 96.0f };
        lens.constants.effect0 = { 0.0f, 48.0f, 1.0f, 1.0f };
        lens.constants.data = { 2u, liquidFlag | (1u << 13u), 89u | (76u << 14u), 0u };
        lens.dispatchWidth = width;
        lens.dispatchHeight = height;
        const auto result = gpu.run(lens, true, 1u, false, false, false, false, nullptr, true, 0u);
        for (unsigned step = 0u; step < greys.size(); ++step)
        {
            const auto x = step * 85u + 42u;
            const auto index = (208u * width + x) * 4u;
            if (std::abs(int(result.bytes[index]) - int(nativeLevels[step])) > 3 ||
                result.bytes[index] != result.bytes[index + 1u] || result.bytes[index] != result.bytes[index + 2u])
                throw std::runtime_error("Glass face tone deviated from the native greyscale landmarks");
            ++checks;
        }
    }
    for (const float scale : { 1.0f, 2.0f })
    for (const uint8_t backdrop : { uint8_t(223), uint8_t(255) })
    {
        std::vector<uint8_t> input(width * height * 4u, backdrop);
        for (size_t index = 3u; index < input.size(); index += 4u) input[index] = 255u;
        for (unsigned image = 0u; image < 5u; ++image) gpu.set_input_pixels(image, input);
        Case paint;
        paint.constants.rect = { 48.0f, 48.0f, 240.0f * scale, 30.0f * scale };
        paint.constants.color0 = { 0.98f, 0.98f, 0.98f, 0.55f };
        paint.constants.color1 = { 0.97f, 0.97f, 0.97f, 0.48f };
        paint.constants.effect0 = { 15.0f * scale, 0.0f, 0.5f, 1.0f };
        paint.constants.effect1 = { 0.0f, 0.0f, 0.0f, 1.0f };
        paint.constants.uvRect = { 0.0f, 0.0f, 0.0f, 1.0f };
        paint.constants.data = { 2u, 1u | 4u, 0u, 0u };
        paint.dispatchWidth = width;
        paint.dispatchHeight = height;
        Case lens;
        lens.constants.rect = paint.constants.rect;
        lens.constants.effect0 = { 0.0f, 15.0f * scale, 1.0f, 1.0f };
        lens.constants.data = { 2u, liquidFlag | (1u << 13u) | (1u << 19u), 89u, 0u };
        lens.dispatchWidth = width;
        lens.dispatchHeight = height;
        const auto unlit = gpu.run(lens, true, 1u, false, false, false, false, nullptr, true, 0u,
            nullptr, nullptr, &paint);
        lens.constants.data[2] |= 76u << 14u;
        const auto lit = gpu.run(lens, true, 1u, false, false, false, false, nullptr, true, 0u,
            nullptr, nullptr, &paint);
        const unsigned centreX = unsigned(48.0f + 120.0f * scale);
        const unsigned centreY = unsigned(48.0f + 15.0f * scale);
        const auto centre = (centreY * width + centreX) * 4u;
        if (lit.bytes[centre] < 232u || std::abs(int(lit.bytes[centre]) - int(unlit.bytes[centre])) > 1)
            throw std::runtime_error("Light panel glass must retain a bright face without dark platter toning");
        unsigned topGain = 0u, bottomGain = 0u;
        for (unsigned depth = 0u; depth < 5u; ++depth)
        {
            const auto top = ((48u + depth) * width + centreX) * 4u;
            const auto bottom = ((unsigned(48.0f + 30.0f * scale) - 1u - depth) * width + centreX) * 4u;
            topGain = std::max(topGain, unsigned(std::max(int(lit.bytes[top]) - int(unlit.bytes[top]), 0)));
            bottomGain = std::max(bottomGain, unsigned(std::max(int(lit.bytes[bottom]) - int(unlit.bytes[bottom]), 0)));
        }
        if (topGain < 3u || bottomGain < 3u)
            throw std::runtime_error("Light panel rim gain scale=" + std::to_string(scale) +
                " backdrop=" + std::to_string(backdrop) + " upper=" + std::to_string(topGain) +
                " lower=" + std::to_string(bottomGain));
        ++checks;
    }


    Harness contactGpu(width, height, shape, shape);
    for (const float scale : { 1.0f, 2.0f })
    {
        std::vector<uint8_t> input(width * height * 4u, 64u);
        for (size_t index = 3u; index < input.size(); index += 4u) input[index] = 255u;
        for (unsigned image = 0u; image < 5u; ++image) contactGpu.set_input_pixels(image, input);
        Case contact;
        contact.constants.rect = { 40.0f, 40.0f, 280.0f * scale, 120.0f * scale };
        contact.constants.uvRect = { 0.5f, 0.5f, 60.0f * scale, 0.0f };
        contact.constants.color0 = { 1.0f, 1.0f, 1.0f, 0.16f };
        contact.constants.effect0 = { 24.0f * scale, 0.0f, 0.5f, 1.0f };
        contact.constants.effect1 = { 0.0f, 0.0f, 0.0f, 1.0f };
        contact.constants.data = { 2u, 4u | (1u << 25u), 0u, 0u };
        contact.dispatchWidth = width;
        contact.dispatchHeight = height;
        const auto result = contactGpu.run(contact, true, 1u, false, false, true);
        const unsigned cx = unsigned(40.0f + 140.0f * scale), cy = unsigned(40.0f + 60.0f * scale);
        const auto sample = [&](unsigned x, unsigned y) { return int(result.bytes[(y * width + x) * 4u]); };
        const unsigned near = unsigned(20.0f * scale), far = unsigned(45.0f * scale);
        if (std::abs(sample(cx + near, cy) - sample(cx, cy + near)) > 2 ||
            sample(cx, cy) <= sample(cx + near, cy) || sample(cx + near, cy) <= sample(cx + far, cy) ||
            sample(cx + far, cy) <= 64)
            throw std::runtime_error("Contact light must have a circular, smoothly fading feather at every DPI");
        contact.constants.uvRect[0] = 0.0f;
        contact.constants.uvRect[1] = 0.0f;
        const auto edge = contactGpu.run(contact, true, 1u, false, false, true);
        if (edge.bytes[(40u * width + 40u) * 4u] != 64u ||
            edge.bytes[(39u * width + 39u) * 4u] != 64u)
            throw std::runtime_error("Contact light must stay inside the lens's rounded outline");

        contact.constants.rect = { 40.0f, 40.0f, 240.0f * scale, 24.0f * scale };
        contact.constants.uvRect = { 0.5f, 0.5f, 24.0f * scale * 2.4f, 0.0f };
        contact.constants.effect0[0] = 12.0f * scale;
        const auto small = contactGpu.run(contact, true, 1u, false, false, true);
        const unsigned sx = unsigned(40.0f + 120.0f * scale), sy = unsigned(40.0f + 12.0f * scale);
        const auto gain = [&](unsigned distance) {
            return int(small.bytes[(sy * width + sx + unsigned(distance * scale)) * 4u]) - 64;
        };
        if (gain(0u) > 32 || gain(30u) < gain(0u) / 2 || gain(60u) < 2 ||
            std::abs(gain(0u) - gain(3u)) > 1)
            throw std::runtime_error("Small-control contact light must be broad and dim, without a bright core");
        ++checks;
    }
    for (const bool detailed : { false, true })
    {
        const auto input = background(width, height, detailed);
        auto gallery = input;
        for (unsigned variant = 0u; variant < 3u; ++variant)
        for (unsigned primitive = 0u; primitive < 3u; ++primitive)
        {
            for (unsigned image = 0; image < 5; ++image) gpu.set_input_pixels(image, input);
            Case lens;
            const float x = 24.0f + variant * 252.0f, y = 40.0f + primitive * 154.0f;
            lens.constants.rect = primitive == 0u ? std::array<float, 4>{ x + 42.0f, y, 144.0f, 144.0f } :
                std::array<float, 4>{ x, y + 28.0f, 228.0f, 96.0f };
            const auto radius = primitive == 2u ? 20.0f : 48.0f;
            lens.constants.effect0 = { variant == 1u ? vibrance::glass::blur_radius(0.45f) : 0.0f,
                radius, 1.0f, 1.0f };
            lens.constants.effect1[1] = 0.5f;
            lens.constants.data = { primitive == 0u ? 3u : 2u, liquidFlag | (1u << 13u),
                89u | (20u << 7u) | (76u << 14u), 0u };
            lens.dispatchWidth = width;
            lens.dispatchHeight = height;
            Case paint;
            paint.constants.rect = lens.constants.rect;
            paint.constants.effect0 = { radius, 0.0f, 0.5f, 1.0f };
            paint.constants.data = { lens.constants.data[0], 4u, 0u, 0u };
            paint.dispatchWidth = width;
            paint.dispatchHeight = height;
            paint.constants.color0 = paint.constants.color1 = variant == 2u ?
                std::array<float, 4>{ 0.0f, 0.40f, 0.90f, 0.94f } :
                std::array<float, 4>{ 1.0f, 1.0f, 1.0f, variant == 1u ? 0.24f : 0.09f };
            if (variant == 2u) lens.constants.data[1] |= 1u << 19u;
            const auto result = gpu.run(lens, true, 1u, false, false, false, false, nullptr, true, 0u,
                nullptr, nullptr, variant == 2u ? &paint : nullptr, variant == 2u ? nullptr : &paint);
            const auto& rect = lens.constants.rect;
            unsigned changed = 0u;
            for (unsigned py = 0; py < height; ++py)
                for (unsigned px = 0; px < width; ++px)
                {
                    const auto index = (py * width + px) * 4u;
                    const bool inside = px >= rect[0] - 2.0f && px <= rect[0] + rect[2] + 2.0f &&
                        py >= rect[1] - 2.0f && py <= rect[1] + rect[3] + 2.0f;
                    const bool different = !std::equal(input.begin() + index, input.begin() + index + 4u,
                        result.bytes.begin() + index);
                    if (!inside && different) throw std::runtime_error("Glass changed pixels outside its contour bounds");
                    if (inside)
                    {
                        changed += different;
                        std::copy_n(result.bytes.begin() + index, 4u, gallery.begin() + index);
                    }
                }
            if (changed < 1000u) throw std::runtime_error("A glass variant did not produce its optical surface");
            ++checks;
        }
        if (argc == 4)
        {
            std::filesystem::create_directories(argv[3]);
            std::ofstream output(std::filesystem::path(argv[3]) / (detailed ? "detailed.rgba" : "illustrated.rgba"),
                std::ios::binary);
            output.write(reinterpret_cast<const char*>(gallery.data()), gallery.size());
            if (!output) throw std::runtime_error("Cannot save glass comparison render");
        }
    }
    if (validationErrors.load() != 0u) return 1;
    std::cout << checks << " native tone and clear, frosted and tinted material checks passed\n";
    return 0;
}
catch (const std::exception& error)
{
    std::cerr << error.what() << '\n';
    return std::string_view(error.what()).starts_with("No Vulkan") ? 77 : 1;
}

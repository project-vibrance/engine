#include "../src/renderer/vulkan/media_pipeline_policy.h"

#include <array>
#include <iostream>
#include <stdexcept>

int main()
{
    using vibrance::vulkan::media_pipeline_shader_name;
    try
    {
        if (media_pipeline_shader_name(0x10deu) != "media_2d_nvidia")
            throw std::runtime_error("NVIDIA did not select its validated media loop variant");
        for (const auto vendor : std::array { 0x1002u, 0x8086u, 0u, 0xffffffffu })
            if (media_pipeline_shader_name(vendor) != "media_2d")
                throw std::runtime_error("An integrated or unmeasured vendor lost its default media shader");
        for (const auto vendor : std::array { 0x10deu, 0x1002u, 0x8086u, 0u })
            if (media_pipeline_shader_name(vendor, false) != "media_2d")
                throw std::runtime_error("An unavailable loop variant did not fall back to the default shader");
        std::cout << "Media pipeline vendor and fallback policy passed.\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}

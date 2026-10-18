#pragma once

#include <cstdint>
#include <string_view>

namespace vibrance::vulkan
{
// The loop variant reduces NVIDIA compilation and rendering work. AMD's
// measured faster default also remains the fallback for unmeasured vendors.
constexpr std::string_view media_pipeline_shader_name(
    std::uint32_t vendorId, bool loopVariantAvailable = true)
{
    return vendorId == 0x10deu && loopVariantAvailable ?
        "media_2d_nvidia" : "media_2d";
}
}

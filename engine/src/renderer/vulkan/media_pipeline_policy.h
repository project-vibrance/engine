// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstdint>
#include <string_view>

namespace vibrance::vulkan
{


constexpr std::string_view media_pipeline_shader_name(
    std::uint32_t vendorId, bool loopVariantAvailable = true)
{
    return vendorId == 0x10deu && loopVariantAvailable ?
        "media_2d_nvidia" : "media_2d";
}
}

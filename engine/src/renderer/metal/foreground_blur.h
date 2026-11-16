// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "device.h"
#include <vibranceUI/renderer/render_types.h>
#include <vibranceUI/renderer/dispatch_bounds.h>

namespace vibrance::metal
{


inline void render_foreground_blur(Device& device, Renderer2DPushConstants constants,
    uint32_t width, uint32_t height, MTL::Texture* source, MTL::Texture* destination,
    MTL::Texture* temporary, MTL::Texture* blurred)
{
    constants.data.y = renderer2d_blur_dispatch_flags(eRenderer2DStyleClear, width, height);
    device.dispatch("blur_2d_comp", &constants, sizeof(constants), width, height,
        {source, source, source, temporary, blurred});
    constants.data.y = renderer2d_blur_dispatch_flags(
        eRenderer2DStyleBlurHorizontal | eRenderer2DStyleBlurForegroundGroupSource, width, height);
    device.dispatch("blur_2d_comp", &constants, sizeof(constants), width, height,
        {source, source, source, temporary, blurred});
    constants.data.y = renderer2d_blur_dispatch_flags(
        eRenderer2DStyleBlurVertical | eRenderer2DStyleBlurForegroundGroupSource, width, height);
    device.dispatch("blur_2d_comp", &constants, sizeof(constants), width, height,
        {source, source, source, temporary, blurred});
    constants.data.y = renderer2d_blur_dispatch_flags(eRenderer2DStyleBlurComposite, width, height);
    device.dispatch("blur_2d_comp", &constants, sizeof(constants), width, height,
        {destination, source, source, temporary, blurred});
}
}

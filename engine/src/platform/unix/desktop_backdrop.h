// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <functional>
#include <memory>
#include <span>
#include <glm/vec2.hpp>
#include <vibranceUI/graphics/backdrop.h>

struct GLFWwindow;

namespace vibrance
{
    struct DesktopBackdropPaint
    {
        bool opaqueSurface = false;
    };

    class DesktopBackdrop
    {
    public:
        DesktopBackdrop(GLFWwindow* window, bool opaqueSurface);
        ~DesktopBackdrop();

        DesktopBackdropPaint paint() const;
        bool available() const;
        bool frame_ready();
        void update(std::span<const SystemBackdropRegion> regions, glm::uvec2 outputSize,
            const std::function<bool()>& waitForFrame);
        void presentation_failed();

    private:
        struct Impl;
        std::unique_ptr<Impl> impl;
    };
}

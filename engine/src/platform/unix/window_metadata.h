// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/glfw/window.h>

namespace vibrance::unix_window
{
    void configure(GLFWwindow* window, const GlfwWindowCreateInfo& options, bool useDesktopLayer);
    void set_presence(GLFWwindow* window, bool showInTaskbar, bool showInAltTab);
    void set_position(GLFWwindow* window, glm::ivec2 position);
    void set_above(GLFWwindow* window, bool above);
    bool fixed_position(GLFWwindow* window, int& x, int& y);
    bool begin_move(GLFWwindow* window);
    bool begin_resize(GLFWwindow* window, int horizontal, int vertical);
    void forget(GLFWwindow* window);
    void shutdown();
}

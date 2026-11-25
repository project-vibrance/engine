// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/export.h>

#include <functional>
#include <optional>
#include <glm/vec2.hpp>

struct GLFWwindow;

using GlfwPointerPositionQuery = std::function<std::optional<glm::vec2>()>;

VIBRANCE_GLFW_API void set_glfw_pointer_position_query(GLFWwindow* window, GlfwPointerPositionQuery query);

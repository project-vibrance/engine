// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vibranceUI/export.h>
#include <vibranceUI/renderer/renderer2d_components.h>
#include <functional>
#include <vector>

class Engine;

class VIBRANCE_ENGINE_API UiModalLifecycle
{
public:
    void show(Renderer2DScene& scene, entt::entity dim, entt::entity card, bool animate = true,
        const std::function<bool(entt::entity)>& allowBackgroundInput = {});
    bool begin_exit(Renderer2DScene& scene);
    bool exit_ready(Engine& engine);
    void restore_input(Renderer2DScene& scene);
    void reset();
    bool is_closing() const noexcept { return closing; }
    bool awaiting_presentation() const noexcept { return awaitingPresentation; }
private:
    entt::entity dim = entt::null;
    entt::entity card = entt::null;
    bool closing = false;
    bool awaitingPresentation = false;
    std::vector<std::function<void(entt::registry&)>> inputRestorations;
};

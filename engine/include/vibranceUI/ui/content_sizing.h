#pragma once

#include <vibranceUI/ui/builder.h>
#include <vibranceUI/renderer/content_size.h>

inline void ui_fit_content_size(
    UiBuilder& ui, entt::entity container, entt::entity content,
    glm::vec2 minimumLogicalSize, glm::vec4 logicalPadding,
    bool fitWidth = true, bool fitHeight = false)
{
    ContentSize2DComponent fit {};
    fit.source = content;
    fit.minimumSize = scaled_size(minimumLogicalSize.x, minimumLogicalSize.y, ui.scale());
    fit.padding = { scaled_scalar(logicalPadding.x, ui.scale()),
        scaled_scalar(logicalPadding.y, ui.scale()),
        scaled_scalar(logicalPadding.z, ui.scale()),
        scaled_scalar(logicalPadding.w, ui.scale()) };
    fit.fitWidth = fitWidth;
    fit.fitHeight = fitHeight;
    ui.registry().emplace_or_replace<ContentSize2DComponent>(container, fit);
    ui.scene().mark_dirty(container);
}

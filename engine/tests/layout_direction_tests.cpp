#include <vibranceUI/ui/controls.h>
#include <vibranceUI/ui/visuals.h>
#include <vibranceUI/notifications/card.h>
#include <vibranceUI/ui/animated_text_view.h>

#include <cmath>
#include <iostream>

namespace
{
    int failures = 0;
    void expect(bool condition, const char* message)
    {
        if (!condition) { std::cerr << message << '\n'; ++failures; }
    }
    bool close(float a, float b) { return std::abs(a - b) < 0.01f; }
}

int main()
{
    for (const auto locale : { "ar-SA", "AR_sa", "he_IL", "fa", "ur_PK", "ckb_IQ", "az-Arab-IR", "ar-u-nu-latn" })
        expect(ui_locale_layout_direction(locale) == UiLayoutDirection::eRightToLeft,
            "RTL languages and explicit Arabic script must resolve independently of region/case");
    for (const auto locale : { "", "en_GB", "ja_JP", "ar-Latn", "az-Cyrl", "en-Arabic", "en-US-u-nu-arab" })
        expect(ui_locale_layout_direction(locale) == UiLayoutDirection::eLeftToRight,
            "LTR languages and explicit LTR scripts must retain their direction");

    for (float dpi : { 1.0f, 1.5f, 2.0f })
    {
        Renderer2DScene scene;
        auto& registry = scene.registry();
        LayoutScale scale;
        scale.logicalSize = { 500.0f, 300.0f };
        scale.factor = scale.contentScale = glm::vec2(dpi);
        UiBuilder ui(scene, scale);
        const auto root = ui.root();
        const auto panel = ui.block("#FFFFFFFF");
        ui.attach_aligned(panel, root, UiAlignment::eTopLeft, { 20.0f * dpi, 10.0f * dpi }, { 400.0f * dpi, 240.0f * dpi });
        ui.set_padding(panel, 10.0f, 5.0f, 30.0f, 5.0f);
        ui.set_grid(panel, { GridTrack2D::pixels(100.0f), GridTrack2D::fraction(1.0f) }, { GridTrack2D::fraction(1.0f) }, { 8.0f, 0.0f });
        const auto child = ui.block("#FF0000FF");
        ui.attach_grid_fill(child, panel, 0, 0, 1, 1, { 3.0f, 4.0f, 9.0f, 7.0f });
        const auto label = ui.create_aligned_text("Arabic 123", Renderer2DFontAtlas {}, child,
            UiAlignment::eTopLeft, 12.0f, make_flat_text_style("#000000FF"), 4, 0, { 2.0f, 3.0f });
        scene.build_render_plan(0.0, 0);
        const auto panelLtr = ui_entity_framebuffer_rect(registry, panel);
        const auto childLtr = ui_entity_framebuffer_rect(registry, child);
        const auto labelLtr = ui_entity_framebuffer_rect(registry, label);
        const auto authored = registry.get<Layout2DComponent>(child);
        registry.ctx().insert_or_assign(UiLayoutDirectionComponent { UiLayoutDirection::eRightToLeft });
        scene.mark_dirty();
        scene.build_render_plan(0.0, 0);
        const auto panelRtl = ui_entity_framebuffer_rect(registry, panel);
        const auto childRtl = ui_entity_framebuffer_rect(registry, child);
        const auto labelRtl = ui_entity_framebuffer_rect(registry, label);
        const float width = 500.0f * dpi;
        expect(close(panelRtl.x, width - panelLtr.x - panelLtr.z) && close(panelRtl.z, panelLtr.z),
            "RTL must mirror a panel without scaling its dimensions");
        expect(close(childRtl.x, width - childLtr.x - childLtr.z) && close(childRtl.z, childLtr.z),
            "mixed grid tracks, asymmetric padding and cell margins must mirror together");
        expect(close(labelRtl.x, width - labelLtr.x - labelLtr.z) && registry.get<TextComponent>(label).text == "Arabic 123",
            "labels must change alignment while preserving their text and glyph order");
        expect(registry.get<Layout2DComponent>(child).margin == authored.margin,
            "layout resolution must leave authored values intact");
        expect(scene.entity_at({ childRtl.x + childRtl.z / 2, childRtl.y + childRtl.w / 2 }) == child,
            "pointer picking must follow the mirrored geometry");

        registry.emplace<UiLayoutDirectionComponent>(child, UiLayoutDirectionComponent { UiLayoutDirection::eLeftToRight });
        scene.mark_dirty();
        scene.build_render_plan(0.0, 0);
        const auto fixedLabel = ui_entity_framebuffer_rect(registry, label);
        expect(close(fixedLabel.x - childRtl.x, labelLtr.x - childLtr.x),
            "an LTR subtree must retain internal ruler/digit placement while its container moves");

        UiSliderOptions sliderOptions;
        sliderOptions.minValue = 0;
        sliderOptions.maxValue = 100;
        sliderOptions.value = 25;
        sliderOptions.smoothScrubbing = false;
        const auto slider = ui_create_slider(ui, root, UiAlignment::eTopLeft, { 20, 260 }, { 200, 8 }, 5, 0, sliderOptions);
        scene.build_render_plan(0, 0);
        const auto sliderRect = ui_entity_framebuffer_rect(registry, slider.root);
        ui_set_slider_value_from_point(scene, slider.root, { sliderRect.x, sliderRect.y });
        expect(close(registry.get<SliderInputComponent>(slider.root).value, 100),
            "RTL slider maximum must be at the left pointer endpoint");
        ui_set_slider_value_from_point(scene, slider.root, { sliderRect.x + sliderRect.z, sliderRect.y });
        expect(close(registry.get<SliderInputComponent>(slider.root).value, 0),
            "RTL slider minimum must be at the right pointer endpoint");

        const Renderer2DFontAtlas atlas;
        const auto search = ui_create_search_field(ui, atlas, root, UiAlignment::eTopLeft,
            { 20, 40 }, { 200, 30 }, 8, 0);
        auto& input = registry.get<TextInputComponent>(search.root);
        input.focused = true;
        input.value = "123";
        ui_update_text_input_visual(scene, atlas, search.root);
        scene.build_render_plan(0, 0);
        auto valueRect = ui_entity_framebuffer_rect(registry, search.label);
        // The unloaded atlas fixture uses the renderer's fallback text bounds.
        // Supply those measured bounds to exercise the caret independently.
        ui_update_text_input_caret_layout(scene, atlas, search.root, input, valueRect.z);
        scene.build_render_plan(0, 0);
        auto caretRect = ui_entity_framebuffer_rect(registry, search.caret);
        expect(std::abs(caretRect.x - valueRect.x - valueRect.z) <= caretRect.z,
            "Latin and numeric input in an RTL field must keep the append caret at the right of the run");
        input.value = "\xD9\x85\xD8\xB1\xD8\xAD\xD8\xA8\xD8\xA7";
        ui_update_text_input_visual(scene, atlas, search.root);
        scene.build_render_plan(0, 0);
        valueRect = ui_entity_framebuffer_rect(registry, search.label);
        ui_update_text_input_caret_layout(scene, atlas, search.root, input, valueRect.z);
        scene.build_render_plan(0, 0);
        caretRect = ui_entity_framebuffer_rect(registry, search.caret);
        expect(caretRect.x + caretRect.z <= valueRect.x,
            "Arabic input must keep the append caret at the left of the shaped run");

        UiAnimatedCharacterTextOptions digitsOptions;
        digitsOptions.characterCount = 5;
        digitsOptions.fontSize = 14;
        const auto digits = ui_create_animated_character_text(ui, Renderer2DFontAtlas {}, root, "14:28", digitsOptions);
        scene.build_render_plan(0, 0);
        float previousX = -1;
        for (auto character : digits.characters)
        {
            const auto rect = ui_entity_framebuffer_rect(registry, character);
            expect(rect.x > previousX, "RTL countdown digits must retain their original left-to-right order");
            previousX = rect.x;
        }

        Media2DHandle backIcon;
        backIcon.id = 10;
        backIcon.drawable = true;
        backIcon.pixelSize = { 16, 16 };
        auto forwardIcon = backIcon;
        forwardIcon.id = 11;
        UiNavClusterOptions navOptions;
        navOptions.backIcon = backIcon;
        navOptions.forwardIcon = forwardIcon;
        bool backPressed = false;
        navOptions.onBack = [&backPressed](const PointerInputEvent&) { backPressed = true; };
        const auto nav = ui_create_nav_cluster(ui, Renderer2DFontAtlas {}, root,
            UiAlignment::eTopLeft, { 0, 0 }, { 80, 30 }, 7, 0, navOptions);
        const auto artwork = ui.create_aligned_media(backIcon, root, UiAlignment::eBottomLeft, { 16, 16 }, 7, 0);
        const auto plan = scene.build_render_plan(0, 0);
        const auto backRect = ui_entity_framebuffer_rect(registry, nav.label);
        const auto forwardRect = ui_entity_framebuffer_rect(registry, nav.caret);
        expect(backRect.x > forwardRect.x, "RTL Back must appear to the right of Forward");
        registry.get<ButtonInputComponent>(nav.label).onClick({});
        expect(backPressed, "mirrored navigation must preserve the original action");
        bool arrowMirrored = false;
        bool artworkPreserved = false;
        for (const auto* batches : { &plan.media, &plan.cachedMedia })
            for (const auto& batch : *batches)
            {
                if (batch.entity == nav.leadingIcon) arrowMirrored = batch.uvRect.x > batch.uvRect.z;
                if (batch.entity == artwork) artworkPreserved = batch.uvRect.x < batch.uvRect.z;
            }
        expect(arrowMirrored && artworkPreserved,
            "directional arrows must mirror while artwork keeps its original orientation");
        registry.get<ShapeComponent>(panel).set_corner_radii(1, 2, 3, 4);
        expect(ui_layout_corner_radii(registry, panel, registry.get<ShapeComponent>(panel)) == glm::vec4(2, 1, 4, 3),
            "asymmetric window corners must follow the mirrored surface");
        const glm::vec4 sidebarRadii { 27.0f, 0.0f, 0.0f, 27.0f };
        const auto nativeRtl = ui_layout_corner_radii(registry, panel, sidebarRadii) * dpi;
        expect(nativeRtl == glm::vec4(0.0f, 27.0f, 27.0f, 0.0f) * dpi,
            "Explicit RTL sidebar glass must have square joining corners and round outer right corners");
        registry.ctx().insert_or_assign(UiLayoutDirectionComponent {});
        expect(ui_layout_corner_radii(registry, panel, sidebarRadii) == sidebarRadii,
            "Switching to LTR must restore explicit native left corners without accumulating mirrors");
        scene.mark_dirty();
        scene.build_render_plan(0.0, 0);
        const auto restored = ui_entity_framebuffer_rect(registry, panel);
        expect(close(restored.x, panelLtr.x), "switching back to LTR must restore the original geometry");
    }
    return failures ? 1 : 0;
}

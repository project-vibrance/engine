// Copyright 2026 59xa
// SPDX-License-Identifier: Apache-2.0

#include <vibranceUI/graphics/colour_palette.h>
#include <vibranceUI/graphics/svg_colour_template.h>
#include <vibranceUI/platform/image_services.h>
#include <vibranceUI/platform/native_dialog.h>
#include <vibranceUI/renderer/font_file.h>
#include <vibranceUI/ui/colour_palette_panel.h>
#include <vibranceUI/ui/control_appearance.h>
#include <vibranceUI/ui/modal_lifecycle.h>
#include <vibranceUI/ui/theme_bindings.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace
{
void expect(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

void bindings_and_materials()
{
    Renderer2DScene scene;
    auto& registry = scene.registry();
    const std::array previous { glm::vec4(.1f, .2f, .3f, 1.0f), glm::vec4(.8f, .7f, .6f, 1.0f) };
    const std::array next { glm::vec4(.4f, .3f, .2f, 1.0f), glm::vec4(.6f, .7f, .8f, 1.0f) };
    const std::array roles { 0, 1 };
    const ui_theme_colours::RoleOrder order { roles, roles, roles, roles };
    ShapeStyleComponent paint;
    paint.color0 = paint.color1 = glm::vec4(glm::vec3(previous[0]), .16f);
    paint.outlineColor = previous[1];
    const auto surface = scene.create_shape({}, { 100, 40 }, paint);
    registry.emplace<UiSurfaceMaterialPaint>(surface, paint);
    registry.get<UiSurfaceMaterialPaint>(surface).applied = ui_liquid_glass_options();
    const auto label = registry.create();
    auto& text = registry.emplace<TextStyleComponent>(label);
    text.color0 = text.color1 = previous[1];
    const auto icon = registry.create();
    auto& media = registry.emplace<Media2DComponent>(icon);
    media.tintAsMask = true;
    media.tint = media.tintEnd = previous[1];
    ui_recolour_scene(scene, previous, next, order);
    expect(registry.get<ShapeStyleComponent>(surface).color0 == glm::vec4(glm::vec3(next[0]), .16f),
        "Recolouring must preserve authored surface transparency");
    expect(!registry.get<UiSurfaceMaterialPaint>(surface).applied &&
        registry.get<UiSurfaceMaterialPaint>(surface).authored.color0 == glm::vec4(glm::vec3(next[0]), .16f),
        "Recolouring must invalidate material caches and retain the new authored paint");
    expect(text.color0 == next[1] && media.tint == next[1], "Text and masked icons must share retained role bindings");
    auto custom = next;
    custom[0] = glm::vec4(.9f, .2f, .8f, 1.0f);
    ui_recolour_scene(scene, next, custom, order, [surface](entt::entity entity) { return entity != surface; });
    expect(registry.get<ShapeStyleComponent>(surface).color0.r == next[0].r, "A caller's scene filter must exclude unrelated controls");
    ui_theme_colours::Binding invalid { 42, false };
    glm::vec4 colour = previous[0];
    const std::array invalidRoles { -1, 90, 0 };
    expect(ui_theme_colours::recolour(colour, invalid, previous, next, invalidRoles, true) && colour == next[0],
        "Invalid or retired role indices must be rebound without indexing outside a palette");
    LayoutScale scale;
    UiBuilder ui(scene, scale);
    const auto glass = ui_liquid_glass_options();
    ui_apply_surface_material(ui, surface, glass);
    auto off = glass;
    off.material = GlassMaterial::eOff;
    ui_apply_surface_material(ui, surface, off);
    expect(registry.get<ShapeStyleComponent>(surface).color0.a == 1.0f &&
        registry.get<UiSurfaceMaterialPaint>(surface).authored.color0.a == .16f,
        "Disabling glass must preserve the authored appearance for later reactivation");
}

void preview_material_backend()
{
    Renderer2DScene scene;
    auto& registry = scene.registry();
    const auto surface = scene.create_shape({}, { 100, 40 }, make_solid_style("#EEEEEEFF"));
    registry.emplace<UiLiquidControlSurface>(surface);
    auto glass = ui_liquid_glass_options();
    glass.liquidBackend = LiquidGlassBackend::eEngineRenderer;
    glass.sceneBackdrop = true;
    glass.liquidRimAnimated = false;
    const auto checkLocal = [&] {
        ui_update_liquid_controls(scene, true, glass);
        const auto& material = registry.get<LiquidGlassComponent>(surface);
        expect(material.backend == LiquidGlassBackend::eEngineRenderer && material.sceneBackdrop && !material.rimAnimated,
            "A local preview must honour its explicit renderer backend and static optics");
        expect(!registry.any_of<SystemBackdropComponent>(surface) && registry.get<ShapeStyleComponent>(surface).color0.a > 0.0f,
            "A local preview must retain its paint and avoid desktop backdrop surfaces");
    };
    checkLocal();
#if defined(__APPLE__)
    glass.liquidBackend = LiquidGlassBackend::eMacOSNative;
    ui_update_liquid_controls(scene, true, glass);
    expect(registry.get<LiquidGlassComponent>(surface).backend == LiquidGlassBackend::eMacOSNative &&
        registry.any_of<SystemBackdropComponent>(surface) && registry.get<ShapeStyleComponent>(surface).color0.a == 0.0f,
        "Native glass must continue delegating its material and paint to AppKit");
    glass.liquidBackend = LiquidGlassBackend::eEngineRenderer;
    checkLocal();
#endif
}

void modal_input()
{
    Renderer2DScene scene;
    auto& registry = scene.registry();
    ShapeStyleComponent paint;
    const auto background = scene.create_shape({}, { 300, 200 }, paint);
    const auto dim = scene.create_shape({}, { 300, 200 }, paint);
    const auto card = scene.create_shape({}, { 100, 80 }, paint);
    const auto action = scene.create_shape({}, { 40, 20 }, paint);
    registry.emplace<Parent2DComponent>(action).parent = card;
    registry.emplace<ButtonInputComponent>(background);
    registry.emplace<TextInputComponent>(background).focused = true;
    registry.emplace<SliderInputComponent>(background).dragging = true;
    registry.emplace<ButtonInputComponent>(action);
    registry.emplace<ButtonInputComponent>(dim);
    UiModalLifecycle modal;
    modal.show(scene, dim, card);
    expect(!registry.get<ButtonInputComponent>(background).enabled && !registry.get<TextInputComponent>(background).focused &&
        !registry.get<SliderInputComponent>(background).dragging && registry.get<ButtonInputComponent>(action).enabled,
        "A modal must block background gestures and focus while keeping its actions usable");
    expect(!modal.awaiting_presentation() && !modal.is_closing(), "A newly presented modal must begin in its open state");
    expect(modal.begin_exit(scene) && modal.is_closing() && !registry.get<ButtonInputComponent>(action).enabled,
        "Starting dismissal must disable repeated actions until the final frame has been presented");
    const auto transition = registry.get<DisplayTransition2DComponent>(card);
    expect(transition.toOpacity == 0.0f && !transition.removeWhenComplete && transition.toScale == glm::vec2(.88f),
        "Dismissal must retain its final transparent state until the owner removes the modal");
    modal.restore_input(scene);
    expect(registry.get<ButtonInputComponent>(background).enabled && registry.get<TextInputComponent>(background).focused &&
        registry.get<ButtonInputComponent>(action).enabled && !modal.is_closing(),
        "Completing a modal must restore the previous input and focus state");
    modal.show(scene, dim, card, false, [background](entt::entity entity) { return entity == background; });
    expect(registry.get<ButtonInputComponent>(background).enabled, "Callers must be able to preserve explicit modal escape controls");
    scene.destroy_entity(card);
    expect(!modal.begin_exit(scene), "A discarded modal scene must not dereference stale entity handles");
    modal.reset();
}

void svg_templates(const std::filesystem::path& root)
{
    const auto path = root / "template.svg";
    { std::ofstream out(path); out << "<svg><path fill=\"#00Ff00\"/><path stroke=\"#00ff00\"/><path fill=\"#ffffff\"/></svg>"; }
    vibrance::SvgColourTemplate artwork("#00FF00", "sample");
    std::string error;
    expect(artwork.load(path, error), "SVG templates must accept caller-defined colour markers");
    const auto rendered = artwork.render(0x123456u);
    expect(rendered && rendered->find("#00Ff00") == std::string::npos && rendered->find("#00ff00") == std::string::npos &&
        rendered->find("#ffffff") != std::string::npos, "Template recolouring must replace every marker and preserve other artwork");
    expect(artwork.filename(1u) != artwork.filename(2u), "Raster caches must distinguish different requested colours");
    const auto before = artwork.filename(1u);
    { std::ofstream out(path, std::ios::app); out << " "; }
    expect(artwork.load(path, error) && artwork.filename(1u) != before, "Changing source artwork must invalidate its cached variants");
    vibrance::SvgColourTemplate invalid("#xyz123");
    expect(!invalid.load(path, error) && !invalid.render(0u), "Malformed markers must fail without producing a corrupted preview");
    std::filesystem::remove(path);
}

void font_names(const std::filesystem::path& path)
{
    if (!std::filesystem::exists(path)) return;
    std::string error;
    expect(font_files::validate_font_file(path, error), "The real font fixture must pass both table and native validation");
    const auto names = font_files::font_face_names(path, error);
    expect(!names.empty() && !names[0].family.empty() && !names[0].style.empty() && !names[0].displayName.empty(),
        "Font inspection must expose the original family, style and display names");
    expect(font_files::font_display_name(path) == names[0].displayName, "Display names must come from font metadata");
}
}

int main(int argc, char** argv)
{
    const auto root = std::filesystem::temp_directory_path() /
        ("vibrance-engine-appearance-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directory(root);
    try
    {
        bindings_and_materials();
        preview_material_backend();
        modal_input();
        svg_templates(root);
        if (argc > 1) font_names(argv[1]);
        std::string error;
        expect(!native_open_directory(root / "missing", error) && !std::filesystem::exists(root / "missing"),
            "Opening a directory must not create it without the caller's explicit policy");
        const auto theme = ui_light_theme();
        const auto dropdown = ui_panel_dropdown_options(theme);
        const auto button = ui_panel_button_options(theme);
        expect(button.cornerRadius == dropdown.buttonCornerRadius && button.backgroundColor == dropdown.buttonBackgroundColor &&
            button.hoveredBackgroundColor == button.backgroundColor, "Panel buttons must retain the shared control appearance");
        {
            UiColourPalettePanel picker;
            picker.select_tab(99u);
            expect(picker.active_tab() == 4u && !picker.is_open(), "The generic picker must work without app resources or policy");
        }
        std::filesystem::remove(root);
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        std::filesystem::remove(root / "template.svg");
        std::filesystem::remove(root);
        return 1;
    }
}

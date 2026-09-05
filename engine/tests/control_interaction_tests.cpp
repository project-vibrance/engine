#include <vibranceUI/engine.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "control interaction test failed: " << message << '\n';
        }
        return condition;
    }
}

int main()
{
    bool passed = true;

    const std::filesystem::path themeDirectory =
        std::filesystem::temp_directory_path() /
        ("vibrance-theme-choices-" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code fileError;
    std::filesystem::create_directories(themeDirectory, fileError);
    {
        std::ofstream(themeDirectory / "light.json") << "{}";
        std::ofstream(themeDirectory / "dark.json") << "{}";
    }
    int selectedTheme = -1;
    const Localisation localisation;
    const std::vector<UiOptionChoice> themeChoices =
        ui_theme_choices_from_directory(
            localisation,
            themeDirectory,
            "theme",
            &selectedTheme);
    passed &= expect(
        themeChoices.size() == 2u &&
            themeChoices[0].code == "light" &&
            themeChoices[1].code == "dark",
        "the legacy light alias and light.json must produce one Light choice");
    passed &= expect(
        selectedTheme == 0,
        "the legacy theme preference should select canonical light");
    std::filesystem::remove_all(themeDirectory, fileError);

    Renderer2DScene scene;
    entt::registry& registry = scene.registry();
    ShapeStyleComponent visibleStyle = {};
    visibleStyle.set_color("#FFFFFFFF");
    ShapeStyleComponent transparentStyle = {};
    transparentStyle.set_color("#00000000");

    const entt::entity menu = scene.create_shape(
        { 0.0f, 0.0f },
        { 160.0f, 80.0f },
        visibleStyle,
        Renderer2DPrimitive::eRectangle);
    registry.get<Transform2DComponent>(menu).origin = { 0.0f, 0.0f };
    registry.emplace<ButtonInputComponent>(menu);

    bool rowClicked = false;
    const entt::entity row = scene.create_shape(
        { 8.0f, 8.0f },
        { 144.0f, 28.0f },
        transparentStyle,
        Renderer2DPrimitive::eRectangle);
    registry.get<Transform2DComponent>(row).origin = { 0.0f, 0.0f };
    registry.emplace<Parent2DComponent>(row, Parent2DComponent { menu });
    ButtonInputComponent rowInput = {};
    rowInput.onClick = [&rowClicked](const PointerInputEvent&) {
        rowClicked = true;
    };
    registry.emplace<ButtonInputComponent>(row, std::move(rowInput));
    registry.emplace<HitRegion2DComponent>(row);

    const Renderer2DFontAtlas fontAtlas;
    UiInputState inputState = {};
    ui_update_input_hover(scene, fontAtlas, inputState, true, { 4.0f, 4.0f });
    passed &= expect(
        inputState.hoveredButton == menu,
        "the menu surface should initially own its padding hover");
    ui_update_input_hover(scene, fontAtlas, inputState, true, { 120.0f, 20.0f });
    passed &= expect(
        inputState.hoveredButton == row &&
            registry.get<ButtonInputComponent>(row).hovered,
        "a nested row should take hover ownership from its menu parent");

    const UiPointerButtonInput press {
        true,
        { 120.0f, 20.0f },
        PointerButton::eLeft,
        0,
        UiInputAction::ePress,
        {}
    };
    UiPointerButtonInput release = press;
    release.action = UiInputAction::eRelease;
    ui_handle_pointer_button(scene, fontAtlas, inputState, press);
    ui_handle_pointer_button(scene, fontAtlas, inputState, release);
    passed &= expect(
        rowClicked,
        "transparent whitespace inside a row should trigger its click");

    LayoutScale scale = {};
    scale.factor = { 1.0f, 1.0f };
    scale.contentScale = scale.factor;
    scale.logicalSize = { 400.0f, 300.0f };
    UiBuilder ui(scene, scale);
    const entt::entity root = ui.root();
    UiButtonOptions transparentButton = {};
    transparentButton.backgroundColor = "#00000000";
    const UiControlHandle textButton = ui_create_text_button(
        ui,
        fontAtlas,
        "Submenu",
        { 260.0f, 52.0f },
        5,
        0u,
        std::move(transparentButton));
    ui.attach_aligned(
        textButton.root,
        root,
        UiAlignment::eTopLeft,
        { 0.0f, 100.0f },
        { 260.0f, 52.0f });
    passed &= expect(
        registry.all_of<HitRegion2DComponent>(textButton.root),
        "standard transparent buttons should own their full rectangle");

    UiDropdownOptions dropdownOptions = {};
    const UiControlHandle dropdown = ui_create_dropdown(
        ui,
        fontAtlas,
        root,
        UiAlignment::eTopLeft,
        { 0.0f, 170.0f },
        { 180.0f, 36.0f },
        { "First", "Second" },
        6,
        0u,
        std::move(dropdownOptions));
    const UiDropdownControlComponent& dropdownControl =
        registry.get<UiDropdownControlComponent>(dropdown.root);
    passed &= expect(
        dropdownControl.itemRows.size() == 2u &&
            registry.all_of<HitRegion2DComponent>(dropdownControl.itemRows[0]) &&
            registry.all_of<HitRegion2DComponent>(dropdownControl.itemRows[1]),
        "every transparent dropdown option should own its complete row");

    return passed ? 0 : 1;
}

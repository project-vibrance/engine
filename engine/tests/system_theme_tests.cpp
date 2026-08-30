#include <vibranceUI/ui/styles.h>

#include <iostream>
#include <string>
#include <string_view>

namespace
{
bool expect(bool condition, std::string_view message)
{
    if (!condition)
    {
        std::cerr << "system theme test failed: " << message << '\n';
    }
    return condition;
}

bool valid_colour(const std::string& value)
{
    return renderer2d_parse_color(value).has_value();
}
}

int main()
{
    bool passed = true;
    const UiSystemAccentPalette palette = ui_system_accent_palette();
    passed &= expect(valid_colour(palette.accent), "accent must be parseable");
    passed &= expect(valid_colour(palette.hovered), "hover tone must be parseable");
    passed &= expect(valid_colour(palette.pressed), "press tone must be parseable");
    passed &= expect(
        valid_colour(palette.gradientBottom),
        "gradient tone must be parseable");

    const UiSystemAccentPalette fullTone = ui_system_accent_palette(1.0f);
    passed &= expect(
        fullTone.gradientBottom == fullTone.accent,
        "a full lower tone must preserve the native accent");

    const UiSystemAccentPalette zeroTone = ui_system_accent_palette(-1.0f);
    passed &= expect(
        zeroTone.gradientBottom == "#000000FF",
        "lower tone input must clamp to zero");
    return passed ? 0 : 1;
}

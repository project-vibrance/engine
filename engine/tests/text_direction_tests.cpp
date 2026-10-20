#include "../src/renderer/common/text_direction.h"
#include <iostream>

int main()
{
    bool passed = true;
    const auto expect = [&](bool condition, const char* message) {
        if (!condition) { std::cerr << message << '\n'; passed = false; }
    };
    const auto dal = 0xFEA9u;
    const auto seen = 0xFEB1u;
    expect(renderer2d_text_is_right_to_left("14د"),
        "Leading digits must not override the Arabic unit's paragraph direction");
    expect(renderer2d_visual_codepoints("14د") == std::vector<std::uint32_t> { dal, '1', '4' },
        "An Arabic minute unit must appear left of 14 without reversing its digits");
    expect(renderer2d_visual_codepoints("2س") == std::vector<std::uint32_t> { seen, '2' },
        "An Arabic hour unit must appear left of its value");
    expect(renderer2d_visual_codepoints("1س 28د") ==
            std::vector<std::uint32_t> { dal, '2', '8', ' ', seen, '1' },
        "Arabic hour/minute groups must read right to left with each number intact");
    expect(renderer2d_visual_codepoints("١٤د") == std::vector<std::uint32_t> { dal, 0x0661u, 0x0664u } &&
            renderer2d_text_is_right_to_left("١٤د"),
        "Arabic-Indic digits must retain their order in an Arabic unit label");
    expect(renderer2d_visual_codepoints("۱۴د") == std::vector<std::uint32_t> { dal, 0x06F1u, 0x06F4u } &&
            renderer2d_text_is_right_to_left("۱۴د"),
        "Persian digits must retain their order in an Arabic unit label");
    for (const auto* text : { "14m", "1hr 28m", "1h 28min", "14:28", "1:02:34", "1.5", "123" })
    {
        expect(!renderer2d_text_is_right_to_left(text),
            "Latin labels and standalone numeric times must remain left to right");
        expect(renderer2d_visual_codepoints(text) == renderer2d_decode_utf8(text),
            "Latin and numeric runs must preserve every character's order");
    }
    expect(!renderer2d_text_is_right_to_left("14min د"),
        "The first Latin letter must still establish a left-to-right paragraph");
    const auto numberedArabic = renderer2d_visual_codepoints("123 العربية");
    expect(renderer2d_text_is_right_to_left("123 العربية") && numberedArabic.size() >= 3 &&
            std::vector<std::uint32_t>(numberedArabic.end() - 3, numberedArabic.end()) ==
                std::vector<std::uint32_t> { '1', '2', '3' },
        "A numbered Arabic label must retain its leading number as an intact visual run");
    return passed ? 0 : 1;
}

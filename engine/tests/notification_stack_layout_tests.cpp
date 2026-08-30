#include <vibranceUI/notifications/stack_layout.h>

#include <iostream>
#include <string_view>
#include <vector>

namespace
{
    bool expect(bool condition, std::string_view message)
    {
        if (!condition)
        {
            std::cerr << "notification stack layout test failed: "
                << message << '\n';
        }
        return condition;
    }
}

int main()
{
    const std::vector<NotificationStackItemLayout> items = {
        { { 376.0f, 104.0f }, 4u },
        { { 320.0f, 88.0f }, 1u }
    };
    NotificationStackLayoutOptions options = {};
    const std::vector<NotificationStackPlacement> placements =
        layout_notification_stack(items, options);

    bool passed = true;
    passed &= expect(
        placements.size() == items.size(),
        "every input should produce one placement");
    passed &= expect(
        placements[0].offset == glm::vec2(16.0f, 44.0f) &&
            placements[0].size == items[0].size &&
            placements[0].layerDepth == 14.0f,
        "the first card should align right and cap compact layers");
    passed &= expect(
        placements[1].offset == glm::vec2(72.0f, 170.0f) &&
            placements[1].layerDepth == 0.0f,
        "later cards should follow measured height, layers, and gap");

    options.topPadding = -1.0f;
    options.rightPadding = -1.0f;
    options.gap = -1.0f;
    options.layerOffset = -1.0f;
    const std::vector<NotificationStackPlacement> clamped =
        layout_notification_stack(items, options);
    passed &= expect(
        clamped[0].offset == glm::vec2(52.0f, 0.0f) &&
            clamped[0].layerDepth == 0.0f &&
            clamped[1].offset.y == 104.0f,
        "negative development options should clamp to safe geometry");

    return passed ? 0 : 1;
}

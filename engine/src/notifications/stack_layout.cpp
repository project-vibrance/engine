#include <vibranceUI/notifications/stack_layout.h>

#include <algorithm>

std::vector<NotificationStackPlacement> layout_notification_stack(
    std::span<const NotificationStackItemLayout> items,
    const NotificationStackLayoutOptions& options)
{
    std::vector<NotificationStackPlacement> placements;
    placements.reserve(items.size());

    const float topPadding = std::max(options.topPadding, 0.0f);
    const float rightPadding = std::max(options.rightPadding, 0.0f);
    const float gap = std::max(options.gap, 0.0f);
    const float layerOffset = std::max(options.layerOffset, 0.0f);
    float nextY = topPadding;

    for (const NotificationStackItemLayout& item : items)
    {
        NotificationStackPlacement placement = {};
        placement.size = {
            std::max(item.size.x, 0.0f),
            std::max(item.size.y, 0.0f)
        };
        placement.offset = {
            std::max(
                options.viewportWidth - rightPadding - placement.size.x,
                0.0f),
            nextY
        };
        const std::size_t backingItemCount =
            item.groupSize > 0u ? item.groupSize - 1u : 0u;
        placement.layerDepth = static_cast<float>(std::min(
            backingItemCount,
            options.maximumLayers)) * layerOffset;
        placements.push_back(placement);
        nextY += placement.size.y + placement.layerDepth + gap;
    }

    return placements;
}

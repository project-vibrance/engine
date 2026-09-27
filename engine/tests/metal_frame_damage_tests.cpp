#include "../src/renderer/metal/frame_damage.h"

#include <iostream>
#include <vector>

int main()
{
    using vibrance::metal::FrameDamage;
    FrameDamage damage;
    FrameDamage::Item shape{};
    shape.bounds = {10, 10, 20, 20};
    shape.batch.rect = {10, 10, 20, 20};
    FrameDamage::Item blur{};
    blur.bounds = {80, 10, 60, 50};
    blur.backdropEffect = true;
    blur.phase = 2;
    std::vector<FrameDamage::Item> items{shape, blur};

    const auto first = damage.update(items, 200, 100, false);
    const auto unchanged = damage.update(items, 200, 100, false);
    items[0].batch.rect.x += 1.0f;
    const auto changed = damage.update(items, 200, 100, false);
    const auto model = damage.update(items, 200, 100, true);
    items.pop_back();
    const auto removed = damage.update(items, 200, 100, false);

    FrameDamage blurRemoval;
    std::vector<FrameDamage::Item> blurItems{shape, blur};
    blurRemoval.update(blurItems, 200, 100, false);
    blurItems.pop_back();
    const auto oldBlur = blurRemoval.update(blurItems, 200, 100, false);

    if (first != glm::uvec4(0, 0, 200, 100) ||
        unchanged != glm::uvec4(0) ||
        changed != glm::uvec4(10, 0, 180, 100) ||
        model != glm::uvec4(0, 0, 200, 100) ||
        removed != glm::uvec4(0, 0, 200, 100) ||
        oldBlur != glm::uvec4(30, 0, 160, 100))
    {
        std::cerr << "Metal damage failed to preserve blur or model dependencies: first="
                  << first.x << ',' << first.y << ',' << first.z << ',' << first.w
                  << " unchanged=" << unchanged.x << ',' << unchanged.y << ',' << unchanged.z << ',' << unchanged.w
                  << " changed=" << changed.x << ',' << changed.y << ',' << changed.z << ',' << changed.w
                  << " model=" << model.x << ',' << model.y << ',' << model.z << ',' << model.w
                  << " removed=" << removed.x << ',' << removed.y << ',' << removed.z << ',' << removed.w
                  << " oldBlur=" << oldBlur.x << ',' << oldBlur.y << ',' << oldBlur.z << ',' << oldBlur.w << '\n';
        return 1;
    }
}

#include "../src/renderer/metal/frame_damage.h"

#include <iostream>
#include <vector>

int main()
{
    using vibrance::metal::FrameDamage;
    FrameDamage damage;
    FrameDamage::Item shape{};
    shape.bounds = {10, 10, 21, 20};
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

    FrameDamage unrelated;
    std::vector<FrameDamage::Item> separated {shape, blur};
    separated[1].bounds = {180, 80, 60, 50};
    unrelated.update(separated, 400, 240, false);
    separated[0].batch.color0.a = 0.5f;
    if (unrelated.update(separated, 400, 240, false) != separated[0].bounds)
    {
        std::cerr << "An unrelated moving control repainted a distant filtered panel\n";
        return 1;
    }

    // Filter dependencies can cross several source halos. Expansion must
    // continue after a later effect pulls an earlier one into the repaint.
    FrameDamage chained;
    auto fartherBlur = blur;
    fartherBlur.bounds = {190, 10, 20, 20};
    auto nearerBlur = blur;
    nearerBlur.bounds = {80, 10, 20, 20};
    std::vector<FrameDamage::Item> chain {shape, fartherBlur, nearerBlur};
    chained.update(chain, 320, 120, false);
    chain[0].batch.color0.a = 0.4f;
    if (chained.update(chain, 320, 120, false) != glm::uvec4(10, 0, 250, 80))
    {
        std::cerr << "Metal damage lost a transitive filter source dependency\n";
        return 1;
    }

    // A moving glyph changes a filtered foreground group's whole output,
    // including pixels outside the glyph and other unchanged group members.
    FrameDamage foregroundDamage;
    FrameDamage::Item group{};
    group.bounds = {60, 60, 100, 70};
    group.backdropEffect = true;
    group.phase = 100;
    std::vector<FrameDamage::Item> foregroundItems{shape, group};
    foregroundDamage.update(foregroundItems, 240, 200, false);
    foregroundItems[0].batch.rect.x += 1.0f;
    const auto movedGlyph = foregroundDamage.update(foregroundItems, 240, 200, false);
    foregroundItems[1].bounds = {80, 70, 60, 40};
    const auto contractedGroup = foregroundDamage.update(foregroundItems, 240, 200, false);
    foregroundItems.pop_back();
    const auto endedGroup = foregroundDamage.update(foregroundItems, 240, 200, false);
    FrameDamage capDamage;
    capDamage.update(std::span<const FrameDamage::Item>(&shape, 1), 200, 100, false);
    shape.batch.backdropCapColour = 0x203040u;
    if (capDamage.update(std::span<const FrameDamage::Item>(&shape, 1), 200, 100, false) != shape.bounds)
    {
        std::cerr << "Changed backdrop cap colour did not invalidate retained pixels\n";
        return 1;
    }
    if (movedGlyph != glm::uvec4(10, 10, 200, 170) ||
        contractedGroup != glm::uvec4(10, 10, 200, 170) ||
        endedGroup != glm::uvec4(30, 20, 160, 140))
    {
        std::cerr << "Foreground blur lost its source/output halo during motion or contraction\n";
        return 1;
    }

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

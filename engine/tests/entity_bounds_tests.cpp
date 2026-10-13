#include "../src/renderer/common/entity_bounds.h"

#include <iostream>

namespace
{
    using vibrance::renderer::EntityBounds;
    using vibrance::renderer::coalesce_entity_bounds;
    using vibrance::renderer::collect_entity_damage;
    using vibrance::renderer::union_entity_rect;

    bool expect(bool condition, const char* message)
    {
        if (!condition) std::cerr << "entity bounds test failed: " << message << '\n';
        return condition;
    }

    EntityBounds entry(std::uint32_t entity, glm::uvec4 bounds)
    {
        return { static_cast<entt::entity>(entity), bounds };
    }
}

int main()
{
    bool passed = true;
    std::vector<EntityBounds> batches {
        entry(4u, { 10u, 10u, 4u, 4u }),
        entry(2u, { 3u, 7u, 2u, 8u }),
        entry(4u, { 5u, 4u, 20u, 15u }),
        { entt::null, { 1u, 1u, 10u, 10u } },
        entry(3u, { 1u, 1u, 0u, 10u }) };
    const auto capacity = batches.capacity();
    coalesce_entity_bounds(batches);
    passed &= expect(batches.size() == 2u && batches[0] == entry(2u, { 3u, 7u, 2u, 8u }) &&
        batches[1] == entry(4u, { 5u, 4u, 20u, 15u }) && batches.capacity() == capacity,
        "unordered glyph/effect envelopes must coalesce by entity while retaining scratch capacity");

    const auto versioned = static_cast<entt::entity>(4u | (1u << 20u));
    batches.push_back({ versioned, { 200u, 200u, 5u, 5u } });
    coalesce_entity_bounds(batches);
    passed &= expect(batches.size() == 3u && batches.back().first == versioned,
        "different entity generations must keep independent bounds in deterministic order");

    std::vector<EntityBounds> previous {
        entry(3u, { 90u, 90u, 5u, 5u }),
        entry(1u, { 10u, 10u, 10u, 10u }),
        entry(2u, { 60u, 60u, 4u, 4u }) };
    std::vector<EntityBounds> current {
        entry(4u, { 100u, 20u, 8u, 8u }),
        entry(2u, { 60u, 60u, 4u, 4u }),
        entry(1u, { 30u, 10u, 10u, 10u }) };
    coalesce_entity_bounds(previous);
    coalesce_entity_bounds(current);
    glm::uvec4 damage { 0u };
    std::size_t envelopes = 0u;
    std::size_t damageQueries = 0u;
    collect_entity_damage(current, previous,
        [&](const EntityBounds& bounds) {
            damage = union_entity_rect(damage, bounds.second);
            ++envelopes;
        },
        [&](entt::entity) { ++damageQueries; return false; });
    passed &= expect(damage == glm::uvec4(10u, 10u, 98u, 85u) &&
        envelopes == 4u && damageQueries == 1u,
        "motion, insertion and deletion must damage old and new envelopes with one query for the unchanged entity");

    damage = glm::uvec4(0u);
    envelopes = 0u;
    damageQueries = 0u;
    collect_entity_damage(previous, previous,
        [&](const EntityBounds&) { ++envelopes; },
        [&](entt::entity) { ++damageQueries; return false; });
    passed &= expect(envelopes == 0u && damageQueries == previous.size(),
        "unchanged retained bounds must require only one linear damage query per entity");

    collect_entity_damage(previous, previous,
        [&](const EntityBounds& bounds) {
            damage = union_entity_rect(damage, bounds.second);
            ++envelopes;
        },
        [](entt::entity entity) { return entity == static_cast<entt::entity>(2u); });
    passed &= expect(damage == glm::uvec4(60u, 60u, 4u, 4u) && envelopes == 1u,
        "time-driven rim damage must remain local even when its envelope and scene generation are unchanged");

    const std::vector<EntityBounds> empty;
    envelopes = 0u;
    collect_entity_damage(empty, previous,
        [&](const EntityBounds&) { ++envelopes; }, [](entt::entity) { return false; });
    passed &= expect(envelopes == previous.size(), "removing every entity must clear all old envelopes");
    envelopes = 0u;
    collect_entity_damage(current, empty,
        [&](const EntityBounds&) { ++envelopes; }, [](entt::entity) { return false; });
    passed &= expect(envelopes == current.size(), "the first frame must populate all current envelopes");

    return passed ? 0 : 1;
}

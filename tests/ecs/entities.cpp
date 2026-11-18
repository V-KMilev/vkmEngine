// Entities and components as a game holds them: any struct a component, handles that go stale, and
// walks that find what they ask for.

#include <algorithm>
#include <map>
#include <vector>

#include "ecs/scene.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

struct Health {
    int hp = 100;
};

struct Poisoned {
    float perSecond = 1.0f;
};

struct Shield {};

} // namespace

TEST("any plain struct is a component, added, found and removed with no registration") {
    Scene scene;
    const EntityId hero = scene.createEntity();
    const EntityId foe  = scene.createEntity();
    scene.add(hero, Health{80});
    scene.add(foe, Health{20});
    scene.add(foe, Poisoned{3.0f});

    CHECK(scene.has<Health>(hero));
    CHECK(!scene.has<Poisoned>(hero));
    CHECK(scene.get<Health>(hero).hp == 80);
    CHECK(scene.tryGet<Poisoned>(foe)->perSecond == 3.0f);
    CHECK(scene.tryGet<Shield>(hero) == nullptr);
    CHECK(scene.count<Health>() == 2u);

    scene.get<Health>(foe).hp -= 5;
    CHECK(scene.get<Health>(foe).hp == 15);

    scene.remove<Health>(hero);
    CHECK(!scene.has<Health>(hero));
    CHECK(scene.tryGet<Health>(hero) == nullptr);
    CHECK(scene.count<Health>() == 1u);
    CHECK(scene.get<Health>(foe).hp == 15);
    scene.getOrAdd<Health>(hero).hp = 5;
    CHECK(scene.get<Health>(hero).hp == 5);
}

TEST("a destroyed entity's handle goes stale, and the next entity in its slot is not it") {
    Scene scene;
    const EntityId first = scene.createEntity();
    scene.add(first, Health{1});
    scene.add(first, Shield{});
    scene.destroyEntity(first);

    CHECK(!scene.isAlive(first));
    CHECK(scene.tryGet<Health>(first) == nullptr);
    CHECK(!scene.has<Shield>(first));
    CHECK(scene.entityCount() == 0u);

    const EntityId next = scene.createEntity();
    CHECK(next.slot() == first.slot());
    CHECK(next != first);
    CHECK(scene.isAlive(next));
    CHECK(!scene.isAlive(first));
    // It starts with nothing of the one before.
    CHECK(!scene.has<Health>(next));
    CHECK(!scene.has<Shield>(next));
    CHECK(scene.count<Health>() == 0u);

    // A handle that never named anything is no entity.
    CHECK(!EntityId{});
    CHECK(!scene.isAlive(EntityId{}));
}

TEST("forEach visits every entity that holds every type asked, once, and no other") {
    Scene scene;
    std::vector<EntityId> made;
    for (int i = 0; i < 40; ++i) {
        const EntityId id = scene.createEntity();
        made.push_back(id);
        if (i % 2 == 0) scene.add(id, Health{i});
        if (i % 3 == 0) scene.add(id, Poisoned{static_cast<float>(i)});
        if (i % 4 == 0) scene.add(id, Shield{});
    }
    // Removals reorder what a walk meets; they change nothing of what it finds.
    for (const int gone : {0, 6, 13, 22}) scene.destroyEntity(made[gone]);
    scene.remove<Health>(made[30]);

    const auto holding = [&](int every) {
        std::map<uint32_t, int> slots;
        for (int i = 0; i < 40; ++i) {
            const bool gone = i == 0 || i == 6 || i == 13 || i == 22 || i == 30;
            if (!gone && i % every == 0) slots[made[i].slot()] = 1;
        }
        return slots;
    };
    std::map<uint32_t, int> visits;
    scene.forEach<Health, Poisoned>([&](EntityId id, Health& health, Poisoned& poison) {
        ++visits[id.slot()];
        CHECK(health.hp == static_cast<int>(poison.perSecond));
    });
    CHECK(visits == holding(6));
    // Three asked: an entity must hold all three, not the first and any one of the rest.
    visits.clear();
    scene.forEach<Health, Poisoned, Shield>(
        [&](EntityId id, Health&, Poisoned&, Shield&) { ++visits[id.slot()]; }
    );
    CHECK(visits == holding(12));

    int healthy = 0;
    scene.forEach<Health>([&](EntityId, Health&) { ++healthy; });
    CHECK(healthy == static_cast<int>(scene.count<Health>()));
}

TEST("a tie is broken by the lowest slot, however the entities were made and removed") {
    Scene scene;
    std::vector<EntityId> made;
    for (int i = 0; i < 8; ++i) made.push_back(scene.createEntity());
    // Added high slot first, so storage order is the reverse of slot order.
    for (int i = 7; i >= 2; --i) scene.add(made[i], Health{i});
    scene.remove<Health>(made[4]);

    const auto lowestHealthy = [&](int above) {
        return findLowestSlot<Health>(scene, [&](const Health& h) { return h.hp > above; });
    };
    CHECK(lowestHealthy(0) == made[2]);
    CHECK(lowestHealthy(3) == made[5]);
    CHECK(!lowestHealthy(10));

    scene.add(made[0], Health{9});
    CHECK(lowestHealthy(0) == made[0]);
}

TEST("a scene numbered from another's generations gives each entity the id it had there") {
    Scene played;
    played.destroyEntity(played.createEntity());
    const EntityId again = played.createEntity();
    const EntityId other = played.createEntity();
    const EntityId freed = played.createEntity();
    played.destroyEntity(freed);
    REQUIRE(again.generation() > 0);

    Scene rebuilt;
    rebuilt.startSlotsAt(played.slotGenerations());
    CHECK(rebuilt.createEntityAt(again.slot()) == again);
    CHECK(rebuilt.createEntityAt(other.slot()) == other);
    // The slot free there hands out no id it gave before.
    const EntityId next = rebuilt.createEntity();
    CHECK(next.slot() == freed.slot());
    CHECK(next.generation() > freed.generation());
    CHECK(rebuilt.entityCount() == 3u);
}

#include "net/replication/names.h"

#include "core/memory/slot_allocator.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"

namespace Vkm::Engine {

void NetNames::adoptWorld(const Scene& scene) {
    m_world.clear();
    m_claimed.clear();
    m_ended.clear();
    m_epoch = scene.epoch();
    scene.forEachEntity([&](EntityId entity) { m_world.emplace(entity.slot(), entity); });
}

EntityId NetNames::find(const Scene& scene, uint32_t slot) const {
    const auto claimed = m_claimed.find(slot);
    if (claimed != m_claimed.end() && scene.isAlive(claimed->second)) return claimed->second;
    const auto loaded = m_world.find(slot);
    if (loaded != m_world.end() && scene.isAlive(loaded->second)) return loaded->second;
    return {};
}

EntityId NetNames::claim(Scene& scene, uint32_t slot) {
    if (const EntityId known = find(scene, slot)) return known;

    // Its own slot when that is free here; else this end's own entity is standing in it.
    EntityId made = scene.isAliveAtIndex(slot) ? EntityId{} : scene.createEntityAt(slot);
    if (!made) {
        if (slot == 0 || slot > SlotAllocator::MAX_CLAIMED_INDEX) return {};
        made = scene.createEntity();
    }
    m_claimed[slot] = made;
    m_epoch         = scene.epoch();
    return made;
}

void NetNames::forget(Scene& scene, uint32_t slot) {
    // The subtree goes too: an instance's children have no name on the wire.
    if (const EntityId named = find(scene, slot)) HierarchyOperations::destroyHierarchy(scene, named);
    m_claimed.erase(slot);
    m_world.erase(slot);
}

void NetNames::endSession() {
    for (const auto& [slot, entity] : m_claimed) m_ended.push_back(entity);
    m_claimed.clear();
}

void NetNames::sweepEnded(Scene& scene) {
    if (m_ended.empty()) return;
    if (scene.epoch() == m_epoch) {
        for (const EntityId entity : m_ended) {
            if (scene.isAlive(entity)) HierarchyOperations::destroyHierarchy(scene, entity);
        }
    }
    m_ended.clear();
}

void NetNames::clear() {
    m_world.clear();
    m_claimed.clear();
    m_ended.clear();
}

} // namespace Vkm::Engine

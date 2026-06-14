#include "system/script/behavior.h"

#include "ecs/scene.h"

namespace Engine {

Entity Behavior::spawn() {
    return m_scene->createEntity();
}

void Behavior::destroy(EntityId entity) {
    // Deferred: BehaviorSystem drains the queue (via HierarchyOperations::
    // destroyHierarchy) after the hook pass, so a behavior can safely destroy
    // its own entity without the iteration freeing it mid-loop.
    if (m_pendingDestroy) m_pendingDestroy->push_back(entity);
}

} // namespace Engine

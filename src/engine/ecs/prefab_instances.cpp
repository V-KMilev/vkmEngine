#include "ecs/prefab_instances.h"

#include "ecs/scene.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/hierarchy_operations.h"

namespace Vkm::Engine::Prefab {

EntityId instanceRootOf(const Scene& scene, EntityId id) {
    return HierarchyOperations::findInSelfOrAncestors<PrefabInstance>(scene, id);
}

bool isInsideInstance(const Scene& scene, EntityId id) {
    // From the parent, so the instance root itself answers false.
    const Hierarchy* node = scene.tryGet<Hierarchy>(id);
    return node && instanceRootOf(scene, node->parent);
}

BuiltSlots builtSlotsOf(const Scene& scene, EntityId root) {
    BuiltSlots slots;
    for (const EntityId id : HierarchyOperations::collectSubtree(scene, root)) {
        if (id == root) continue;
        if (const PrefabEntity* stamped = scene.tryGet<PrefabEntity>(id)) {
            slots[stamped->uid] = id.slot();
        }
    }
    return slots;
}

InstanceSlots instanceSlotsOf(const Scene& scene) {
    InstanceSlots instances;
    scene.forEach<PrefabInstance>([&](EntityId root, const PrefabInstance&) {
        instances[root.slot()] = builtSlotsOf(scene, root);
    });
    return instances;
}

} // namespace Vkm::Engine::Prefab

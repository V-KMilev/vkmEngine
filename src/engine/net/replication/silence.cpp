#include "net/replication/silence.h"

#include "ecs/component/core/hierarchy.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/scene.h"
#include "system/hierarchy/hierarchy_operations.h"
#include "system/physics/ragdoll_system.h"

namespace Vkm::Engine {

bool isInsidePrefabInstance(const Scene& scene, EntityId entity) {
    if (!scene.isAlive(entity) || !scene.has<Hierarchy>(entity)) return false;

    // Upward from the parent, so the instance root itself answers false: it is
    // the one entity of the subtree that does replicate.
    return static_cast<bool>(HierarchyOperations::findInSelfOrAncestors<PrefabInstance>(
        scene, scene.get<Hierarchy>(entity).parent));
}
bool isImmovable(const Scene& scene, EntityId entity) {
    return scene.has<Rigidbody>(entity) && scene.get<Rigidbody>(entity).isStatic;
}
NetSilence netSilence(const Scene& scene, EntityId entity) {
    if (isInsidePrefabInstance(scene, entity)) return NetSilence::InsidePrefabInstance;
    if (isPosedByAnimation(scene, entity))     return NetSilence::PosedByAnimation;
    if (isImmovable(scene, entity))            return NetSilence::Immovable;
    return NetSilence::None;
}
const char* toString(NetSilence reason) {
    switch (reason) {
        case NetSilence::InsidePrefabInstance:
            return "Both ends build the subtree from the same prefab file and each allocates "
                   "its children's slots locally, so the two disagree about what a slot is. "
                   "The root replicates; the hierarchy carries the rest.";
        case NetSilence::PosedByAnimation:
            return "A ragdoll bone is placed from the animated pose every tick while the "
                   "ragdoll is inactive, and the clip is chosen from a velocity that does "
                   "replicate - so both ends work out the same skeleton. Switching the "
                   "ragdoll on puts every bone on the wire.";
        case NetSilence::Immovable:
            return "A static body cannot move, so both ends hold it exactly as the scene "
                   "file wrote it. Sending it would replace that with a quantised copy.";
        default:
            return nullptr;
    }
}
bool isPosedByAnimation(const Scene& scene, EntityId entity) {
    const EntityId owner = ragdollOwnerOf(scene, entity);
    return owner && !scene.get<Ragdoll>(owner).active;
}

} // namespace Vkm::Engine

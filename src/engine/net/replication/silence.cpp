#include "net/replication/silence.h"

#include "ecs/component/net/net_local.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/scene.h"
#include "ecs/prefab_instances.h"

namespace Vkm::Engine {

namespace {

/// The rules in the order they are asked, with the costly one answered.
NetSilence classify(const Scene& scene, EntityId entity, bool posed) {
    if (HierarchyOperations::findInSelfOrAncestors<NetLocal>(scene, entity)) return NetSilence::Local;
    if (Prefab::isInsideInstance(scene, entity)) return NetSilence::InsidePrefabInstance;
    if (posed)                                   return NetSilence::PosedByAnimation;
    if (isStaticBody(scene, entity))             return NetSilence::StaticBody;
    if (isFixedMesh(scene, entity))              return NetSilence::FixedMesh;
    return NetSilence::None;
}

} // namespace

bool isStaticBody(const Scene& scene, EntityId entity) {
    const Rigidbody* body = scene.tryGet<Rigidbody>(entity);
    return body && body->motion == RigidbodyMotion::Static;
}

bool isFixedMesh(const Scene& scene, EntityId entity) {
    const Mesh* mesh = scene.tryGet<Mesh>(entity);
    return mesh && !mesh->movable && !scene.has<Rigidbody>(entity);
}

NetSilence netSilence(const Scene& scene, EntityId entity) {
    return classify(scene, entity, isPosedByAnimation(scene, entity));
}

void NetSilenceMap::build(const Scene& scene) {
    markPosedByAnimation(scene, m_posed);

    m_bySlot.clear();
    scene.forEachEntity([&](EntityId entity) {
        const uint32_t slot = entity.slot();
        const bool posed = slot < m_posed.size() && m_posed[slot];
        const NetSilence said = classify(scene, entity, posed);
        if (said == NetSilence::None) return;

        if (m_bySlot.size() <= slot) m_bySlot.resize(slot + 1, NetSilence::None);
        m_bySlot[slot] = said;
    });
}

NetSilence NetSilenceMap::of(EntityId entity) const {
    const uint32_t slot = entity.slot();
    return slot < m_bySlot.size() ? m_bySlot[slot] : NetSilence::None;
}

// Exhaustive, not defaulted: a new reason must be explained to an author.
NetSilenceText explain(NetSilence reason) {
    switch (reason) {
        case NetSilence::InsidePrefabInstance:
            return {
                "inside a prefab instance",
                "Both ends build the subtree from the same prefab file. "
                    "The root replicates; the hierarchy carries the rest."
            };
        case NetSilence::PosedByAnimation:
            return {
                "the animation places it",
                "A ragdoll bone is placed from the animated pose every tick while the "
                    "ragdoll is inactive, and the clip is chosen from a velocity that does "
                    "replicate - so both ends work out the same skeleton. Switching the "
                    "ragdoll on puts every bone on the wire."
            };
        case NetSilence::StaticBody:
            return {
                "it cannot move",
                "A static body cannot move, so both ends hold it "
                    "exactly as the scene file wrote it. Sending it would replace that with "
                    "a quantized copy."
            };
        case NetSilence::FixedMesh:
            return {
                "its mesh never moves",
                "Its Mesh has movable off: it promises never to move, so both ends hold it "
                    "exactly as the scene file wrote it. Sending it would move it to a quantized copy."
            };
        case NetSilence::Local:
            return {
                "each end runs its own",
                "It or an entity above it is NetLocal: every end works it out for itself, "
                    "so the server never describes it and a client never draws it from a snapshot."
            };
        case NetSilence::None:
            return {};
    }
    return {};
}

} // namespace Vkm::Engine

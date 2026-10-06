#include "net/replication/silence.h"

#include "ecs/component/physics/rigidbody.h"
#include "ecs/scene.h"
#include "io/scene/prefab.h"

namespace Vkm::Engine {

namespace {

/// The rules in the order they are asked, with the costly one answered.
NetSilence classify(const Scene& scene, EntityId entity, bool posed) {
    if (Prefab::isInsideInstance(scene, entity)) return NetSilence::InsidePrefabInstance;
    if (posed)                                   return NetSilence::PosedByAnimation;
    if (isStaticBody(scene, entity))             return NetSilence::StaticBody;
    return NetSilence::None;
}

} // namespace

bool isStaticBody(const Scene& scene, EntityId entity) {
    const Rigidbody* body = scene.tryGet<Rigidbody>(entity);
    return body && body->motion == RigidbodyMotion::Static;
}

NetSilence netSilence(const Scene& scene, EntityId entity) {
    return classify(scene, entity, isPosedByAnimation(scene, entity));
}

void NetSilenceMap::build(const Scene& scene) {
    markPosedByAnimation(scene, m_posed);

    m_bySlot.clear();
    scene.forEachEntity([&](EntityId entity) {
        const uint32_t slot   = entity.slot();
        const bool     posed  = slot < m_posed.size() && m_posed[slot];
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
const char* toString(NetSilence reason) {
    switch (reason) {
        case NetSilence::InsidePrefabInstance:
            return "Both ends build the subtree from the same prefab file, into the same slots. "
                "The root replicates; the hierarchy carries the rest.";
        case NetSilence::PosedByAnimation:
            return "A ragdoll bone is placed from the animated pose every tick while the "
                "ragdoll is inactive, and the clip is chosen from a velocity that does "
                "replicate - so both ends work out the same skeleton. Switching the "
                "ragdoll on puts every bone on the wire.";
        case NetSilence::StaticBody:
            return "A static body cannot move, so both ends hold it "
                "exactly as the scene file wrote it. Sending it would replace that with "
                "a quantised copy.";
        case NetSilence::None:
        case NetSilence::Count:
            return nullptr;
    }
    return nullptr;
}

} // namespace Vkm::Engine

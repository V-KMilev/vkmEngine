#include "ecs/component/physics/ragdoll.h"

#include "ecs/scene.h"
#include "ecs/hierarchy_operations.h"

namespace Vkm::Engine {

EntityId ragdollOwnerOf(const Scene& scene, EntityId body, int32_t* outBone) {
    if (!body) return {};

    auto* storage = scene.storage<Ragdoll>();
    if (!storage) return {};

    const uint32_t count = static_cast<uint32_t>(storage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const Ragdoll& ragdoll = storage->dataAt(i);
        for (const RagdollBone& bone : ragdoll.bones) {
            if (bone.body != body) continue;
            if (outBone) *outBone = bone.bone;
            return scene.entityAt(storage->keyAt(i));
        }
    }
    return {};
}

void destroyRagdollBodies(Scene& scene, const Ragdoll& ragdoll) {
    for (const RagdollBone& bone : ragdoll.bones) {
        if (bone.body && scene.isAlive(bone.body)) scene.destroyEntity(bone.body);
    }
    if (ragdoll.root && scene.isAlive(ragdoll.root)) {
        HierarchyOperations::destroyHierarchy(scene, ragdoll.root);
    }
}

namespace {

/**
 * @brief Call @p fn with every body an inactive ragdoll poses.
 *
 * The one statement of the rule, so the per-entity and per-world questions agree.
 *
 * @tparam Fn Callable taking the bone's body EntityId.
 * @param scene World whose ragdolls are walked.
 * @param fn Called once per bone; the body may be dead, and @p fn decides.
 */
template<typename Fn>
void forEachPosedBody(const Scene& scene, Fn&& fn) {
    const auto* ragdolls = scene.storage<Ragdoll>();
    if (!ragdolls) return;

    for (size_t i = 0; i < ragdolls->size(); ++i) {
        const Ragdoll& ragdoll = ragdolls->dataAt(static_cast<uint32_t>(i));
        if (ragdoll.active) continue;
        for (const RagdollBone& bone : ragdoll.bones) fn(bone.body);
    }
}

} // namespace

bool isPosedByAnimation(const Scene& scene, EntityId entity) {
    bool posed = false;
    forEachPosedBody(scene, [&](EntityId body) { posed = posed || body == entity; });
    return posed && scene.isAlive(entity);
}

void markPosedByAnimation(const Scene& scene, std::vector<bool>& bySlot) {
    bySlot.clear();
    forEachPosedBody(scene, [&](EntityId body) {
        // By id: a dead bone must not mark the slot's new occupant.
        if (!scene.isAlive(body)) return;
        if (bySlot.size() <= body.slot()) bySlot.resize(body.slot() + 1, false);
        bySlot[body.slot()] = true;
    });
}

} // namespace Vkm::Engine

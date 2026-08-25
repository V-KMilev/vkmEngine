#include "system/physics/ragdoll_system.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/math/rotation.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "system/animation/pose_buffer.h"
#include "system/hierarchy/hierarchy_operations.h"

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

void RagdollSystem::init(FrameContext& ctx) {
    m_scene = &ctx.scene;
    ctx.scene.addObserver(this);
}

void RagdollSystem::shutdown() {
    if (m_scene) m_scene->removeObserver(this);
    m_scene = nullptr;
}

void RagdollSystem::onEntityDestroyed(EntityId id) {
    if (!m_scene || !m_scene->has<Ragdoll>(id)) return;

    // Fired before the entity is torn down, so the component is still readable.
    // Destroying the bones re-enters destroyEntity for each, which is safe:
    // they carry no Ragdoll of their own, so nothing recurses past one level.
    const Ragdoll& ragdoll = m_scene->get<Ragdoll>(id);
    for (const RagdollBone& bone : ragdoll.bones) {
        if (!bone.body || !m_scene->isAlive(bone.body)) continue;
        m_scene->destroyEntity(bone.body);
    }

    // The node they hung under goes too. It is a child of the entity being
    // destroyed, so a hierarchy teardown would have taken it - but a plain
    // destroyEntity does not, and an empty rig node outliving its character is
    // exactly the litter this observer exists to prevent.
    if (ragdoll.root && m_scene->isAlive(ragdoll.root)) {
        HierarchyOperations::destroyHierarchy(*m_scene, ragdoll.root);
    }
}

void RagdollSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("RagdollSystem");

    Scene& scene = ctx.scene;
    auto* storage = scene.storage<Ragdoll>();
    if (!storage) return;

    const uint32_t count = static_cast<uint32_t>(storage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const EntityId self = scene.entityAt(storage->keyAt(i));
        Ragdoll& ragdoll = storage->dataAt(i);
        if (ragdoll.bones.empty()) continue;

        if (ragdoll.active) {
            // Handed over: the bodies are ordinary dynamics from here, and the
            // pose follows them rather than the other way round.
            for (const RagdollBone& bone : ragdoll.bones) {
                if (!bone.body || !scene.isAlive(bone.body)
                    || !scene.has<Rigidbody>(bone.body)) continue;
                scene.get<Rigidbody>(bone.body).isKinematic = false;
            }
            continue;
        }

        // Inactive: the bodies follow the animation instead of gravity. Placed
        // rather than merely frozen, so the frame `active` is switched on the
        // solver inherits the pose the character was actually in.
        const EntityId rigNode =
            HierarchyOperations::findInSelfOrDescendants<Animator>(scene, self);

        const PoseSlice* slice = nullptr;
        if (ctx.poses && rigNode) slice = ctx.poses->sliceOf(rigNode.index);

        const glm::mat4 rigWorld = rigNode
            ? HierarchyOperations::computeWorldMatrix(scene, rigNode)
            : glm::mat4(1.0f);

        // The bones are children of this entity, so their Transform is its
        // frame rather than the world's. The pose arrives in world space and
        // comes back through here - the same conversion writeback does when the
        // solver hands a world pose to a parented body.
        // The frame the bodies are stored in, which is the node they hang
        // under rather than the character itself.
        const EntityId under = ragdoll.root ? ragdoll.root : self;
        const glm::mat4 toParent =
            glm::inverse(HierarchyOperations::computeWorldMatrix(scene, under));

        for (const RagdollBone& bone : ragdoll.bones) {
            // Alive before has: has() asserts on a stale handle, and a
            // ragdoll resurrected by an editor undo names bones that died
            // with the original. Such a ragdoll simply stops posing bodies -
            // the rig follows the animation as though it were never built.
            if (!bone.body || !scene.isAlive(bone.body)
                || !scene.has<Rigidbody>(bone.body)) continue;

            Rigidbody& body = scene.get<Rigidbody>(bone.body);
            body.isKinematic = true;
            body.linearVelocity = glm::vec3(0.0f);
            body.angularVelocity = glm::vec3(0.0f);

            if (!slice || bone.bone < 0) continue;
            const uint32_t index = static_cast<uint32_t>(bone.bone);
            if (index >= slice->count) continue;
            if (!scene.has<Transform>(bone.body)) continue;

            // The bone's place in the world, then back through the offset the
            // build recorded - the body covers the limb and the bone sits at
            // its head, so the two are never the same transform.
            const glm::mat4 boneWorld =
                rigWorld * ctx.poses->global()[slice->first + index];
            const glm::mat4 bodyWorld =
                boneWorld * glm::inverse(bone.boneFromBody);
            const glm::mat4 bodyLocal = toParent * bodyWorld;

            Transform& transform = scene.get<Transform>(bone.body);
            transform.position = glm::vec3(bodyLocal[3]);
            transform.rotation = Math::worldRotationOf(bodyLocal);
        }
    }
}

} // namespace Vkm::Engine

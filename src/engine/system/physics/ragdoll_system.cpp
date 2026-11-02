#include "system/physics/ragdoll_system.h"

#include <cmath>
#include <string>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "vkm_physics/geometry/rotation.h"
#include "vkm_physics/mass_model.h"

#include "core/clock.h"
#include "base/math/rotation.h"
#include "debug/error/engine_error_log.h"
#include "base/profile/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/name.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/hierarchy_operations.h"
#include "core/frame/pose_buffer.h"

namespace Vkm::Engine {

namespace {

// How far the rig's scale ratio to its build scale may stray from one: a unit change is a factor of a
// hundred, matrix-chain noise far below this.
constexpr float STALE_SCALE_TOLERANCE = 0.01f;

} // namespace

void RagdollSystem::init(FrameContext& ctx) {
    m_scene = &ctx.scene;
    ctx.scene.addObserver(this);
}

void RagdollSystem::shutdown() {
    if (m_scene) m_scene->removeObserver(this);
    m_scene = nullptr;
}

void RagdollSystem::onEntityDestroyed(EntityId id) {
    if (!m_scene) return;

    // Fired before teardown, so the component is readable. Re-entering destroyEntity per bone is safe:
    // bones carry no Ragdoll, so nothing recurses past one level.
    const Ragdoll* held = m_scene->tryGet<Ragdoll>(id);
    if (!held) return;

    destroyRagdollBodies(*m_scene, *held);
}

void RagdollSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("RagdollSystem");

    Scene& scene  = ctx.scene;
    auto* storage = scene.storage<Ragdoll>();
    if (!storage) {
        m_scaled.endPass();
        return;
    }

    const uint32_t count = static_cast<uint32_t>(storage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const EntityId self = scene.entityAt(storage->keyAt(i));
        Ragdoll& ragdoll    = storage->dataAt(i);
        if (ragdoll.bones.empty()) continue;

        Transform* rootAt = ragdoll.root ? scene.tryGet<Transform>(ragdoll.root) : nullptr;
        const Hierarchy* rootIn = ragdoll.root ? scene.tryGet<Hierarchy>(ragdoll.root) : nullptr;
        if (ragdoll.active && rootAt) {
            // Live, the group node stays where it stood in the world, and the bones with it: what moves
            // the character does not drag the body it has become.
            const glm::mat4 parentWorld = rootIn && rootIn->parent
                ? HierarchyOperations::computeWorldMatrix(scene, rootIn->parent)
                : glm::mat4(1.0f);
            if (!ragdoll.pinned) {
                ragdoll.pinnedWorld = HierarchyOperations::computeWorldMatrix(scene, ragdoll.root);
                ragdoll.rootRest    = *rootAt;
                ragdoll.pinned      = true;
            } else {
                *rootAt = Transform::fromModelMatrix(glm::inverse(parentWorld) * ragdoll.pinnedWorld);
            }
        } else if (ragdoll.pinned) {
            if (rootAt) *rootAt = ragdoll.rootRest;
            ragdoll.pinned = false;
        }

        if (ragdoll.active) {
            // Handed over: the bodies are ordinary dynamics, and the pose follows them.
            for (RagdollBone& bone : ragdoll.bones) {
                bone.posed = false;
                // Once, at hand-over: a held bone keeps its sleep state, and one handed over asleep
                // would never fall. Waking every tick would keep a still ragdoll from sleeping.
                if (!ragdoll.held) continue;
                if (Rigidbody* body = scene.tryGet<Rigidbody>(bone.body)) Rigidbody::wake(*body);
            }
            ragdoll.held = false;
            continue;
        }

        const EntityId rigNode = HierarchyOperations::findInSelfOrDescendants<Animator>(scene, self);

        const PoseSlice* slice = nullptr;
        if (ctx.poses && rigNode) slice = ctx.poses->sliceOf(rigNode);

        const glm::mat4 rigWorld = rigNode
            ? HierarchyOperations::computeWorldMatrix(scene, rigNode)
            : glm::mat4(1.0f);

        // Bones are children of the group node, so world poses come back through its frame.
        const EntityId under     = ragdoll.root ? ragdoll.root : self;
        const glm::mat4 toParent = glm::inverse(HierarchyOperations::computeWorldMatrix(scene, under));

        // Bone offsets carry the build scale, so a rescaled rig misplaces every body. Asked of the rig,
        // not a posed bone, which a clip may scale on purpose.
        const float scaleNow = glm::length(glm::vec3(rigWorld[0])) / ragdoll.rigScale;
        if (std::abs(scaleNow - 1.0f) > STALE_SCALE_TOLERANCE && m_scaled.report()) {
            const Name* name = scene.tryGet<Name>(self);
            const std::string message = "its bones were built for its rig at another scale (now x"
                + std::to_string(scaleNow) + "); rebuild the ragdoll";
            reportError("Physics", name ? name->value : "a ragdoll", message);
        }

        ragdoll.held = true;
        const float dt = ctx.clock.getFixedStep();
        for (RagdollBone& bone : ragdoll.bones) {
            const bool wasPosed = bone.posed;
            bone.posed = false;

            // tryGet is total: covers a bone whose body the world no longer holds.
            Rigidbody* held = scene.tryGet<Rigidbody>(bone.body);
            if (!held) continue;

            Rigidbody& body = *held;
            body.linearVelocity  = glm::vec3(0.0f);
            body.angularVelocity = glm::vec3(0.0f);

            if (!slice || bone.bone < 0) continue;
            const uint32_t index = static_cast<uint32_t>(bone.bone);
            if (index >= slice->count) continue;

            Transform* transform = scene.tryGet<Transform>(bone.body);
            if (!transform) continue;

            // Back through the recorded offset: the body covers the limb, the bone sits at its head.
            const glm::mat4 boneWorld = rigWorld * ctx.poses->global()[slice->first + index];
            const glm::mat4 bodyWorld = boneWorld * glm::inverse(bone.bodyFromBone);

            const glm::mat4 bodyLocal = toParent * bodyWorld;

            transform->position = glm::vec3(bodyLocal[3]);
            transform->rotation = Math::worldRotationOf(bodyLocal);

            // The pose's speed is what activation hands the solver: felled mid-stride, it falls forward.
            // Of the point the bone is solved about once it falls.
            const Physics::MassModel model = massModelOf(scene.tryGet<Collider>(bone.body));
            const glm::vec3 center   = Physics::solverCenter(Physics::Motion::Dynamic, body.mass, model);
            const glm::vec3 position = glm::vec3(bodyWorld * glm::vec4(center, 1.0f));
            const glm::quat rotation = Math::worldRotationOf(bodyWorld);
            if (wasPosed && dt > 0.0f) {
                body.linearVelocity  = (position - bone.lastPosition) / dt;
                body.angularVelocity = Physics::rotationBetween(bone.lastRotation, rotation) / dt;
            }
            bone.lastPosition = position;
            bone.lastRotation = rotation;
            bone.posed        = true;
        }
    }
    m_scaled.endPass();
}

} // namespace Vkm::Engine

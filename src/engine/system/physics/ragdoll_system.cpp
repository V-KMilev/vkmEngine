#include "system/physics/ragdoll_system.h"

#include <cmath>
#include <string>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/clock.h"
#include "core/math/rotation.h"
#include "debug/engine_error_log.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/hierarchy_operations.h"
#include "system/animation/pose_buffer.h"

namespace Vkm::Engine {

namespace {

// How far the rig's scale ratio to its build scale may stray from one: a unit change is a factor of a
// hundred, matrix-chain noise far below this.
constexpr float STALE_SCALE_TOLERANCE = 0.01f;

// The world-space spin that turns `from` into `to` over dt, the short way round.
glm::vec3 spinBetween(const glm::quat& from, const glm::quat& to, float dt) {
    glm::quat delta = to * glm::conjugate(from);
    if (delta.w < 0.0f) delta = -delta;
    const glm::vec3 axis(delta.x, delta.y, delta.z);
    const float sine = glm::length(axis);
    if (sine <= glm::epsilon<float>()) return axis * (2.0f / dt);
    return axis * (2.0f * std::atan2(sine, delta.w) / (sine * dt));
}

} // namespace

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
    if (!m_scene) return;

    // Fired before teardown, so the component is readable. Re-entering destroyEntity per bone is safe:
    // bones carry no Ragdoll, so nothing recurses past one level.
    const Ragdoll* held = m_scene->tryGet<Ragdoll>(id);
    if (!held) return;

    const Ragdoll& ragdoll = *held;
    for (const RagdollBone& bone : ragdoll.bones) {
        if (!bone.body || !m_scene->isAlive(bone.body)) continue;
        m_scene->destroyEntity(bone.body);
    }

    // The group node goes too: a plain destroyEntity would leave it outliving its character.
    if (ragdoll.root && m_scene->isAlive(ragdoll.root)) {
        HierarchyOperations::destroyHierarchy(*m_scene, ragdoll.root);
    }
}

void RagdollSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("RagdollSystem");

    Scene& scene = ctx.scene;
    auto* storage = scene.storage<Ragdoll>();
    if (!storage) {
        m_scaled.endPass();
        return;
    }

    const uint32_t count = static_cast<uint32_t>(storage->size());
    for (uint32_t i = 0; i < count; ++i) {
        const EntityId self = scene.entityAt(storage->keyAt(i));
        Ragdoll& ragdoll = storage->dataAt(i);
        if (ragdoll.bones.empty()) continue;

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
        const EntityId under = ragdoll.root ? ragdoll.root : self;
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
            body.linearVelocity = glm::vec3(0.0f);
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
            const glm::vec3 position = glm::vec3(bodyWorld[3]);
            const glm::quat rotation = Math::worldRotationOf(bodyWorld);
            if (wasPosed && dt > 0.0f) {
                body.linearVelocity  = (position - bone.lastPosition) / dt;
                body.angularVelocity = spinBetween(bone.lastRotation, rotation, dt);
            }
            bone.lastPosition = position;
            bone.lastRotation = rotation;
            bone.posed        = true;
        }
    }
    m_scaled.endPass();
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

#define VKM_LOG_CATEGORY "ANIM"

#include "system/animation/skeletal_animation_system.h"

#include <algorithm>
#include <cmath>
#include <utility>

#include <glm/gtc/epsilon.hpp>

#include "logger.h"

#include "core/clock.h"
#include "core/event/event_bus.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/render/mesh.h"
#include "ecs/hierarchy_operations.h"
#include "platform/threading/thread_pool.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/resource_manager.h"
#include "system/animation/animation_events.h"
#include "system/animation/pose_evaluator.h"
#include "ecs/component/physics/ragdoll.h"
#include "system/animation/ragdoll_pose.h"

namespace Vkm::Engine {

namespace {

// Below this many bones the pool's dispatch costs more than the sweep. Bones, not rigs: one rig is ~100.
constexpr size_t MIN_PARALLEL_BONES = 2048;

// How far a skinned mesh's own transform may sit from identity before it is reported: above authoring
// noise, below a real offset.
constexpr float IDENTITY_EPSILON = 1e-4f;

bool isIdentity(const Transform& transform) {
    return glm::all(glm::epsilonEqual(transform.position, glm::vec3(0.0f), IDENTITY_EPSILON))
        && glm::all(glm::epsilonEqual(transform.scale,    glm::vec3(1.0f), IDENTITY_EPSILON))
        && std::abs(std::abs(transform.rotation.w) - 1.0f) <= IDENTITY_EPSILON;
}

} // namespace

void SkeletalAnimationSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("SkeletalAnimationSystem");

    // Published every frame, so a frame with no tick draws the last pose; null reads as "no rig here".
    ctx.poses = &m_poses;

    // A replaced world reuses the old one's slots, so the old map would pose new entities from wrong slices.
    if (!ctx.clock.isPaused() && ctx.scene.epoch() == m_epoch) return;
    const bool announceMarkers = false;
    run(ctx, 0.0f, announceMarkers);
}

void SkeletalAnimationSystem::fixedUpdate(FrameContext& ctx) {
    PROFILE_SCOPE("SkeletalAnimationSystem::fixed");
    const bool announceMarkers = true;
    run(ctx, ctx.clock.getFixedStep(), announceMarkers);
}

void SkeletalAnimationSystem::run(FrameContext& ctx, float step, bool announceMarkers) {
    m_poses.clear();
    m_work.clear();
    m_ragdollBodies.clear();
    m_epoch = ctx.scene.epoch();
    ctx.poses = &m_poses;

    poseRigs(ctx, step);
    if (announceMarkers) publishMarkers(ctx);

    // Here rather than in poseRigs, so every exit it takes closes the pass.
    m_badSkeleton.endPass();
    m_badClip.endPass();
    m_clipMismatch.endPass();
    m_rigMismatch.endPass();
    m_meshOffset.endPass();
}

void SkeletalAnimationSystem::poseRigs(FrameContext& ctx, float step) {
    Scene& scene = ctx.scene;

    auto* animators = scene.storage<Animator>();
    if (!animators || animators->size() == 0) return;

    const ResourceManager& resources = ctx.resources;
    const auto animatorCount = static_cast<uint32_t>(animators->size());

    // Serial, because each slice's range is a running total.
    size_t totalBones = 0;
    for (uint32_t i = 0; i < animatorCount; ++i) {
        const Animator& animator = animators->dataAt(i);
        if (!animator.skeleton || !resources.isAlive(animator.skeleton)) continue;

        const SkeletonAsset& skeleton = resources.get(animator.skeleton);
        if (skeleton.bones.empty() || !isPoseable(skeleton)) continue;

        RigWork work;
        work.animatorIndex = i;
        work.entityIndex   = animators->keyAt(i);
        work.skeleton      = &skeleton;

        work.clip     = resolveClip(resources, animator.clip,     skeleton);
        work.fadeClip = resolveClip(resources, animator.fadeFrom, skeleton);
        work.slice    = m_poses.addSlice(static_cast<uint32_t>(skeleton.bones.size()));

        // Read here, because the parallel pass never touches the scene; nothing is added or removed between.
        const EntityId rigEntity = scene.entityAt(work.entityIndex);

        // Upward: importModelIntoScene can put the Animator a node or two below the Ragdoll's entity.
        const EntityId ragdollEntity =
            HierarchyOperations::findInSelfOrAncestors<Ragdoll>(scene, rigEntity);
        if (ragdollEntity) {
            const Ragdoll& ragdoll = scene.get<Ragdoll>(ragdollEntity);
            if (ragdoll.active && !ragdoll.bones.empty()) {
                work.ragdoll   = &ragdoll;
                work.firstBody = static_cast<uint32_t>(m_ragdollBodies.size());
                gatherRagdollBodies(scene, ragdoll, m_ragdollBodies);
            }
        }
        // Walked like the bodies, not read off WorldTransform, which is a frame behind: mixing them drifts.
        if (work.ragdoll) {
            work.rigWorld = HierarchyOperations::computeWorldMatrix(scene, rigEntity);
        }

        totalBones += skeleton.bones.size();
        m_work.push_back(std::move(work));
    }
    if (m_work.empty()) return;

    // By slot, not storage order (add/remove history): the markers feed the simulation, so their order
    // must be a function of the world.
    std::sort(
        m_work.begin(),
        m_work.end(),
        [](const RigWork& a, const RigWork& b) { return a.entityIndex < b.entityIndex; }
    );

    // A character is several mesh entities under one Animator (see importModelIntoScene).
    for (const RigWork& work : m_work) {
        const EntityId rig = scene.entityAt(work.entityIndex);
        m_poses.mapEntity(rig, work.slice);
        // The rig itself can carry a skinned mesh (a one-mesh file).
        checkSkinnedMesh(scene, resources, rig, *work.skeleton, true);
        stampDescendants(scene, resources, rig, work, 0);
    }

    const float simDelta = step;
    const size_t grain = (totalBones < MIN_PARALLEL_BONES)
        ? m_work.size()
        : std::max<size_t>(1, m_work.size() / (ThreadPool::get().threadCount() + 1));

    {
        // Each iteration writes one Animator and one disjoint slice; no slice is allocated past here.
        PROFILE_SCOPE("SkeletalAnimation/Evaluate");
        parallelFor(m_work.size(), grain, [&](size_t i) {
            RigWork& work      = m_work[i];
            Animator& animator = animators->dataAt(work.animatorIndex);

            // A ragdoll takes the clock too: the head does not move and markers (from work.step) do not fire.
            if (!work.ragdoll) {
                work.step = advancePlayback(
                    animator,
                    work.clip     ? work.clip->duration     : 0.0f,
                    work.fadeClip ? work.fadeClip->duration : 0.0f,
                    simDelta
                );
            }

            PoseSample sample;
            sample.clip     = work.clip;
            sample.time     = animator.time;
            sample.from     = work.fadeClip;
            sample.fromTime = animator.fadeTime;
            // advancePlayback clears a spent fade, so zero means nothing left to blend.
            sample.weight   = (animator.fadeDuration > 0.0f)
                ? 1.0f - animator.fadeRemaining / animator.fadeDuration
                : 1.0f;
            sample.adjust      = animator.adjust.data();
            sample.adjustCount = static_cast<uint32_t>(animator.adjust.size());

            // Ragdoll bodies are not blended with a clip: that drags limbs between two unrelated poses.
            if (work.ragdoll) {
                composeRagdollPose(
                    *work.ragdoll,
                    m_ragdollBodies.data() + work.firstBody,
                    *work.skeleton,
                    work.rigWorld,
                    m_poses.writeTo(work.slice)
                );
            } else {
                composePose(*work.skeleton, sample, m_poses.writeTo(work.slice));
            }
        });
    }
}

void SkeletalAnimationSystem::publishMarkers(FrameContext& ctx) {
    for (const RigWork& work : m_work) {
        if (!work.clip || work.clip->markers.empty()) continue;

        const EntityId rig = ctx.scene.entityAt(work.entityIndex);
        for (const ClipMarker& marker : work.clip->markers) {
            if (!crossesMarker(work.step, marker.time, work.clip->duration)) continue;
            // Enqueued, so no listener runs mid-walk over storage it may edit (see EventBus::enqueue).
            ctx.events.enqueue(AnimationEvent{rig, marker.name});
        }
    }
}

bool SkeletalAnimationSystem::isPoseable(const SkeletonAsset& skeleton) {
    const std::string fault = findSkeletonFault(skeleton);
    if (fault.empty()) return true;

    if (m_badSkeleton.report()) {
        LOG_WARNING(
            "Skeleton '%s' cannot be posed - %s; its rig is left unposed",
            skeleton.name().c_str(),
            fault.c_str()
        );
    }
    return false;
}

const AnimationClipAsset* SkeletalAnimationSystem::resolveClip(
    const ResourceManager& resources,
    const AnimationClipHandle& handle,
    const SkeletonAsset& skeleton
) {
    if (!handle || !resources.isAlive(handle)) return nullptr;

    // A clip's per-bone table is bound at cook time to one rig's name and length.
    const AnimationClipAsset& clip = resources.get(handle);
    if (clip.skeleton != skeleton.name() || clip.bones.size() != skeleton.bones.size()) {
        if (m_clipMismatch.report()) {
            LOG_WARNING(
                "Clip '%s' (rig '%s', %zu bones) does not fit rig '%s' (%zu bones) - "
                "holding the bind pose",
                clip.name().c_str(),
                clip.skeleton.c_str(),
                clip.bones.size(),
                skeleton.name().c_str(),
                skeleton.bones.size()
            );
        }
        return nullptr;
    }

    const std::string fault = findClipFault(clip);
    if (fault.empty()) return &clip;

    if (m_badClip.report()) {
        LOG_WARNING(
            "Clip '%s' cannot be played - %s; holding the bind pose",
            clip.name().c_str(),
            fault.c_str()
        );
    }
    return nullptr;
}

void SkeletalAnimationSystem::stampDescendants(
    Scene& scene,
    const ResourceManager& resources,
    EntityId entity,
    const RigWork& work,
    uint32_t depth
) {
    if (depth >= HierarchyOperations::MAX_DEPTH) {
        HierarchyOperations::warnWalkBound("skeleton stamp", HierarchyOperations::MAX_DEPTH);
        return;
    }

    HierarchyOperations::forEachChild(scene, entity, [&](EntityId child) {
        // A nested rig owns its subtree and its own slice.
        if (scene.has<Animator>(child)) return;
        m_poses.mapEntity(child, work.slice);
        checkSkinnedMesh(scene, resources, child, *work.skeleton, false);
        stampDescendants(scene, resources, child, work, depth + 1);
    });
}

void SkeletalAnimationSystem::checkSkinnedMesh(
    const Scene& scene,
    const ResourceManager& resources,
    EntityId entity,
    const SkeletonAsset& skeleton,
    bool onRig
) {
    const Mesh* mesh = scene.tryGet<Mesh>(entity);
    if (!mesh || !mesh->mesh || !resources.isAlive(mesh->mesh)) return;

    const MeshAsset& asset = resources.get(mesh->mesh);
    if (asset.skin.empty()) return;

    if (asset.skeleton != skeleton.name() && m_rigMismatch.report()) {
        LOG_WARNING(
            "Mesh '%s' is skinned to rig '%s' but sits under '%s' - "
            "its bone indices address the wrong joints",
            asset.name().c_str(),
            asset.skeleton.c_str(),
            skeleton.name().c_str()
        );
    }

    // Rig-space palette vertices land right only at identity, as importModelIntoScene places them.
    const Transform* local = onRig ? nullptr : scene.tryGet<Transform>(entity);
    if (local && !isIdentity(*local) && m_meshOffset.report()) {
        LOG_WARNING(
            "Skinned mesh '%s' does not sit at its rig's origin - "
            "skinned vertices are already in rig space, so its own "
            "transform is applied twice",
            asset.name().c_str()
        );
    }
}

} // namespace Vkm::Engine

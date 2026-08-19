#define VKM_LOG_CATEGORY "ANIM"

#include "system/animation/skeletal_animation_system.h"

#include <algorithm>
#include <cmath>

#include <glm/gtc/epsilon.hpp>

#include "logger.h"

#include "core/clock.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animator.h"
#include "ecs/component/mesh.h"
#include "ecs/component/transform.h"
#include "platform/threading/thread_pool.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/mesh_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/resource_manager.h"
#include "system/animation/pose_evaluator.h"
#include "system/hierarchy/hierarchy_operations.h"

namespace Vkm::Engine {

namespace {

// Below this much bone work the pool's dispatch cost (mutex, a notify_all wake
// of every worker, a done-CV round trip) outweighs the sweep itself. Counted in
// bones rather than in rigs because one rig is a hundred bones' work, so the
// animator count alone says nothing about how long the loop takes.
constexpr size_t MIN_PARALLEL_BONES = 2048;

// How far a skinned mesh's own transform may sit from identity before it is
// reported. Loose enough that authoring noise is not a warning, tight enough
// that a real offset - which doubles the transform - always is.
constexpr float IDENTITY_EPSILON = 1e-4f;

bool isIdentity(const Transform& transform) {
    return glm::all(glm::epsilonEqual(transform.position, glm::vec3(0.0f), IDENTITY_EPSILON))
        && glm::all(glm::epsilonEqual(transform.scale,    glm::vec3(1.0f), IDENTITY_EPSILON))
        && std::abs(std::abs(transform.rotation.w) - 1.0f) <= IDENTITY_EPSILON;
}

} // namespace

void SkeletalAnimationSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("SkeletalAnimationSystem");

    m_poses.clear();
    m_work.clear();
    ctx.poses = &m_poses;

    FaultsSeen seen;
    poseRigs(ctx, seen);

    // Each latch holds only while its fault is still there, so fixing one is
    // reported again if it comes back - which is why poseRigs reports into seen
    // and every exit it takes lands on these three lines.
    m_clipMismatchLogged = seen.clipMismatch;
    m_rigMismatchLogged  = seen.rigMismatch;
    m_meshOffsetLogged   = seen.meshOffset;
}

void SkeletalAnimationSystem::poseRigs(FrameContext& ctx, FaultsSeen& seen) {
    Scene& scene = ctx.scene;

    auto* animators = scene.storage<Animator>();
    if (!animators || animators->size() == 0) return;

    const ResourceManager& resources = ctx.resources;
    const auto animatorCount = static_cast<uint32_t>(animators->size());

    // Allocate. Serial, because each slice's range is a running total, and
    // because every handle has to be resolved before the parallel phase, which
    // never touches the ResourceManager.
    size_t totalBones = 0;
    for (uint32_t i = 0; i < animatorCount; ++i) {
        const Animator& animator = animators->dataAt(i);
        if (!animator.skeleton || !resources.isAlive(animator.skeleton)) continue;

        const SkeletonAsset& skeleton = resources.get(animator.skeleton);
        if (skeleton.bones.empty()) continue;

        RigWork work;
        work.animatorIndex = i;
        work.entityIndex   = animators->keyAt(i);
        work.skeleton      = &skeleton;

        work.clip     = resolveClip(resources, animator.clip,     skeleton, seen);
        work.fadeClip = resolveClip(resources, animator.fadeFrom, skeleton, seen);
        work.slice    = m_poses.addSlice(static_cast<uint32_t>(skeleton.bones.size()));

        totalBones += skeleton.bones.size();
        m_work.push_back(work);
    }
    if (m_work.empty()) return;

    // Map. A rig poses itself and everything under it, because import spawns a
    // mesh entity per aiMesh and a character is body plus clothes plus hair -
    // all of them driven by the one Animator above them.
    for (const RigWork& work : m_work) {
        const EntityId rig = scene.entityAt(work.entityIndex);
        m_poses.mapEntity(work.entityIndex, work.slice);
        // The rig itself can carry a skinned mesh (a one-mesh file whose rig is
        // rooted at the scene node), so it is checked like any other.
        checkSkinnedMesh(scene, resources, rig, *work.skeleton, seen);
        stampDescendants(scene, resources, rig, work, seen);
    }

    const float simDelta = ctx.clock.getSimDelta();
    const size_t grain = (totalBones < MIN_PARALLEL_BONES)
        ? m_work.size()
        : std::max<size_t>(1, m_work.size() / (ThreadPool::get().threadCount() + 1));

    {
        // Safe across threads because each iteration writes one Animator and one
        // disjoint slice of the pose arrays, and no slice is allocated past this
        // point - the same argument AnimationSystem's parallel pass makes.
        PROFILE_SCOPE("SkeletalAnimation/Evaluate");
        parallelFor(m_work.size(), grain, [&](size_t i) {
            const RigWork& work = m_work[i];
            Animator& animator  = animators->dataAt(work.animatorIndex);

            advancePlayback(animator,
                            work.clip     ? work.clip->duration     : 0.0f,
                            work.fadeClip ? work.fadeClip->duration : 0.0f,
                            simDelta);

            PoseSample sample;
            sample.clip     = work.clip;
            sample.time     = animator.time;
            sample.from     = work.fadeClip;
            sample.fromTime = animator.fadeTime;
            // advancePlayback clears the fade the moment it runs out, so a
            // duration of zero here means there is nothing left to blend.
            sample.weight   = (animator.fadeDuration > 0.0f)
                ? 1.0f - animator.fadeRemaining / animator.fadeDuration
                : 1.0f;

            composePose(*work.skeleton, sample, m_poses.writeTo(work.slice));
        });
    }
}

const AnimationClipAsset* SkeletalAnimationSystem::resolveClip(
    const ResourceManager& resources, const AnimationClipHandle& handle,
    const SkeletonAsset& skeleton, FaultsSeen& seen) {
    if (!handle || !resources.isAlive(handle)) return nullptr;

    // A clip's per-bone table is bound to one rig's bone order at cook time, so
    // it fits only a rig of that name and that length - a rig recooked longer
    // while the clip stayed current fails the second half alone. Playing either
    // one would pose the wrong joints from matching indices, so the bind pose
    // stands and the mismatch is named, which is the failure that actually
    // happens rather than a character that stands still for no stated reason.
    const AnimationClipAsset& clip = resources.get(handle);
    if (clip.skeleton == skeleton.name && clip.bones.size() == skeleton.bones.size()) return &clip;

    seen.clipMismatch = true;
    if (!m_clipMismatchLogged) {
        LOG_WARNING("Clip '%s' (rig '%s', %zu bones) does not fit rig '%s' (%zu bones) - "
                    "holding the bind pose",
                    clip.name.c_str(), clip.skeleton.c_str(), clip.bones.size(),
                    skeleton.name.c_str(), skeleton.bones.size());
        m_clipMismatchLogged = true;
    }
    return nullptr;
}

void SkeletalAnimationSystem::stampDescendants(Scene& scene, const ResourceManager& resources,
                                               EntityId entity, const RigWork& work,
                                               FaultsSeen& seen) {
    HierarchyOperations::forEachChild(scene, entity, [&](EntityId child) {
        // A nested rig owns its own subtree: it allocated a slice of its own,
        // and stamping through it would hand its meshes the wrong pose.
        if (scene.has<Animator>(child)) return;
        m_poses.mapEntity(child.index, work.slice);
        checkSkinnedMesh(scene, resources, child, *work.skeleton, seen);
        stampDescendants(scene, resources, child, work, seen);
    });
}

void SkeletalAnimationSystem::checkSkinnedMesh(const Scene& scene, const ResourceManager& resources,
                                               EntityId entity, const SkeletonAsset& skeleton,
                                               FaultsSeen& seen) {
    if (!scene.has<Mesh>(entity)) return;

    const Mesh& mesh = scene.get<Mesh>(entity);
    if (!mesh.mesh || !resources.isAlive(mesh.mesh)) return;

    const MeshAsset& asset = resources.get(mesh.mesh);
    if (asset.skin.empty()) return;

    if (asset.skeleton != skeleton.name) {
        seen.rigMismatch = true;
        if (!m_rigMismatchLogged) {
            LOG_WARNING("Mesh '%s' is skinned to rig '%s' but sits under '%s' - "
                        "its bone indices address the wrong joints",
                        asset.name.c_str(), asset.skeleton.c_str(), skeleton.name.c_str());
            m_rigMismatchLogged = true;
        }
    }

    // The palette already resolves a vertex into rig space, so the matrix that
    // multiplies it has to be the rig's world matrix. Import parents skinned
    // meshes to the rig at identity to make that true; hand-authoring can undo
    // it, and the result is a character transformed twice.
    if (scene.has<Transform>(entity) && !isIdentity(scene.get<Transform>(entity))) {
        seen.meshOffset = true;
        if (!m_meshOffsetLogged) {
            LOG_WARNING("Skinned mesh '%s' does not sit at its rig's origin - "
                        "skinned vertices are already in rig space, so its own "
                        "transform is applied twice", asset.name.c_str());
            m_meshOffsetLogged = true;
        }
    }
}

} // namespace Vkm::Engine

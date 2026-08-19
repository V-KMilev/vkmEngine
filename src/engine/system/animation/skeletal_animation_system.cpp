#define VKM_LOG_CATEGORY "ANIM"

#include "system/animation/skeletal_animation_system.h"

#include <algorithm>

#include "logger.h"

#include "core/clock.h"
#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animator.h"
#include "platform/threading/thread_pool.h"
#include "resource/asset/animation_clip_asset.h"
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

} // namespace

void SkeletalAnimationSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("SkeletalAnimationSystem");

    Scene& scene = ctx.scene;
    m_poses.clear();
    m_work.clear();
    ctx.poses = &m_poses;

    auto* animators = scene.storage<Animator>();
    if (!animators || animators->size() == 0) return;

    const ResourceManager& resources = ctx.resources;
    const auto animatorCount = static_cast<uint32_t>(animators->size());

    // Allocate. Serial, because each slice's range is a running total, and
    // because every handle has to be resolved before the parallel phase, which
    // never touches the ResourceManager.
    size_t totalBones = 0;
    bool mismatchSeen = false;
    for (uint32_t i = 0; i < animatorCount; ++i) {
        const Animator& animator = animators->dataAt(i);
        if (!animator.skeleton || !resources.isAlive(animator.skeleton)) continue;

        const SkeletonAsset& skeleton = resources.get(animator.skeleton);
        if (skeleton.bones.empty()) continue;

        RigWork work;
        work.animatorIndex = i;
        work.entityIndex   = animators->keyAt(i);
        work.skeleton      = &skeleton;

        if (animator.clip && resources.isAlive(animator.clip)) {
            const AnimationClipAsset& clip = resources.get(animator.clip);
            // A clip's per-bone table is bound to one rig's bone order at cook
            // time. Playing it on another rig would pose the wrong joints from
            // matching indices, so the bind pose stands and the mismatch is
            // named - which is the failure that actually happens, rather than a
            // character that stands still for no stated reason.
            if (clip.skeleton == skeleton.name) {
                work.clip = &clip;
            } else {
                mismatchSeen = true;
                if (!m_clipMismatchLogged) {
                    LOG_WARNING("Clip '%s' is bound to rig '%s' but plays on '%s' - holding the bind pose",
                        clip.name.c_str(), clip.skeleton.c_str(), skeleton.name.c_str());
                    m_clipMismatchLogged = true;
                }
            }
        }
        work.slice = m_poses.addSlice(static_cast<uint32_t>(skeleton.bones.size()));

        totalBones += skeleton.bones.size();
        m_work.push_back(work);
    }
    if (!mismatchSeen) m_clipMismatchLogged = false;
    if (m_work.empty()) return;

    // Map. A rig poses itself and everything under it, because import spawns a
    // mesh entity per aiMesh and a character is body plus clothes plus hair -
    // all of them driven by the one Animator above them.
    for (const RigWork& work : m_work) {
        m_poses.mapEntity(work.entityIndex, work.slice);
        stampDescendants(scene, scene.entityAt(work.entityIndex), work.slice);
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

            advancePlayback(animator, work.clip ? work.clip->duration : 0.0f, simDelta);
            composePose(*work.skeleton, work.clip, animator.time, m_poses.writeTo(work.slice));
        });
    }
}

void SkeletalAnimationSystem::stampDescendants(Scene& scene, EntityId entity, uint32_t slice) {
    HierarchyOperations::forEachChild(scene, entity, [&](EntityId child) {
        // A nested rig owns its own subtree: it allocated a slice of its own,
        // and stamping through it would hand its meshes the wrong pose.
        if (scene.has<Animator>(child)) return;
        m_poses.mapEntity(child.index, slice);
        stampDescendants(scene, child, slice);
    });
}

} // namespace Vkm::Engine

#define VKM_LOG_CATEGORY "ANIM"

#include "system/animation/bone_socket_system.h"

#include <cstdint>

#include <glm/glm.hpp>

#include "logger.h"

#include "debug/profiler.h"
#include "ecs/scene.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/core/hierarchy.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/resource_manager.h"
#include "system/animation/pose_buffer.h"

namespace Vkm::Engine {

namespace {

const char* nameOf(const Scene& scene, EntityId entity) {
    return scene.has<Name>(entity) ? scene.get<Name>(entity).value : "<unnamed>";
}

// The rig's linear name lookup, memoised on the socket. It re-runs when either
// half of the pairing changes - a different rig, or a different bone name - so
// SkeletonAsset::indexOf stays the once-per-pairing call it says it is instead
// of a hundred string compares per socket per frame. Failure is memoised too: a
// name the rig does not carry resolves to -1 once, rather than rescanning the
// whole bone list every frame to fail again.
void resolveBone(BoneSocket& socket, SkeletonHandle rig, const SkeletonAsset& skeleton) {
    if (socket.resolvedRig == rig && socket.resolvedName == socket.bone) return;

    socket.resolvedRig  = rig;
    socket.resolvedName = socket.bone;
    socket.boneIndex    = socket.bone.empty() ? -1 : skeleton.indexOf(socket.bone);
}

} // namespace

void BoneSocketSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("BoneSocketSystem");

    FaultsSeen seen;
    placeSockets(ctx, seen);

    // Each latch holds only while its fault is still there, so a socket that is
    // fixed and then broken again is reported again - which is why placeSockets
    // reports into seen and every exit it takes lands on these three lines.
    m_noPoseLogged   = seen.noPose;
    m_unrootedLogged = seen.unrooted;
    m_noBoneLogged   = seen.noBone;
}

void BoneSocketSystem::placeSockets(FrameContext& ctx, FaultsSeen& seen) {
    Scene& scene = ctx.scene;

    auto* sockets = scene.storage<BoneSocket>();
    if (!sockets || sockets->size() == 0) return;

    // A scheduling fault rather than an authoring one, and it disables every
    // socket in the scene at once, so it is named here rather than per entity.
    if (!ctx.poses) {
        seen.noPose = true;
        if (!m_noPoseLogged) {
            LOG_WARNING("%zu bone socket(s) but no pose was published this frame - "
                        "SkeletalAnimationSystem has to run before BoneSocketSystem",
                        sockets->size());
            m_noPoseLogged = true;
        }
        return;
    }

    const ResourceManager& resources = ctx.resources;
    const auto count = static_cast<uint32_t>(sockets->size());

    for (uint32_t i = 0; i < count; ++i) {
        BoneSocket& socket    = sockets->dataAt(i);
        const EntityId entity = scene.entityAt(sockets->keyAt(i));

        if (!scene.has<Transform>(entity)) {
            seen.unrooted = true;
            if (!m_unrootedLogged) {
                LOG_WARNING("Bone socket '%s' has no Transform to place",
                            nameOf(scene, entity));
                m_unrootedLogged = true;
            }
            continue;
        }

        // The parent is the rig, and has to be: what this writes is a local
        // transform, and HierarchySystem's parentWorld * local only reaches the
        // bone when the parent's world matrix is the frame the pose was composed
        // in. A socket hung deeper would be placed somewhere plausible and
        // wrong, which is the failure this whole subsystem is built to refuse.
        const EntityId rig = scene.has<Hierarchy>(entity)
            ? scene.get<Hierarchy>(entity).parent
            : EntityId{};
        if (!rig || !scene.has<Animator>(rig)) {
            seen.unrooted = true;
            if (!m_unrootedLogged) {
                LOG_WARNING("Bone socket '%s' is not a direct child of a rig - a socket hangs "
                            "off the entity carrying the Animator, not off a mesh under it",
                            nameOf(scene, entity));
                m_unrootedLogged = true;
            }
            continue;
        }

        const Animator& animator = scene.get<Animator>(rig);
        const PoseSlice* slice   = ctx.poses->sliceOf(rig.index);
        if (!slice || !animator.skeleton || !resources.isAlive(animator.skeleton)) {
            seen.noPose = true;
            if (!m_noPoseLogged) {
                LOG_WARNING("Bone socket '%s' hangs off rig '%s', which nothing posed this "
                            "frame - it stays where it last was",
                            nameOf(scene, entity), nameOf(scene, rig));
                m_noPoseLogged = true;
            }
            continue;
        }

        const SkeletonAsset& skeleton = resources.get(animator.skeleton);
        resolveBone(socket, animator.skeleton, skeleton);

        // The slice was allocated for this rig, this frame, so an index that
        // resolved against it is in range; the compare is what makes the read
        // below a fact rather than an argument about ordering.
        if (socket.boneIndex < 0 || static_cast<uint32_t>(socket.boneIndex) >= slice->count) {
            seen.noBone = true;
            if (!m_noBoneLogged) {
                if (socket.bone.empty()) {
                    LOG_WARNING("Bone socket '%s' names no bone - it stays where it is",
                                nameOf(scene, entity));
                } else {
                    LOG_WARNING("Bone socket '%s' names bone '%s', which rig '%s' does not "
                                "have - it stays where it is", nameOf(scene, entity),
                                socket.bone.c_str(), skeleton.name.c_str());
                }
                m_noBoneLogged = true;
            }
            continue;
        }

        const glm::mat4& bone =
            ctx.poses->global()[slice->first + static_cast<uint32_t>(socket.boneIndex)];
        scene.get<Transform>(entity) =
            Transform::fromModelMatrix(bone * Transform::computeModelMatrix(socket.offset));
    }
}

} // namespace Vkm::Engine

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
    const Name* name = scene.tryGet<Name>(entity);
    return name ? name->value : "<unnamed>";
}

/**
 * @brief Resolve a socket's bone index, memoising it on the socket.
 *
 * Failure is memoised too, so a missing name is not rescanned every frame.
 *
 * @param socket Socket whose boneIndex is brought up to date.
 * @param rig Skeleton the index is resolved against.
 * @param skeleton The asset @p rig names.
 */
void resolveBone(BoneSocket& socket, SkeletonHandle rig, const SkeletonAsset& skeleton) {
    if (socket.resolvedRig == rig && socket.resolvedName == socket.bone) return;

    socket.resolvedRig  = rig;
    socket.resolvedName = socket.bone;
    socket.boneIndex    = socket.bone.empty() ? -1 : skeleton.indexOf(socket.bone);
}

} // namespace

void BoneSocketSystem::update(FrameContext& ctx) {
    PROFILE_SCOPE("BoneSocketSystem");

    placeSockets(ctx);

    // Here rather than in placeSockets, so every exit it takes closes the pass.
    m_noPublishedPose.endPass();
    m_noTransform.endPass();
    m_unrooted.endPass();
    m_noSkeleton.endPass();
    m_bonelessSkeleton.endPass();
    m_noBone.endPass();
}

void BoneSocketSystem::placeSockets(FrameContext& ctx) {
    Scene& scene = ctx.scene;

    auto* sockets = scene.storage<BoneSocket>();
    if (!sockets || sockets->size() == 0) return;

    // A scheduling fault that disables every socket at once, so named here rather than per entity.
    if (!ctx.poses) {
        if (m_noPublishedPose.report()) {
            LOG_WARNING(
                "%zu bone socket(s) but no pose was published this frame - "
                "SkeletalAnimationSystem has to run before BoneSocketSystem",
                sockets->size()
            );
        }
        return;
    }

    const ResourceManager& resources = ctx.resources;
    const auto count = static_cast<uint32_t>(sockets->size());

    for (uint32_t i = 0; i < count; ++i) {
        BoneSocket& socket    = sockets->dataAt(i);
        const EntityId entity = scene.entityAt(sockets->keyAt(i));

        Transform* placed = scene.tryGet<Transform>(entity);
        if (!placed) {
            if (m_noTransform.report()) {
                LOG_WARNING("Bone socket '%s' has no Transform to place", nameOf(scene, entity));
            }
            continue;
        }

        // parentWorld * local reaches the bone only when the parent's world matrix is the pose's frame.
        const Hierarchy* link = scene.tryGet<Hierarchy>(entity);
        const EntityId   rig  = link ? link->parent : EntityId{};

        const Animator* animator = scene.tryGet<Animator>(rig);
        if (!animator) {
            if (m_unrooted.report()) {
                LOG_WARNING(
                    "Bone socket '%s' is not a direct child of a rig - a socket hangs "
                    "off the entity carrying the Animator, not off a mesh under it",
                    nameOf(scene, entity)
                );
            }
            continue;
        }

        if (!animator->skeleton || !resources.isAlive(animator->skeleton)) {
            if (m_noSkeleton.report()) {
                LOG_WARNING(
                    "Bone socket '%s' hangs off rig '%s', whose Animator names no "
                    "skeleton - it stays where it last was",
                    nameOf(scene, entity),
                    nameOf(scene, rig)
                );
            }
            continue;
        }

        const SkeletonAsset& skeleton = resources.get(animator->skeleton);
        if (skeleton.bones.empty()) {
            if (m_bonelessSkeleton.report()) {
                LOG_WARNING(
                    "Bone socket '%s' hangs off rig '%s', whose skeleton '%s' has no "
                    "bones - it stays where it last was",
                    nameOf(scene, entity),
                    nameOf(scene, rig),
                    skeleton.name().c_str()
                );
            }
            continue;
        }

        // No slice yet is not a fault: a rig spawned outside a tick is posed on the next one, and the
        // socket holds its last place until then.
        const PoseSlice* slice = ctx.poses->sliceOf(rig);
        if (!slice) continue;

        resolveBone(socket, animator->skeleton, skeleton);

        if (socket.boneIndex < 0 || static_cast<uint32_t>(socket.boneIndex) >= slice->count) {
            if (!m_noBone.report()) continue;
            if (socket.bone.empty()) {
                LOG_WARNING("Bone socket '%s' names no bone - it stays where it is", nameOf(scene, entity));
            } else {
                LOG_WARNING(
                    "Bone socket '%s' names bone '%s', which rig '%s' does not "
                    "have - it stays where it is",
                    nameOf(scene, entity),
                    socket.bone.c_str(),
                    skeleton.name().c_str()
                );
            }
            continue;
        }

        const glm::mat4& bone =
            ctx.poses->global()[slice->first + static_cast<uint32_t>(socket.boneIndex)];
        *placed = Transform::fromModelMatrix(bone * Transform::computeModelMatrix(socket.offset));
    }
}

} // namespace Vkm::Engine

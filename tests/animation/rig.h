#pragma once

// A rig and its clips made in code, as a game with no asset file makes one: three bones standing in
// a line up +Y, each a meter above its parent.

#include <cmath>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include "core/frame/pose_buffer.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/scene/transform.h"
#include "ecs/scene.h"
#include "resource/asset/animation_clip_asset.h"
#include "resource/asset/skeleton_asset.h"
#include "resource/resource_manager.h"

namespace Vkm::Test {

using namespace Vkm::Engine;

inline constexpr const char* RIG = "test:rig";

enum Bone : int32_t { ROOT = 0, UPPER = 1, LOWER = 2 };

/// root at the rig's origin, upper a meter over it, lower a meter over that.
inline SkeletonHandle addRig(ResourceManager& resources) {
    SkeletonAsset rig;
    rig.bones = {{"root", -1}, {"upper", ROOT}, {"lower", UPPER}};
    rig.bindPose.resize(3);
    rig.bindPose[UPPER].position = {0.0f, 1.0f, 0.0f};
    rig.bindPose[LOWER].position = {0.0f, 1.0f, 0.0f};
    for (const float up : {0.0f, 1.0f, 2.0f}) {
        rig.inverseBind.push_back(glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, -up, 0.0f)));
    }
    return resources.add(std::move(rig), RIG);
}

inline glm::quat aboutZ(float degrees) { return glm::angleAxis(
    glm::radians(degrees),
    glm::vec3(0.0f, 0.0f, 1.0f)
); }

/// A clip of @p seconds with no keys, for the caller to key.
inline AnimationClipAsset emptyClip(float seconds) {
    AnimationClipAsset clip;
    clip.skeleton = RIG;
    clip.duration = seconds;
    clip.bones.resize(3);
    return clip;
}

/// A clip turning upper about Z from @p from to @p to degrees over @p seconds.
inline AnimationClipHandle addTurn(
    ResourceManager& resources,
    const std::string& name,
    float from,
    float to,
    float seconds = 1.0f
) {
    AnimationClipAsset clip = emptyClip(seconds);
    clip.rotationTimes         = {0.0f, seconds};
    clip.rotations             = {aboutZ(from), aboutZ(to)};
    clip.bones[UPPER].rotation = {0, 2};
    return resources.add(std::move(clip), name);
}

/// A clip holding upper @p across meters along X from its bind place.
inline AnimationClipHandle addLean(ResourceManager& resources, const std::string& name, float across) {
    AnimationClipAsset clip = emptyClip(1.0f);
    clip.positionTimes         = {0.0f};
    clip.positions             = {glm::vec3(across, 1.0f, 0.0f)};
    clip.bones[UPPER].position = {0, 1};
    return resources.add(std::move(clip), name);
}

/// An entity carrying an Animator that plays @p clip on the rig, at @p at.
inline EntityId addCharacter(
    Scene& scene,
    SkeletonHandle rig,
    AnimationClipHandle clip,
    const glm::vec3& at = glm::vec3(0.0f)
) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = at;
    scene.add(id, pose);
    Animator animator;
    animator.skeleton = rig;
    animator.clip     = clip;
    scene.add(id, animator);
    return id;
}

/// Where bone @p bone of @p character's rig stands in the pose, in the rig's frame; NaN when unposed.
inline glm::mat4 boneIn(const PoseBuffer* poses, EntityId character, int32_t bone) {
    const PoseSlice* slice = poses ? poses->sliceOf(character) : nullptr;
    if (!slice || bone >= static_cast<int32_t>(slice->count)) return glm::mat4(std::nanf(""));
    return poses->global()[slice->first + static_cast<uint32_t>(bone)];
}

inline glm::vec3 placeOf(const glm::mat4& m) { return glm::vec3(m[3]); }

/// A static slab 40 m across whose top is at height 0, for a ragdoll to fall on.
inline EntityId addFloor(Scene& scene) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = {0.0f, -0.5f, 0.0f};
    scene.add(id, pose);
    Rigidbody body;
    body.motion = RigidbodyMotion::Static;
    scene.add(id, body);
    ColliderPart slab;
    slab.shape       = ColliderShape::Box;
    slab.halfExtents = {20.0f, 0.5f, 20.0f};
    Collider collider;
    collider.parts = {slab};
    scene.add(id, collider);
    return id;
}

} // namespace Vkm::Test

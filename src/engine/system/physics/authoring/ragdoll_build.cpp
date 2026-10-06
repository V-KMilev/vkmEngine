#define VKM_LOG_CATEGORY "PHYSICS"

#include "system/physics/authoring/ragdoll_build.h"

#include <cmath>
#include <string>
#include <vector>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include "logger.h"

#include "core/math/rotation.h"
#include "ecs/scene.h"
#include "ecs/component/core/name.h"
#include "ecs/component/core/transform.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/hierarchy_operations.h"
#include "ecs/physics_settings.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

// Model-space transform of every bone at bind; the format orders parents first, so one pass.
std::vector<glm::mat4> bindModel(const SkeletonAsset& rig) {
    std::vector<glm::mat4> model(rig.bones.size(), glm::mat4(1.0f));
    for (size_t i = 0; i < rig.bones.size(); ++i) {
        const glm::mat4 local = i < rig.bindPose.size()
            ? Transform::computeModelMatrix(rig.bindPose[i])
            : glm::mat4(1.0f);
        const int32_t parent = rig.bones[i].parent;
        model[i] = parent >= 0 ? model[parent] * local : local;
    }
    return model;
}

// The first child of each bone, or -1 for a tip, which has nothing to span.
std::vector<int32_t> firstChildren(const SkeletonAsset& rig) {
    std::vector<int32_t> child(rig.bones.size(), -1);
    for (size_t i = 0; i < rig.bones.size(); ++i) {
        const int32_t parent = rig.bones[i].parent;
        if (parent < 0 || child[parent] >= 0) continue;
        child[parent] = static_cast<int32_t>(i);
    }
    return child;
}

// Drops a rig's bone namespace (Mixamo's "mixamorig:"), which is noise in an entity's label.
const char* displayBoneName(const std::string& name) {
    const size_t colon = name.rfind(':');
    if (colon == std::string::npos || colon + 1 >= name.size()) return name.c_str();
    return name.c_str() + colon + 1;
}

} // namespace

void clearRagdoll(Scene& scene, EntityId rigEntity) {
    const Ragdoll* held = scene.tryGet<Ragdoll>(rigEntity);
    if (!held) return;

    const Ragdoll& ragdoll = *held;
    // The bones first, by hand: a scene may have moved one out from under the node below.
    for (const RagdollBone& bone : ragdoll.bones) {
        if (bone.body && scene.isAlive(bone.body)) scene.destroyEntity(bone.body);
    }
    if (ragdoll.root && scene.isAlive(ragdoll.root)) {
        HierarchyOperations::destroyHierarchy(scene, ragdoll.root);
    }

    // The bit goes back: the owner's mask is serialized, and left cleared it ignores a whole layer.
    if (Rigidbody* owner = scene.tryGet<Rigidbody>(rigEntity)) {
        owner->collidesWith |= ragdoll.boneLayer;
    }
    scene.remove<Ragdoll>(rigEntity);
}

uint32_t buildRagdoll(
    Scene& scene,
    EntityId rigEntity,
    const SkeletonAsset& rig,
    const RagdollSettings& settings
) {
    if (rig.bones.empty()) return 0;

    const EntityId poseEntity =
        HierarchyOperations::findInSelfOrDescendants<Animator>(scene, rigEntity);
    const EntityId frameEntity = poseEntity ? poseEntity : rigEntity;
    if (!scene.has<Transform>(frameEntity)) return 0;

    clearRagdoll(scene, rigEntity);

    const glm::mat4 rigModel =
        HierarchyOperations::computeWorldMatrix(scene, frameEntity);

    const std::vector<glm::mat4> bind = bindModel(rig);
    const std::vector<int32_t> child = firstChildren(rig);

    // Two passes: a body's mass is its share of the whole ragdoll's volume.
    std::vector<EntityId> bodyOf(rig.bones.size(), EntityId{});
    std::vector<float> volumes(rig.bones.size(), 0.0f);

    Ragdoll ragdoll;
    float totalVolume = 0.0f;

    for (size_t i = 0; i < rig.bones.size(); ++i) {
        if (child[i] < 0) continue;

        const glm::vec3 head = glm::vec3(rigModel * bind[i][3]);
        const glm::vec3 tail = glm::vec3(rigModel * bind[child[i]][3]);
        const glm::vec3 along = tail - head;
        const float length = glm::length(along);
        if (length < settings.minBoneLength) continue;

        const float radius = glm::max(length * settings.thickness, Physics::MIN_HALF_EXTENT);
        // The caps add the radius, so the half-height stops short of the bone's ends, not wedging limbs.
        const float halfHeight = glm::max(length * 0.5f - radius, Physics::MIN_HALF_EXTENT);

        const EntityId body = scene.createEntity();
        scene.add(body, makeName(displayBoneName(rig.bones[i].name)));

        // A capsule stands along local +Y.
        const glm::quat aim = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f), along / length);
        const glm::vec3 centre = (head + tail) * 0.5f;

        Transform transform;
        transform.position = centre;
        transform.rotation = aim;
        scene.add(body, std::move(transform));

        ColliderPart part;
        part.shape = ColliderShape::Capsule;
        part.radius = radius;
        part.halfHeight = halfHeight;
        Collider collider;
        collider.parts = { part };
        scene.add(body, std::move(collider));

        bodyOf[i] = body;

        // Mass follows size, so a forearm does not swing a torso around.
        const float r2 = radius * radius;
        volumes[i] = glm::pi<float>() * r2 * (2.0f * halfHeight)
            + (4.0f / 3.0f) * glm::pi<float>() * r2 * radius;
        totalVolume += volumes[i];

        RagdollBone entry;
        entry.bone = static_cast<int32_t>(i);
        entry.body = body;
        // The body is centred on the limb, the bone at its head.
        const glm::mat4 bodyModel =
            glm::translate(glm::mat4(1.0f), centre) * glm::mat4_cast(aim);
        entry.bodyFromBone = glm::inverse(bodyModel) * (rigModel * bind[i]);
        ragdoll.bones.push_back(entry);
    }

    if (ragdoll.bones.empty()) {
        // Logged: the old ragdoll is already gone by here.
        LOG_WARNING(
            "buildRagdoll: no bone of '%s' is longer than %.3f m, so "
            "there is nothing to build; the previous ragdoll is cleared",
            rig.name().c_str(),
            static_cast<double>(settings.minBoneLength)
        );
        return 0;
    }

    // The nearest ancestor with a body, so a skipped short bone does not break the chain.
    const auto bodiedAncestor = [&](int32_t bone) {
        int32_t ancestor = rig.bones[bone].parent;
        while (ancestor >= 0 && !bodyOf[ancestor]) ancestor = rig.bones[ancestor].parent;
        return ancestor;
    };

    for (const RagdollBone& entry : ragdoll.bones) {
        Rigidbody body;
        body.mass = totalVolume > glm::epsilon<float>()
            ? settings.mass * (volumes[entry.bone] / totalVolume)
            : settings.mass / static_cast<float>(ragdoll.bones.size());
        body.canSleep = true;
        body.layer = settings.boneLayer;
        body.collidesWith &= ~settings.boneLayer;
        scene.add(entry.body, std::move(body));
    }

    // What each joint carries: every body beyond it, by mass and by where that mass sits.
    std::vector<float> carriedMass(rig.bones.size(), 0.0f);
    std::vector<glm::vec3> carriedMoment(rig.bones.size(), glm::vec3(0.0f));
    for (const RagdollBone& entry : ragdoll.bones) {
        const float mass = scene.get<Rigidbody>(entry.body).mass;
        const glm::vec3 at = scene.get<Transform>(entry.body).position;
        for (int32_t bone = entry.bone; bone >= 0; bone = bodiedAncestor(bone)) {
            carriedMass[bone] += mass;
            carriedMoment[bone] += mass * at;
        }
    }
    const float gravity = glm::length(PhysicsSettings{}.gravity);

    for (const RagdollBone& entry : ragdoll.bones) {
        const int32_t ancestor = bodiedAncestor(entry.bone);
        if (ancestor < 0) continue;

        const glm::vec3 pivot = glm::vec3(rigModel * bind[entry.bone][3]);
        const Transform& self = scene.get<Transform>(entry.body);
        const Transform& up   = scene.get<Transform>(bodyOf[ancestor]);

        Joint joint;
        joint.type = JointType::Point;
        joint.connected = bodyOf[ancestor];
        // Anchors are local: the pivot in each body's frame.
        joint.anchor = glm::conjugate(self.rotation) * (pivot - self.position);
        joint.connectedAnchor =
            glm::conjugate(up.rotation) * (pivot - up.position);
        joint.stiffness = settings.stiffness;

        // muscle of what holds the carried mass level about the pivot.
        const float carried = carriedMass[entry.bone];
        const glm::vec3 centre = carriedMoment[entry.bone] / carried;
        joint.holdTorque = settings.muscle * carried * gravity * glm::length(centre - pivot);
        scene.add(entry.body, std::move(joint));
    }

    if (Rigidbody* owner = scene.tryGet<Rigidbody>(rigEntity)) {
        owner->collidesWith &= ~settings.boneLayer;
    }

    // Parented last: everything above works in world space, so the frame changes once.
    const EntityId root = scene.createEntity();
    scene.add(root, makeName("Ragdoll"));
    scene.add(root, Transform{});
    HierarchyOperations::setParent(scene, root, rigEntity);
    ragdoll.root = root;
    ragdoll.boneLayer = settings.boneLayer;
    ragdoll.rigScale = glm::length(glm::vec3(rigModel[0]));

    const glm::mat4 toParent =
        glm::inverse(HierarchyOperations::computeWorldMatrix(scene, root));
    for (const RagdollBone& entry : ragdoll.bones) {
        Transform& transform = scene.get<Transform>(entry.body);
        const glm::mat4 world =
            glm::translate(glm::mat4(1.0f), transform.position)
            * glm::mat4_cast(transform.rotation);
        const glm::mat4 local = toParent * world;

        transform.position = glm::vec3(local[3]);
        transform.rotation = Math::worldRotationOf(local);
        HierarchyOperations::setParent(scene, entry.body, root);
    }

    const uint32_t count = static_cast<uint32_t>(ragdoll.bones.size());
    scene.add(rigEntity, std::move(ragdoll));
    return count;
}

} // namespace Vkm::Engine

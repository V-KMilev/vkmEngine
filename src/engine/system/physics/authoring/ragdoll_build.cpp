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
#include "system/hierarchy/hierarchy_operations.h"
#include "system/physics/tolerance.h"

namespace Vkm::Engine {

namespace {

// Model-space transform of every bone at bind, walked parent before child -
// which the format guarantees, so one pass is enough.
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

// The first child of each bone, or -1. A limb's capsule spans a bone to its
// child, so a bone with none is a tip and has nothing to span.
std::vector<int32_t> firstChildren(const SkeletonAsset& rig) {
    std::vector<int32_t> child(rig.bones.size(), -1);
    for (size_t i = 0; i < rig.bones.size(); ++i) {
        const int32_t parent = rig.bones[i].parent;
        if (parent < 0 || child[parent] >= 0) continue;
        child[parent] = static_cast<int32_t>(i);
    }
    return child;
}

// Rig formats namespace their bones - Mixamo stamps every one with
// "mixamorig:" - which is identity to the skeleton and noise in a hierarchy
// panel. The entity name is a label, so it carries the part that reads.
const char* displayBoneName(const std::string& name) {
    const size_t colon = name.rfind(':');
    if (colon == std::string::npos || colon + 1 >= name.size()) return name.c_str();
    return name.c_str() + colon + 1;
}

} // namespace

void clearRagdoll(Scene& scene, EntityId rigEntity) {
    if (!scene.has<Ragdoll>(rigEntity)) return;
    const Ragdoll& ragdoll = scene.get<Ragdoll>(rigEntity);
    // The bones first and by hand: they are normally children of the node
    // below, but a scene is free to have moved one, and a bone left behind is
    // a limb lying on the floor with nothing to pick it up.
    for (const RagdollBone& bone : ragdoll.bones) {
        if (bone.body && scene.isAlive(bone.body)) scene.destroyEntity(bone.body);
    }
    if (ragdoll.root && scene.isAlive(ragdoll.root)) {
        HierarchyOperations::destroyHierarchy(scene, ragdoll.root);
    }

    // The bit the build took out of the owner's mask goes back: it is an authored,
    // serialized field, and leaving it cleared means a character that once had a
    // ragdoll quietly stops colliding with a whole layer.
    if (scene.has<Rigidbody>(rigEntity)) {
        scene.get<Rigidbody>(rigEntity).collidesWith |= ragdoll.boneLayer;
    }
    scene.remove<Ragdoll>(rigEntity);
}

uint32_t buildRagdoll(Scene& scene, EntityId rigEntity, const SkeletonAsset& rig,
                      const RagdollSettings& settings) {
    if (rig.bones.empty()) return 0;

    // The frame the bones are composed in belongs to whatever carries the
    // Animator, which an import puts under the entity the physics is on; the
    // selected entity's frame builds the right shape in the wrong place.
    const EntityId poseEntity =
        HierarchyOperations::findInSelfOrDescendants<Animator>(scene, rigEntity);
    const EntityId frameEntity = poseEntity ? poseEntity : rigEntity;
    if (!scene.has<Transform>(frameEntity)) return 0;

    clearRagdoll(scene, rigEntity);

    const glm::mat4 rigModel =
        HierarchyOperations::computeWorldMatrix(scene, frameEntity);

    const std::vector<glm::mat4> bind = bindModel(rig);
    const std::vector<int32_t> child = firstChildren(rig);

    // Two passes: every body is created before any joint, because a joint names
    // a body and half of them name a parent built after their own bone.
    std::vector<EntityId> bodyOf(rig.bones.size(), EntityId{});
    std::vector<EntityId> bornThisBuild;
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

        const float radius = glm::max(length * settings.thickness,
                                      Physics::MIN_HALF_EXTENT);
        // The capsule's segment is the bone; the caps are what the radius adds,
        // so the half-height stops short of the ends rather than overshooting
        // them and wedging every limb into its neighbour.
        const float halfHeight = glm::max(length * 0.5f - radius,
                                          Physics::MIN_HALF_EXTENT);

        const EntityId body = scene.createEntity();
        scene.add(body, makeName(displayBoneName(rig.bones[i].name)));
        bornThisBuild.push_back(body);

        // A capsule stands along local +Y, so the body turns down the bone by the
        // shortest arc from +Y. A look-at would need an up vector to be wrong
        // about, and a spine points along the only one worth guessing.
        const glm::quat aim = glm::rotation(glm::vec3(0.0f, 1.0f, 0.0f),
                                            along / length);
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

        // Volume of the capsule, so mass follows size instead of every limb
        // weighing the same and a forearm swinging a torso around.
        const float r2 = radius * radius;
        volumes[i] = glm::pi<float>() * r2 * (2.0f * halfHeight)
                   + (4.0f / 3.0f) * glm::pi<float>() * r2 * radius;
        totalVolume += volumes[i];

        RagdollBone entry;
        entry.bone = static_cast<int32_t>(i);
        entry.body = body;
        // Where the bone sits in the body's frame, kept because the two differ:
        // the body is centred on the limb and the bone is at its head.
        const glm::mat4 bodyModel =
            glm::translate(glm::mat4(1.0f), centre) * glm::mat4_cast(aim);
        entry.boneFromBody = glm::inverse(bodyModel) * (rigModel * bind[i]);
        ragdoll.bones.push_back(entry);
    }

    if (ragdoll.bones.empty()) {
        // Said rather than returned quietly: the old ragdoll is already gone by
        // here, so a silent zero is an author watching their skeleton vanish
        // with nothing to explain it.
        LOG_WARNING("buildRagdoll: no bone of '%s' is longer than %.3f m, so "
                    "there is nothing to build; the previous ragdoll is cleared",
                    rig.name().c_str(), static_cast<double>(settings.minBoneLength));
        return 0;
    }

    for (const RagdollBone& entry : ragdoll.bones) {
        Rigidbody body;
        body.mass = totalVolume > glm::epsilon<float>()
            ? settings.mass * (volumes[entry.bone] / totalVolume)
            : settings.mass / static_cast<float>(ragdoll.bones.size());
        body.canSleep = true;
        body.layer = settings.boneLayer;
        // And not with each other: limbs are built overlapping, each spanning a
        // bone to its child, so a self-colliding ragdoll starts every activation
        // by resolving interpenetration it was authored with.
        body.collidesWith &= ~settings.boneLayer;
        scene.add(entry.body, std::move(body));

        // Joined to the nearest ancestor that got a body: a bone skipped for
        // being too short must not break the chain, or the limb below it falls
        // away on its own.
        int32_t ancestor = rig.bones[entry.bone].parent;
        while (ancestor >= 0 && !bodyOf[ancestor]) {
            ancestor = rig.bones[ancestor].parent;
        }
        if (ancestor < 0) continue;

        const glm::vec3 pivot = glm::vec3(rigModel * bind[entry.bone][3]);
        const Transform& self = scene.get<Transform>(entry.body);
        const Transform& up   = scene.get<Transform>(bodyOf[ancestor]);

        Joint joint;
        joint.type = JointType::Point;
        joint.connected = bodyOf[ancestor];
        // Anchors are local, so each is the pivot brought back into its own
        // body's frame - which is what keeps the joint at the actual joint as
        // both bodies move.
        joint.anchor = glm::conjugate(self.rotation) * (pivot - self.position);
        joint.connectedAnchor =
            glm::conjugate(up.rotation) * (pivot - up.position);
        joint.stiffness = settings.stiffness;
        scene.add(entry.body, std::move(joint));
    }

    // The one relationship a build can decide on its own: a rig's bones and the
    // body they hang off are never two things that should push each other.
    if (scene.has<Rigidbody>(rigEntity)) {
        scene.get<Rigidbody>(rigEntity).collidesWith &= ~settings.boneLayer;
    }

    // Parented last, with the poses converted then: everything above works in
    // world space, so the frame change happens once. The cost is that the entity a
    // ragdoll is built on must not carry a scale - the solver ignores it.
    const EntityId root = scene.createEntity();
    scene.add(root, makeName("Ragdoll"));
    scene.add(root, Transform{});
    HierarchyOperations::setParent(scene, root, rigEntity);
    ragdoll.root = root;
    ragdoll.boneLayer = settings.boneLayer;

    const glm::mat4 toParent =
        glm::inverse(HierarchyOperations::computeWorldMatrix(scene, root));
    for (EntityId body : bornThisBuild) {
        Transform& transform = scene.get<Transform>(body);
        const glm::mat4 world =
            glm::translate(glm::mat4(1.0f), transform.position)
            * glm::mat4_cast(transform.rotation);
        const glm::mat4 local = toParent * world;

        transform.position = glm::vec3(local[3]);
        transform.rotation = Math::worldRotationOf(local);
        HierarchyOperations::setParent(scene, body, root);
    }

    const uint32_t count = static_cast<uint32_t>(ragdoll.bones.size());
    scene.add(rigEntity, std::move(ragdoll));
    return count;
}

} // namespace Vkm::Engine

#include "physics/physics_support.h"

#include <vector>

#include "debug/engine_error_log.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/animation/animator.h"
#include "ecs/component/physics/joint.h"
#include "ecs/hierarchy_operations.h"
#include "system/animation/pose_buffer.h"
#include "system/animation/ragdoll_pose.h"
#include "system/hierarchy/hierarchy_system.h"
#include "system/physics/authoring/ragdoll_build.h"
#include "system/physics/physics_system.h"
#include "system/physics/query/query.h"

namespace {

void testRagdoll() {
    std::printf("Ragdoll:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});

    const EntityId rig = scene.createEntity();
    scene.add<Transform>(rig, Transform{});

    const SkeletonAsset skeleton = makeTestRig();
    const uint32_t built = buildRagdoll(scene, rig, skeleton);

    // A limb spans a bone to its first child, so the childless chest and feet get nothing.
    check("a ragdoll is built from the rig", built == 4);
    check(
        "  and the component records it",
        scene.has<Ragdoll>(rig) && scene.get<Ragdoll>(rig).bones.size() == 4
    );

    // Every simulated bone got a body with a capsule and a mass.
    bool wellFormed = true;
    float totalMass = 0.0f;
    for (const RagdollBone& bone : scene.get<Ragdoll>(rig).bones) {
        if (!scene.has<Collider>(bone.body) || !scene.has<Rigidbody>(bone.body)) {
            wellFormed = false;
            continue;
        }
        const Collider& collider = scene.get<Collider>(bone.body);
        if (collider.parts.empty() || collider.parts[0].shape != ColliderShape::Capsule) {
            wellFormed = false;
        }
        totalMass += scene.get<Rigidbody>(bone.body).mass;
    }
    check("  every bone got a capsule and a mass", wellFormed);
    check("  sharing out the total rather than each taking it", std::fabs(totalMass - 70.0f) < 1.0f);

    // Three joints for four bodies: the hips are the root and hang from nothing.
    int joints = 0;
    for (const RagdollBone& bone : scene.get<Ragdoll>(rig).bones) {
        if (scene.has<Joint>(bone.body)) ++joints;
    }
    check("  and a joint for every bone but the root", joints == 3);

    // Let it fall: held together it lands as a heap; not held, the pieces scatter.
    const EntityId hipBody = scene.get<Ragdoll>(rig).bones[0].body;
    const glm::vec3 hips = scene.get<Transform>(hipBody).position;
    simulate(scene, 240);

    float lowest = 100.0f;
    float highest = -100.0f;
    float spread = 0.0f;
    for (const RagdollBone& bone : scene.get<Ragdoll>(rig).bones) {
        const glm::vec3 at = scene.get<Transform>(bone.body).position;
        lowest = glm::min(lowest, at.y);
        highest = glm::max(highest, at.y);
        spread = glm::max(spread, glm::length(glm::vec2(at.x, at.z)));
    }

    check("it falls", lowest < hips.y);
    check("  and stops on the floor rather than through it", lowest > -1.0f);
    check("  landing as one body, not several", spread < 2.0f);
    check("  with nothing left standing", highest < 1.5f);

    // Clearing takes the bodies with it, so a rebuild leaves no second skeleton.
    const size_t before = scene.entityCount();
    clearRagdoll(scene, rig);
    check(
        "clearing a ragdoll destroys its bodies",
        !scene.has<Ragdoll>(rig) && scene.entityCount() == before - 5
    );

    // As does destroying the character: the hierarchy takes the group node and its
    // bones, but a bone moved out of the group is the observer's job - so this must
    // hold either way.
    Scene owned;
    const EntityId doomed = owned.createEntity();
    owned.add<Transform>(doomed, Transform{});
    buildRagdoll(owned, doomed, skeleton);

    RagdollSystem lifetime;
    TestFrame ownedFrame(owned);
    FrameContext& ownedCtx = ownedFrame.ctx;
    lifetime.init(ownedCtx);

    const size_t withRagdoll = owned.entityCount();
    check("a built ragdoll adds its bones to the scene", withRagdoll == 6);
    owned.destroyEntity(doomed);
    check("destroying the character takes its bones with it", owned.entityCount() == 0);
    lifetime.shutdown();
}

// Dying twice. A rested ragdoll is asleep, and held inactive its bones are kinematic,
// which skips the sleep test rather than clearing it. Handed back still asleep,
// PhysicsSystem::gatherBodies freezes them, so the second death never falls. Holding
// writes no motion, so they come back as they were built.
// The hold torques the build gives the joints, in bone order.
std::vector<float> builtHolds(float muscle) {
    Scene scene;
    const EntityId rig = scene.createEntity();
    scene.add<Transform>(rig, Transform{});
    RagdollSettings settings;
    settings.muscle = muscle;
    buildRagdoll(scene, rig, makeTestRig(), settings);

    std::vector<float> holds;
    for (const RagdollBone& bone : scene.get<Ragdoll>(rig).bones) {
        if (const Joint* joint = scene.tryGet<Joint>(bone.body)) holds.push_back(joint->holdTorque);
    }
    return holds;
}

void testARagdollsMuscleSetsItsHolds() {
    std::printf("Ragdoll muscle:\n");

    const std::vector<float> limp = builtHolds(0.0f);
    const std::vector<float> toned = builtHolds(0.4f);
    const std::vector<float> stiff = builtHolds(0.8f);

    bool limpIsFree = !limp.empty();
    for (const float hold : limp) limpIsFree = limpIsFree && hold == 0.0f;
    check("no muscle leaves every joint free", limpIsFree);

    bool toneHolds = toned.size() == limp.size() && !toned.empty();
    bool scales = toneHolds && stiff.size() == toned.size();
    for (size_t i = 0; scales && i < toned.size(); ++i) {
        toneHolds = toneHolds && toned[i] > 0.0f;
        scales = std::fabs(stiff[i] - 2.0f * toned[i]) <= 1e-3f * stiff[i];
    }
    check("  some gives every joint a hold", toneHolds);
    check("  in proportion to the muscle", scales);
}

void testRagdollFallsTwice() {
    std::printf("A ragdoll that falls twice:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId rig = scene.createEntity();
    Transform at;
    at.position = {0.0f, 3.0f, 0.0f};
    scene.add<Transform>(rig, std::move(at));
    check("a rig to knock down", buildRagdoll(scene, rig, makeTestRig()) == 4);

    RagdollSystem ragdolls;
    TestFrame frame(scene);
    FrameContext& ctx = frame.ctx;
    ragdolls.init(ctx);

    const EntityId hips = scene.get<Ragdoll>(rig).bones[0].body;

    // A rested ragdoll's state, set rather than waited for: the test is what holding
    // it and handing it back do about it.
    for (const RagdollBone& bone : scene.get<Ragdoll>(rig).bones) {
        scene.get<Rigidbody>(bone.body).sleeping = true;
    }
    ragdolls.fixedUpdate(ctx);
    scene.get<Ragdoll>(rig).active = true;
    ragdolls.fixedUpdate(ctx);

    check("a rested ragdoll held and handed back has its bones awake", !scene.get<Rigidbody>(hips).sleeping);
    check("  and as they were built", scene.get<Rigidbody>(hips).motion == RigidbodyMotion::Dynamic);

    const float before =
        HierarchyOperations::computeWorldMatrix(scene, hips)[3][1];
    for (int i = 0; i < 200; ++i) {
        ragdolls.fixedUpdate(ctx);
        simulate(scene, 1);
    }
    const float after =
        HierarchyOperations::computeWorldMatrix(scene, hips)[3][1];
    check("  so it falls rather than hanging where it slept", after < before - 0.5f);

    ragdolls.shutdown();
}

// Switched on while the character moves. Bones posed with zero velocity would hand the
// solver a skeleton at rest, and a character felled at a run would drop where it stood.
void testARagdollInheritsTheMotionItWasPosedWith() {
    std::printf("A ragdoll switched on mid-stride:\n");

    constexpr float SPEED = 3.0f;

    Scene scene;
    const EntityId rig = scene.createEntity();
    Transform at;
    at.position = {0.0f, 4.0f, 0.0f};
    scene.add<Transform>(rig, std::move(at));
    scene.add<Animator>(rig, Animator{});

    const SkeletonAsset skeleton = makeTestRig();
    check("a rig to run with", buildRagdoll(scene, rig, skeleton) == 4);

    // The bind pose, in rig model space, as the animation system publishes it.
    PoseBuffer poses;
    poses.clear();
    const uint32_t slice = poses.addSlice(static_cast<uint32_t>(skeleton.bones.size()));
    poses.mapEntity(rig, slice);
    const PoseWrite pose = poses.writeTo(slice);
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), skeleton.bindPose[i].position);
        const int32_t parent = skeleton.bones[i].parent;
        pose.global[i] = parent < 0 ? local : pose.global[static_cast<size_t>(parent)] * local;
    }

    RagdollSystem ragdolls;
    PhysicsSystem physics;
    HierarchySystem hierarchy;
    TestFrame frame(scene);
    FrameContext& ctx = frame.ctx;
    ctx.poses = &poses;
    ragdolls.init(ctx);

    const float dt = ctx.clock.getFixedStep();
    const EntityId hips = scene.get<Ragdoll>(rig).bones[0].body;

    // Carried along +X by the character, as a clip playing on a moving body is.
    for (int tick = 0; tick < 10; ++tick) {
        scene.get<Transform>(rig).position.x += SPEED * dt;
        ragdolls.fixedUpdate(ctx);
        physics.fixedUpdate(ctx);
        hierarchy.update(ctx);
    }
    check(
        "a driven bone moves at the speed its pose does",
        std::fabs(scene.get<Rigidbody>(hips).linearVelocity.x - SPEED) < 0.01f
    );

    scene.get<Ragdoll>(rig).active = true;
    const float before = HierarchyOperations::computeWorldMatrix(scene, hips)[3][0];
    for (int tick = 0; tick < 10; ++tick) {
        ragdolls.fixedUpdate(ctx);
        physics.fixedUpdate(ctx);
        hierarchy.update(ctx);
    }
    const float travelled = HierarchyOperations::computeWorldMatrix(scene, hips)[3][0] - before;
    check("  and once handed to the solver it carries on forward", travelled > 0.8f * SPEED * 10.0f * dt);

    ragdolls.shutdown();
}

// A ragdoll records its rig's scale when built; a rig re-imported in other units poses
// every bone a hundred times too near its body. The system says so, once, and only of
// the rig: a pose scaling one bone (a first-person body hiding its head) is not stale.
void testARagdollBuiltAtAnotherScaleSaysSo() {
    std::printf("A ragdoll whose rig changed scale under it:\n");

    Scene scene;
    const EntityId rig = scene.createEntity();
    scene.add<Transform>(rig, Transform{});
    scene.add<Animator>(rig, Animator{});
    const SkeletonAsset skeleton = makeTestRig();
    check("a rig to rescale", buildRagdoll(scene, rig, skeleton) == 4);

    PoseBuffer poses;
    poses.clear();
    const uint32_t slice = poses.addSlice(static_cast<uint32_t>(skeleton.bones.size()));
    poses.mapEntity(rig, slice);
    const PoseWrite pose = poses.writeTo(slice);
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        const glm::mat4 local = glm::translate(glm::mat4(1.0f), skeleton.bindPose[i].position);
        const int32_t parent = skeleton.bones[i].parent;
        pose.global[i] = parent < 0 ? local : pose.global[static_cast<size_t>(parent)] * local;
    }

    EngineErrorLog errors;
    setErrorSink(&errors);
    RagdollSystem ragdolls;
    TestFrame frame(scene);
    frame.ctx.poses = &poses;
    ragdolls.init(frame.ctx);

    ragdolls.fixedUpdate(frame.ctx);
    check("built at its rig's scale, nothing is said", errors.entries().empty());

    const size_t limb = static_cast<size_t>(scene.get<Ragdoll>(rig).bones.front().bone);
    const glm::mat4 held = pose.global[limb];
    pose.global[limb] = held * glm::scale(glm::mat4(1.0f), glm::vec3(0.001f));
    ragdolls.fixedUpdate(frame.ctx);
    check("  nor of a pose that scales a bone", errors.entries().empty());
    pose.global[limb] = held;

    scene.get<Transform>(rig).scale = glm::vec3(100.0f);
    for (int tick = 0; tick < 5; ++tick) ragdolls.fixedUpdate(frame.ctx);
    check(
        "rescaled under it, it is named",
        errors.entries().size() == 1 && errors.entries()[0].category == "Physics"
    );
    check("  once, not every tick", !errors.entries().empty() && errors.entries()[0].repeatCount == 1);

    setErrorSink(nullptr);
    ragdolls.shutdown();
}

// One ragdoll's bodies, through gatherRagdollBodies.
std::vector<RagdollBodyPose> bodiesOf(const Scene& scene, const Ragdoll& ragdoll) {
    std::vector<RagdollBodyPose> bodies;
    gatherRagdollBodies(scene, ragdoll, bodies);
    return bodies;
}

// Bones are woken only while held: woken every active tick, their sleep timers reset
// faster than resting fills them, and a still body simulates for as long as the level
// lasts.
void testAnActiveRagdollFallsAsleep() {
    std::printf("A ragdoll lying still:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});
    const EntityId rig = scene.createEntity();
    scene.add<Transform>(rig, Transform{});
    check("a rig to lay down", buildRagdoll(scene, rig, makeTestRig()) == 4);

    RagdollSystem ragdolls;
    PhysicsSystem physics;
    HierarchySystem hierarchy;
    TestFrame frame(scene);
    FrameContext& ctx = frame.ctx;
    ragdolls.init(ctx);

    // Never held, so a sleeping bone stays asleep when switched on.
    scene.get<Ragdoll>(rig).active = true;
    const EntityId hips = scene.get<Ragdoll>(rig).bones[0].body;
    scene.get<Rigidbody>(hips).sleeping = true;
    ragdolls.fixedUpdate(ctx);
    check("  an active ragdoll's sleeping bone is left asleep", scene.get<Rigidbody>(hips).sleeping);
    scene.get<Rigidbody>(hips).sleeping = false;

    // One system across the run, as the app has, so each tick starts from the last
    // one's impulses.
    bool asleep = false;
    for (int tick = 0; tick < 1200 && !asleep; ++tick) {
        ragdolls.fixedUpdate(ctx);
        physics.fixedUpdate(ctx);
        hierarchy.update(ctx);
        asleep = scene.get<Rigidbody>(hips).sleeping;
    }
    check("  and one that has fallen and come to rest falls asleep", asleep);

    ragdolls.shutdown();
}

// The half on screen: the rig follows the bodies, which is what anyone sees.
void testRagdollPose() {
    std::printf("Ragdoll pose:\n");

    Scene scene;
    const EntityId rig = scene.createEntity();
    scene.add<Transform>(rig, Transform{});

    const SkeletonAsset skeleton = makeTestRig();
    check("a ragdoll to pose from", buildRagdoll(scene, rig, skeleton) == 4);

    const Ragdoll& ragdoll = scene.get<Ragdoll>(rig);
    const uint32_t bones = static_cast<uint32_t>(skeleton.bones.size());

    PoseBuffer poses;
    poses.clear();
    const uint32_t slice = poses.addSlice(bones);

    // Composed where the build left the bodies, so every bone lands on its bind position:
    // the recorded offsets undo the gap between a limb's middle and its head.
    composeRagdollPose(
        ragdoll,
        bodiesOf(scene, ragdoll).data(),
        skeleton,
        glm::mat4(1.0f),
        poses.writeTo(slice)
    );

    const std::vector<glm::mat4>& global = poses.global();
    // hips sits a metre up in the rig, and its children hang below it.
    check("a simulated bone poses where it was built", nearly(global[slice + 0][3][1], 1.0f));
    // The chest stands at hips + spine + chest.
    check("  and a bone with no body of its own follows its parent", nearly(global[slice + 2][3][1], 1.6f));

    // Move every body a known distance: a pose falling back to the bind pose passes
    // every assertion above but not this.
    constexpr float SHIFT = 5.0f;
    for (const RagdollBone& bone : ragdoll.bones) {
        scene.get<Transform>(bone.body).position.x += SHIFT;
    }
    composeRagdollPose(
        ragdoll,
        bodiesOf(scene, ragdoll).data(),
        skeleton,
        glm::mat4(1.0f),
        poses.writeTo(slice)
    );

    check("moving the bodies moves the bones with them", nearly(global[slice + 0][3][0], SHIFT));
    check("  carrying the bones that have no body along", nearly(global[slice + 2][3][0], SHIFT));

    // The rig's transform is the pose's frame, so a moved rig must be taken back out, or
    // a character walks away from its skeleton at twice the speed.
    const glm::mat4 rigWorld =
        glm::translate(glm::mat4(1.0f), {SHIFT, 0.0f, 0.0f});
    composeRagdollPose(ragdoll, bodiesOf(scene, ragdoll).data(), skeleton, rigWorld, poses.writeTo(slice));
    check("the pose is relative to the rig, not the world", nearly(global[slice + 0][3][0], 0.0f));

    // Visibility sizes a skinned mesh from this bound; a composer writing no bound
    // leaves a stale one, and the ragdoll is culled as it starts moving.
    const PoseSlice& bound = poses.slices().front();
    check(
        "the pose publishes a bound the visibility pass can use",
        bound.originMax.y > bound.originMin.y && bound.maxBoneScale >= 1.0f
    );

    // The vertex shader reads the palette; without it the character draws in bind shape.
    const std::vector<glm::mat4>& palette = poses.palette();
    check(
        "the skinning palette is written beside the pose",
        palette.size() == global.size() && nearly(palette[slice + 0][3][1], global[slice + 0][3][1])
    );

    // A bone whose body was destroyed - a partly cleared ragdoll, or a scene a body short
    // - must still be posed, not left as the buffer held it.
    Scene broken;
    const EntityId partial = broken.createEntity();
    broken.add<Transform>(partial, Transform{});
    buildRagdoll(broken, partial, skeleton);
    broken.destroyEntity(broken.get<Ragdoll>(partial).bones[1].body);

    PoseBuffer second;
    second.clear();
    const uint32_t slice2 = second.addSlice(bones);
    composeRagdollPose(
        broken.get<Ragdoll>(partial),
        bodiesOf(broken, broken.get<Ragdoll>(partial)).data(),
        skeleton,
        glm::mat4(1.0f),
        second.writeTo(slice2)
    );
    // Where the fallback puts it, not just a number: the zero-filled buffer is finite for
    // an untouched bone too.
    check(
        "a bone whose body is gone falls back to its parent",
        nearly(second.global()[slice2 + 1][3][1], 1.3f)
    );
}

// Ragdoll bones are bodies with colliders, so they double as hit boxes. The whole path:
// shoot, find the limb, find whose it is.
void testRagdollAsHitboxes() {
    std::printf("A ragdoll as hit boxes:\n");

    Scene scene;
    addBox(scene, {0.0f, -0.5f, 0.0f}, {8.0f, 0.5f, 8.0f});

    const EntityId character = addFallingBody(scene, {0.0f, 1.0f, 0.0f}, 0.3f);
    scene.get<Rigidbody>(character).freezeRotation = true;
    const SkeletonAsset skeleton = makeTestRig();
    check("a rig to shoot at", buildRagdoll(scene, character, skeleton) == 4);

    // Held to the pose, as an inactive ragdoll is: the bones are where animation put them.
    RagdollSystem ragdolls;
    TestFrame frame(scene);
    FrameContext& ctx = frame.ctx;
    ragdolls.fixedUpdate(ctx);

    // Asked for limbs, because the character's own collider blocks an unfiltered ray -
    // why bones have their own layer.
    QueryFilter limbs;
    limbs.layerMask = RagdollSettings{}.boneLayer;

    RayHit hit;
    check("a ray finds a limb", raycast(scene, {5.0f, 1.3f, 0.0f}, {-1,0,0}, 20.0f, hit, limbs));

    int32_t boneIndex = -1;
    const EntityId owner = ragdollOwnerOf(scene, hit.entity, &boneIndex);
    check("  and the limb says whose it is", owner == character);
    const int32_t boneCount = static_cast<int32_t>(skeleton.bones.size());
    check("  and which bone it was", boneIndex >= 0 && boneIndex < boneCount);

    // The bone's name is the rig's own.
    if (boneIndex >= 0) {
        std::printf("      (it was '%s')\n", skeleton.bones[static_cast<size_t>(boneIndex)].name.c_str());
    }

    // Something that is not a limb answers nothing rather than guessing.
    check("something that is not a limb has no owner", !ragdollOwnerOf(scene, character, nullptr));

    // Bones masked out, asserted as two facts: "missed, or found no bone" is satisfied
    // by the miss alone, and a mask excluding nothing would pass it.
    QueryFilter withoutBones;
    withoutBones.layerMask = ~RagdollSettings{}.boneLayer;
    RayHit other;
    const bool foundSomething =
        raycast(scene, {5.0f, 1.3f, 0.0f}, {-1,0,0}, 20.0f, other, withoutBones);
    check("  the same ray without them still finds the character", foundSomething);
    check(
        "    and what it finds is not one of its bones",
        foundSomething && !ragdollOwnerOf(scene, other.entity, nullptr)
    );
    check("    which is a different entity from the limb", other.entity != hit.entity);
}

// A walking character's inactive ragdoll bones, kinematic and animated, overlap a
// crate. Kinematic means infinite mass, moving any crate alike; while animation holds
// the ragdoll its bones are hitboxes and touch nothing.
void testAHeldRagdollBoneShovesNothing() {
    std::printf("A ragdoll bone the animation still holds:\n");

    const auto crateAfter = [](bool heldByRagdoll, bool& boneStillHit) {
        Scene scene;
        addBox(scene, {0.0f, -0.5f, 0.0f}, {20.0f, 0.5f, 4.0f});

        const EntityId crate = scene.createEntity();
        Transform crateAt;
        crateAt.position = {0.0f, 0.5f, 0.0f};
        scene.add<Transform>(crate, std::move(crateAt));
        Rigidbody crateBody;
        crateBody.mass = 800.0f;
        crateBody.freezeRotation = true;
        scene.add<Rigidbody>(crate, std::move(crateBody));
        ColliderPart crateShape;
        crateShape.shape = ColliderShape::Box;
        crateShape.halfExtents = {0.5f, 0.5f, 0.5f};
        Collider crateCollider;
        crateCollider.parts = { crateShape };
        scene.add<Collider>(crate, std::move(crateCollider));

        // A shin, a hand's width into the crate's side and moving into it.
        const EntityId shin = scene.createEntity();
        Transform shinAt;
        shinAt.position = {-0.5f, 0.5f, 0.0f};
        scene.add<Transform>(shin, std::move(shinAt));
        Rigidbody shinBody;
        shinBody.motion = RigidbodyMotion::Kinematic;
        shinBody.linearVelocity = {2.0f, 0.0f, 0.0f};
        scene.add<Rigidbody>(shin, std::move(shinBody));
        ColliderPart shinShape;
        shinShape.shape = ColliderShape::Capsule;
        shinShape.radius = 0.1f;
        shinShape.halfHeight = 0.2f;
        Collider shinCollider;
        shinCollider.parts = { shinShape };
        scene.add<Collider>(shin, std::move(shinCollider));

        if (heldByRagdoll) {
            Ragdoll ragdoll;
            ragdoll.active = false;
            RagdollBone bone;
            bone.body = shin;
            ragdoll.bones.push_back(bone);
            scene.add<Ragdoll>(scene.createEntity(), std::move(ragdoll));
        }

        simulate(scene, 32);
        RayHit hit;
        boneStillHit = raycast(scene, {-0.55f, 5.0f, 0.0f}, {0, -1, 0}, 100.0f, hit) && hit.entity == shin;
        return scene.get<Transform>(crate).position.x;
    };

    bool hitWhenFree = false;
    bool hitWhenHeld = false;
    const float free = crateAfter(false, hitWhenFree);
    const float held = crateAfter(true, hitWhenHeld);
    std::printf("      a free kinematic body moved the crate %.3f m, a held bone %.3f m\n", free, held);
    check("a kinematic body shoves even an 800 kg crate", free > 0.05f);
    check("  a bone its ragdoll holds does not", std::fabs(held) < 1e-3f);
    check("  and is still what a ray finds", hitWhenHeld);
}

} // namespace

void runPhysicsRagdollTests() {
    testARagdollsMuscleSetsItsHolds();
    testAHeldRagdollBoneShovesNothing();
    testARagdollBuiltAtAnotherScaleSaysSo();
    testRagdoll();
    testRagdollFallsTwice();
    testARagdollInheritsTheMotionItWasPosedWith();
    testAnActiveRagdollFallsAsleep();
    testRagdollPose();
    testRagdollAsHitboxes();
}

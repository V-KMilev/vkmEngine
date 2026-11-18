// Ragdolls: bodies built in a rig's shape, hitboxes following its clip until let go.

#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/scene/world_transform.h"
#include "system/physics/authoring/ragdoll_build.h"

#include "animation/rig.h"
#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

TEST("buildRagdoll makes a body per bone, which follows the clip while inactive and falls once active") {
    Game game;
    Scene& scene = game.scene();
    addFloor(scene);
    const SkeletonHandle rig = addRig(game.resources());
    const EntityId walker    = addCharacter(
        scene,
        rig,
        addTurn(game.resources(), "test:turn", 0.0f, 90.0f),
        {0.0f, 3.0f, 0.0f}
    );
    scene.get<Animator>(walker).looping = false;

    // root and upper span a meter each to their child; lower is a tip, with nothing to span.
    REQUIRE(buildRagdoll(scene, walker, game.resources().get(rig)) == 2);
    Ragdoll& doll = scene.get<Ragdoll>(walker);
    REQUIRE(doll.bones.size() == 2);
    CHECK(doll.bones[0].bone == ROOT);
    CHECK(doll.bones[1].bone == UPPER);
    for (const RagdollBone& bone : doll.bones) {
        int32_t found = -1;
        CHECK(ragdollOwnerOf(scene, bone.body, &found) == walker);
        CHECK(found == bone.bone);
        REQUIRE(scene.has<Collider>(bone.body));
        CHECK(scene.get<Collider>(bone.body).parts.at(0).shape == ColliderShape::Capsule);
        CHECK(scene.has<Rigidbody>(bone.body));
    }
    const EntityId upper = doll.bones[1].body;
    const auto centerOf  = [&](EntityId id) { return glm::vec3(scene.get<WorldTransform>(id).model[3]); };
    // The body spans upper from its head to lower's: its middle.
    const auto spanning = [](float degrees) {
        return glm::vec3(
            0.0f,
            3.0f,
            0.0f
        ) + glm::vec3(0.0f, 1.0f, 0.0f) + aboutZ(degrees) * glm::vec3(0.0f, 0.5f, 0.0f);
    };

    glm::vec3 turning(0.0f), ended(0.0f);
    bool hitbox = false;
    game.run(240, [&](int frame) {
        if (frame == 30) {
            turning = centerOf(upper);
            hitbox  = isPosedByAnimation(scene, upper);
        }
        // The clip has ended a half second before; the bodies hang where it left them, then go.
        if (frame == 90) {
            ended       = centerOf(upper);
            doll.active = true;
        }
    });

    CHECK_NEAR(turning, spanning(45.0f), 1e-3f);
    CHECK(hitbox);
    CHECK_NEAR(ended, spanning(90.0f), 1e-3f);
    CHECK(!isPosedByAnimation(scene, upper));
    for (const RagdollBone& bone : doll.bones) CHECK(centerOf(bone.body).y < 0.6f);
}

TEST("a rig with no bone long enough builds no ragdoll, and says so") {
    Game game;
    const SkeletonHandle rig = addRig(game.resources());
    const EntityId walker    = addCharacter(game.scene(), rig, {});
    // A centimeter a bone, under any limb a body could stand for.
    game.scene().get<Transform>(walker).scale = glm::vec3(0.01f);

    CHECK(buildRagdoll(game.scene(), walker, game.resources().get(rig), RagdollSettings{}) == 0);
    CHECK(!game.scene().has<Ragdoll>(walker));
    CHECK(countInLog("nothing to build") == 1);
}

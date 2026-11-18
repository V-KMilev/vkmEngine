// A ragdoll taking a rig over: the pose the skin draws comes from its bodies.

#include "ecs/component/physics/ragdoll.h"
#include "ecs/component/scene/world_transform.h"
#include "system/physics/authoring/ragdoll_build.h"

#include "animation/rig.h"
#include "harness/check.h"
#include "harness/watch.h"
#include "harness/world.h"

using namespace Vkm::Test;

TEST("an active ragdoll's bodies drive the pose the skin draws") {
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
    REQUIRE(buildRagdoll(scene, walker, game.resources().get(rig)) == 2);
    scene.get<Ragdoll>(walker).active = true;

    Watch& seen = watch(game);
    game.run(120);

    // Each bone where its body holds it, fallen to the floor, not where the clip would hold it.
    const glm::mat4 rigWorld = scene.get<WorldTransform>(walker).model;
    for (const RagdollBone& bone : scene.get<Ragdoll>(walker).bones) {
        const glm::mat4 drawn = rigWorld * boneIn(seen.frame->poses, walker, bone.bone);
        const glm::mat4 held  = scene.get<WorldTransform>(bone.body).model * bone.bodyFromBone;
        CHECK_NEAR(placeOf(drawn), placeOf(held), 1e-3f);
        CHECK(placeOf(drawn).y < 1.0f);
    }
}

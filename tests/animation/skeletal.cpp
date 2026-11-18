// Skeletal animation: an Animator posing a rig on the tick, blending clips, taking a behavior's
// adjustments, announcing its clip's markers, and placing what hangs off its bones.

#include <string>
#include <vector>

#include "ecs/component/animation/bone_socket.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/hierarchy_operations.h"
#include "system/animation/animation_events.h"
#include "system/animation/bone_socket_system.h"
#include "system/physics/drawn_ahead.h"

#include "animation/rig.h"
#include "harness/check.h"
#include "harness/watch.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

/// Where lower stands when upper has turned @p degrees about Z: a meter along upper from its head.
glm::vec3 lowerAt(float degrees) { return glm::vec3(
    0.0f,
    1.0f,
    0.0f
) + aboutZ(degrees) * glm::vec3(0.0f, 1.0f, 0.0f); }

} // namespace

TEST("an Animator plays its clip on the tick and publishes the pose, for its rig and the meshes under it") {
    Game game;
    const SkeletonHandle rig = addRig(game.resources());
    const AnimationClipHandle turn = addTurn(game.resources(), "test:turn", 0.0f, 90.0f);
    const EntityId looped = addCharacter(game.scene(), rig, turn);
    const EntityId once   = addCharacter(game.scene(), rig, turn, {5.0f, 0.0f, 0.0f});
    game.scene().get<Animator>(once).looping = false;
    const EntityId skin = game.scene().createEntity();
    game.scene().add(skin, Transform{});
    HierarchyOperations::setParent(game.scene(), skin, looped);

    Watch& seen = watch(game);
    glm::vec3 halfway(0.0f);
    bool skinnedByRig = false;
    game.run(90, [&](int frame) {
        if (frame != 30) return;
        const PoseSlice* slice = seen.frame->poses->sliceOf(looped);
        halfway      = placeOf(boneIn(seen.frame->poses, looped, LOWER));
        skinnedByRig = slice && seen.frame->poses->sliceOf(skin) == slice;
    });

    // Half a second in, upper has turned 45 degrees, carrying lower with it.
    CHECK_NEAR(halfway, lowerAt(45.0f), 1e-3f);
    CHECK(skinnedByRig);
    // A second and a half in: the loop half into its second lap, the one-shot held at its end.
    CHECK_NEAR(placeOf(boneIn(seen.frame->poses, looped, LOWER)), lowerAt(45.0f), 1e-3f);
    CHECK_NEAR(placeOf(boneIn(seen.frame->poses, once, LOWER)), lowerAt(90.0f), 1e-4f);
    CHECK(!game.scene().get<Animator>(once).playing);
    CHECK(game.scene().get<Animator>(looped).playing);
}

TEST("a world standing still moves no Animator's head, and a head set by hand shows in the pose at once") {
    Game game;
    const SkeletonHandle rig = addRig(game.resources());
    const EntityId walker    = addCharacter(
        game.scene(),
        rig,
        addTurn(game.resources(), "test:turn", 0.0f, 90.0f)
    );
    game.scene().get<Animator>(walker).time = 0.25f;

    Watch& seen = watch(game);
    game.run(10, {}, 0);

    CHECK_NEAR(placeOf(boneIn(seen.frame->poses, walker, LOWER)), lowerAt(22.5f), 1e-3f);
    CHECK(game.scene().get<Animator>(walker).time == 0.25f);
}

TEST("a crossfade blends two clips' poses over its length, and turned round mid-way it does not jump") {
    Game game;
    const SkeletonHandle rig = addRig(game.resources());
    const AnimationClipHandle left  = addLean(game.resources(), "test:left", 0.0f);
    const AnimationClipHandle right = addLean(game.resources(), "test:right", 2.0f);
    const EntityId walker = addCharacter(game.scene(), rig, left);
    Animator& animator = game.scene().get<Animator>(walker);

    // Across from left to right from tick 11; turned back at tick 25, a quarter of the way.
    Watch& seen = watch(game);
    std::vector<float> across;
    game.run(40, [&](int frame) {
        across.push_back(placeOf(boneIn(seen.frame->poses, walker, UPPER)).x);
        if (frame == 10) Animator::crossFadeTo(animator, right, 1.0f, true);
        if (frame == 25) Animator::crossFadeTo(animator, left, 1.0f, true);
    });

    CHECK(across[9] == 0.0f);
    CHECK_NEAR(across[15], 2.0f * 6.0f / 60.0f, 1e-4f);
    CHECK_NEAR(across[24], 0.5f, 1e-4f);
    CHECK_NEAR(across[25], 2.0f * (0.25f - 1.0f / 60.0f), 1e-4f);
    CHECK_NEAR(across[39], 0.0f, 1e-4f);
    CHECK(animator.clip == left);
    CHECK(!animator.fadeFrom);
}

TEST("Animator::adjust turns, moves and scales a bone after the clip, its subtree following") {
    Game game;
    const SkeletonHandle rig = addRig(game.resources());
    const EntityId walker    = addCharacter(
        game.scene(),
        rig,
        addTurn(game.resources(), "test:held", 30.0f, 30.0f)
    );
    std::vector<BoneAdjust>& adjust = game.scene().get<Animator>(walker).adjust;
    auto entry = [](float degrees, const glm::vec3& offset, float scale, bool absolute) {
        BoneAdjust a;
        a.bone     = UPPER;
        a.rotation = aboutZ(degrees);
        a.offset   = offset;
        a.scale    = scale;
        a.absolute = absolute;
        return a;
    };

    Watch& seen = watch(game);
    std::vector<glm::vec3> lower;
    game.run(6, [&](int frame) {
        lower.push_back(placeOf(boneIn(seen.frame->poses, walker, LOWER)));
        adjust.clear();
        if (frame == 1) adjust.push_back(entry(60.0f, glm::vec3(0.0f), 1.0f, false));
        if (frame == 2) adjust.push_back(entry(0.0f, {0.0f, 0.0f, 0.5f}, 1.0f, false));
        if (frame == 3) adjust.push_back(entry(0.0f, glm::vec3(0.0f), 2.0f, false));
        if (frame == 4) adjust.push_back(entry(0.0f, glm::vec3(0.0f), 1.0f, true));
        if (frame == 5) {
            adjust.push_back(entry(60.0f, glm::vec3(0.0f), 1.0f, false));
            adjust.back().bone = 7;
        }
    });

    CHECK_NEAR(lower[0], lowerAt(30.0f), 1e-4f);
    CHECK_NEAR(lower[1], lowerAt(90.0f), 1e-4f);
    CHECK_NEAR(lower[2], lowerAt(30.0f) + glm::vec3(0.0f, 0.0f, 0.5f), 1e-4f);
    CHECK_NEAR(lower[3], glm::vec3(0.0f, 1.0f, 0.0f) + aboutZ(30.0f) * glm::vec3(0.0f, 2.0f, 0.0f), 1e-4f);
    CHECK_NEAR(lower[4], lowerAt(0.0f), 1e-4f);
    // A bone the rig does not have adjusts nothing.
    CHECK_NEAR(lower[5], lowerAt(30.0f), 1e-4f);
}

TEST("a clip's markers are announced once each as the head passes them, a loop's start as it wraps") {
    Game game;
    const SkeletonHandle rig  = addRig(game.resources());
    AnimationClipAsset stride = emptyClip(1.0f);
    stride.markers = {{"start", 0.0f}, {"left", 0.31f}, {"right", 0.81f}};
    const AnimationClipHandle clip = game.resources().add(std::move(stride), "test:stride");
    const EntityId walker = addCharacter(game.scene(), rig, clip);
    const EntityId idle   = addCharacter(game.scene(), rig, clip);
    game.scene().get<Animator>(idle).playOnStart = false;

    std::vector<AnimationEvent> heard;
    game.events().subscribe<AnimationEvent>([&](const AnimationEvent& e) { heard.push_back(e); });
    game.run(110);

    std::vector<std::string> names;
    for (const AnimationEvent& e : heard) {
        CHECK(e.entity == walker);
        names.push_back(e.marker);
    }
    CHECK(names == std::vector<std::string>({"left", "right", "start", "left", "right"}));
}

TEST("a BoneSocket holds its child on its bone, at its offset, every frame") {
    Game game;
    Scene& scene = game.scene();
    const SkeletonHandle rig = addRig(game.resources());
    AnimationClipHandle turn = addTurn(game.resources(), "test:turn", 0.0f, 90.0f);
    const EntityId walker = addCharacter(scene, rig, turn, {5.0f, 0.0f, 0.0f});
    scene.get<Animator>(walker).looping = false;
    auto socketUnder = [&](EntityId parent) {
        const EntityId id = scene.createEntity();
        scene.add(id, Transform{});
        BoneSocket socket;
        socket.bone            = "lower";
        socket.offset.position = {0.0f, 0.5f, 0.0f};
        scene.add(id, socket);
        HierarchyOperations::setParent(scene, id, parent);
        return id;
    };
    const EntityId hat = socketUnder(walker);
    // Under a mesh of the rig rather than the rig itself: refused, and said.
    const EntityId body = scene.createEntity();
    scene.add(body, Transform{});
    HierarchyOperations::setParent(scene, body, walker);
    socketUnder(body);

    const auto worldOf = [&](EntityId id) { return glm::vec3(scene.get<WorldTransform>(id).model[3]); };
    glm::vec3 turning(0.0f);
    game.run(90, [&](int frame) {
        if (frame == 30) turning = worldOf(hat);
    });

    const glm::vec3 rigAt(5.0f, 0.0f, 0.0f);
    CHECK_NEAR(turning, rigAt + lowerAt(45.0f) + aboutZ(45.0f) * glm::vec3(0.0f, 0.5f, 0.0f), 1e-3f);
    CHECK_NEAR(worldOf(hat), rigAt + lowerAt(90.0f) + glm::vec3(-0.5f, 0.0f, 0.0f), 1e-3f);
    CHECK(countInLog("is not a direct child of a rig") == 1);
}

TEST("a tick finds a socket where the tick's pose put it, wherever a frame between ticks drew it") {
    Frame world;
    const EntityId rigged = addCharacter(world.scene, addRig(world.resources), {});
    const EntityId hat    = world.scene.createEntity();
    world.scene.add(hat, Transform{});
    BoneSocket socket;
    socket.bone = "lower";
    world.scene.add(hat, socket);
    HierarchyOperations::setParent(world.scene, hat, rigged);

    // The tick's pose, the bind pose; and the one a frame drew ahead of it, lower a meter on in X.
    const auto posed = [&](PoseBuffer& poses, const glm::vec3& lowerOffBind) {
        const uint32_t slice = poses.addSlice(3);
        poses.mapEntity(rigged, slice);
        const PoseWrite out = poses.writeTo(slice);
        for (const int32_t bone : {ROOT, UPPER, LOWER}) {
            const float up = static_cast<float>(bone);
            out.global[bone] = glm::translate(glm::mat4(1.0f), glm::vec3(0.0f, up, 0.0f));
        }
        out.global[LOWER] = glm::translate(out.global[LOWER], lowerOffBind);
    };
    PoseBuffer tick, drawn;
    posed(tick, glm::vec3(0.0f));
    posed(drawn, {1.0f, 0.0f, 0.0f});

    BoneSocketSystem sockets;
    DrawnAhead ahead;
    world.ctx.poses = &tick;
    sockets.fixedUpdate(world.ctx);
    const glm::vec3 placed = world.scene.get<Transform>(hat).position;

    // As Engine::run brackets a frame drawn 8 ms past its tick.
    ahead.carry(world.scene, world.net, 0.008f);
    world.ctx.poses = &drawn;
    sockets.update(world.ctx);
    const glm::vec3 shown = world.scene.get<Transform>(hat).position;
    ahead.putBack(world.scene);

    CHECK_NEAR(placed, glm::vec3(0.0f, 2.0f, 0.0f), 1e-6f);
    CHECK_NEAR(shown, glm::vec3(1.0f, 2.0f, 0.0f), 1e-6f);
    CHECK(world.scene.get<Transform>(hat).position == placed);
}

TEST("a socketed item a frame drops, unsockets or hangs from another parent stays where that frame put it") {
    Frame world;
    const EntityId rigged = addCharacter(world.scene, addRig(world.resources), {});
    const EntityId rack   = world.scene.createEntity();
    world.scene.add(rack, Transform{});
    const auto held = [&] {
        const EntityId item = world.scene.createEntity();
        world.scene.add(item, Transform{});
        BoneSocket socket;
        socket.bone = "lower";
        world.scene.add(item, socket);
        HierarchyOperations::setParent(world.scene, item, rigged);
        return item;
    };
    const EntityId hat   = held();
    const EntityId sword = held();
    const EntityId torch = held();

    PoseBuffer poses;
    const uint32_t slice = poses.addSlice(3);
    poses.mapEntity(rigged, slice);
    const PoseWrite out = poses.writeTo(slice);
    for (const int32_t bone : {ROOT, UPPER, LOWER}) out.global[bone] = glm::mat4(1.0f);
    BoneSocketSystem sockets;
    world.ctx.poses = &poses;
    sockets.fixedUpdate(world.ctx);

    // A behavior, in a frame between ticks, drops the hat where it stands, racks the sword, and lets the torch
    // go from its bone while it stays the character's child.
    DrawnAhead ahead;
    ahead.carry(world.scene, world.net, 0.008f);
    sockets.update(world.ctx);
    const glm::vec3 dropped(3.0f, 0.0f, 1.0f);
    const glm::vec3 racked(0.0f, 1.0f, 0.0f);
    world.scene.remove<BoneSocket>(hat);
    HierarchyOperations::removeFromParent(world.scene, hat);
    world.scene.get<Transform>(hat).position = dropped;
    HierarchyOperations::setParent(world.scene, sword, rack);
    world.scene.get<Transform>(sword).position = racked;
    const glm::vec3 lowered(0.0f, -1.0f, 0.5f);
    world.scene.remove<BoneSocket>(torch);
    world.scene.get<Transform>(torch).position = lowered;
    ahead.putBack(world.scene);

    CHECK(world.scene.get<Transform>(hat).position == dropped);
    CHECK(world.scene.get<Transform>(sword).position == racked);
    CHECK(world.scene.get<Transform>(torch).position == lowered);
}

TEST("a clip made for another rig holds the bind pose, and says so once") {
    Game game;
    const SkeletonHandle rig   = addRig(game.resources());
    AnimationClipAsset foreign = emptyClip(1.0f);
    foreign.skeleton              = "test:another";
    foreign.rotationTimes         = {0.0f};
    foreign.rotations             = {aboutZ(90.0f)};
    foreign.bones[UPPER].rotation = {0, 1};
    const EntityId walker = addCharacter(
        game.scene(),
        rig,
        game.resources().add(std::move(foreign), "test:foreign")
    );

    Watch& seen = watch(game);
    game.run(10);

    CHECK_NEAR(placeOf(boneIn(seen.frame->poses, walker, LOWER)), lowerAt(0.0f), 1e-5f);
    CHECK(countInLog("does not fit rig") == 1);
}

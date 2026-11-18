// Keyframe animation: an Animation's tracks driving its entity's Transform on the tick.

#include <map>
#include <string>
#include <vector>

#include "base/math/easing.h"
#include "base/reflect/enum_names.h"
#include "ecs/component/animation/animation.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/scene/transform.h"
#include "io/scene/scene_serializer.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;

namespace {

/// From @p from to @p to over a second; a one-shot unless @p looping.
Animation slide(const glm::vec3& from, const glm::vec3& to, bool looping) {
    Animation animation;
    animation.positionTrack.addKeyframe(0.0f, from);
    animation.positionTrack.addKeyframe(1.0f, to);
    animation.looping = looping;
    return animation;
}

EntityId addAnimated(Scene& scene, const Animation& animation, const glm::vec3& scale = glm::vec3(1.0f)) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.scale = scale;
    scene.add(id, pose);
    scene.add(id, animation);
    return id;
}

EntityId addBox(Scene& scene, const glm::vec3& at, const glm::vec3& halfExtents, RigidbodyMotion motion) {
    const EntityId id = scene.createEntity();
    Transform pose;
    pose.position = at;
    scene.add(id, pose);
    Rigidbody body;
    body.motion = motion;
    scene.add(id, body);
    ColliderPart part;
    part.shape       = ColliderShape::Box;
    part.halfExtents = halfExtents;
    Collider collider;
    collider.parts = {part};
    scene.add(id, collider);
    return id;
}

glm::vec3 at(Game& game, EntityId id) { return game.scene().get<Transform>(id).position; }

} // namespace

TEST("a keyframe track moves its entity through its keys, looped or played once, at its speed") {
    Game game;
    Scene& scene = game.scene();
    Animation turning = slide({0.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, true);
    turning.rotationTrack.addKeyframe(0.0f, glm::quat(1.0f, 0.0f, 0.0f, 0.0f));
    turning.rotationTrack.addKeyframe(1.0f, glm::angleAxis(glm::radians(90.0f), glm::vec3(0.0f, 1.0f, 0.0f)));
    // No scale track: the authored scale stands.
    const EntityId looped = addAnimated(scene, turning, glm::vec3(2.0f));
    const EntityId once   = addAnimated(scene, slide({0.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, false));
    Animation fast = slide({0.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, true);
    fast.speed = 2.0f;
    const EntityId doubled = addAnimated(scene, fast);

    game.run(20);
    CHECK_NEAR(at(game, looped), glm::vec3(2.0f, 0.0f, 0.0f), 1e-3f);
    CHECK_NEAR(scene.get<Transform>(looped).rotation,
               glm::angleAxis(glm::radians(30.0f), glm::vec3(0.0f, 1.0f, 0.0f)), 1e-3f);
    CHECK(scene.get<Transform>(looped).scale == glm::vec3(2.0f));
    CHECK_NEAR(at(game, once), glm::vec3(2.0f, 0.0f, 0.0f), 1e-3f);
    CHECK_NEAR(at(game, doubled).x, 4.0f, 1e-3f);

    // A second and a quarter in: the loop is a quarter into its second lap, the one-shot held at its
    // end, the doubled loop half into its third.
    Game later;
    const EntityId loopedLater = addAnimated(later.scene(), turning);
    const EntityId onceLater   = addAnimated(
        later.scene(),
        slide({0.0f, 0.0f, 0.0f}, {6.0f, 0.0f, 0.0f}, false)
    );
    const EntityId fastLater = addAnimated(later.scene(), fast);
    later.run(75);
    CHECK_NEAR(at(later, loopedLater).x, 1.5f, 1e-3f);
    CHECK_NEAR(at(later, onceLater).x, 6.0f, 1e-5f);
    CHECK(!later.scene().get<Animation>(onceLater).playing);
    CHECK(later.scene().get<Animation>(loopedLater).playing);
    CHECK_NEAR(at(later, fastLater).x, 3.0f, 1e-3f);
}

TEST("every easing curve runs from its first key to its last and bends the way its name says") {
    std::vector<AnimationTrack<float>> tracks;
    std::map<std::string, size_t> byName;
    for (uint8_t e = 0; e < static_cast<uint8_t>(Math::Easing::Count); ++e) {
        AnimationTrack<float> track(static_cast<Math::Easing>(e));
        track.addKeyframe(0.0f, 0.0f);
        track.addKeyframe(1.0f, 1.0f);
        tracks.push_back(track);
        byName[Reflect::enumName(static_cast<Math::Easing>(e))] = e;
    }
    for (uint8_t e = 0; e < static_cast<uint8_t>(Math::Easing::Count); ++e) {
        const std::string name = Reflect::enumName(static_cast<Math::Easing>(e));
        const AnimationTrack<float>& track = tracks[e];
        CHECK(track.getValue(0.0f) == 0.0f);
        CHECK(track.getValue(1.0f) == 1.0f);
        const bool inOut = name.rfind("easeInOut", 0) == 0;
        if (name == "linear") {
            CHECK_NEAR(track.getValue(0.3f), 0.3f, 1e-6f);
        } else if (inOut) {
            // Slow at both ends, through the middle at the middle, the second half the first turned round.
            CHECK_NEAR(track.getValue(0.5f), 0.5f, 1e-5f);
            CHECK(track.getValue(0.25f) < 0.25f);
            for (const float t : {0.1f, 0.3f, 0.45f}) CHECK_NEAR(
                track.getValue(1.0f - t),
                1.0f - track.getValue(t),
                1e-4f
            );
        } else if (name.rfind("easeIn", 0) == 0) {
            CHECK(track.getValue(0.25f) < 0.25f);
        } else {
            REQUIRE(name.rfind("easeOut", 0) == 0);
            CHECK(track.getValue(0.75f) > 0.75f);
            // Out is its In played backward.
            REQUIRE(byName.count("easeIn" + name.substr(7)) == 1);
            const AnimationTrack<float>& in = tracks[byName["easeIn" + name.substr(7)]];
            for (const float t : {0.1f, 0.4f, 0.8f}) CHECK_NEAR(
                track.getValue(t),
                1.0f - in.getValue(1.0f - t),
                1e-4f
            );
        }
    }
}

TEST("a clip on a kinematic body carries what stands on it, and leaves it still when the clip ends") {
    Game game;
    Scene& scene = game.scene();
    const EntityId deck = addBox(scene, {0.0f, 1.0f, 0.0f}, {2.0f, 0.25f, 2.0f}, RigidbodyMotion::Kinematic);
    // Gentle enough that friction keeps the crate with it: at most 1.9 m/s^2.
    Animation ferry;
    ferry.positionTrack.setEasing(Math::Easing::EaseInOutSine);
    ferry.positionTrack.addKeyframe(0.0f, {0.0f, 1.0f, 0.0f});
    ferry.positionTrack.addKeyframe(4.0f, {6.0f, 1.0f, 0.0f});
    ferry.looping = false;
    scene.add(deck, ferry);
    const EntityId crate = addBox(scene, {0.0f, 1.75f, 0.0f}, glm::vec3(0.5f), RigidbodyMotion::Dynamic);

    glm::vec3 deckSpeed(0.0f);
    game.run(300, [&](int frame) {
        if (frame == 120) deckSpeed = scene.get<Rigidbody>(deck).linearVelocity;
    });

    // Halfway, the clip moves at 3 pi / 4 m/s.
    CHECK_NEAR(deckSpeed, glm::vec3(2.3562f, 0.0f, 0.0f), 0.01f);
    CHECK(scene.get<Rigidbody>(deck).linearVelocity == glm::vec3(0.0f));
    CHECK_NEAR(at(game, deck), glm::vec3(6.0f, 1.0f, 0.0f), 1e-4f);
    CHECK_NEAR(at(game, crate).x, 6.0f, 0.05f);
    CHECK_NEAR(at(game, crate).y, 1.75f, 0.02f);
    CHECK(glm::length(scene.get<Rigidbody>(crate).linearVelocity) < 0.05f);
}

TEST("a finished one-shot does not start again, and a saved scene plays it again from the start") {
    Game game;
    const EntityId shot = addAnimated(game.scene(), slide({0.0f, 0.0f, 0.0f}, {4.0f, 0.0f, 0.0f}, false));
    game.run(150);
    CHECK_NEAR(at(game, shot).x, 4.0f, 1e-5f);
    CHECK(!game.scene().get<Animation>(shot).playing);

    // What was authored is saved, not what was happening: loaded, it is a clip yet to play.
    const std::string saved = SceneSerializer::saveToString(game.scene(), game.resources());
    Game again;
    REQUIRE(SceneSerializer::loadFromString(saved, again.scene(), again.resources()));
    EntityId loaded;
    again.scene().forEach<Animation>([&](EntityId id, Animation&) { loaded = id; });
    REQUIRE(loaded);
    const Animation& fresh = again.scene().get<Animation>(loaded);
    CHECK(fresh.playOnStart);
    CHECK(!fresh.playing);
    CHECK(fresh.time == 0.0f);
    again.run(30);
    CHECK_NEAR(at(again, loaded).x, 2.0f, 1e-3f);
}

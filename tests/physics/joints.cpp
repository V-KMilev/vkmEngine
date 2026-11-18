// Joints: what they hold, how a chain hangs, and how a held angle gives way.

#include <vector>

#include "ecs/component/physics/joint.h"

#include "harness/check.h"
#include "harness/world.h"
#include "physics/bodies.h"

using namespace Vkm::Test;

namespace {

/// An entity with a pose and no body: a joint to it holds to a fixed point in the world.
EntityId addPoint(Scene& scene, const glm::vec3& at) {
    const EntityId id = scene.createEntity();
    Transform transform;
    transform.position = at;
    scene.add(id, transform);
    return id;
}

Joint& joinTo(
    Scene& scene,
    EntityId body,
    EntityId other,
    JointType type,
    const glm::vec3& anchor,
    const glm::vec3& otherAnchor = glm::vec3(0.0f)
) {
    Joint joint;
    joint.type            = type;
    joint.connected       = other;
    joint.anchor          = anchor;
    joint.connectedAnchor = otherAnchor;
    return scene.add(body, joint);
}

glm::vec3 worldAnchor(Scene& scene, EntityId id, const glm::vec3& local) {
    const Transform& t = scene.get<Transform>(id);
    return t.position + t.rotation * local;
}

} // namespace

TEST("a point joint holds a body to a fixed point as it swings") {
    Game game;
    Scene& scene = game.scene();
    const EntityId pivot = addPoint(scene, {0.0f, 5.0f, 0.0f});
    // Hung out sideways, so it swings.
    const EntityId bob = addCrate(scene, {2.0f, 5.0f, 0.0f});
    bodyOf(scene, bob).canSleep = false;
    joinTo(scene, bob, pivot, JointType::Point, {-2.0f, 0.0f, 0.0f});

    float worstGap = 0.0f, lowest = 5.0f;
    game.run(180, [&](int) {
        worstGap = std::max(
            worstGap,
            glm::length(worldAnchor(scene, bob, {-2.0f, 0.0f, 0.0f}) - glm::vec3(0.0f, 5.0f, 0.0f))
        );
        lowest = std::min(lowest, positionOf(scene, bob).y);
    });

    CHECK(worstGap < 0.02f);
    CHECK(lowest < 3.2f);
}

TEST("a distance joint keeps its length, pushing and pulling, measured on the first tick when unset") {
    Game game;
    Scene& scene = game.scene();
    auto pair = [&](float z, float push) {
        const EntityId a = addCrate(scene, {0.0f, 0.0f, z});
        const EntityId b = addCrate(scene, {3.0f, 0.0f, z});
        for (const EntityId id : {a, b}) {
            bodyOf(scene, id).gravityScale = 0.0f;
            bodyOf(scene, id).canSleep     = false;
        }
        joinTo(scene, a, b, JointType::Distance, glm::vec3(0.0f)).distance = -1.0f;
        bodyOf(scene, b).linearVelocity = {push, 0.0f, 0.0f};
        return std::pair{a, b};
    };
    const std::pair pulled = pair(-3.0f, 4.0f);
    const std::pair pushed = pair(3.0f, -4.0f);

    float worst = 0.0f;
    game.run(60, [&](int) {
        for (const auto& [a, b] : {pulled, pushed}) {
            worst = std::max(
                worst,
                std::fabs(glm::length(positionOf(scene, b) - positionOf(scene, a)) - 3.0f)
            );
        }
    });

    CHECK(worst < 0.03f);
    CHECK_NEAR(scene.get<Joint>(pulled.first).resolvedDistance, 3.0f, 1e-4f);
    CHECK(scene.get<Joint>(pulled.first).distance == -1.0f);
    // The pull carried the pair off together.
    CHECK(positionOf(scene, pulled.first).x > 1.0f);
}

TEST("a chain hangs at its length") {
    Game game;
    Scene& scene   = game.scene();
    EntityId above = addPoint(scene, {0.0f, 10.0f, 0.0f});
    std::vector<EntityId> links;
    for (int i = 0; i < 8; ++i) {
        const EntityId link = addBody(scene, {0.0f, 9.75f - i * 0.5f, 0.0f}, box({0.1f, 0.25f, 0.1f}));
        bodyOf(scene, link).mass = 2.0f;
        joinTo(
            scene,
            link,
            above,
            JointType::Point,
            {0.0f, 0.25f, 0.0f},
            i == 0 ? glm::vec3(0.0f) : glm::vec3(0.0f, -0.25f, 0.0f)
        );
        links.push_back(link);
        above = link;
    }
    // A weight at the end, as a lamp on a chain.
    const EntityId lamp = addBody(scene, {0.0f, 5.75f, 0.0f}, box(glm::vec3(0.25f)));
    bodyOf(scene, lamp).mass = 20.0f;
    joinTo(scene, lamp, links.back(), JointType::Point, {0.0f, 0.25f, 0.0f}, {0.0f, -0.25f, 0.0f});

    game.run(240);

    // Eight links of half a meter and half the lamp: its center hangs 4.25 m below the hook, give or
    // take a stretch of 2%.
    CHECK_NEAR(positionOf(scene, lamp).y, 5.75f, 0.085f);
    CHECK(bodyOf(scene, lamp).sleeping);
}

TEST("a held joint keeps its angle under a light load and gives way under one past its torque") {
    Game game;
    Scene& scene = game.scene();
    auto arm = [&](float z, float holdTorque) {
        const EntityId post = addBody(scene, {0.0f, 3.0f, z}, box(glm::vec3(0.25f)), RigidbodyMotion::Static);
        // A 1 m beam of 2 kg sticking out level: about 10 N m at the joint.
        const EntityId beam = addBody(scene, {0.75f, 3.0f, z}, box({0.5f, 0.05f, 0.05f}));
        bodyOf(scene, beam).mass = 2.0f;
        Joint& joint = joinTo(scene, beam, post, JointType::Point, {-0.5f, 0.0f, 0.0f}, {0.25f, 0.0f, 0.0f});
        joint.holdTorque = holdTorque;
        return beam;
    };
    const EntityId strong = arm(-2.0f, 200.0f);
    const EntityId weak   = arm(2.0f, 2.0f);

    game.run(180);

    CHECK(tiltDegrees(scene.get<Transform>(strong).rotation) < 2.0f);
    CHECK(positionOf(scene, strong).y > 2.95f);
    CHECK(positionOf(scene, weak).y < 2.6f);
}

TEST("a joint naming no body, or itself, holds nothing") {
    Game game;
    Scene& scene = game.scene();
    const EntityId loose = addCrate(scene, {0.0f, 10.0f, 0.0f});
    joinTo(scene, loose, EntityId{}, JointType::Point, glm::vec3(0.0f));
    const EntityId self = addCrate(scene, {3.0f, 10.0f, 0.0f});
    joinTo(scene, self, self, JointType::Point, glm::vec3(0.0f));

    game.run(60);

    CHECK(positionOf(scene, loose).y < 6.0f);
    CHECK(positionOf(scene, self).y < 6.0f);
}

// Parents and children: where a child stands in the world, what a reparent may do, and what goes
// with a parent.

#include <algorithm>
#include <vector>

#include <glm/gtc/matrix_transform.hpp>

#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/scene/world_transform.h"
#include "ecs/hierarchy_operations.h"

#include "harness/check.h"
#include "harness/world.h"

using namespace Vkm::Test;
namespace Ops = Vkm::Engine::HierarchyOperations;

namespace {

EntityId posed(
    Scene& scene,
    const glm::vec3& position,
    const glm::quat& rotation = glm::quat(1, 0, 0, 0),
    const glm::vec3& scale = glm::vec3(1.0f)
) {
    const EntityId id = scene.createEntity();
    Transform t;
    t.position = position;
    t.rotation = rotation;
    t.scale    = scale;
    scene.add(id, t);
    return id;
}

glm::mat4 local(
    const Scene& scene,
    EntityId id
) { return Transform::computeModelMatrix(scene.get<Transform>(id)); }

glm::mat4 world(const Scene& scene, EntityId id) { return scene.get<WorldTransform>(id).model; }

float apart(const glm::mat4& a, const glm::mat4& b) {
    float most = 0.0f;
    for (int c = 0; c < 4; ++c) most = std::max(most, glm::length(a[c] - b[c]));
    return most;
}

/// The children of @p parent, in sibling order.
std::vector<EntityId> childrenOf(const Scene& scene, EntityId parent) {
    std::vector<EntityId> out;
    Ops::forEachChild(scene, parent, [&](EntityId child) { out.push_back(child); });
    return out;
}

} // namespace

TEST("a child stands at its parent's pose times its own, down a chain, as the frame resolves it") {
    Game game;
    Scene& scene = game.scene();
    const EntityId root = posed(scene, {5, 0, 0}, glm::angleAxis(0.7f, glm::vec3(0, 1, 0)), glm::vec3(2.0f));
    const EntityId mid  = posed(scene, {0, 1, 0}, glm::angleAxis(-0.4f, glm::vec3(1, 0, 0)));
    const EntityId leaf = posed(scene, {0, 0, -3}, glm::quat(1, 0, 0, 0), glm::vec3(0.5f, 1.0f, 3.0f));
    Ops::setParent(scene, mid, root);
    Ops::setParent(scene, leaf, mid);

    game.run(1);

    const glm::mat4 expected = local(scene, root) * local(scene, mid) * local(scene, leaf);
    CHECK(apart(world(scene, leaf), expected) < 1e-4f);
    CHECK(apart(world(scene, mid), local(scene, root) * local(scene, mid)) < 1e-4f);
    CHECK(apart(Ops::computeWorldMatrix(scene, leaf), expected) < 1e-4f);
    CHECK_NEAR(resolvedWorldPosition(scene, leaf, scene.get<Transform>(leaf)), glm::vec3(expected[3]), 1e-4f);
}

TEST("a moved parent moves its children in the frame it moved") {
    Game game;
    Scene& scene = game.scene();
    const EntityId root  = posed(scene, {0, 0, 0});
    const EntityId child = posed(scene, {1, 0, 0});
    const EntityId grand = posed(scene, {0, 2, 0});
    Ops::setParent(scene, child, root);
    Ops::setParent(scene, grand, child);

    int wrong = 0;
    game.run(6, [&](int frame) {
        // Where this frame put the root, the grandchild stands relative to it.
        wrong += glm::length(
            glm::vec3(world(scene, grand)[3]) - (scene.get<Transform>(root).position + glm::vec3(1, 2, 0))
        ) > 1e-5f;
        // The root moves between frames, as a game's code moves it.
        scene.get<Transform>(root).position = glm::vec3(frame * 3.0f, 0.0f, -frame * 1.0f);
    });
    CHECK(wrong == 0);
}

TEST("a reparent keeps sibling order, and one that would make a ring or a chain too deep is refused") {
    Scene scene;
    const EntityId parent = posed(scene, {0, 0, 0});
    const EntityId a      = posed(scene, {}), b = posed(scene, {}), c = posed(scene, {});
    Ops::setParent(scene, a, parent);
    Ops::setParent(scene, c, parent);
    Ops::setParent(scene, b, parent, c);
    CHECK(childrenOf(scene, parent) == std::vector<EntityId>{a, b, c});

    // A parent under its own child would be a ring: refused, and nothing moves.
    Ops::setParent(scene, parent, b);
    CHECK(countInLog("cycle") == 1);
    CHECK(!scene.get<Hierarchy>(parent).parent);
    CHECK(childrenOf(scene, parent) == std::vector<EntityId>{a, b, c});

    // A chain past the deepest every walk follows.
    EntityId above     = posed(scene, {});
    const EntityId top = above;
    for (uint32_t i = 1; i < Ops::MAX_DEPTH; ++i) {
        const EntityId next = posed(scene, {});
        Ops::setParent(scene, next, above);
        above = next;
    }
    const EntityId deepest = above;
    CHECK(countInLog("deeper than") == 0);
    const EntityId tooDeep = posed(scene, {});
    Ops::setParent(scene, tooDeep, deepest);
    CHECK(countInLog("deeper than") == 1);
    CHECK(!scene.has<Hierarchy>(tooDeep));
    CHECK(Ops::isAncestorOf(scene, top, deepest));

    // Taken out, it stands alone again.
    Ops::removeFromParent(scene, b);
    CHECK(childrenOf(scene, parent) == std::vector<EntityId>{a, c});
    CHECK(!scene.has<Hierarchy>(b));
}

TEST("destroying a parent destroys its whole subtree and nothing else") {
    Scene scene;
    const EntityId keep   = posed(scene, {});
    const EntityId root   = posed(scene, {});
    const EntityId left   = posed(scene, {}), right = posed(scene, {});
    const EntityId deep   = posed(scene, {});
    const EntityId beside = posed(scene, {});
    Ops::setParent(scene, root, keep);
    Ops::setParent(scene, left, root);
    Ops::setParent(scene, right, root);
    Ops::setParent(scene, deep, right);
    Ops::setParent(scene, beside, keep);

    CHECK(Ops::collectSubtree(scene, root) == std::vector<EntityId>{root, left, right, deep});
    Ops::destroyHierarchy(scene, root);

    for (const EntityId id : {root, left, right, deep}) CHECK(!scene.isAlive(id));
    CHECK(scene.isAlive(keep));
    CHECK(scene.isAlive(beside));
    CHECK(childrenOf(scene, keep) == std::vector<EntityId>{beside});
    CHECK(scene.entityCount() == 2u);
}

TEST("destroying one entity hands its children to its parent in its place, each standing where it stood") {
    Scene scene;
    const glm::quat turned = glm::angleAxis(0.5f, glm::vec3(0, 1, 0));
    const glm::quat tipped = glm::angleAxis(-0.8f, glm::vec3(1, 0, 0));
    const glm::quat rolled = glm::angleAxis(1.1f, glm::vec3(0, 0, 1));

    const EntityId top    = posed(scene, {2, 0, 0}, turned, glm::vec3(2.0f));
    const EntityId before = posed(scene, {});
    const EntityId middle = posed(scene, {0, 3, 0}, tipped, glm::vec3(0.5f));
    const EntityId after  = posed(scene, {});
    const EntityId first  = posed(scene, {1, 0, 0});
    const EntityId second = posed(scene, {0, 0, -2}, rolled);
    const EntityId below  = posed(scene, {0, 1, 0});
    Ops::setParent(scene, before, top);
    Ops::setParent(scene, middle, top);
    Ops::setParent(scene, after, top);
    Ops::setParent(scene, first, middle);
    Ops::setParent(scene, second, middle);
    Ops::setParent(scene, below, second);
    const glm::mat4 firstWas  = Ops::computeWorldMatrix(scene, first);
    const glm::mat4 secondWas = Ops::computeWorldMatrix(scene, second);
    const glm::mat4 belowWas  = Ops::computeWorldMatrix(scene, below);

    scene.destroyEntity(middle);

    CHECK(!scene.isAlive(middle));
    CHECK(childrenOf(scene, top) == (std::vector<EntityId>{before, first, second, after}));
    CHECK(childrenOf(scene, second) == std::vector<EntityId>{below});
    CHECK(apart(Ops::computeWorldMatrix(scene, first), firstWas) < 1e-4f);
    CHECK(apart(Ops::computeWorldMatrix(scene, second), secondWas) < 1e-4f);
    CHECK(apart(Ops::computeWorldMatrix(scene, below), belowWas) < 1e-4f);

    // A root's children become roots, each where it stood.
    scene.destroyEntity(top);
    for (const EntityId root : {before, first, second, after}) {
        const Hierarchy* node = scene.tryGet<Hierarchy>(root);
        CHECK((!node || !node->parent));
    }
    CHECK(apart(Ops::computeWorldMatrix(scene, second), secondWas) < 1e-4f);
    CHECK(apart(Ops::computeWorldMatrix(scene, below), belowWas) < 1e-4f);
}

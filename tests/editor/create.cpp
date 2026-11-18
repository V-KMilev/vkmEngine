// The Create menu: a new entity stands where the view looks, a camera takes the view's pose, and
// primitives share their mesh and material until one is given its own.

#include <cmath>

#include <glm/gtc/quaternion.hpp>

#include "actions/asset_actions.h"
#include "actions/create_entity.h"
#include "actions/edit_actions.h"
#include "base/math/rotation.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/hierarchy_operations.h"
#include "undo/component_edit.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;
using namespace Vkm::Engine::EditorActions;

namespace {

/// The view at @p from, looking toward @p at, with @p focus to what it is on: as the editor states it.
void look(Desk& desk, const glm::vec3& from, const glm::vec3& at, float focus = 10.0f) {
    ViewAim& aim = desk.state.viewAim;
    aim.ray      = {from, glm::normalize(at - from)};
    aim.rotation = glm::quatLookAt(aim.ray.direction, glm::vec3(0.0f, 1.0f, 0.0f));
    aim.focus    = focus;
    aim.known    = true;
}

/// A slab 20 m across whose top is at @p top, made from a cube as the author would.
EntityId slab(Desk& desk, float top) {
    const EntityId id = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Cube);
    {
        EditScope<Transform> pose(desk.scene(), desk.resources(), desk.state, id, "Edit Transform");
        pose->position = {0.0f, top - 0.5f, 0.0f};
        pose->scale    = {20.0f, 1.0f, 20.0f};
    }
    desk.state.commands.endGesture();
    return id;
}

glm::vec3 at(Desk& desk, EntityId id) { return desk.scene().get<Transform>(id).position; }

} // namespace

TEST("a new entity stands on the first surface the view looks at, else on the ground near it, else ahead") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    ResourceManager& resources = desk.resources();
    slab(desk, 0.0f);
    // A shelf between the view and the floor, nearer: what the view meets first.
    const EntityId shelf = createEntity(scene, resources, desk.state, EntityKind::Cube);
    {
        EditScope<Transform> pose(scene, resources, desk.state, shelf, "Edit Transform");
        pose->position = {0.0f, 2.0f, 2.0f};
        pose->scale    = {4.0f, 1.0f, 4.0f};
    }

    look(desk, {0.0f, 10.0f, 10.0f}, {0.0f, 0.0f, 0.0f});
    const EntityId onShelf = createEntity(scene, resources, desk.state, EntityKind::Cube);
    CHECK_NEAR(at(desk, onShelf), glm::vec3(0.0f, 3.0f, 2.5f), 1e-4f);
    // Off the origin, where a new entity stands before it is placed.
    look(desk, {0.0f, 10.0f, -10.0f}, {3.0f, 0.0f, 0.0f});
    const EntityId onFloor = createEntity(scene, resources, desk.state, EntityKind::Cube);
    CHECK_NEAR(at(desk, onFloor), glm::vec3(3.0f, 0.5f, 0.0f), 1e-4f);

    // Off the floor: the ground plane when it is near, ahead at the focus when it is far or above.
    look(desk, {100.0f, 3.0f, 0.0f}, {97.0f, 0.0f, 0.0f});
    const EntityId onGround = createEntity(scene, resources, desk.state, EntityKind::Cube);
    CHECK_NEAR(at(desk, onGround), glm::vec3(97.0f, 0.5f, 0.0f), 1e-4f);
    look(desk, {100.0f, 30.0f, 0.0f}, {70.0f, 0.0f, 0.0f}, 50.0f);
    const EntityId far = createEntity(scene, resources, desk.state, EntityKind::PointLight);
    CHECK_NEAR(glm::length(at(desk, far) - glm::vec3(100.0f, 30.0f, 0.0f)), 20.0f, 1e-3f);
    look(desk, {50.0f, 1.0f, 0.0f}, {50.0f, 2.0f, 1.0f}, 7.0f);
    const EntityId above = createEntity(scene, resources, desk.state, EntityKind::PointLight);
    CHECK_NEAR(
        at(desk, above),
        glm::vec3(50.0f, 1.0f, 0.0f) + glm::normalize(glm::vec3(0.0f, 1.0f, 1.0f)) * 7.0f,
        1e-4f
    );
}

TEST("a camera takes the view's pose and is the game's only when the scene has none, and ground and sea stay at the origin") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    ResourceManager& resources = desk.resources();
    look(desk, {4.0f, 3.0f, 9.0f}, {0.0f, 0.0f, 0.0f});

    const EntityId first = createEntity(scene, resources, desk.state, EntityKind::Camera);
    CHECK(at(desk, first) == desk.state.viewAim.ray.origin);
    CHECK_NEAR(
        Math::computeForward(scene.get<Transform>(first).rotation),
        desk.state.viewAim.ray.direction,
        1e-5f
    );
    CHECK(scene.get<Camera>(first).active);
    const EntityId second = createEntity(scene, resources, desk.state, EntityKind::Camera);
    CHECK(!scene.get<Camera>(second).active);

    const EntityId ground = createEntity(scene, resources, desk.state, EntityKind::Terrain);
    const EntityId sea    = createEntity(scene, resources, desk.state, EntityKind::Sea);
    CHECK(at(desk, ground) == glm::vec3(0.0f));
    CHECK(at(desk, sea) == glm::vec3(0.0f));

    // Create Child: at its parent's origin, under it.
    const EntityId child = createEntity(scene, resources, desk.state, EntityKind::PointLight, second);
    CHECK(scene.get<Hierarchy>(child).parent == second);
    CHECK(at(desk, child) == glm::vec3(0.0f));
    undo(scene, desk.state);
    redo(scene, desk.state);
    REQUIRE(scene.isAliveAtIndex(child.slot()));
    CHECK(scene.get<Hierarchy>(scene.entityAt(child.slot())).parent == second);
}

TEST("primitives share one mesh and one material, and one given its own material leaves the rest") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    ResourceManager& resources = desk.resources();
    const EntityId a = createEntity(scene, resources, desk.state, EntityKind::Cube);
    const EntityId b = createEntity(scene, resources, desk.state, EntityKind::Cube);
    const EntityId c = createEntity(scene, resources, desk.state, EntityKind::Cube);
    const MaterialHandle shared = scene.get<Mesh>(a).material;
    CHECK(scene.get<Mesh>(b).mesh == scene.get<Mesh>(a).mesh);
    CHECK(scene.get<Mesh>(c).mesh == scene.get<Mesh>(a).mesh);
    CHECK(scene.get<Mesh>(b).material == shared);
    CHECK(scene.get<Mesh>(c).material == shared);

    const std::string sharedName = resources.get(shared).name();
    const MaterialHandle own     = forkMaterial(scene, resources, desk.state, b, shared);
    REQUIRE(own);
    CHECK(own != shared);
    CHECK(scene.get<Mesh>(b).material == own);
    CHECK(scene.get<Mesh>(a).material == shared);
    CHECK(scene.get<Mesh>(c).material == shared);
    CHECK(resources.get(shared).name() == sharedName);
    resources.edit(own).roughness = 0.1f;
    CHECK(resources.get(shared).roughness != 0.1f);

    // A copy of the engine's default is a new material; a copy of a copy is numbered, not "copy copy".
    CHECK(resources.get(own).name() == "Material");
    const MaterialHandle named = copyMaterial(resources, shared, "Brick");
    const MaterialHandle brick = forkMaterial(scene, resources, desk.state, c, named);
    const MaterialHandle more  = forkMaterial(scene, resources, desk.state, c, brick);
    CHECK(resources.get(brick).name() == "Brick copy");
    CHECK(resources.get(more).name() == "Brick copy 2");

    // The Assets window's New: a copy under a free name each time.
    const MaterialHandle made  = createNewMaterial(resources, desk.state);
    const MaterialHandle again = createNewMaterial(resources, desk.state);
    CHECK(resources.get(made).name() == "Material 2");
    CHECK(resources.get(again).name() == "Material 3");
}

TEST("a Static Body is a solid block in one step: a cube the world collides with, its collider the cube's size") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId block = createEntity(scene, desk.resources(), desk.state, EntityKind::StaticBody);
    const EntityId cube  = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    REQUIRE(scene.has<Mesh>(block));
    CHECK(scene.get<Mesh>(block).mesh == scene.get<Mesh>(cube).mesh);
    REQUIRE(scene.has<Rigidbody>(block));
    CHECK(scene.get<Rigidbody>(block).motion == RigidbodyMotion::Static);
    REQUIRE(scene.has<Collider>(block));
    REQUIRE(scene.get<Collider>(block).parts.size() == 1u);
    CHECK(scene.get<Collider>(block).parts[0].shape == ColliderShape::Box);
    CHECK(scene.get<Collider>(block).parts[0].halfExtents == glm::vec3(0.5f));
}

TEST("a Character is seen: a body of its capsule's height stands on its feet, and goes with it as one step") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId walker = createEntity(scene, desk.resources(), desk.state, EntityKind::Character);
    REQUIRE(scene.has<Collider>(walker));
    const ColliderPart& capsule = scene.get<Collider>(walker).parts[0];
    const Hierarchy* link = scene.tryGet<Hierarchy>(walker);
    REQUIRE(link != nullptr);
    const EntityId body = link->firstChild;
    REQUIRE(scene.has<Mesh>(body));
    CHECK(scene.get<Transform>(body).position == capsule.center);
    const Math::AABB bounds = desk.resources().get(scene.get<Mesh>(body).mesh).bounds();
    CHECK(std::abs(bounds.max.y - bounds.min.y - 2.0f * (capsule.halfHeight + capsule.radius)) < 1e-4f);
    CHECK(std::abs(bounds.max.x - capsule.radius) < 1e-4f);

    desk.state.commands.endGesture();
    undo(scene, desk.state);
    CHECK(!scene.isAlive(walker));
    CHECK(!scene.isAlive(body));
}

TEST("a Trigger is one step: a static body whose box senses what moves into it") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId zone = createEntity(scene, desk.resources(), desk.state, EntityKind::Trigger);
    REQUIRE(scene.has<Rigidbody>(zone));
    CHECK(scene.get<Rigidbody>(zone).motion == RigidbodyMotion::Static);
    REQUIRE(scene.has<Collider>(zone));
    CHECK(scene.get<Collider>(zone).isTrigger);
    CHECK(scene.get<Collider>(zone).parts.size() == 1u);
    CHECK(!scene.has<Mesh>(zone));
}

TEST("Drop to Ground rests each selected subtree on what is below it, as one step") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    ResourceManager& resources = desk.resources();
    slab(desk, 1.0f);
    look(desk, {30.0f, 20.0f, 0.0f}, {30.0f, 30.0f, 0.0f}, 5.0f);
    const EntityId high = createEntity(scene, resources, desk.state, EntityKind::Cube);
    const EntityId bulb = createEntity(scene, resources, desk.state, EntityKind::Sphere, high);
    look(desk, {5.0f, 20.0f, 5.0f}, {5.0f, 30.0f, 5.0f}, 5.0f);
    const EntityId over = createEntity(scene, resources, desk.state, EntityKind::Cube);
    desk.state.commands.endGesture();
    const glm::vec3 highWas = at(desk, high);

    desk.state.selection.select(high);
    desk.state.selection.add(over);
    dropToGround(scene, resources, desk.state);
    // Off the slab the ground is the grid; over it, the slab's top. The child comes along.
    CHECK_NEAR(at(desk, high).y, 0.5f, 1e-4f);
    CHECK_NEAR(at(desk, over).y, 1.5f, 1e-4f);
    CHECK_NEAR(glm::vec3(HierarchyOperations::computeWorldMatrix(scene, bulb)[3]).y, 0.5f, 1e-4f);
    undo(scene, desk.state);
    CHECK(at(desk, high) == highWas);
    CHECK(at(desk, over).y > 20.0f);
}

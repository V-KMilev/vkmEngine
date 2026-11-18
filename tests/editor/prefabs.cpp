// The prefab workflow: Save as Prefab, Apply, Revert All and Unpack keep the scene and the prefab file
// in step, and a gesture whose result the scene could not store is refused.

#include <filesystem>
#include <string>
#include <vector>

#include "actions/component_actions.h"
#include "actions/copy_parent.h"
#include "actions/create_entity.h"
#include "actions/edit_actions.h"
#include "actions/prefab_actions.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/light/light.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/name.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/scene_serializer.h"
#include "undo/component_edit.h"
#include "undo/prefab_overrides.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;
using namespace Vkm::Engine::EditorActions;

namespace {

/// Name @p id as the Hierarchy's rename does.
void rename(Desk& desk, EntityId id, const char* name) {
    {
        EditScope<Name> named(desk.scene(), desk.resources(), desk.state, id, "Rename");
        *named = makeName(name);
    }
    desk.state.commands.endGesture();
}

/// A lamp with a bulb under it, named, made as the author makes them.
EntityId makeLamp(Desk& desk, const char* name) {
    const EntityId lamp = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::PointLight);
    rename(desk, lamp, name);
    createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Sphere, lamp);
    desk.state.commands.endGesture();
    return lamp;
}

void setIntensity(Desk& desk, EntityId id, float intensity) {
    {
        EditScope<Light> light(desk.scene(), desk.resources(), desk.state, id, "Edit Light");
        light->intensity = intensity;
    }
    desk.state.commands.endGesture();
}

/**
 * @brief The scene saved and read back into a world of its own, as the next open reads it.
 */
struct Reopened {
    Scene           scene;
    ResourceManager resources;

    explicit Reopened(Desk& desk, const std::filesystem::path& file) {
        REQUIRE(SceneSerializer::save(desk.scene(), desk.resources(), file.string()));
        REQUIRE(SceneSerializer::load(scene, resources, file.string()));
    }

    /// The light of the root named @p name.
    float intensityOf(const char* name) {
        float found = -1.0f;
        scene.forEach<Name, Light>([&](EntityId, const Name& n, const Light& light) {
            if (std::string(n.value) == name) found = light.intensity;
        });
        return found;
    }
};

} // namespace

TEST("Save as Prefab writes the subtree and makes it an instance, and another of its name takes a free one") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::filesystem::path root = openProject();
    const EntityId stray = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Cube);
    desk.state.commands.endGesture();
    const EntityId lamp = makeLamp(desk, "Lamp");
    const EntityId twin = makeLamp(desk, "Lamp");

    REQUIRE(saveAsPrefab(desk.scene(), desk.resources(), desk.state, lamp));
    REQUIRE(desk.scene().has<PrefabInstance>(lamp));
    CHECK(desk.scene().get<PrefabInstance>(lamp).source == "prefabs/Lamp.json");
    CHECK(std::filesystem::exists(root / "prefabs" / "Lamp.json"));
    const std::string first = readText(root / "prefabs" / "Lamp.json");

    REQUIRE(saveAsPrefab(desk.scene(), desk.resources(), desk.state, twin));
    CHECK(desk.scene().get<PrefabInstance>(twin).source == "prefabs/Lamp 2.json");
    CHECK(holds(root / "prefabs" / "Lamp.json", first));
    CHECK(desk.said("Saved prefab 'Lamp 2'"));
    CHECK(listPrefabs() == std::vector<std::string>{"prefabs/Lamp 2.json", "prefabs/Lamp.json"});

    // A step naming the saved subtrees would undo to values the scene no longer stores; the rest stay.
    CHECK(desk.state.commands.undoDepth() == 1);
    undo(desk.scene(), desk.state);
    CHECK(!desk.scene().isAlive(stray));

    // The scene now stores a reference, and the file builds the subtree again.
    Reopened again(desk, root / "scenes" / "lamps.json");
    int bulbs = 0;
    again.scene.forEach<PrefabInstance, Hierarchy>([&](
        EntityId,
        const PrefabInstance&,
        const Hierarchy& node
    ) {
        bulbs += node.firstChild ? 1 : 0;
    });
    CHECK(bulbs == 2);
}

TEST("an edit inside an instance is its override, Apply writes it to the prefab and Revert All takes it back") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::filesystem::path root = openProject();
    const EntityId lamp = makeLamp(desk, "Lamp");
    REQUIRE(saveAsPrefab(desk.scene(), desk.resources(), desk.state, lamp));
    const float authored = desk.scene().get<Light>(lamp).intensity;
    const EntityId other = placePrefab(desk.scene(), desk.resources(), desk.state, "prefabs/Lamp.json");
    REQUIRE(other);
    desk.state.commands.endGesture();

    setIntensity(desk, lamp, authored + 5.0f);
    const auto overrides = [&] { return desk.scene().get<PrefabInstance>(lamp).overrides; };
    REQUIRE(overrides().size() == 1);
    CHECK(overrides()[0].component == "Light");
    CHECK(overrides()[0].field == "intensity");
    undo(desk.scene(), desk.state);
    CHECK(overrides().empty());
    CHECK(desk.scene().get<Light>(lamp).intensity == authored);
    redo(desk.scene(), desk.state);
    CHECK(overrides().size() == 1);

    // Revert All: the prefab's value, as one step undo gives back.
    PrefabOverrides::revertAll(desk.scene(), desk.resources(), desk.state, lamp);
    CHECK(overrides().empty());
    CHECK(desk.scene().get<Light>(lamp).intensity == authored);
    undo(desk.scene(), desk.state);
    CHECK(desk.scene().get<Light>(lamp).intensity == authored + 5.0f);

    // Apply: the file takes the instance as it stands, and every instance built from it.
    REQUIRE(saveAsPrefab(desk.scene(), desk.resources(), desk.state, lamp));
    CHECK(overrides().empty());
    rename(desk, other, "Other");
    Reopened again(desk, root / "scenes" / "lamps.json");
    CHECK(again.intensityOf("Lamp") == authored + 5.0f);
    CHECK(again.intensityOf("Other") == authored + 5.0f);
}

TEST("inside an instance, what the scene could not store is refused and what it could is kept") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId lamp = makeLamp(desk, "Lamp");
    REQUIRE(saveAsPrefab(scene, desk.resources(), desk.state, lamp));
    const EntityId bulb     = scene.get<Hierarchy>(lamp).firstChild;
    const EntityId outsider = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    desk.state.commands.endGesture();
    const std::string before = desk.sceneText();
    const size_t depth = desk.state.commands.undoDepth();

    CHECK(!createEntity(scene, desk.resources(), desk.state, EntityKind::Cube, bulb));
    CHECK(desk.said("prefab"));
    reparentKeepingWorld(scene, desk.state, outsider, bulb, "Reparent");
    reparentKeepingWorld(scene, desk.state, bulb, {}, "Reparent");
    desk.state.selection.select(bulb);
    groupSelection(scene, desk.state);
    CHECK(!saveAsPrefab(scene, desk.resources(), desk.state, bulb));
    CHECK(desk.said("Save as Prefab on 'Lamp'"));
    CHECK(changes(before, desk.sceneText()) == "");
    CHECK(desk.state.commands.undoDepth() == depth);

    // The root moves anywhere; a component added inside works, and the toast says where it belongs.
    reparentKeepingWorld(scene, desk.state, lamp, outsider, "Reparent");
    CHECK(scene.get<Hierarchy>(lamp).parent == outsider);
    addComponent(scene, desk.state, bulb, Rigidbody{}, "Rigidbody", "Add Rigidbody");
    CHECK(scene.has<Rigidbody>(bulb));
    CHECK(desk.said("not stored in the scene"));
}

TEST("a duplicated instance is built from its prefab again with its overrides, and a deleted one comes back whole") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId lamp = makeLamp(desk, "Lamp");
    REQUIRE(saveAsPrefab(scene, desk.resources(), desk.state, lamp));
    setIntensity(desk, lamp, 42.0f);
    // The prefab changes on disk after the instance was built: its light turns red.
    const EntityId red = makeLamp(desk, "Red");
    {
        EditScope<Light> light(scene, desk.resources(), desk.state, red, "Edit Light");
        light->color = {1.0f, 0.0f, 0.0f};
    }
    REQUIRE(Prefab::save(scene, red, "prefabs/Lamp.json", desk.resources()));

    desk.state.selection.select(lamp);
    duplicateSelection(scene, desk.resources(), desk.state);
    desk.state.commands.endGesture();
    const EntityId copy = desk.state.selection.active();
    REQUIRE(copy);
    REQUIRE(copy != lamp);
    REQUIRE(scene.has<PrefabInstance>(copy));
    CHECK(scene.get<PrefabInstance>(copy).source == "prefabs/Lamp.json");
    CHECK(scene.get<PrefabInstance>(copy).overrides.size() == 1);
    CHECK(scene.get<Light>(copy).intensity == 42.0f);
    CHECK(scene.get<Light>(copy).color == glm::vec3(1.0f, 0.0f, 0.0f));
    CHECK(scene.get<Hierarchy>(copy).firstChild);

    // A part of an instance copied stands at the root, where it stood in the world.
    const EntityId bulb = scene.get<Hierarchy>(lamp).firstChild;
    desk.state.selection.select(bulb);
    duplicateSelection(scene, desk.resources(), desk.state);
    desk.state.commands.endGesture();
    const EntityId loose = desk.state.selection.active();
    REQUIRE(loose != bulb);
    const Hierarchy* link = scene.tryGet<Hierarchy>(loose);
    CHECK(!(link && link->parent));
    CHECK(!scene.has<PrefabEntity>(loose));
    undo(scene, desk.state);

    const std::string whole = desk.sceneText();
    deleteEntity(scene, desk.state, copy);
    desk.state.commands.endGesture();
    undo(scene, desk.state);
    CHECK(changes(whole, desk.sceneText()) == "");
}

TEST("Unpack keeps the entities, and the scene stores them instead of the prefab") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::filesystem::path root = openProject();
    const EntityId lamp = makeLamp(desk, "Lamp");
    REQUIRE(saveAsPrefab(desk.scene(), desk.resources(), desk.state, lamp));
    setIntensity(desk, lamp, 42.0f);

    unpackPrefab(desk.scene(), desk.state, lamp);
    CHECK(!desk.scene().has<PrefabInstance>(lamp));
    CHECK(desk.scene().get<Light>(lamp).intensity == 42.0f);

    // The prefab going away no longer touches the scene.
    std::filesystem::remove(root / "prefabs" / "Lamp.json");
    Reopened again(desk, root / "scenes" / "lamps.json");
    CHECK(again.intensityOf("Lamp") == 42.0f);
    int parts = 0;
    again.scene.forEachEntity([&](EntityId) { ++parts; });
    CHECK(parts == 2);

    // The subtree comes back in its slots, under new generations.
    undo(desk.scene(), desk.state);
    CHECK(desk.scene().has<PrefabInstance>(desk.scene().entityAt(lamp.slot())));
}

TEST("a joint into an instance holds what it held over Unpack, its undo and its redo") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId lamp = makeLamp(desk, "Lamp");
    REQUIRE(saveAsPrefab(scene, desk.resources(), desk.state, lamp));
    const EntityId bulb  = scene.get<Hierarchy>(lamp).firstChild;
    const EntityId frame = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    Joint joint;
    joint.connected = bulb;
    addComponent(scene, desk.state, frame, joint, "Joint", "Add Joint");
    desk.state.commands.endGesture();

    unpackPrefab(scene, desk.state, lamp);
    desk.state.commands.endGesture();
    const auto holdsBulb = [&] {
        const EntityId held = scene.get<Joint>(frame).connected;
        return scene.isAlive(held) && held.slot() == bulb.slot();
    };
    for (int time = 0; time < 2; ++time) {
        undo(scene, desk.state);
        CHECK(holdsBulb());
        redo(scene, desk.state);
        CHECK(holdsBulb());
    }
}

TEST("a Joint set to an entity of an instance just placed, both undone and both redone, holds it again") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    REQUIRE(saveAsPrefab(scene, desk.resources(), desk.state, makeLamp(desk, "Lamp")));
    // One frame holds the instance's root, the other an entity inside it.
    const EntityId frames[2] = {
        createEntity(scene, desk.resources(), desk.state, EntityKind::Cube),
        createEntity(scene, desk.resources(), desk.state, EntityKind::Cube),
    };
    for (const EntityId frame : frames) addComponent(scene, desk.state, frame, Joint{}, "Joint", "Add Joint");
    desk.state.commands.endGesture();
    const EntityId placed = placePrefab(scene, desk.resources(), desk.state, "prefabs/Lamp.json");
    REQUIRE(placed);
    desk.state.commands.endGesture();
    const EntityId held[2] = {placed, scene.get<Hierarchy>(placed).firstChild};
    for (int i = 0; i < 2; ++i) {
        {
            EditScope<Joint> joint(scene, desk.resources(), desk.state, frames[i], "Edit Joint");
            joint->connected = held[i];
        }
        desk.state.commands.endGesture();
    }

    for (int step = 0; step < 3; ++step) undo(scene, desk.state);
    REQUIRE(!scene.isAliveAtIndex(placed.slot()));
    for (int step = 0; step < 3; ++step) redo(scene, desk.state);
    const EntityId root = scene.entityAt(placed.slot());
    CHECK(scene.get<Joint>(frames[0]).connected == root);
    CHECK(scene.get<Joint>(frames[1]).connected == scene.get<Hierarchy>(root).firstChild);
    CHECK(scene.isAlive(scene.get<Joint>(frames[1]).connected));
}

TEST("Revert on a component the prefab no longer defines is refused, and the override kept") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId lamp = makeLamp(desk, "Lamp");
    REQUIRE(saveAsPrefab(scene, desk.resources(), desk.state, lamp));
    setIntensity(desk, lamp, 42.0f);

    // The prefab saved again from a subtree with no light, under the same name.
    const EntityId bare = createEntity(scene, desk.resources(), desk.state, EntityKind::Empty);
    REQUIRE(Prefab::save(scene, bare, "prefabs/Lamp.json", desk.resources()));
    desk.state.commands.endGesture();
    const size_t depth = desk.state.commands.undoDepth();

    PrefabOverrides::revertComponent(scene, desk.resources(), desk.state, lamp, "Light");
    PrefabOverrides::revertAll(scene, desk.resources(), desk.state, lamp);
    CHECK(scene.get<PrefabInstance>(lamp).overrides.size() == 1);
    CHECK(scene.get<Light>(lamp).intensity == 42.0f);
    CHECK(desk.said("no longer defines"));
    CHECK(desk.state.commands.undoDepth() == depth);
}

TEST("a copy of a part of an instance lands outside it, and nothing takes a new child inside one") {
    Scene scene;
    const EntityId house = scene.createEntity();
    const EntityId door  = scene.createEntity();
    const EntityId knob  = scene.createEntity();
    const EntityId shelf = scene.createEntity();
    scene.add(house, PrefabInstance{});
    HierarchyOperations::setParent(scene, door, house);
    HierarchyOperations::setParent(scene, knob, door);

    // The knob's copy asks for the knob's parent, as Paste in the same world and Duplicate do.
    CHECK(!copyParent(scene, door));
    CHECK(!copyParent(scene, house));
    CHECK(copyParent(scene, shelf) == shelf);
    // The one rule a create, a re-parent and a copy all ask.
    CHECK(refusesNewChild(scene, knob));
    CHECK(refusesNewChild(scene, house));
    CHECK(!refusesNewChild(scene, shelf));
    CHECK(!refusesNewChild(scene, EntityId{}));
    // A copy whose parent is gone goes to the root.
    scene.destroyEntity(shelf);
    CHECK(!copyParent(scene, shelf));
    CHECK(!copyParent(scene, EntityId{}));
}

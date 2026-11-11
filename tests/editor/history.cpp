// The editor's history: every edit the author makes is a step undo takes back and redo makes again,
// a gesture is one step, and the stack keeps the last 200.

#include <memory>
#include <string>
#include <vector>

#include "actions/component_actions.h"
#include "actions/create_entity.h"
#include "actions/edit_actions.h"
#include "actions/entity_clipboard.h"
#include "ecs/component/light/light.h"
#include "ecs/component/physics/collider.h"
#include "ecs/component/physics/joint.h"
#include "ecs/component/physics/rigidbody.h"
#include "ecs/component/render/mesh.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/ui/ui_slider.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/scene_serializer.h"
#include "ui/rows.h"
#include "undo/asset_commands.h"
#include "undo/component_edit.h"

#include "editor/desk.h"
#include "harness/check.h"

using namespace Vkm::Test;
using namespace Vkm::Engine::EditorActions;

namespace {

/// Move @p id to @p x as the Transform card's field does, one finished gesture.
void moveTo(Desk& desk, EntityId id, float x) {
    {
        EditScope<Transform> pose(desk.scene(), desk.resources(), desk.state, id, "Edit Transform");
        pose->position.x = x;
    }
    desk.state.commands.endGesture();
}

/// A door and the frame holding it by a Joint, made as the author makes them.
struct Hinge {
    EntityId door;
    EntityId frame;
};

Hinge makeHinge(Desk& desk) {
    Scene& scene = desk.scene();
    Hinge hinge;
    hinge.door  = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    hinge.frame = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    Joint joint;
    joint.connected = hinge.door;
    addComponent(scene, desk.state, hinge.frame, joint, "Joint", "Add Joint");
    desk.state.commands.endGesture();
    return hinge;
}

/// Whether the frame, in its slot, holds the door, in its slot, alive.
bool joined(const Scene& scene, const Hinge& hinge) {
    const Joint* joint = scene.tryGet<Joint>(scene.entityAt(hinge.frame.slot()));
    return joint && scene.isAlive(joint->connected) && joint->connected.slot() == hinge.door.slot();
}

/// Point the frame's Joint at @p body, as its card's Connected row does: one finished gesture.
void connect(Desk& desk, const Hinge& hinge, EntityId body) {
    {
        EditScope<Joint> joint(desk.scene(), desk.resources(), desk.state, hinge.frame, "Edit Joint");
        joint->connected = body;
    }
    desk.state.commands.endGesture();
}

/// Delete the door and undo it: it is back in its slot under an id no step made before has seen.
void bringBackDoor(Desk& desk, const Hinge& hinge) {
    deleteEntity(desk.scene(), desk.state, desk.scene().entityAt(hinge.door.slot()));
    desk.state.commands.endGesture();
    undo(desk.scene(), desk.state);
}

} // namespace

TEST("every edit undone puts the scene back as it was, and redone as it came to be") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    ResourceManager& resources = desk.resources();

    std::vector<std::string> seen = {desk.sceneText()};
    const auto done = [&] {
        desk.state.commands.endGesture();
        seen.push_back(desk.sceneText());
    };

    const EntityId cube = createEntity(scene, resources, desk.state, EntityKind::Cube);
    done();
    const EntityId lamp = createEntity(scene, resources, desk.state, EntityKind::PointLight);
    done();
    moveTo(desk, cube, 3.0f);
    seen.push_back(desk.sceneText());
    reparentKeepingWorld(scene, desk.state, lamp, cube, "Reparent");
    done();
    addComponent(scene, desk.state, cube, Rigidbody{}, "Rigidbody", "Add Rigidbody");
    done();
    desk.state.selection.select(cube);
    duplicateSelection(scene, resources, desk.state);
    done();
    groupSelection(scene, desk.state);
    done();
    desk.state.selection.select(cube);
    copySelection(scene, resources, desk.state);
    paste(scene, resources, desk.state);
    done();
    desk.state.selection.select(cube);
    deleteSelection(scene, desk.state);
    done();
    CHECK(!scene.isAlive(cube));
    REQUIRE(desk.state.commands.undoDepth() == seen.size() - 1);

    desk.state.sceneDirty = false;
    for (size_t step = seen.size() - 1; step > 0; --step) {
        undo(scene, desk.state);
        CHECK(changes(seen[step - 1], desk.sceneText()) == "");
    }
    CHECK(desk.state.sceneDirty);
    CHECK(!desk.state.commands.canUndo());
    for (size_t step = 1; step < seen.size(); ++step) {
        redo(scene, desk.state);
        CHECK(changes(seen[step], desk.sceneText()) == "");
    }
}

TEST("a new edit after an undo drops what was undone") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    const EntityId cube = createEntity(desk.scene(), desk.resources(), desk.state, EntityKind::Cube);
    desk.state.commands.endGesture();
    moveTo(desk, cube, 1.0f);
    moveTo(desk, cube, 2.0f);

    undo(desk.scene(), desk.state);
    CHECK(desk.state.commands.canRedo());
    moveTo(desk, cube, 5.0f);

    CHECK(!desk.state.commands.canRedo());
    redo(desk.scene(), desk.state);
    CHECK(desk.scene().get<Transform>(cube).position.x == 5.0f);
    undo(desk.scene(), desk.state);
    CHECK(desk.scene().get<Transform>(cube).position.x == 1.0f);
}

TEST("the history keeps the last 200 steps and lets the oldest go") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    const EntityId cube = desk.scene().createEntity();
    desk.scene().add(cube, Transform{});

    for (int edit = 1; edit <= 205; ++edit) moveTo(desk, cube, static_cast<float>(edit));
    CHECK(desk.state.commands.undoDepth() == 200);

    while (desk.state.commands.canUndo()) undo(desk.scene(), desk.state);
    // The first five steps are gone: the oldest kept puts the cube where the fifth left it.
    CHECK(desk.scene().get<Transform>(cube).position.x == 5.0f);
    CHECK(desk.state.commands.redoDepth() == 200);
}

TEST("a drag is one step, and the next drag of the same field is another") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId cube = scene.createEntity();
    scene.add(cube, Transform{});
    const MaterialHandle paint = desk.resources().add(MaterialAsset{}, "paint");

    // A field dragged over ten frames pushes each frame, as the gizmo and the cards do.
    const auto drag = [&](float from, float to) {
        for (int frame = 1; frame <= 10; ++frame) {
            Transform& pose     = scene.get<Transform>(cube);
            const Transform was = pose;
            pose.position.x = from + (to - from) * frame / 10.0f;
            pushEdit(scene, desk.resources(), desk.state, cube, was, pose, "Move");
        }
        desk.state.commands.endGesture();
    };
    // And a material's slider, as its window does.
    const auto slide = [&](float to) {
        for (int frame = 1; frame <= 10; ++frame) {
            MaterialAsset before(desk.resources().get(paint));
            MaterialAsset after(before);
            after.roughness = before.roughness + (to - before.roughness) * frame / 10.0f;
            MaterialAsset live(after);
            desk.resources().swapValue(paint, live);
            desk.state.pushStep(std::make_unique<MaterialEditCommand>(
                desk.resources(),
                paint,
                std::move(before),
                std::move(after),
                "Edit Material"
            ));
        }
        desk.state.commands.endGesture();
    };

    drag(0.0f, 4.0f);
    drag(4.0f, 9.0f);
    slide(0.9f);
    CHECK(desk.state.commands.undoDepth() == 3);

    undo(scene, desk.state);
    CHECK_NEAR(desk.resources().get(paint).roughness, 0.5f, 1e-6f);
    undo(scene, desk.state);
    CHECK(scene.get<Transform>(cube).position.x == 4.0f);
    undo(scene, desk.state);
    CHECK(scene.get<Transform>(cube).position.x == 0.0f);
    redo(scene, desk.state);
    redo(scene, desk.state);
    redo(scene, desk.state);
    CHECK(scene.get<Transform>(cube).position.x == 9.0f);
    CHECK_NEAR(desk.resources().get(paint).roughness, 0.9f, 1e-6f);
}

TEST("an edit carried to every selected entity is one step") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    std::vector<EntityId> lamps;
    for (int i = 0; i < 3; ++i) {
        lamps.push_back(scene.createEntity());
        scene.add(lamps.back(), Transform{});
        scene.add(lamps.back(), Light{});
    }
    for (const EntityId lamp : lamps) desk.state.selection.add(lamp);

    // The intensity row, edited on the active lamp and shared by its card with the rest.
    Light& active    = scene.get<Light>(lamps.back());
    const Light was  = active;
    active.intensity = 7.0f;
    const uint64_t row = RowsDetail::shareBit<Light, &Light::intensity>();
    desk.state.pushStep(editSelectionStep(
        scene,
        desk.resources(),
        desk.state.selection.entities(),
        lamps.back(),
        was,
        active,
        "Edit Light",
        row
    ));

    for (const EntityId lamp : lamps) CHECK(scene.get<Light>(lamp).intensity == 7.0f);
    CHECK(desk.state.commands.undoDepth() == 1);
    undo(scene, desk.state);
    for (const EntityId lamp : lamps) CHECK(scene.get<Light>(lamp).intensity == was.intensity);
}

TEST("a scale dragged in the Transform card carries into every selected box's collider, one step for all") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    std::vector<EntityId> boxes;
    for (const float wide : {1.0f, 2.0f}) {
        boxes.push_back(scene.createEntity());
        Transform pose;
        pose.scale = {wide, 1.0f, 1.0f};
        scene.add(boxes.back(), pose);
        Collider collider;
        collider.parts[0].halfExtents = {0.5f * wide, 0.5f, 0.5f};
        scene.add(boxes.back(), collider);
        desk.state.selection.add(boxes.back());
    }
    const EntityId active = boxes.front();
    desk.state.selection.makeActive(active);

    // Its scale's Y dragged from 1 to 3 over ten frames, a step a frame, as the card pushes them.
    for (int frame = 1; frame <= 10; ++frame) {
        Transform& pose     = scene.get<Transform>(active);
        const Transform was = pose;
        pose.scale.y = 1.0f + 2.0f * frame / 10.0f;
        desk.state.pushStep(transformSelectionStep(
            scene,
            desk.resources(),
            desk.state.selection.entities(),
            active,
            was,
            pose,
            "Edit Transform"
        ));
    }
    desk.state.commands.endGesture();

    // Each box keeps its own width and takes the height, and its collider with it.
    CHECK(desk.state.commands.undoDepth() == 1);
    CHECK_NEAR(scene.get<Collider>(boxes[0]).parts[0].halfExtents, glm::vec3(0.5f, 1.5f, 0.5f), 1e-5f);
    CHECK_NEAR(scene.get<Collider>(boxes[1]).parts[0].halfExtents, glm::vec3(1.0f, 1.5f, 0.5f), 1e-5f);
    undo(scene, desk.state);
    CHECK(scene.get<Transform>(boxes[1]).scale == glm::vec3(2.0f, 1.0f, 1.0f));
    CHECK(scene.get<Collider>(boxes[0]).parts[0].halfExtents == glm::vec3(0.5f, 0.5f, 0.5f));
    CHECK(scene.get<Collider>(boxes[1]).parts[0].halfExtents == glm::vec3(1.0f, 0.5f, 0.5f));
    redo(scene, desk.state);
    CHECK_NEAR(scene.get<Collider>(boxes[1]).parts[0].halfExtents, glm::vec3(1.0f, 1.5f, 0.5f), 1e-5f);

    // A move takes no collider along: the parts are already where the entity is.
    Transform& pose     = scene.get<Transform>(active);
    const Transform was = pose;
    pose.position.x = 4.0f;
    const Collider before = scene.get<Collider>(active);
    desk.state.pushStep(transformStep(scene, desk.resources(), active, was, pose, "Edit Transform"));
    CHECK(scene.get<Collider>(active).parts[0].halfExtents == before.parts[0].halfExtents);
}

TEST("a deleted entity comes back in its slot, and the steps before the delete find it again") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId cube = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    desk.state.commands.endGesture();
    moveTo(desk, cube, 2.0f);
    deleteEntity(scene, desk.state, cube);
    desk.state.commands.endGesture();

    undo(scene, desk.state);
    REQUIRE(scene.isAliveAtIndex(cube.slot()));
    const EntityId back = scene.entityAt(cube.slot());
    CHECK(back != cube);
    CHECK(scene.get<Transform>(back).position.x == 2.0f);
    undo(scene, desk.state);
    CHECK(scene.get<Transform>(back).position.x != 2.0f);
    undo(scene, desk.state);
    CHECK(!scene.isAlive(back));
}

TEST("a group of entities under different parents undone puts each back under its own") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const auto place = [&](float x, EntityId parent) {
        const EntityId id = scene.createEntity();
        Transform pose;
        pose.position.x = x;
        scene.add(id, pose);
        if (parent) HierarchyOperations::setParent(scene, id, parent);
        return id;
    };
    const EntityId left  = place(-10.0f, {});
    const EntityId right = place(10.0f, {});
    const EntityId a     = place(1.0f, left);
    const EntityId b     = place(2.0f, right);

    desk.state.selection.select(a);
    desk.state.selection.add(b);
    groupSelection(scene, desk.state);
    desk.state.commands.endGesture();
    undo(scene, desk.state);

    CHECK(scene.get<Hierarchy>(a).parent == left);
    CHECK(scene.get<Hierarchy>(b).parent == right);
    CHECK(scene.get<Transform>(a).position.x == 1.0f);
    CHECK(scene.get<Transform>(b).position.x == 2.0f);
}

TEST("a deleted entity undone is named again by what held it, each time it comes back") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId door  = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    const EntityId frame = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    Joint hinge;
    hinge.connected = door;
    addComponent(scene, desk.state, frame, hinge, "Joint", "Add Joint");
    desk.state.commands.endGesture();

    deleteEntity(scene, desk.state, door);
    desk.state.commands.endGesture();
    for (int time = 0; time < 2; ++time) {
        undo(scene, desk.state);
        const EntityId back = scene.get<Joint>(frame).connected;
        CHECK(scene.isAlive(back));
        CHECK(back.slot() == door.slot());
        redo(scene, desk.state);
    }
}

TEST("a deleted entity undone after Stop is named again by what held it") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const Hinge hinge = makeHinge(desk);
    UISlider slider;
    slider.knob = hinge.door;
    addComponent(scene, desk.state, hinge.frame, slider, "UISlider", "Add Slider");
    deleteEntity(scene, desk.state, hinge.door);
    desk.state.commands.endGesture();

    REQUIRE(desk.startPlay());
    desk.stop();
    undo(scene, desk.state);
    CHECK(joined(scene, hinge));
    const EntityId knob = scene.get<UISlider>(hinge.frame).knob;
    CHECK(scene.isAlive(knob));
    CHECK(knob.slot() == hinge.door.slot());
}

TEST("a joint and its other body deleted together come back joined, in either selection order") {
    for (const bool doorFirst : {true, false}) {
        Bench bench;
        Desk& desk = bench.desk;
        openProject();
        Scene& scene = desk.scene();
        const Hinge hinge = makeHinge(desk);

        desk.state.selection.select(doorFirst ? hinge.door : hinge.frame);
        desk.state.selection.add(doorFirst ? hinge.frame : hinge.door);
        deleteSelection(scene, desk.state);
        desk.state.commands.endGesture();
        for (int time = 0; time < 2; ++time) {
            undo(scene, desk.state);
            CHECK(joined(scene, hinge));
            redo(scene, desk.state);
        }
    }
}

TEST("a joint and its other body deleted one after the other come back joined, in either order") {
    for (const bool doorFirst : {true, false}) {
        Bench bench;
        Desk& desk = bench.desk;
        openProject();
        Scene& scene = desk.scene();
        const Hinge hinge = makeHinge(desk);

        deleteEntity(scene, desk.state, doorFirst ? hinge.door : hinge.frame);
        desk.state.commands.endGesture();
        deleteEntity(scene, desk.state, doorFirst ? hinge.frame : hinge.door);
        desk.state.commands.endGesture();
        for (int time = 0; time < 2; ++time) {
            undo(scene, desk.state);
            undo(scene, desk.state);
            CHECK(joined(scene, hinge));
            redo(scene, desk.state);
            redo(scene, desk.state);
        }
    }
}

TEST("a Joint an undo or redo writes back holds its other body as it came back since the step was made") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const Hinge hinge = makeHinge(desk);
    connect(desk, hinge, {});
    connect(desk, hinge, hinge.door);
    bringBackDoor(desk, hinge);

    // The second edit undone and redone, then the first undone, then the add undone and redone.
    undo(scene, desk.state);
    redo(scene, desk.state);
    CHECK(joined(scene, hinge));
    undo(scene, desk.state);
    undo(scene, desk.state);
    CHECK(joined(scene, hinge));
    undo(scene, desk.state);
    redo(scene, desk.state);
    CHECK(joined(scene, hinge));

    // Removed, as its card's Remove does, then put back.
    const Joint removed = scene.get<Joint>(hinge.frame);
    scene.remove<Joint>(hinge.frame);
    desk.state.pushStep(std::make_unique<RemoveComponentCommand<Joint>>(hinge.frame, removed, "Remove"));
    desk.state.commands.endGesture();
    bringBackDoor(desk, hinge);
    undo(scene, desk.state);
    CHECK(joined(scene, hinge));
}

TEST("a Joint set to an entity just made, both undone and both redone, holds it again") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const EntityId frame = createEntity(scene, desk.resources(), desk.state, EntityKind::Cube);
    addComponent(scene, desk.state, frame, Joint{}, "Joint", "Add Joint");
    desk.state.commands.endGesture();
    const Hinge hinge{createEntity(scene, desk.resources(), desk.state, EntityKind::Cube), frame};
    desk.state.commands.endGesture();
    connect(desk, hinge, hinge.door);

    undo(scene, desk.state);
    undo(scene, desk.state);
    REQUIRE(!scene.isAliveAtIndex(hinge.door.slot()));
    redo(scene, desk.state);
    redo(scene, desk.state);
    CHECK(joined(scene, hinge));
}

TEST("a world the game loads in play is named by its own ids, not by what came back in the one before") {
    Bench bench;
    Desk& desk = bench.desk;
    const std::string level = (openProject() / "level.json").string();
    Scene& scene = desk.scene();
    const Hinge hinge = makeHinge(desk);
    deleteEntity(scene, desk.state, hinge.door);
    desk.state.commands.endGesture();
    undo(scene, desk.state);
    REQUIRE(SceneSerializer::save(scene, desk.resources(), level));

    // The game reads the level in play: the same slots, their ids numbered afresh.
    REQUIRE(desk.startPlay());
    REQUIRE(SceneSerializer::load(scene, desk.resources(), level));
    REQUIRE(joined(scene, hinge));
    deleteEntity(scene, desk.state, scene.entityAt(hinge.frame.slot()));
    desk.state.commands.endGesture();
    undo(scene, desk.state);
    CHECK(joined(scene, hinge));
}

TEST("a step made before Play writes, after Stop, the entity it named, under the id it had") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    // The door takes a deleted entity's slot, so its id is past the slot's first generation.
    deleteEntity(scene, desk.state, createEntity(scene, desk.resources(), desk.state, EntityKind::Cube));
    desk.state.commands.endGesture();
    const Hinge hinge = makeHinge(desk);
    REQUIRE(hinge.door.generation() > 0);
    connect(desk, hinge, {});
    connect(desk, hinge, hinge.door);

    REQUIRE(desk.startPlay());
    desk.stop();
    undo(scene, desk.state);
    redo(scene, desk.state);
    CHECK(scene.isAlive(hinge.door));
    CHECK(scene.get<Joint>(hinge.frame).connected == hinge.door);
}

TEST("an entity brought back before Play is followed after Stop, and not as play brought it back") {
    Bench bench;
    Desk& desk = bench.desk;
    openProject();
    Scene& scene = desk.scene();
    const Hinge hinge = makeHinge(desk);
    connect(desk, hinge, {});
    connect(desk, hinge, hinge.door);
    bringBackDoor(desk, hinge);
    const EntityId door = scene.entityAt(hinge.door.slot());

    // Play brings the door back again, under an id the world Stop puts back never had.
    REQUIRE(desk.startPlay());
    bringBackDoor(desk, hinge);
    desk.stop();

    // The edit naming the door as it was made writes it as it came back before Play.
    undo(scene, desk.state);
    redo(scene, desk.state);
    CHECK(scene.isAlive(door));
    CHECK(scene.get<Joint>(hinge.frame).connected == door);
}

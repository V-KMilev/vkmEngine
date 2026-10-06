#include "support.h"

#include <nlohmann/json.hpp>

#include "command/command_host.h"
#include "command/command_stack.h"
#include "command/component_edit.h"
#include "command/editor_commands.h"
#include "command/prefab_overrides.h"
#include "editor_state.h"
#include "session/play_snapshot.h"

#include "ecs/component/prefab/prefab_entity.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/render/camera.h"
#include "ecs/component/render/light.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/scene_serializer.h"

namespace {

// What a command may reach outside the scene; the editor passes its EditorState.
// This one records what it was told, for assertions to read back.
class RecordingHost : public CommandHost {
    public:
        RecordingHost() = default;
        ~RecordingHost() override = default;

        RecordingHost(const RecordingHost& other) = delete;
        RecordingHost& operator=(const RecordingHost& other) = delete;

        RecordingHost(RecordingHost && other) = delete;
        RecordingHost& operator=(RecordingHost && other) = delete;

    public:
        void markSceneDirty() override { dirtied++; }
        void pushToast(ToastKind kind, std::string message, float) override {
            toasts.emplace_back(kind, std::move(message));
        }
        void selectEntity(EntityId id) override { selected = id; }
        CommandStack& commandStack() override { return stack; }

    public:
        CommandStack stack;
        EntityId     selected{};
        int          dirtied = 0;
        std::vector<std::pair<ToastKind, std::string>> toasts;
};

EntityId oneEntityAt(Scene& scene, const glm::vec3& position) {
    const EntityId id = scene.createEntity();
    Transform t;
    t.position = position;
    scene.add(id, std::move(t));
    return id;
}

void testAnEditIsUndoneAndRedoneWhole() {
    std::printf("An edit taken back and put again:\n");

    Scene scene;
    ResourceManager resources;
    RecordingHost host;
    const EntityId id = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});

    const Transform before = scene.get<Transform>(id);
    scene.get<Transform>(id).position = {5.0f, 0.0f, 0.0f};
    pushEdit<Transform>(scene, resources, host, id, before, scene.get<Transform>(id), "Move");

    check("the edit is on the history", host.stack.canUndo());
    check("  and named there", std::string(host.stack.undoLabel()) == "Move");
    check("  and the scene knows it is unsaved", host.dirtied == 1);
    check("  with nothing to redo yet", !host.stack.canRedo());

    host.stack.undo(scene, host);
    check("undo puts the value back", nearly(scene.get<Transform>(id).position.x, 0.0f));
    check("  and moves the step to the redo side", !host.stack.canUndo() && host.stack.canRedo());

    host.stack.redo(scene, host);
    check("redo puts it forward again", nearly(scene.get<Transform>(id).position.x, 5.0f));
    check("  and the step is back on the undo side", host.stack.canUndo() && !host.stack.canRedo());
}

// A play session edits the simulation's copy, which Stop throws away. Its steps are
// its own: one Ctrl+Z too many must not undo an authored step against the simulated
// world, and a step taken there must not cost the history from before Play.
void testAPlaySessionKeepsItsStepsToItself() {
    std::printf("Undo inside a play session:\n");

    Scene scene;
    ResourceManager resources;
    RecordingHost host;
    const EntityId id = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});

    Transform before = scene.get<Transform>(id);
    scene.get<Transform>(id).position = {5.0f, 0.0f, 0.0f};
    pushEdit<Transform>(scene, resources, host, id, before, scene.get<Transform>(id), "Authored");
    host.stack.endGesture();

    host.stack.park();
    check(
        "Play starts the session on an empty history",
        host.stack.isParked() && !host.stack.canUndo() && !host.stack.canRedo()
    );

    host.stack.undo(scene, host);
    check(
        "  so an undo there cannot reach the authored step",
        nearly(scene.get<Transform>(id).position.x, 5.0f)
    );

    before = scene.get<Transform>(id);
    scene.get<Transform>(id).position = {9.0f, 0.0f, 0.0f};
    pushEdit<Transform>(scene, resources, host, id, before, scene.get<Transform>(id), "Session");
    host.stack.endGesture();
    host.stack.undo(scene, host);
    check("the session's own step still undoes", nearly(scene.get<Transform>(id).position.x, 5.0f));
    host.stack.undo(scene, host);
    check("  and the next undo stops there", nearly(scene.get<Transform>(id).position.x, 5.0f));

    host.stack.unpark();
    check(
        "Stop discards the session's history and brings the authored one back",
        !host.stack.isParked()
            && !host.stack.canRedo()
            && host.stack.canUndo()
            && std::string(host.stack.undoLabel()) == "Authored"
    );
    host.stack.undo(scene, host);
    check("  which undoes as it did before Play", nearly(scene.get<Transform>(id).position.x, 0.0f));

    host.stack.park();
    host.stack.clear();
    host.stack.unpark();
    check(
        "a swap inside a session drops the parked history with the rest",
        !host.stack.isParked() && !host.stack.canUndo() && !host.stack.canRedo()
    );
}

// Undo then a new edit drops what was undone: a surviving redo would put back a
// value the author has since edited past.
void testANewEditDropsWhatWasUndone() {
    std::printf("Editing after an undo:\n");

    Scene scene;
    ResourceManager resources;
    RecordingHost host;
    const EntityId id = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});

    const Transform origin = scene.get<Transform>(id);
    scene.get<Transform>(id).position = {5.0f, 0.0f, 0.0f};
    pushEdit<Transform>(scene, resources, host, id, origin, scene.get<Transform>(id), "Move");
    host.stack.undo(scene, host);
    check("there is something to redo", host.stack.canRedo());

    const Transform now = scene.get<Transform>(id);
    scene.get<Transform>(id).position = {0.0f, 9.0f, 0.0f};
    pushEdit<Transform>(scene, resources, host, id, now, scene.get<Transform>(id), "Lift");
    check("a new edit drops the redo history", !host.stack.canRedo());
    check("  and is itself undoable", std::string(host.stack.undoLabel()) == "Lift");

    host.stack.undo(scene, host);
    check(
        "undoing it lands on what was there, not on the abandoned branch",
        nearly(scene.get<Transform>(id).position.y, 0.0f) && nearly(scene.get<Transform>(id).position.x, 0.0f)
    );
}

// A drag writes a step per frame; merging makes it one step, and the gesture boundary
// stops the *next* drag joining it.
void testADragIsOneUndoStep() {
    std::printf("A drag that writes every frame:\n");

    Scene scene;
    ResourceManager resources;
    RecordingHost host;
    const EntityId id = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});

    const Transform start = scene.get<Transform>(id);
    for (int frame = 1; frame <= 8; ++frame) {
        const Transform was = scene.get<Transform>(id);
        scene.get<Transform>(id).position.x = static_cast<float>(frame);
        pushEdit<Transform>(scene, resources, host, id, was, scene.get<Transform>(id), "Move");
    }
    check("eight frames of dragging are one step", host.stack.undoDepth() == 1);

    // Released, then dragged again: two gestures, two steps. The release alone separates
    // them, since undo() and redo() also shut the merge window.
    host.stack.endGesture();
    const Transform afterFirst = scene.get<Transform>(id);
    scene.get<Transform>(id).position.x = 99.0f;
    pushEdit<Transform>(scene, resources, host, id, afterFirst, scene.get<Transform>(id), "Move");
    check("a second gesture is a second step", host.stack.undoDepth() == 2);

    host.stack.undo(scene, host);
    check(
        "  and undoing it stops at the end of the first",
        nearly(scene.get<Transform>(id).position.x, afterFirst.position.x)
    );
    host.stack.undo(scene, host);
    check(
        "  and undoing that one goes back to where the drag started",
        nearly(scene.get<Transform>(id).position.x, start.position.x)
    );
}

// The Animation panel edits the keyframes and the Transform the pose writes each frame,
// pushed as one composite. Two plain steps would alternate at the top, beyond any
// merge, so a few seconds' drag would evict everything before it.
void testAPosingDragIsOneUndoStep() {
    std::printf("A drag that edits a clip and the pose it writes:\n");

    Scene scene;
    ResourceManager resources;
    RecordingHost host;
    const EntityId id = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});
    scene.add(id, Animation{});

    const Transform earlier = scene.get<Transform>(id);
    scene.get<Transform>(id).position.y = 1.0f;
    pushEdit<Transform>(scene, resources, host, id, earlier, scene.get<Transform>(id), "Lift");
    host.stack.endGesture();

    const float startSpeed = scene.get<Animation>(id).speed;
    constexpr int FRAMES = 300;
    for (int frame = 1; frame <= FRAMES; ++frame) {
        const Animation anim = scene.get<Animation>(id);
        const Transform pose = scene.get<Transform>(id);
        scene.get<Animation>(id).speed += 0.01f;
        auto step = std::make_unique<CompositeCommand>("Edit Animation");
        step->add(
            editStep<Animation>(scene, resources, id, anim, scene.get<Animation>(id), "Edit Animation")
        );
        // Every other frame the moved key leaves the pose where it was.
        if (frame % 2 == 0) {
            scene.get<Transform>(id).position.x = static_cast<float>(frame);
            step->add(
                editStep<Transform>(scene, resources, id, pose, scene.get<Transform>(id), "Edit Animation")
            );
        }
        host.stack.push(std::move(step));
    }
    check("a frame-by-frame drag is one step beside what came before it", host.stack.undoDepth() == 2);

    host.stack.undo(scene, host);
    check("  undoing it restores the clip", nearly(scene.get<Animation>(id).speed, startSpeed));
    check(
        "  and the pose it wrote",
        nearly(scene.get<Transform>(id).position.x, 0.0f) && nearly(scene.get<Transform>(id).position.y, 1.0f)
    );

    host.stack.undo(scene, host);
    check(
        "  and the step before the drag is still there to undo",
        nearly(scene.get<Transform>(id).position.y, 0.0f)
    );
}

// The history drops its oldest steps rather than growing without limit. Measured,
// not compared against the constant: it must stop growing, and drop whole steps off
// the far end.
void testTheHistoryIsBoundedFromTheOldEnd() {
    std::printf("A history longer than the limit:\n");

    Scene scene;
    ResourceManager resources;
    RecordingHost host;
    const EntityId id = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});

    // More steps than any sane limit; if this stops exceeding it, the first check says so.
    constexpr size_t PUSHES = 1000;
    for (size_t i = 0; i < PUSHES; ++i) {
        const Transform was = scene.get<Transform>(id);
        scene.get<Transform>(id).position.x = static_cast<float>(i + 1);
        pushEdit<Transform>(scene, resources, host, id, was, scene.get<Transform>(id), "Move");
        host.stack.endGesture();   // each is its own step, or they would merge
    }

    const size_t kept = host.stack.undoDepth();
    std::printf("      %zu pushed, %zu kept\n", PUSHES, kept);
    check("the history stops growing", kept > 0 && kept < PUSHES);

    // Every kept step still undoes, and the last lands exactly where the oldest kept
    // step began: whole commands came off the old end, leaving no hole.
    for (size_t i = 0; i < kept; ++i) host.stack.undo(scene, host);
    check("  and every step it kept undoes", !host.stack.canUndo());
    check(
        "  landing where the oldest kept step began",
        nearly(scene.get<Transform>(id).position.x, static_cast<float>(PUSHES - kept))
    );
}

// A command addressing a gone entity loses work: undoing it would write onto whatever
// that slot holds now.
void testStepsAddressingAGoneEntityAreForgotten() {
    std::printf("History pointing at an entity that is gone:\n");

    Scene scene;
    ResourceManager resources;
    RecordingHost host;
    const EntityId kept  = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});
    const EntityId doomed = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});

    const Transform a = scene.get<Transform>(kept);
    scene.get<Transform>(kept).position.x = 1.0f;
    pushEdit<Transform>(scene, resources, host, kept, a, scene.get<Transform>(kept), "Move kept");
    host.stack.endGesture();

    const Transform b = scene.get<Transform>(doomed);
    scene.get<Transform>(doomed).position.x = 2.0f;
    pushEdit<Transform>(scene, resources, host, doomed, b, scene.get<Transform>(doomed), "Move doomed");
    host.stack.endGesture();
    check("both steps are on the history", host.stack.undoDepth() == 2);

    host.stack.forget({doomed.slot()});
    check("the step addressing the gone entity is dropped", host.stack.undoDepth() == 1);
    check("  and the one that does not is kept", std::string(host.stack.undoLabel()) == "Move kept");

    host.stack.undo(scene, host);
    check(
        "  which still undoes onto the entity it meant",
        nearly(scene.get<Transform>(kept).position.x, 0.0f)
    );
}

// Undoing a delete puts each entity back at its slot. A slot no longer free cannot
// take it back: during play Delete is not gated, and the game's next spawn can take it
// before Ctrl+Z. Later passes must skip a refused slot rather than ask the scene,
// which would hand back the stranger - restamped into a Joint, or parented in.
void testUndoingADeleteDoesNotAdoptWhoeverTookTheSlot() {
    std::printf("Undoing a delete onto a slot something else has taken:\n");

    Scene scene;
    const EntityId root  = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});
    const EntityId child = oneEntityAt(scene, {1.0f, 0.0f, 0.0f});
    HierarchyOperations::setParent(scene, child, root);

    const uint32_t rootSlot  = root.slot();
    const uint32_t childSlot = child.slot();

    const SubtreeSnapshot snapshot = SubtreeSnapshot::capture(scene, root);
    check("the subtree was captured whole", snapshot.nodes.size() == 2);

    scene.destroyEntity(child);
    scene.destroyEntity(root);

    // Somebody else takes the child's slot before the undo.
    const EntityId squatter = scene.createEntityAt(childSlot);
    check("a stranger holds the child's slot", squatter && squatter.slot() == childSlot);
    Transform elsewhere;
    elsewhere.position = {9.0f, 9.0f, 9.0f};
    scene.add(squatter, std::move(elsewhere));

    snapshot.apply(scene);

    const EntityId back = scene.entityAt(rootSlot);
    check("the root still comes back", scene.isAlive(back));
    check("  and the stranger is still alive", scene.isAlive(squatter));

    // It was never part of this subtree.
    const Hierarchy* adopted = scene.tryGet<Hierarchy>(squatter);
    check("  and it has not been adopted into the subtree", adopted == nullptr || !adopted->parent);
    check("  and it still holds its own transform", nearly(scene.get<Transform>(squatter).position.x, 9.0f));

    const Hierarchy* rootNode = scene.tryGet<Hierarchy>(back);
    check(
        "  so the root comes back with no child rather than a stranger's",
        rootNode == nullptr || !rootNode->firstChild
    );
}

// @p parent's children's names, in hierarchy order.
std::vector<std::string> childNames(const Scene& scene, EntityId parent) {
    std::vector<std::string> names;
    HierarchyOperations::forEachChild(scene, parent, [&](EntityId id) {
        const Name* name = scene.tryGet<Name>(id);
        names.push_back(name ? name->value : "?");
    });
    return names;
}

// Sibling order is what the hierarchy panel lists and a prefab's entity order is read
// back in, so a restored subtree keeps its captured order - see SubtreeSnapshot::apply.
void testADeletedSubtreeComesBackInOrder() {
    std::printf("A deleted subtree put back:\n");

    Scene scene;
    const EntityId root = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});

    const auto under = [&](const char* name, EntityId parent) {
        const EntityId id = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});
        scene.add(id, makeName(name));
        HierarchyOperations::setParent(scene, id, parent);
        return id;
    };
    const EntityId first = under("First", root);
    under("Second", root);
    under("Third", root);
    under("Under First", first);

    const std::vector<std::string> before = childNames(scene, root);
    check("the root has three children in a definite order", before.size() == 3);

    const uint32_t rootSlot  = root.slot();
    const uint32_t firstSlot = first.slot();

    const SubtreeSnapshot snapshot = SubtreeSnapshot::capture(scene, root);
    check("  and the capture holds all five entities", snapshot.nodes.size() == 5);

    HierarchyOperations::destroyHierarchy(scene, root);
    check("  which the delete takes away", !scene.isAliveAtIndex(rootSlot));

    snapshot.apply(scene);
    const EntityId back = scene.entityAt(rootSlot);
    check("undo brings the root back", scene.isAlive(back));
    check("  with its children in the order they were in", childNames(scene, back) == before);
    check(
        "  and the grandchild still under the child that had it",
        scene.isAliveAtIndex(firstSlot)
            && childNames(scene, scene.entityAt(firstSlot)) == std::vector<std::string>{"Under First"}
    );
}

// One command driven as the editor drives it: what Delete does, what undo gives
// back, and the selection following the entity rather than the slot.
void testDeleteIsOneStepThatTakesTheSelectionWithIt() {
    std::printf("Delete, pushed the way the editor pushes it:\n");

    Scene scene;
    RecordingHost host;
    const EntityId root  = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});
    const EntityId child = oneEntityAt(scene, {1.0f, 0.0f, 0.0f});
    HierarchyOperations::setParent(scene, child, root);
    host.selectEntity(root);

    const uint32_t rootSlot  = root.slot();
    const uint32_t childSlot = child.slot();

    auto command = std::make_unique<DestroySubtreeCommand>(
        SubtreeSnapshot::capture(scene, root),
        host.selected,
        "Delete"
    );
    command->redo(scene, host);
    host.commandStack().push(std::move(command));
    host.markSceneDirty();

    check(
        "the whole subtree goes, not just the entity",
        !scene.isAliveAtIndex(rootSlot) && !scene.isAliveAtIndex(childSlot)
    );
    check("  the selection names no live entity", !scene.isAlive(host.selected));
    check("  and it is one step", host.stack.undoDepth() == 1);

    host.stack.undo(scene, host);
    check("undo brings both back", scene.isAliveAtIndex(rootSlot) && scene.isAliveAtIndex(childSlot));
    check("  re-linked, not loose", childrenOf(scene, scene.entityAt(rootSlot)) == 1);
    // Back at its slot with a bumped generation, so the selection gets the live id,
    // not the recorded one.
    check(
        "  and selected again, by the id it has now",
        host.selected == scene.entityAt(rootSlot) && scene.isAlive(host.selected)
    );

    host.stack.redo(scene, host);
    check("redo takes it away again", !scene.isAliveAtIndex(rootSlot));
}

// What Duplicate is built on. Snapshots are shaped for undo, where slots are free and
// referenced entities gone; a copy stands beside the original, so every slot is taken
// and every reference still names something alive. Both fail silently: the copy
// lands on no slot, or its ragdoll drives the original's bones.
void testACopyStandsBesideTheOriginalRatherThanOnIt() {
    std::printf("A subtree copied into fresh slots:\n");

    Scene scene;
    const EntityId parent = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});
    const EntityId root   = oneEntityAt(scene, {1.0f, 0.0f, 0.0f});
    const EntityId bone   = oneEntityAt(scene, {2.0f, 0.0f, 0.0f});
    scene.add(root, makeName("Rig"));
    scene.add(bone, makeName("Bone"));
    HierarchyOperations::setParent(scene, root, parent);
    HierarchyOperations::setParent(scene, bone, root);

    // An in-subtree reference, as a ragdoll and a joint both carry.
    Joint joint;
    joint.connected = bone;
    scene.add(root, std::move(joint));

    const SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, root);
    const EntityId copy = snap.apply(scene, SnapshotSlots::Fresh);

    check("the copy is a new entity", copy && copy != root && scene.isAlive(copy));
    check("  and the original is untouched", scene.isAlive(root) && scene.isAlive(bone));
    check("  the copy carries the descendant too", childrenOf(scene, copy) == 1);
    check(
        "  under the original's own parent",
        scene.get<Hierarchy>(copy).parent == parent && childrenOf(scene, parent) == 2
    );

    const EntityId copiedBone = scene.get<Hierarchy>(copy).firstChild;
    check(
        "  and its joint names its own bone, not the original's",
        scene.get<Joint>(copy).connected == copiedBone && copiedBone != bone
    );

    // Applied the other way - an undone delete - it still wants its original slots.
    HierarchyOperations::destroyHierarchy(scene, root);
    const EntityId back = snap.apply(scene, SnapshotSlots::Reuse);
    check(
        "reused, the same capture lands back on the slot it came from",
        back && back.slot() == snap.nodes.front().snap.slotIndex
    );
}

// The mirror of delete, and the one step that creates a subtree: undo takes it all
// away, and redo puts it back on the same slots, or a later step misses.
void testADuplicateIsOneStepThatTakesTheWholeCopyBack() {
    std::printf("A created subtree taken back and put again:\n");

    Scene scene;
    RecordingHost host;
    const EntityId root  = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});
    const EntityId child = oneEntityAt(scene, {1.0f, 0.0f, 0.0f});
    HierarchyOperations::setParent(scene, child, root);

    const SubtreeSnapshot source = SubtreeSnapshot::capture(scene, root);
    const EntityId copy = source.apply(scene, SnapshotSlots::Fresh);
    const uint32_t copySlot  = copy.slot();
    const uint32_t cloneKid  = scene.get<Hierarchy>(copy).firstChild.slot();
    host.selectEntity(copy);

    host.stack.push(
        std::make_unique<CreateSubtreeCommand>(SubtreeSnapshot::capture(scene, copy), "Duplicate Entity")
    );

    host.stack.undo(scene, host);
    check(
        "undo takes the whole copy, not just its root",
        !scene.isAliveAtIndex(copySlot) && !scene.isAliveAtIndex(cloneKid)
    );
    check("  the selection names no live entity", !scene.isAlive(host.selected));
    check("  and the original is still there", scene.isAlive(root) && childrenOf(scene, root) == 1);

    host.stack.redo(scene, host);
    check(
        "redo puts it back on the slots it had",
        scene.isAliveAtIndex(copySlot) && scene.isAliveAtIndex(cloneKid)
    );
    check("  re-linked, not loose", childrenOf(scene, scene.entityAt(copySlot)) == 1);
    check(
        "  and selected again, by the id it has now",
        host.selected == scene.entityAt(copySlot) && scene.isAlive(host.selected)
    );
}

// Delete and an undone Duplicate both take a subtree away; a selected descendant must
// go with it either way, not stay active as a dead id.
// A command destroys without deselecting: pruneSelection, run each frame before any
// panel reads the selection, drops what a delete or an undo took away.
void testAPruneDropsWhatADeleteTookAway() {
    std::printf("A selection inside a subtree that is deleted:\n");

    Scene scene;
    RecordingHost host;
    EditorState state;
    const EntityId root  = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});
    const EntityId child = oneEntityAt(scene, {1.0f, 0.0f, 0.0f});
    const EntityId other = oneEntityAt(scene, {2.0f, 0.0f, 0.0f});
    HierarchyOperations::setParent(scene, child, root);

    state.selectEntity(other);
    state.addToSelection(child);
    DestroySubtreeCommand remove(SubtreeSnapshot::capture(scene, root), state.selectedEntity, "Delete");
    remove.redo(scene, host);
    state.pruneSelection(scene);
    check("the deleted child leaves the selection", !state.isSelected(child) && state.selection.size() == 1);
    check("  and the active entity falls back to what is left", state.selectedEntity == other);
}

// A create outside the history can take the slot a deleted entity's undo awaits, since
// freed slots are reused LIFO. Import Model is such a create, so it pushes a
// CreateSubtreeCommand and the two undos unwind in order.
void testACreateAfterADeleteUndoesInOrder() {
    std::printf("Something created into the slot a delete freed:\n");

    Scene scene;
    RecordingHost host;
    const EntityId deleted = oneEntityAt(scene, {3.0f, 0.0f, 0.0f});
    const uint32_t slot    = deleted.slot();

    auto remove = std::make_unique<DestroySubtreeCommand>(
        SubtreeSnapshot::capture(scene, deleted),
        EntityId{},
        "Delete"
    );
    remove->redo(scene, host);
    host.stack.push(std::move(remove));
    host.stack.endGesture();

    const EntityId imported = oneEntityAt(scene, {7.0f, 0.0f, 0.0f});
    check("the created entity took the freed slot", imported.slot() == slot);
    host.stack.push(
        std::make_unique<CreateSubtreeCommand>(SubtreeSnapshot::capture(scene, imported), "Import Model")
    );

    host.stack.undo(scene, host);
    check("the first undo takes the created entity away", !scene.isAliveAtIndex(slot));
    host.stack.undo(scene, host);
    check(
        "  and the second brings the deleted one back into its slot",
        scene.isAliveAtIndex(slot) && nearly(scene.get<Transform>(scene.entityAt(slot)).position.x, 3.0f)
    );

    host.stack.redo(scene, host);
    host.stack.redo(scene, host);
    check(
        "redoing both lands on the created entity, not the deleted one",
        scene.isAliveAtIndex(slot) && nearly(scene.get<Transform>(scene.entityAt(slot)).position.x, 7.0f)
    );
}

// A prefab on disk and one instance: the smallest world where an override means anything.
struct Instanced {
    ScratchProject  dir{"vkm_undo_tests"};
    ResourceManager resources;
    Scene           scene;
    std::string     path = "prefabs/lamp.json";
    EntityId        root{};
    EntityId        lamp{};
    uint32_t        lampUid = 0;

    Instanced() {
        Scene authored;
        const EntityId post = authored.createEntity();
        authored.add(post, Transform{});
        const EntityId bulb = authored.createEntity();
        authored.add(bulb, Transform{});

        Light light;
        light.intensity = 3.0f;
        authored.add(bulb, std::move(light));
        HierarchyOperations::setParent(authored, bulb, post);

        Prefab::save(authored, post, path, resources);
        lampUid = authored.get<PrefabEntity>(bulb).uid;

        root = Prefab::instantiate(scene, resources, path);
        if (root) HierarchyOperations::forEachChild(scene, root, [&](EntityId id) { lamp = id; });
    }
};

// A scene stores an instance as a reference and rebuilds it from the prefab, so an
// edit not recorded as an override is not stored; hence editStep asks PrefabOverrides first.
void testAnEditInsideAnInstanceIsRecordedAsAnOverride() {
    std::printf("An edit inside a prefab instance:\n");

    Instanced world;
    RecordingHost host;
    check("the instance built", world.root && world.lamp && world.lampUid != PrefabEntity::ROOT);

    const Light was = world.scene.get<Light>(world.lamp);
    world.scene.get<Light>(world.lamp).intensity = 11.0f;
    pushEdit<Light>(
        world.scene,
        world.resources,
        host,
        world.lamp,
        was,
        world.scene.get<Light>(world.lamp),
        "Intensity"
    );

    const std::vector<PrefabOverride>& list =
        world.scene.get<PrefabInstance>(world.root).overrides;
    check(
        "the edit is stored as an override rather than as a component",
        list.size() == 1
            && list.front().uid == world.lampUid
            && list.front().component == "Light"
            && list.front().field == "intensity"
    );
    check(
        "  addressed by the key the serializer writes, so the prefab can resolve it",
        PrefabOverrides::overriddenFields(world.scene, world.lamp, "Light")
            == std::vector<std::string>{"intensity"}
    );
    check(
        "  and it is one step on the history, with the scene marked unsaved",
        host.stack.undoDepth() == 1 && host.dirtied == 1
    );

    host.stack.undo(world.scene, host);
    check(
        "undo re-reads the field from the prefab",
        nearly(world.scene.get<Light>(world.lamp).intensity, 3.0f)
    );
    check("  and takes the entry with it", world.scene.get<PrefabInstance>(world.root).overrides.empty());

    host.stack.redo(world.scene, host);
    check(
        "redo puts both back",
        nearly(world.scene.get<Light>(world.lamp).intensity, 11.0f)
            && world.scene.get<PrefabInstance>(world.root).overrides.size() == 1
    );
}

// Edits that look like overrides and are not. Declining is not refusing: editStep
// records the edit the normal way instead.
void testTheEditsAnInstanceDoesNotRecordAsOverrides() {
    std::printf("What an instance declines to call an override:\n");

    Instanced world;
    Scene& scene = world.scene;

    // The root's pose is the scene's own, restored after the prefab is built, so an
    // override on it could never take.
    const Transform was = scene.get<Transform>(world.root);
    Transform moved = was;
    moved.position = {5.0f, 0.0f, 0.0f};
    check(
        "the instance root's own Transform is not an override",
        PrefabOverrides::record<Transform>(scene, world.resources, world.root, was, moved, "Move") == nullptr
    );

    const Transform childWas = scene.get<Transform>(world.lamp);
    Transform childMoved = childWas;
    childMoved.position = {0.0f, 1.0f, 0.0f};
    check(
        "  while a child's Transform, which the prefab defines, is",
        PrefabOverrides::record<Transform>(scene, world.resources, world.lamp, childWas, childMoved, "Move")
            != nullptr
    );

    // An override is a delta against the prefab's value; a component the prefab lacks
    // has none.
    scene.add(world.lamp, Camera{});
    const Camera cameraWas = scene.get<Camera>(world.lamp);
    Camera cameraNow = cameraWas;
    cameraNow.fovY = 1.0f;
    check(
        "  a component the prefab never held is not",
        PrefabOverrides::record<Camera>(scene, world.resources, world.lamp, cameraWas, cameraNow, "FOV")
            == nullptr
    );

    const EntityId loose = oneEntityAt(scene, {0.0f, 0.0f, 0.0f});
    const Transform looseWas = scene.get<Transform>(loose);
    Transform looseNow = looseWas;
    looseNow.position = {1.0f, 0.0f, 0.0f};
    check(
        "  and an entity that is in no instance is not",
        PrefabOverrides::record<Transform>(scene, world.resources, loose, looseWas, looseNow, "Move")
            == nullptr
    );

    // The inspector asks the recorder's question and must agree, or a field shows a
    // revert arrow that does nothing.
    check(
        "and the root's Transform is not listed as overridden either",
        PrefabOverrides::overriddenFields(scene, world.root, "Transform").empty()
    );
}

// Dropping an override re-reads the prefab's definition, not an undo - and is refused
// when the prefab has since lost the component.
void testRevertingAnOverrideGivesTheFieldBackOrRefusesTo() {
    std::printf("Reverting an override:\n");

    Instanced world;
    RecordingHost host;
    Scene& scene = world.scene;

    const Light was = scene.get<Light>(world.lamp);
    scene.get<Light>(world.lamp).intensity = 11.0f;
    pushEdit<Light>(scene, world.resources, host, world.lamp, was, scene.get<Light>(world.lamp), "Intensity");
    host.stack.endGesture();

    PrefabOverrides::revert(scene, world.resources, host, world.lamp, "Light", "intensity");
    check("the field holds the prefab's value again", nearly(scene.get<Light>(world.lamp).intensity, 3.0f));
    check("  the entry is gone", scene.get<PrefabInstance>(world.root).overrides.empty());
    check(
        "  and the revert is a step of its own",
        host.stack.undoDepth() == 2 && std::string(host.stack.undoLabel()) == "Revert Override"
    );

    host.stack.undo(scene, host);
    check(
        "undoing the revert puts the override back",
        nearly(scene.get<Light>(world.lamp).intensity, 11.0f)
            && scene.get<PrefabInstance>(world.root).overrides.size() == 1
    );

    // The prefab loses the component underneath, as when edited in another window.
    const std::filesystem::path file = world.dir.root() / "prefabs" / "lamp.json";
    nlohmann::json doc;
    {
        std::ifstream in(file);
        in >> doc;
    }
    for (nlohmann::json& entry : doc["entities"]) entry["components"].erase("Light");
    {
        std::ofstream out(file);
        out << doc.dump(2);
    }

    const size_t steps = host.stack.undoDepth();
    host.toasts.clear();
    PrefabOverrides::revert(scene, world.resources, host, world.lamp, "Light", "intensity");
    check(
        "a prefab that has lost the component refuses the revert",
        scene.get<PrefabInstance>(world.root).overrides.size() == 1
            && nearly(scene.get<Light>(world.lamp).intensity, 11.0f)
    );
    check(
        "  and says so rather than dropping the entry silently",
        host.toasts.size() == 1 && host.toasts.front().first == ToastKind::Warning
    );
    check("  with nothing pushed", host.stack.undoDepth() == steps);
}

// Delete a loose entity through the editor's step.
void deleteThroughHistory(Scene& scene, RecordingHost& host, EntityId id) {
    SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, id);
    HierarchyOperations::destroyHierarchy(scene, id);
    host.stack.push(std::make_unique<DestroySubtreeCommand>(std::move(snap), EntityId{}, "Delete Entity"));
}

// Stop keeps a history when the session authored nothing, and the history names slots.
// An instance's entities are not in the scene file, so a restore into whatever was
// free could put the lamp in a deleted entity's slot - undo could not bring the
// deleted back, and redo would destroy the lamp.
void testStopPutsAnInstanceBackInTheSlotsItHeld() {
    std::printf("Play and Stop around a history, with a prefab instance in the world:\n");

    Instanced world;
    RecordingHost host;
    Scene& scene = world.scene;
    const uint32_t lampSlot = world.lamp.slot();

    const EntityId gone = scene.createEntity();
    scene.add(gone, Transform{});
    scene.add(gone, makeName("Gone"));
    const EntityId kept = scene.createEntity();
    scene.add(kept, Transform{});
    scene.add(kept, makeName("Kept"));
    const uint32_t goneSlot = gone.slot();
    check("the deleted entity sits below the highest saved slot", goneSlot < kept.slot());

    deleteThroughHistory(scene, host, gone);

    PlaySnapshot snapshot;
    check("Play captures the world", snapshot.capture(scene, world.resources, false));
    check("Stop restores it", snapshot.restoreInto(scene, world.resources));
    check("  and says every slot came back", snapshot.restoredInPlace(scene));

    const PrefabEntity* lamp = scene.isAliveAtIndex(lampSlot)
        ? scene.tryGet<PrefabEntity>(scene.entityAt(lampSlot)) : nullptr;
    check("the lamp is rebuilt into the slot it held", lamp && lamp->uid == world.lampUid);
    check("  leaving the deleted entity's slot free", !scene.isAliveAtIndex(goneSlot));

    host.stack.undo(scene, host);
    check(
        "undoing the delete brings the entity back into its slot",
        scene.isAliveAtIndex(goneSlot)
            && std::string(scene.get<Name>(scene.entityAt(goneSlot)).value) == "Gone"
    );

    host.stack.redo(scene, host);
    check("redoing it takes that entity again", !scene.isAliveAtIndex(goneSlot));
    check(
        "  and not the lamp",
        scene.isAliveAtIndex(lampSlot) && scene.has<PrefabEntity>(scene.entityAt(lampSlot))
    );
}

// What the snapshot cannot put back: a prefab changed on disk builds other entities,
// so the caller must be told and drop a history those slots would betray.
void testStopSaysWhenAPrefabCameBackDifferent() {
    std::printf("Stop after the prefab changed on disk:\n");

    Instanced world;
    PlaySnapshot snapshot;
    check("Play captures the world", snapshot.capture(world.scene, world.resources, false));

    // Re-saved with the lamp renumbered: to an override, an entity lost and another gained.
    const std::filesystem::path file = world.dir.root() / "prefabs" / "lamp.json";
    nlohmann::json doc;
    {
        std::ifstream in(file);
        in >> doc;
    }
    doc["entities"][1]["uid"] = 99;
    doc["nextUid"] = 100;
    {
        std::ofstream out(file);
        out << doc.dump(2);
    }

    check("Stop restores it", snapshot.restoreInto(world.scene, world.resources));
    check("  and says the instance did not come back in place", !snapshot.restoredInPlace(world.scene));
}

// The backstop: a step that destroys by slot checks the slot still holds its entity.
void testAStepRefusesAnEntityItWasNotMadeAgainst() {
    std::printf("A step whose slot another entity took:\n");

    Scene scene;
    RecordingHost host;
    const EntityId original = scene.createEntity();
    scene.add(original, Transform{});
    scene.add(original, makeName("Original"));
    const uint32_t slot = original.slot();

    deleteThroughHistory(scene, host, original);
    host.stack.undo(scene, host);
    check("the delete is undone", scene.isAliveAtIndex(slot));

    // The world is rebuilt under the history, and another entity takes the slot.
    scene.destroyEntity(scene.entityAt(slot));
    const EntityId stranger = scene.createEntityAt(slot);
    scene.add(stranger, Transform{});
    scene.add(stranger, makeName("Stranger"));

    host.toasts.clear();
    host.stack.redo(scene, host);
    check(
        "redoing the delete leaves the other entity alone",
        scene.isAlive(stranger) && std::string(scene.get<Name>(stranger).value) == "Stranger"
    );
    check(
        "  and says it skipped the step",
        host.toasts.size() == 1 && host.toasts.front().first == ToastKind::Error
    );

    host.toasts.clear();
    host.stack.undo(scene, host);
    check(
        "undoing it into a taken slot refuses instead of asserting",
        scene.isAlive(stranger) && host.toasts.size() == 1
    );
}

// Set as Main Camera flips every camera's flag, each flip an ordinary edit. Written
// directly, a camera in a prefab instance would change on screen but never become an
// override, and the scene, storing instances as overrides, would lose it on save.
void testSetAsMainCameraInsideAnInstanceIsKept() {
    std::printf("Set as Main Camera on a camera inside a prefab instance:\n");

    ScratchProject dir{"vkm_undo_camera_tests"};
    ResourceManager resources;
    RecordingHost host;
    const std::string path = "prefabs/rig.json";

    Scene authored;
    const EntityId rig = authored.createEntity();
    authored.add(rig, Transform{});
    const EntityId lens = authored.createEntity();
    authored.add(lens, Transform{});
    Camera inactive;
    inactive.active = false;
    authored.add(lens, Camera{inactive});
    HierarchyOperations::setParent(authored, lens, rig);
    check("the prefab saves", Prefab::save(authored, rig, path, resources));

    Scene scene;
    const EntityId loose = scene.createEntity();
    scene.add(loose, Transform{});
    scene.add(loose, Camera{});
    const EntityId root = Prefab::instantiate(scene, resources, path);
    EntityId inside{};
    if (root) HierarchyOperations::forEachChild(scene, root, [&](EntityId id) { inside = id; });
    check(
        "the instance's camera starts inactive",
        inside && !scene.get<Camera>(inside).active && scene.get<Camera>(loose).active
    );

    pushActiveCamera(scene, resources, host, inside);
    check(
        "the instance's camera is the main one now",
        scene.get<Camera>(inside).active && !scene.get<Camera>(loose).active
    );
    check("  as one step, with the scene marked unsaved", host.stack.undoDepth() == 1 && host.dirtied == 1);
    const std::vector<PrefabOverride>& list = scene.get<PrefabInstance>(root).overrides;
    check(
        "  recorded as an override on the instance",
        list.size() == 1 && list.front().component == "Camera" && list.front().field == "active"
    );

    Scene reloaded;
    check(
        "the scene keeps it across a save and a load",
        SceneSerializer::loadFromString(SceneSerializer::saveToString(scene, resources), reloaded, resources)
    );
    bool insideActive = false;
    reloaded.forEach<Camera>([&](EntityId id, const Camera& c) {
        if (Prefab::isInsideInstance(reloaded, id)) insideActive = c.active;
    });
    check("  with the instance's camera still the main one", insideActive);

    host.stack.undo(scene, host);
    check("undo puts both flags back", !scene.get<Camera>(inside).active && scene.get<Camera>(loose).active);
    check("  and takes the override with it", scene.get<PrefabInstance>(root).overrides.empty());

    host.stack.redo(scene, host);
    pushActiveCamera(scene, resources, host, inside);
    check("setting the camera that already is the main one pushes nothing", host.stack.undoDepth() == 1);
}

// Duplicating a parented instance pushes a placement; its pose is parent-local, so a
// redo rebuilding the copy at the root would make it jump.
void testARedonePlacementGoesBackUnderItsParent() {
    std::printf("Redoing the placement of an instance that had a parent:\n");

    Instanced world;
    RecordingHost host;
    Scene& scene = world.scene;

    const EntityId holder = oneEntityAt(scene, {10.0f, 0.0f, 0.0f});
    const EntityId copy = scene.createEntity();
    Transform at;
    at.position = {1.0f, 0.0f, 0.0f};
    scene.add(copy, Transform{at});
    const PrefabInstance instance = scene.get<PrefabInstance>(world.root);
    scene.add(copy, PrefabInstance{instance});
    check(
        "the copy builds",
        Prefab::instantiateInto(scene, world.resources, instance.source, copy, instance.overrides)
    );
    HierarchyOperations::setParent(scene, copy, holder);
    const uint32_t copySlot = copy.slot();
    host.stack.push(
        std::make_unique<PlacePrefabCommand>(
            world.resources,
            instance,
            copy,
            at,
            "Duplicate Entity",
            holder.slot()
        )
    );

    host.stack.undo(scene, host);
    check("undo takes the copy away", !scene.isAliveAtIndex(copySlot));

    host.stack.redo(scene, host);
    const Hierarchy* node = scene.isAliveAtIndex(copySlot)
        ? scene.tryGet<Hierarchy>(scene.entityAt(copySlot)) : nullptr;
    check("redo puts it back under the entity it hung off", node && node->parent == holder);
    check(
        "  at the pose it had there",
        nearly(scene.get<Transform>(scene.entityAt(copySlot)).position.x, 1.0f)
    );
}

// Undoing a placement takes the whole instance away; an entity selected inside it
// goes with it, as it does for a deleted subtree.

} // namespace

void runUndoTests() {
    testAnEditIsUndoneAndRedoneWhole();
    testAPlaySessionKeepsItsStepsToItself();
    testANewEditDropsWhatWasUndone();
    testADragIsOneUndoStep();
    testAPosingDragIsOneUndoStep();
    testTheHistoryIsBoundedFromTheOldEnd();
    testStepsAddressingAGoneEntityAreForgotten();
    testUndoingADeleteDoesNotAdoptWhoeverTookTheSlot();
    testADeletedSubtreeComesBackInOrder();
    testDeleteIsOneStepThatTakesTheSelectionWithIt();
    testACopyStandsBesideTheOriginalRatherThanOnIt();
    testADuplicateIsOneStepThatTakesTheWholeCopyBack();
    testAPruneDropsWhatADeleteTookAway();
    testACreateAfterADeleteUndoesInOrder();
    testAnEditInsideAnInstanceIsRecordedAsAnOverride();
    testTheEditsAnInstanceDoesNotRecordAsOverrides();
    testRevertingAnOverrideGivesTheFieldBackOrRefusesTo();
    testStopPutsAnInstanceBackInTheSlotsItHeld();
    testStopSaysWhenAPrefabCameBackDifferent();
    testAStepRefusesAnEntityItWasNotMadeAgainst();
    testSetAsMainCameraInsideAnInstanceIsKept();
    testARedonePlacementGoesBackUnderItsParent();
}

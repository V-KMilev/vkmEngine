#include "actions/edit_actions.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <vector>

#include <glm/glm.hpp>

#include "undo/component_edit.h"
#include "undo/entity_commands.h"
#include "undo/entity_snapshot.h"
#include "undo/composite_command.h"
#include "undo/component_commands.h"
#include "editor_state.h"
#include "view/view_placement.h"
#include "undo/prefab_overrides.h"
#include "actions/copy_parent.h"
#include "ecs/scene.h"
#include "ecs/component/scene/hierarchy.h"
#include "ecs/component/scene/transform.h"
#include "ecs/component/prefab/prefab_instance.h"
#include "ecs/component/ui/ui_canvas.h"
#include "ecs/component/ui/ui_element.h"
#include "ecs/hierarchy_operations.h"
#include "io/scene/prefab.h"
#include "ecs/prefab_instances.h"
#include "resource/resource_manager.h"
#include "actions/entity_copy.h"
namespace Vkm::Engine {
namespace EditorActions {

void reparentKeepingWorld(
    Scene& scene,
    EditorState& state,
    EntityId child,
    EntityId newParent,
    const char* label,
    EntityId before
) {
    if (!scene.isAlive(child) || newParent == child || before == child) return;

    // Refused here although setParent declines a cycle too: the rest would still
    // rebase and push a step for a move that never happened.
    if (HierarchyOperations::isAncestorOf(scene, child, newParent)) {
        state.toasts.push(ToastKind::Warning, "An entity cannot be moved under one of its own descendants");
        return;
    }

    Transform* local = scene.tryGet<Transform>(child);
    const bool hasTransform = local != nullptr;

    // An instance's interior belongs to its prefab, so neither move survives a
    // load; see docs/reference/editor.md, "What an instance will not let you do".
    if (refusesNewChild(scene, newParent)) {
        const char* message =
            "A prefab instance is built from its prefab - an entity "
            "moved into one is not saved with the scene";
        state.toasts.push(ToastKind::Warning, message);
        return;
    }
    const EntityId owningInstance = Prefab::instanceRootOf(scene, child);
    if (owningInstance && owningInstance != child) {
        const char* message =
            "This entity belongs to a prefab - move the instance root, or change the prefab and save it";
        state.toasts.push(ToastKind::Warning, message);
        return;
    }

    EntityId oldParent{};
    EntityId oldNext{};
    if (const Hierarchy* node = scene.tryGet<Hierarchy>(child)) {
        oldParent = node->parent;
        oldNext   = node->nextSibling;
    }
    const bool toParent = newParent && scene.isAlive(newParent);
    // Only a child of the new parent places it; anything else puts it last.
    const Hierarchy* beside = scene.tryGet<Hierarchy>(before);
    const EntityId   next   = toParent && beside && beside->parent == newParent ? before : EntityId{};
    // Dropped where it already stands: under the same parent, before the same sibling (roots have no order).
    const EntityId target = toParent ? newParent : EntityId{};
    if (oldParent == target && (oldNext == next || !target)) return;

    // The world matrix is what stays fixed across the move; `pose` is for undo.
    const Transform pose  = hasTransform ? *local : Transform{};
    const glm::mat4 world = hasTransform
        ? HierarchyOperations::computeWorldMatrix(scene, child)
        : glm::mat4(1.0f);

    if (toParent) {
        HierarchyOperations::setParent(scene, child, newParent, next);
    } else {
        HierarchyOperations::removeFromParent(scene, child);
    }

    Transform after = pose;
    if (hasTransform) {
        // Unparenting to the root leaves local == world.
        glm::mat4 rebased = world;
        if (toParent && scene.has<Transform>(newParent)) {
            rebased = glm::inverse(HierarchyOperations::computeWorldMatrix(scene, newParent)) * world;
        }
        *local = Transform::fromModelMatrix(rebased);
        after = *local;
    }

    state.pushStep(
        std::make_unique<ReparentCommand>(
            child,
            oldParent,
            oldNext,
            toParent ? newParent : EntityId{},
            next,
            pose,
            after,
            label
        )
    );

    if (scene.has<UIElement>(child) && !hasCanvasAncestor(scene, child)) {
        const char* message =
            "UI elements are drawn by the canvas above them - this one has "
            "no canvas ancestor now, so it will not appear";
        state.toasts.push(ToastKind::Warning, message);
    }
}

namespace {
constexpr float DUPLICATE_OFFSET_X = 1.0f;

// duplicateEntity documents what a copy is.
Duplicate duplicateOne(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId source,
    bool inPlace
) {
    const float offset = inPlace ? 0.0f : DUPLICATE_OFFSET_X;
    // Beside the source, under its parent, since the pose is local to it; out of an instance's
    // interior, at the root where the source stands in the world (copyParent).
    const Hierarchy* node = scene.tryGet<Hierarchy>(source);
    const EntityId wasUnder = node ? node->parent : EntityId{};
    const EntityId parent   = copyParent(scene, wasUnder);
    const bool toRoot = parent != wasUnder;
    if (const PrefabInstance* held = scene.tryGet<PrefabInstance>(source)) {
        const Transform* from = scene.tryGet<Transform>(source);
        Transform at = toRoot ? worldPoseOf(scene, source) : from ? *from : Transform{};
        at.position.x += offset;
        const PrefabInstance instance = *held;
        return placeInstance(scene, resources, state, instance, at, parent, "Duplicate Entity");
    }

    SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, source);
    if (snap.nodes.empty()) return {};

    // The root alone: its descendants are placed relative to it.
    std::optional<Transform>& rootPose = snap.nodes.front().snap.get<Transform>();
    if (toRoot) {
        snap.rootParentSlot = 0;
        snap.rootNextSlot   = 0;
        if (rootPose) rootPose = worldPoseOf(scene, source);
    }
    if (rootPose) rootPose->position.x += offset;
    readyCopy(snap);

    const EntityId copy = snap.apply(scene, SnapshotSlots::Fresh);
    if (!copy) return {};
    SubtreeSnapshot placed = SubtreeSnapshot::capture(scene, copy);
    return {copy, std::make_unique<CreateSubtreeCommand>(std::move(placed), "Duplicate Entity")};
}
} // namespace

void duplicateEntity(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId source,
    bool inPlace
) {
    Duplicate copy = duplicateOne(scene, resources, state, source, inPlace);
    if (!copy.entity) return;

    state.pushStep(std::move(copy.step));
    state.selectEntity(copy.entity);
}

void duplicateSelection(Scene& scene, ResourceManager& resources, EditorState& state, bool inPlace) {
    if (state.selection.entities().size() <= 1) {
        const EntityId active = state.selection.active();
        if (active) duplicateEntity(scene, resources, state, active, inPlace);
        return;
    }

    const std::vector<EntityId> sources = state.selection.entities();
    const EntityId active = state.selection.active();
    EntityId                    activeCopy{};
    auto batch = std::make_unique<CompositeCommand>("Duplicate Selection");
    std::vector<EntityId> clones;
    clones.reserve(sources.size());
    for (EntityId src : sources) {
        if (!scene.isAlive(src) || hasSelectedAncestor(scene, state, src)) continue;
        Duplicate copy = duplicateOne(scene, resources, state, src, inPlace);
        if (!copy.entity) continue;
        if (src == active) activeCopy = copy.entity;
        batch->add(std::move(copy.step));
        clones.push_back(copy.entity);
    }
    if (clones.empty()) return;

    state.pushStep(std::move(batch));

    // The active copy is the active original's, so a gizmo held on it stays on it.
    state.selectEntity(clones.front());
    for (size_t i = 1; i < clones.size(); ++i) state.selection.add(clones[i]);
    state.selection.makeActive(activeCopy ? activeCopy : clones.front());
}

bool hasSelectedAncestor(const Scene& scene, const EditorState& state, EntityId id) {
    bool found = false;
    HierarchyOperations::forSelfAndAncestors(scene, id, [&](EntityId at) {
        found = at != id && state.selection.contains(at);
        return !found;
    });
    return found;
}

namespace {
// An instance's interior comes back on every load, so this delete is undone by
// the next unless the instance is written back - a warning, not a refusal.
void warnDeleteInsideInstance(const Scene& scene, EditorState& state, EntityId entity) {
    if (!Prefab::isInsideInstance(scene, entity)) return;
    const char* message =
        "This entity belongs to a prefab - it comes back on the next "
        "load unless the instance root is saved as a prefab";
    state.toasts.push(ToastKind::Warning, message);
}
} // namespace

void groupSelection(Scene& scene, EditorState& state) {
    std::vector<EntityId> roots;
    for (const EntityId id : state.selection.entities()) {
        if (scene.isAlive(id) && !hasSelectedAncestor(scene, state, id)) roots.push_back(id);
    }
    // A group made inside an instance, or a part of one moved under a group, is gone on the next
    // load: the instance is built from its prefab.
    for (const EntityId root : roots) {
        const EntityId owner  = Prefab::instanceRootOf(scene, root);
        const Hierarchy* node = scene.tryGet<Hierarchy>(root);
        if ((owner && owner != root) || (node && refusesNewChild(scene, node->parent))) {
            const char* message =
                "This entity belongs to a prefab - group the instance root, or change the prefab and save it";
            state.toasts.push(ToastKind::Warning, message);
            return;
        }
    }
    if (const EntityId group = pushGroup(scene, state, roots)) state.selectEntity(group);
}

void deleteSelection(Scene& scene, EditorState& state) {
    if (state.selection.entities().size() <= 1) {
        if (state.selection.active()) deleteEntity(scene, state, state.selection.active());
        return;
    }

    // Its roots, while it is still the selection: the deselect below empties it.
    std::vector<EntityId> roots;
    for (EntityId id : state.selection.entities()) {
        if (scene.isAlive(id) && !hasSelectedAncestor(scene, state, id)) roots.push_back(id);
    }

    const EntityId priorSel = state.selection.active();
    state.selection.clear();

    auto batch = std::make_unique<CompositeCommand>("Delete Selection");
    for (EntityId id : roots) {
        warnDeleteInsideInstance(scene, state, id);
        SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, id);
        HierarchyOperations::destroyHierarchy(scene, id);
        batch->add(std::make_unique<DestroySubtreeCommand>(std::move(snap), priorSel, "Delete Entity"));
    }
    if (batch->empty()) return;

    state.pushStep(std::move(batch));
}

void selectAll(const Scene& scene, EditorState& state) {
    const EntityId active = state.selection.active();
    state.selection.clear();
    scene.forEachEntity([&](EntityId id) { state.selection.add(id); });
    if (scene.isAlive(active)) state.selection.makeActive(active);
}

void deleteEntity(Scene& scene, EditorState& state, EntityId entity) {
    warnDeleteInsideInstance(scene, state, entity);

    const EntityId priorSel = state.selection.active();
    SubtreeSnapshot snap = SubtreeSnapshot::capture(scene, entity);
    const Hierarchy* node = scene.tryGet<Hierarchy>(entity);
    const bool hadChildren = node && node->firstChild;
    const char* label = hadChildren ? "Delete Subtree" : "Delete Entity";

    HierarchyOperations::destroyHierarchy(scene, entity);
    state.pushStep(std::make_unique<DestroySubtreeCommand>(std::move(snap), priorSel, label));
}

namespace {
/// The world matrix @p id's local Transform is relative to: its parent's, or the identity.
glm::mat4 parentWorldOf(const Scene& scene, EntityId id) {
    const Hierarchy* link = scene.tryGet<Hierarchy>(id);
    if (!link || !scene.isAlive(link->parent)) return glm::mat4(1.0f);
    return HierarchyOperations::computeWorldMatrix(scene, link->parent);
}
} // namespace

void alignWithView(Scene& scene, ResourceManager& resources, EditorState& state) {
    const EntityId id = state.selection.active();
    if (!scene.isAlive(id) || !scene.has<Transform>(id) || !state.viewAim.known) return;

    const glm::mat4 parent = parentWorldOf(scene, id);
    EditScope<Transform> pose(scene, resources, state, id, "Align With View");
    pose->position = glm::vec3(glm::inverse(parent) * glm::vec4(state.viewAim.ray.origin, 1.0f));
    pose->rotation = glm::normalize(glm::inverse(Math::worldRotationOf(parent)) * state.viewAim.rotation);
}

void dropToGround(Scene& scene, ResourceManager& resources, EditorState& state) {
    // Each selected entity a selected ancestor does not carry, lowest first, so one
    // stacked on another lands on it where it has fallen to.
    std::vector<std::pair<float, EntityId>> falling;
    for (const EntityId id : state.selection.entities()) {
        if (!scene.isAlive(id) || !scene.has<Transform>(id)) continue;
        if (hasSelectedAncestor(scene, state, id)) continue;
        falling.emplace_back(HierarchyOperations::computeWorldMatrix(scene, id)[3].y, id);
    }
    std::sort(falling.begin(), falling.end(), [](const auto& a, const auto& b) { return a.first < b.first; });

    auto batch = std::make_unique<CompositeCommand>("Drop to Ground");
    size_t floating = 0;
    for (const auto& entry : falling) {
        const EntityId id = entry.second;

        float drop = 0.0f;
        if (!ViewPlacement::dropBelow(scene, resources, id, drop)) {
            ++floating;
            continue;
        }
        if (drop == 0.0f) continue;

        Transform& local = scene.get<Transform>(id);
        const Transform before = local;
        const glm::mat4 parent = parentWorldOf(scene, id);
        local.position += glm::vec3(glm::inverse(parent) * glm::vec4(0.0f, -drop, 0.0f, 0.0f));
        batch->add(editStep<Transform>(scene, resources, id, before, local, "Drop to Ground"));
    }
    if (!batch->empty()) state.pushStep(std::move(batch));
    if (floating > 0) state.toasts.push(ToastKind::Warning, "Nothing below to drop onto");
}

void undo(Scene& scene, EditorState& state) {
    // An empty history is not an edit: Ctrl+Z on a fresh scene must not mark it unsaved.
    if (!state.commands.canUndo()) return;
    state.commands.undo(scene, state);
    state.markSceneDirty();
}

void redo(Scene& scene, EditorState& state) {
    if (!state.commands.canRedo()) return;
    state.commands.redo(scene, state);
    state.markSceneDirty();
}

} // namespace EditorActions
} // namespace Vkm::Engine

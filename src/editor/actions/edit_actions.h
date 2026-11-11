#pragma once

#include "ecs/entity.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
struct EditorState;

/// Entity operations the editor's UI invokes, on the Scene and EditorState.
namespace EditorActions {

/**
 * @brief Move the active entity to the view's pose, keeping its scale, as one undo step.
 *
 * @param scene Holds the entity.
 * @param resources Resolves asset handles for the step.
 * @param state Supplies the selection and the view, and takes the step.
 */
void alignWithView(Scene& scene, ResourceManager& resources, EditorState& state);

/**
 * @brief Drop each selected subtree onto what is below it, as one undo step; warns when nothing is.
 *
 * @param scene Holds the selection.
 * @param resources Resolves the assets measured.
 * @param state Supplies the selection and takes the step.
 */
void dropToGround(Scene& scene, ResourceManager& resources, EditorState& state);

/**
 * @brief Duplicate an entity and its subtree, under the parent copyParent gives, nudged off it on X.
 *
 * Its cameras come back inactive, animations paused and ragdolls at rest; a prefab instance is
 * instanced from its file again, with the same overrides.
 *
 * @param scene Holds the source and receives the copy.
 * @param resources Resolves the prefab's asset names when the source is an instance.
 * @param state Takes the step, the dirty flag and the new selection.
 * @param source Entity to copy.
 * @param inPlace Where the source stands, not nudged off it: a copy dragged away at once.
 */
void duplicateEntity(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId source,
    bool inPlace = false
);

/**
 * @brief Delete an entity and its subtree as one undo step.
 *
 * @param scene Scene the entity is removed from.
 * @param state Takes the step, the dirty flag and the selection change.
 * @param entity Root of the subtree to delete.
 */
void deleteEntity(Scene& scene, EditorState& state, EntityId entity);

/**
 * @brief Whether an ancestor of @p id within HierarchyOperations::MAX_DEPTH is itself selected.
 *
 * Reduces a selection to its roots. The bound keeps a Hierarchy ring written around setParent from
 * hanging the editor.
 *
 * @param scene Scene the entities live in.
 * @param state Its selection is looked in.
 * @param id Entity to test.
 * @return Whether a selected ancestor was found within MAX_DEPTH.
 */
bool hasSelectedAncestor(const Scene& scene, const EditorState& state, EntityId id);

/**
 * @brief Put the selection's roots under a new "Group", as one step, and select it.
 *
 * Refused, with a toast, for an entity a prefab instance owns, as a reparent is.
 *
 * @param scene Scene being edited.
 * @param state Holds the selection and receives the step.
 */
void groupSelection(Scene& scene, EditorState& state);

/**
 * @brief Delete every selected entity, with its subtree, as one undo step.
 *
 * @param scene Scene the entities are removed from.
 * @param state Its selection is deleted and cleared; takes the step and the dirty flag.
 */
void deleteSelection(Scene& scene, EditorState& state);

/**
 * @brief Duplicate the selection's roots as duplicateEntity does, as one undo step, and select the copies.
 *
 * @param scene Holds the sources and receives the copies.
 * @param resources Resolves the prefab's asset names when a source is an instance.
 * @param state Its selection is copied and replaced by the copies; the active one's copy is active.
 * @param inPlace See duplicateEntity.
 */
void duplicateSelection(Scene& scene, ResourceManager& resources, EditorState& state, bool inPlace = false);

/**
 * @brief Select every entity in the scene, keeping the active one active.
 *
 * @param scene The scene whose entities are selected.
 * @param state Its selection is replaced.
 */
void selectAll(const Scene& scene, EditorState& state);

/**
 * @brief Undo the latest step, if any, and mark the scene unsaved, which CommandStack::undo does not.
 *
 * @param scene Scene the step is reversed against.
 * @param state Editor state holding the history.
 */
void undo(Scene& scene, EditorState& state);

/**
 * @brief Redo the latest undone step, if any, and mark the scene unsaved.
 *
 * @param scene Scene the step is re-applied against.
 * @param state Editor state holding the history.
 */
void redo(Scene& scene, EditorState& state);

/**
 * @brief Reparent @p child under @p newParent keeping its world transform, as one undo step.
 *
 * Refused, with a toast, into or out of a prefab instance and under its own descendant. An entity
 * with no Transform (a UI element) moves without the re-base.
 *
 * @param scene     Scene holding both entities.
 * @param state     Editor state receiving the history entry and any toast.
 * @param child     Entity being moved.
 * @param newParent New parent, or a null EntityId to unparent to the root.
 * @param label     History entry text.
 * @param before    A child of @p newParent to stand in front of; null puts it last. Ignored for roots.
 */
void reparentKeepingWorld(
    Scene& scene,
    EditorState& state,
    EntityId child,
    EntityId newParent,
    const char* label,
    EntityId before = {}
);

} // namespace EditorActions

} // namespace Vkm::Engine

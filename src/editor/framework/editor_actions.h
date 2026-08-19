#pragma once

#include <string>
#include <vector>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"

#include "framework/asset_picker.h"

namespace Vkm::Engine {

class Scene;
class ResourceManager;
class CameraControllerSystem;
struct FrameContext;
struct EditorState;
struct Mesh;

/**
 * @brief Entity operations invoked by the editor (menu bar, hierarchy, keybinds).
 *
 * Free functions that modify the Scene and update EditorState (selection, dirty flags).
 * Decoupled from any specific panel so both the menu bar and hierarchy can call them.
 */
namespace EditorActions {

/**
 * @brief Built-in entity factory kinds the editor's "Create" menu exposes.
 * Each kind composes a fixed set of components on a fresh entity.
 */
enum class EntityKind {
    Empty,
    Cube,
    Sphere,
    Plane,
    Triangle,
    Pyramid,
    Cone,
    PointLight,
    SpotLight,
    DirectionalLight,
    RectLight,
    DiskLight,
    Camera,
    ReflectionProbe,
    IrradianceVolume,
    Decal,
    ParticleEmitter,
    UICanvas,
    UIPanel,
    UIText,
    UIButton,
};

/**
 * @brief Create a new entity of the given kind and push it as an undoable step.
 *
 * The creation is snapshotted so undo can destroy it and redo re-create it
 * intact.
 *
 * @param scene Scene the entity is created in.
 * @param resources Resource manager that meshes/materials for the kind are registered with.
 * @param state Editor state whose command stack and dirty flag are updated.
 * @param kind Which built-in entity factory to run.
 * @return Id of the newly created entity.
 */
EntityId createEntity(Scene& scene, ResourceManager& resources, EditorState& state, EntityKind kind);
/**
 * @brief Duplicate an entity, copying its components onto a fresh one.
 *
 * Captured via EntitySnapshot, so the component set stays single-sourced and
 * script behaviors carry over. The copy is nudged off the source on X, and a
 * duplicated camera comes back inactive, a duplicated animation paused.
 *
 * A prefab instance is instanced from its file again rather than copied, so the
 * copy is an instance of the same prefab with the same overrides instead of a
 * root with nothing under it.
 *
 * @param scene Scene holding the source and receiving the copy.
 * @param resources Resolves the prefab's asset names when the source is an instance.
 * @param state Editor state whose command stack, dirty flag and selection are updated.
 * @param source Entity to copy from.
 */
void duplicateEntity(Scene& scene, ResourceManager& resources, EditorState& state,
                     EntityId source);
/**
 * @brief Delete an entity (and its subtree) as a single undoable step.
 *
 * Snapshots the subtree before destroying it so undo can restore every node
 * under its original parent, and clears the selection if the deleted entity
 * was selected. The undo label reflects whether children were present.
 *
 * @param scene Scene the entity is removed from.
 * @param state Editor state whose command stack, dirty flag and selection are updated.
 * @param entity Root entity to delete.
 */
void deleteEntity(Scene& scene, EditorState& state, EntityId entity);

/**
 * @brief Write @p entity and its subtree to the project's prefabs/ as a prefab.
 *
 * The file is named after the entity, so saving the same entity again updates
 * the prefab it came from rather than making a second one - which is what makes
 * this the way to edit a prefab: instance it, change it, save it back.
 *
 * On success @p entity becomes an instance of what it just wrote, so the scene
 * stores it as a reference from then on and every other instance picks the
 * change up on its next load.
 *
 * @param scene     Scene holding the subtree.
 * @param resources Resolves asset handles to names.
 * @param state     Editor state, for the toast and the dirty flag.
 * @param entity    Root of the subtree to save.
 * @return True when the prefab was written.
 */
bool saveAsPrefab(Scene& scene, const ResourceManager& resources, EditorState& state,
                  EntityId entity);

/**
 * @brief Build an instance of the prefab at @p path into the scene.
 *
 * The instance lands at the origin, where every other Create-menu entity
 * starts, rather than at the prefab's authored pose: a second copy dropped
 * exactly on top of the first one looks like nothing happened. It becomes the
 * selection, so the usual focus shortcut frames it.
 *
 * @param scene     Scene the instance is built in.
 * @param resources Resolves the prefab's asset names to handles.
 * @param state     Editor state whose command stack, selection and dirty flag
 *                  are updated.
 * @param path      Prefab file, project-relative or absolute.
 * @return The instance root, or a default (invalid) EntityId when the prefab
 *         could not be read.
 */
EntityId placePrefab(Scene& scene, ResourceManager& resources, EditorState& state,
                     const std::string& path);

/**
 * @brief Whether any ancestor of @p id is itself in @p selection.
 *
 * The rule that reduces a selection to its roots: an entity whose ancestor is
 * also selected is already carried by that ancestor, so acting on it again is
 * acting twice - a delete aims at an entity the ancestor's subtree already
 * destroyed, a gizmo drag applies the same delta a second time and sends the
 * child twice as far. Both are silent when they go wrong.
 *
 * The selection is passed rather than read from EditorState because
 * deleteSelection deselects before it destroys and must test against the copy
 * it captured.
 *
 * The walk is bounded by HierarchyOperations::MAX_DEPTH. Hierarchy::parent
 * arrives from a scene file, and setParent's own cycle check is capped at the
 * same depth, so a cycle deeper than that is reachable and an unbounded walk
 * would hang the editor with no error.
 *
 * @param scene Scene the entities live in.
 * @param selection The entities currently selected.
 * @param id The entity to test.
 * @return Whether a selected ancestor was found within MAX_DEPTH.
 */
bool hasSelectedAncestor(const Scene& scene, const std::vector<EntityId>& selection, EntityId id);

/**
 * @brief Delete every selected entity as ONE undo step.
 *
 * Entities whose ancestor is also selected are skipped (they die with the
 * ancestor's subtree). Falls back to deleteEntity for a single selection.
 */
void deleteSelection(Scene& scene, EditorState& state);

/**
 * @brief Duplicate every selected entity as ONE undo step; the clones become
 * the new selection. Falls back to duplicateEntity for a single selection.
 */
void duplicateSelection(Scene& scene, ResourceManager& resources, EditorState& state);

/**
 * @brief Apply the command stack's undo / redo, then flag the scene dirty.
 *
 * Shared by the Edit menu and the keyboard shortcuts so neither can forget the
 * dirty flag.
 */
void undo(Scene& scene, EditorState& state);
void redo(Scene& scene, EditorState& state);

/**
 * @brief Focus the camera on the current selection.
 *
 * Centers on the selected entity's world-space mesh bounds (falling back to its
 * origin), choosing a distance that frames the bounds. No-op if nothing is
 * selected, the selection is dead, or it has no transform.
 *
 * @param ctx Frame context supplying the scene and resources to read.
 * @param state Editor state holding the current selection.
 * @param camera Camera controller moved to frame the target.
 */
void focusOnSelected(FrameContext& ctx, EditorState& state, CameraControllerSystem& camera);

/**
 * @brief Make @p target the active ("main") camera.
 *
 * Flips the active flag across every Camera and records the prior flags so
 * the multi-entity change is one undoable step.
 */
void setActiveCamera(Scene& scene, EditorState& state, EntityId target);

/**
 * @brief Commit a hierarchy mutation: panel rebuild plus scene save flag.
 *
 * Names the pair every structural edit owes the editor, because a call site
 * that remembered the panel rebuild and forgot markSceneDirty lost the user's
 * work at the next load with nothing said.
 */
void commitHierarchyMutation(EditorState& state);

/**
 * @brief Reparent @p child under @p newParent (null = unparent to root) while
 * keeping its world transform fixed, and push an undoable ReparentCommand.
 *
 * The engine-level setParent/removeFromParent keep the local Transform as-is
 * (loaders and the scene serializer rely on that), so an interactive reparent
 * must re-base the local Transform itself - otherwise the entity visibly jumps
 * by the old/new parent's world contribution. Decomposes the preserved world
 * matrix into the new parent's space (same math the transform gizmo uses).
 *
 * A move that crosses into or out of a prefab instance is refused with a toast
 * instead: the instance's interior is the prefab's, and the scene stores none
 * of it, so either move would be lost on the next load without a word.
 */
void reparentKeepingWorld(Scene& scene, EditorState& state, EntityId child,
                          EntityId newParent, const char* label);

/**
 * @brief Mark a non-hierarchy structural change (add/remove entity, etc.).
 * Used by paths that don't have a specific entity to dirty.
 */
void commitStructureChange(EditorState& state);

/**
 * @brief Fork a material asset for safe per-entity edits.
 *
 * Registers a clone of the asset under a " copy" name. If @p assignTo is
 * non-null, also overwrites its material handle with the clone. Returns the new
 * handle, or a null handle on registration failure.
 *
 * Marks the scene dirty when @p assignTo is non-null (asset add alone does
 * not modify any entity, so the caller can decide if a scene-level edit
 * happened).
 */
MaterialHandle duplicateMaterial(
    ResourceManager& resources,
    EditorState& state,
    MaterialHandle source,
    Mesh* assignTo
);

/**
 * @brief Create a fresh standalone PBR material from scratch.
 *
 * Wraps generateDefaultMaterial, then gives the result a unique name
 * ("Material", "Material 1", ...) and a unique AssetId so distinct new
 * materials don't collapse onto the shared "material:default" id on
 * save/load. Marks the scene dirty. Returns the new handle (null on failure).
 * Does not assign it to any entity - the caller decides what to do with it.
 */
MaterialHandle createNewMaterial(ResourceManager& resources, EditorState& state);

/**
 * @brief Frame the entire visible scene: union the world-space AABBs of every
 * visible mesh entity, then focus the camera so the union fits in view.
 * No-op if there is nothing visible.
 */
void frameAll(FrameContext& ctx, CameraControllerSystem& camera);
/**
 * @brief Draw the "Create" submenu and create+select the chosen entity kind.
 *
 * The "Import Model..." item only flags EditorState::requestModelImport because
 * its modal must be drawn outside the menu, which closes on click - see
 * ModelImportDialog::draw.
 *
 * @param scene Scene new entities are created in.
 * @param resources Resource manager passed through to createEntity.
 * @param state Editor state updated with the new selection / import request.
 */
void drawCreateEntityMenu(Scene& scene, ResourceManager& resources, EditorState& state);

/**
 * @brief Render the "Import Model" modal.
 *
 * Must be called once per frame from the menu-bar scope (like
 * SceneIOController::drawDialogs) so the modal survives the Create menu
 * closing when the item is clicked.
 *
 * Owns a cached AssetPicker so the modal does not re-scan the assets tree
 * every frame it is open.
 */
class ModelImportDialog {
    public:
        /**
         * @brief Open the picker when EditorState::requestModelImport is set,
         *        and import what the user chooses.
         */
        void draw(Scene& scene, ResourceManager& resources, EditorState& state);

    private:
        AssetPicker m_picker;
};

/**
 * @brief Render the "Prefab" picker and place what the user chooses.
 *
 * Drawn from the menu-bar scope for the same reason as ModelImportDialog: the
 * Create menu closes the frame its item is clicked, taking any modal opened
 * from inside it with it.
 *
 * Owns a cached AssetPicker so the modal does not re-scan prefabs/ every frame
 * it is open.
 */
class PlacePrefabDialog {
    public:
        /**
         * @brief Open the picker when EditorState::requestPlacePrefab is set,
         *        and instance the prefab through placePrefab.
         *
         * A project with no prefabs yet gets a toast saying where they come from
         * instead of an empty list.
         */
        void draw(Scene& scene, ResourceManager& resources, EditorState& state);

    private:
        AssetPicker m_picker;
};

} // namespace EditorActions

} // namespace Vkm::Engine

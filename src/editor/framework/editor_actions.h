#pragma once

#include <memory>
#include <filesystem>
#include <string>
#include <vector>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/resource_manager.h"

#include "framework/asset_picker.h"
#include "framework/editor_commands.h"
#include "framework/editor_state.h"

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
    AudioSource,
    AudioListener,
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
 * An entity with no Transform is moved all the same, with the re-base skipped:
 * a UI element is placed in screen space by its canvas and has no world pose to
 * keep. The move is then reported when it leaves the element with no UICanvas
 * ancestor, because that is what stops it being drawn.
 *
 * A move that crosses into or out of a prefab instance is refused with a toast
 * instead: the instance's interior is the prefab's, and the scene stores none
 * of it, so either move would be lost on the next load without a word.
 *
 * @param scene     Scene holding both entities.
 * @param state     Editor state receiving the history entry and any toast.
 * @param child     Entity being moved.
 * @param newParent New parent, or a null EntityId to unparent to the root.
 * @param label     History entry text.
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
 * @brief Rename an asset, report the name it actually got, and make it undoable.
 *
 * The one place an asset is renamed from editor UI. Every rename affordance -
 * the Asset Browser's F2 modal, the Material Editor's own - calls this rather
 * than open-coding it, because the sequence has two parts a caller would not
 * guess and one of them was missed the first time it was copied.
 *
 * ResourceManager keeps names unique per type by suffixing a taken one, so the
 * asset may not end up called what was typed. This reads the name back and
 * toasts when it differs: an author who is not told goes looking for a name
 * nothing holds. It then pushes the undo step with the name that was *assigned*
 * rather than the one that was asked for, so redo repeats what happened.
 *
 * Applying before pushing is deliberate and matches the rest of the editor: the
 * command carries the reverse of an edit that has already happened.
 *
 * @tparam Asset Asset type being renamed; @p handle is its Handle.
 * @param resources Resource manager owning the asset and its name index.
 * @param state Editor state whose command stack and toast list are appended to.
 * @param handle Handle naming the asset to rename.
 * @param from Name it had, kept for undo.
 * @param to Name the author typed.
 * @param label Undo-stack label, e.g. "Rename Material".
 */
template<typename Asset>
void renameAsset(ResourceManager& resources, EditorState& state, Handle<Asset> handle,
                 const std::string& from, const std::string& to, const char* label) {
    resources.rename(handle, to);

    const std::string assigned = resources.get(handle).name();
    if (assigned != to) {
        state.pushToast(EditorState::ToastKind::Info,
                        "'" + to + "' was taken - renamed to '" + assigned + "'");
    }

    state.commands.push(std::make_unique<RenameAssetCommand<Handle<Asset>>>(
        resources, handle, from, assigned, label));
}

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

/**
 * @brief Render the "Open Project" dialog: the recent projects, plus a path field.
 *
 * Drawn from the menu-bar scope for the same reason as ModelImportDialog. It
 * chooses a project root and asks for it through
 * EditorState::requestSceneAction - opening one throws the current scene away,
 * so it goes through the same guard New Scene and Open Scene do, and nothing is
 * opened from inside the dialog's own draw.
 */
class OpenProjectDialog {
    public:
        /**
         * @brief Open the dialog when EditorState::requestOpenProject is set,
         *        and request whichever project the user chooses.
         *
         * @param state Editor state supplying the recent-project list and
         *        receiving the request.
         */
        void draw(EditorState& state);

    private:
        bool m_open = false;
        char m_pathBuffer[512] = {};
};

/**
 * @brief Render the "New Project" dialog: a name, a parent directory, and Create.
 *
 * Makes a project the way `vkm new` does - copies the SDK template, names it
 * after the directory the author chose, and stamps the engine that answered -
 * then asks for it through EditorState::requestSceneAction rather than opening
 * it here, because creating one throws the current scene away and goes through
 * the same guard Open Project does.
 */
class NewProjectDialog {
    public:
        /**
         * @brief Open the dialog when EditorState::requestNewProject is set, and
         *        request whichever project the author creates.
         *
         * @param state Editor state carrying the request and receiving the open.
         */
        void draw(EditorState& state);

    private:
        /**
         * @brief Copy the template to @p dest and stamp it for this engine.
         *
         * @param dest Directory to create; must not already exist non-empty.
         * @param error Filled with what went wrong when the result is false.
         * @return true when the project is on disk and ready to open.
         */
        static bool create(const std::filesystem::path& dest, std::string& error);

    private:
        bool m_open = false;
        char m_nameBuffer[128] = {};
        char m_parentBuffer[512] = {};
        std::string m_error;
};

} // namespace EditorActions

} // namespace Vkm::Engine

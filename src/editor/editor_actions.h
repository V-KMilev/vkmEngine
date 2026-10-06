#pragma once

#include <memory>
#include <string>
#include <vector>

#include "ecs/entity.h"
#include "resource/asset/material_asset.h"
#include "resource/resource_manager.h"

#include "command/editor_commands.h"
#include "command/prefab_overrides.h"
#include "editor_state.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief Entity operations the editor's UI invokes, on the Scene and EditorState.
 */
namespace EditorActions {

/**
 * @brief Built-in entity kinds the "Create" menu offers.
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
    UIScrollView,
    UIText,
    UIButton,
    Character,
    StaticBody,
};

/**
 * @brief Create an entity of the given kind as an undoable step.
 *
 * @param scene Receives the entity.
 * @param resources Where the kind's meshes and materials are registered.
 * @param state Takes the step and the dirty flag.
 * @param kind Which built-in factory to run.
 * @return The new entity.
 */
EntityId createEntity(Scene& scene, ResourceManager& resources, EditorState& state, EntityKind kind);
/**
 * @brief Duplicate an entity and everything under it.
 *
 * The copy keeps the source's parent and script behaviors and is nudged off it on
 * X; its cameras come back inactive, animations paused and ragdolls at rest. A
 * prefab instance is instanced from its file again, with the same overrides.
 *
 * @param scene Holds the source and receives the copy.
 * @param resources Resolves the prefab's asset names when the source is an instance.
 * @param state Takes the step, the dirty flag and the new selection.
 * @param source Entity to copy.
 */
void duplicateEntity(Scene& scene, ResourceManager& resources, EditorState& state, EntityId source);
/**
 * @brief Delete an entity and its subtree as one undoable step.
 *
 * Undo restores every node under its original parent.
 *
 * @param scene Scene the entity is removed from.
 * @param state Takes the step, the dirty flag and the selection change.
 * @param entity Root of the subtree to delete.
 */
void deleteEntity(Scene& scene, EditorState& state, EntityId entity);

/**
 * @brief Write @p entity and its subtree to the project's prefabs/ as a prefab.
 *
 * On success @p entity becomes an instance of that file: the scene stores it as a
 * reference, and a second save writes over the same file. An entity not yet an
 * instance takes a free file name.
 *
 * @param scene     Scene holding the subtree.
 * @param resources Resolves asset handles to names.
 * @param state     For the toast and the dirty flag.
 * @param entity    Root of the subtree to save.
 * @return True when the prefab was written.
 */
bool saveAsPrefab(Scene& scene, const ResourceManager& resources, EditorState& state, EntityId entity);

/**
 * @brief Build an instance of the prefab at @p path into the scene.
 *
 * The instance lands at the origin, not the prefab's authored pose, and becomes
 * the selection.
 *
 * @param scene     Scene the instance is built in.
 * @param resources Resolves the prefab's asset names to handles.
 * @param state     Takes the step, the selection and the dirty flag.
 * @param path      Prefab file, project-relative or absolute.
 * @return The instance root, or an invalid EntityId when the prefab could not be read.
 */
EntityId placePrefab(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& path);

/**
 * @brief Whether any ancestor of @p id is itself in @p selection.
 *
 * Reduces a selection to its roots: a selected ancestor already carries @p id.
 * The selection is passed so a caller that cleared it can test a copy. The walk
 * is bounded by HierarchyOperations::MAX_DEPTH, so a Hierarchy ring written
 * around setParent cannot hang the editor.
 *
 * @param scene Scene the entities live in.
 * @param selection Entities to look for.
 * @param id Entity to test.
 * @return Whether a selected ancestor was found within MAX_DEPTH.
 */
bool hasSelectedAncestor(const Scene& scene, const std::vector<EntityId>& selection, EntityId id);

/**
 * @brief Delete every selected entity as one undo step.
 *
 * An entity whose ancestor is also selected is skipped; it dies with that
 * subtree. A single selection goes through deleteEntity.
 *
 * @param scene Scene the entities are removed from.
 * @param state Its selection is deleted and cleared; takes the step and the dirty flag.
 */
void deleteSelection(Scene& scene, EditorState& state);

/**
 * @brief Duplicate every selected entity as one undo step, selecting the copies.
 *
 * An entity whose ancestor is also selected is skipped: the ancestor's copy holds
 * it. A single selection goes through duplicateEntity.
 *
 * @param scene Holds the sources and receives the copies.
 * @param resources Resolves the prefab's asset names when a source is an instance.
 * @param state Its selection is copied and replaced by the copies.
 */
void duplicateSelection(Scene& scene, ResourceManager& resources, EditorState& state);

/**
 * @brief Undo the latest step and mark the scene unsaved; nothing when there is none.
 *
 * CommandStack::undo does not raise the dirty flag itself.
 *
 * @param scene Scene the step is reversed against.
 * @param state Editor state holding the history.
 */
void undo(Scene& scene, EditorState& state);

/**
 * @brief Redo the latest undone step and mark the scene unsaved; nothing when
 *        there is none.
 *
 * @param scene Scene the step is re-applied against.
 * @param state Editor state holding the history.
 */
void redo(Scene& scene, EditorState& state);

/**
 * @brief Reparent @p child under @p newParent keeping its world transform, as an
 *        undoable ReparentCommand.
 *
 * An entity with no Transform (a UI element, placed by its canvas) moves without
 * the re-base, with a warning if it is left with no UICanvas ancestor to draw it.
 * A move into or out of a prefab instance, whose interior the scene does not
 * store, is refused with a toast, as is one under its own descendant; a move
 * onto itself does nothing.
 *
 * @param scene     Scene holding both entities.
 * @param state     Editor state receiving the history entry and any toast.
 * @param child     Entity being moved.
 * @param newParent New parent, or a null EntityId to unparent to the root.
 * @param label     History entry text.
 */
void reparentKeepingWorld(
    Scene& scene,
    EditorState& state,
    EntityId child,
    EntityId newParent,
    const char* label
);

/**
 * @brief Fork a material asset for per-entity edits.
 *
 * Registers a clone under a " copy" name and assigns it to nothing; a caller
 * putting it on an entity pushes its own ComponentEditCommand.
 *
 * @param resources Where the copy is registered.
 * @param source Material to clone.
 * @return The new handle, or a null handle on registration failure.
 */
MaterialHandle duplicateMaterial(ResourceManager& resources, MaterialHandle source);

/**
 * @brief Create a fresh standalone PBR material.
 *
 * A copy of generateDefaultMaterial's material under a unique name ("Material",
 * "Material 2", ...), since the default itself is what "material:default"
 * resolves to. Assigned to no entity.
 *
 * @param resources Where the material is registered.
 * @param state Its dirty flag is raised.
 * @return The new handle, or a null handle on failure.
 */
MaterialHandle createNewMaterial(ResourceManager& resources, EditorState& state);

/**
 * @brief Rename an asset, report the name it actually got, and make it undoable.
 *
 * ResourceManager suffixes a taken name, so the assigned name is read back,
 * toasted when it differs, and is the one the undo step records.
 *
 * The rename is applied before the command is pushed: the command carries the
 * reverse of an edit that has already happened. The scene, which names the
 * asset, is marked unsaved too.
 *
 * @tparam Asset Asset type being renamed.
 * @param resources Owns the asset and its name index.
 * @param state Takes the step and any toast.
 * @param handle Asset to rename.
 * @param from Name it had, kept for undo.
 * @param to Name the author typed.
 * @param label Undo-stack label, e.g. "Rename Material".
 */
template<typename Asset>
void renameAsset(
    ResourceManager& resources,
    EditorState& state,
    Handle<Asset> handle,
    const std::string& from,
    const std::string& to,
    const char* label
) {
    resources.rename(handle, to);

    const std::string assigned = resources.get(handle).name();
    if (assigned != to) {
        state.pushToast(ToastKind::Info, "'" + to + "' was taken - renamed to '" + assigned + "'");
    }

    state.pushStep(
        std::make_unique<RenameAssetCommand<Handle<Asset>>>(resources, handle, from, assigned, label)
    );
}

/**
 * @brief Add @p component to @p id as one undoable step.
 *
 * Inside a prefab instance it warns that a component the prefab does not define
 * is not stored: the instance's components come back from the prefab on load.
 *
 * @tparam T Component type in VKM_EDITOR_COMPONENTS, or the command does not exist.
 * @param scene Holds the entity; it must not already carry a T.
 * @param state Takes the step and the warning.
 * @param id Entity to add to.
 * @param component The value added.
 * @param title The component's name as the author sees it, for the warning.
 * @param label History entry text; the command keeps the pointer.
 * @return The component as stored.
 */
template<typename T>
T& addComponent(
    Scene& scene,
    EditorState& state,
    EntityId id,
    T component,
    const char* title,
    const char* label
) {
    T& stored = scene.add(id, std::move(component));
    state.pushStep(std::make_unique<AddComponentCommand<T>>(id, stored, label));
    PrefabOverrides::warnComponentIsPrefabs(scene, state, id, title, "is not stored in the scene");
    return stored;
}

/**
 * @brief Draw the "Create" submenu and create and select the chosen kind.
 *
 * "Import Model" only sets EditorState::requestModelImport: its modal must be
 * drawn outside the menu, which closes on click (see ModelImportDialog::draw).
 *
 * @param scene Where new entities are created.
 * @param resources Passed through to createEntity.
 * @param state Takes the new selection or the import request.
 */
void drawCreateEntityMenu(Scene& scene, ResourceManager& resources, EditorState& state);

} // namespace EditorActions

} // namespace Vkm::Engine

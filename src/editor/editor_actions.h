#pragma once

#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "core/memory/types.h"
#include "ecs/entity.h"
#include "resource/asset_type.h"
#include "resource/asset/material_asset.h"
#include "resource/resource_manager.h"

#include "command/editor_commands.h"
#include "command/prefab_overrides.h"
#include "editor_state.h"

namespace Vkm::Engine {

class Scene;

/**
 * @brief What Copy and Cut took, for Paste.
 *
 * The copied subtrees as a scene document (SceneSerializer::saveToString), each root at its world
 * pose: assets named rather than held by handle, so it pastes into another scene, or the scene
 * back from Play, as into this one.
 */
struct EntityClipboard {
    std::string scene;  ///< The copies: the plain subtrees in selection order, then the instances.
    /// Each root's parent when taken, by its slot in `scene`; null for a root.
    std::vector<std::pair<uint32_t, EntityId>> parents;
    uint64_t world = 0;  ///< Scene::epoch() when taken: where those parents mean anything.
};

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
    PlanarReflector,
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
    UISlider,
    UIToggle,
    UIChoice,
    Character,
    StaticBody,
    Terrain,
    Water,
    Sea,
};

/**
 * @brief Create an entity of the given kind as an undoable step.
 *
 * A root goes where the view is looking (ViewPlacement::placeInView), a mesh standing
 * on what it meets; a camera takes the view's pose; a terrain and a sea stay at the origin.
 * A child goes at its parent's origin; one asked for inside a prefab instance is refused
 * with a toast, since the instance is rebuilt from its prefab.
 *
 * @param scene Receives the entity.
 * @param resources Where the kind's meshes and materials are registered.
 * @param state Takes the step, the dirty flag and any refusal's toast.
 * @param kind Which built-in factory to run.
 * @param parent Puts it under this entity, or {} for the view or the selected canvas.
 * @return The new entity, or null when refused.
 */
EntityId createEntity(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityKind kind,
    EntityId parent = {}
);

/**
 * @brief Move the active entity to where the editor's view stands, facing as it faces.
 *
 * One undo step; its scale is kept. A camera then sees what the author sees.
 *
 * @param scene Holds the entity.
 * @param resources Resolves asset handles for the step.
 * @param state Supplies the selection and the view, and takes the step.
 */
void alignWithView(Scene& scene, ResourceManager& resources, EditorState& state);

/**
 * @brief Let each selected entity, with everything under it, fall onto what is below it.
 *
 * One undo step for the selection (ViewPlacement::dropBelow measures each); an entity
 * whose ancestor is also selected moves with it. Says so when nothing is below one.
 *
 * @param scene Holds the selection.
 * @param resources Resolves the assets measured.
 * @param state Supplies the selection and takes the step.
 */
void dropToGround(Scene& scene, ResourceManager& resources, EditorState& state);

/**
 * @brief Whether createFromAsset makes an entity of an asset of @p type.
 *
 * @param type The dragged asset's kind.
 * @return True for a mesh, a sound and a terrain.
 */
bool makesEntity(AssetType type);

/**
 * @brief Make an entity of a dragged asset, standing at @p at: a mesh drawn with the
 *        default material, a sound's Audio Source, or a terrain's ground, which stands at the
 *        origin instead.
 *
 * One undo step, selected.
 *
 * @param scene Receives the entity.
 * @param resources Holds the asset.
 * @param state Takes the step and the selection.
 * @param type One makesEntity accepts; anything else makes nothing.
 * @param key The asset.
 * @param at World position of its origin.
 * @return The new entity, or null.
 */
EntityId createFromAsset(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    AssetType type,
    StorageIndex key,
    const glm::vec3& at
);

/**
 * @brief Duplicate an entity and everything under it.
 *
 * The copy keeps the source's parent and script behaviors and is nudged off it on
 * X; its cameras come back inactive, animations paused and ragdolls at rest. A
 * prefab instance is instanced from its file again, with the same overrides. A copy
 * of a part of an instance stands at the root, where the source stands (copyParent).
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
 * @brief Delete an entity and its subtree as one undoable step.
 *
 * Undo restores every node under its original parent.
 *
 * @param scene Scene the entity is removed from.
 * @param state Takes the step, the dirty flag and the selection change.
 * @param entity Root of the subtree to delete.
 */
void deleteEntity(Scene& scene, EditorState& state, EntityId entity);

/// A prefab file's extension.
inline constexpr const char* PREFAB_EXTENSION = ".json";

/**
 * @brief The project's prefabs, sorted, each by its project-relative path: what an instance stores.
 *
 * Walked under ProjectPaths::prefabs(); a folder that cannot be read ends the walk, not the editor.
 *
 * @return The paths; empty for none.
 */
std::vector<std::string> listPrefabs();

/**
 * @brief Write @p entity and its subtree to the project's prefabs/ as a prefab.
 *
 * On success @p entity becomes an instance of that file: the scene stores it as a
 * reference, and a second save writes over the same file. An entity not yet an
 * instance takes a free file name.
 *
 * The assets it names are cooked and recorded first, as a scene's save does, or the
 * names would resolve to nothing once the session that imported them ends.
 *
 * @param scene     Scene holding the subtree.
 * @param resources Resolves asset handles to names, and is cooked.
 * @param state     For the toast and the dirty flag.
 * @param entity    Root of the subtree to save.
 * @return True when the prefab was written.
 */
bool saveAsPrefab(Scene& scene, ResourceManager& resources, EditorState& state, EntityId entity);

/**
 * @brief Make the instance @p root an ordinary subtree, as it is now, overrides and all: the scene
 *        then stores its entities, not a reference to the prefab. An undo step.
 *
 * @param scene The scene holding it.
 * @param state Receives the step.
 * @param root  An instance root.
 */
void unpackPrefab(Scene& scene, EditorState& state, EntityId root);

/**
 * @brief Build an instance of the prefab at @p path into the scene.
 *
 * The instance lands where the view looks (ViewPlacement::placeInView), not at the prefab's
 * authored pose, and becomes the selection.
 *
 * @param scene     Scene the instance is built in.
 * @param resources Resolves the prefab's asset names to handles.
 * @param state     Takes the step, the selection and the dirty flag.
 * @param path      Prefab file, project-relative or absolute.
 * @return The instance root, or an invalid EntityId when the prefab could not be read.
 */
EntityId placePrefab(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& path);

/**
 * @brief The effects the engine ships: the prefabs of its effects pack (ContentPack::shipped).
 *
 * @return Each one's prefab file name, in name order.
 */
std::vector<std::string> shippedEffects();

/**
 * @brief Place one of the shipped effects, bringing the effects pack into the project first.
 *
 * Copies each file of the pack the project lacks, at the same path, never over one it has: the
 * project then owns the copy, edits and all. The instance lands as placePrefab's does.
 *
 * @param scene     Scene the instance is built in.
 * @param resources Resolves the prefab's asset names to handles.
 * @param state     Takes the step, the selection, the dirty flag and the toasts.
 * @param file      The effect's prefab file name, as shippedEffects() gives it.
 * @return The instance root, or an invalid EntityId when the pack could not be copied or read.
 */
EntityId placeEffect(Scene& scene, ResourceManager& resources, EditorState& state, const std::string& file);

/**
 * @brief Whether any ancestor of @p id is itself selected.
 *
 * Reduces a selection to its roots: a selected ancestor already carries @p id. Asked of the
 * selection's slot index, so a whole scene selected is reduced in a walk of each entity's
 * ancestors. The walk is bounded by HierarchyOperations::MAX_DEPTH, so a Hierarchy ring written
 * around setParent cannot hang the editor.
 *
 * @param scene Scene the entities live in.
 * @param state Its selection is looked in.
 * @param id Entity to test.
 * @return Whether a selected ancestor was found within MAX_DEPTH.
 */
bool hasSelectedAncestor(const Scene& scene, const EditorState& state, EntityId id);

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
 * @brief Take every selected entity, and everything under each, for Paste.
 *
 * An entity whose ancestor is also selected is skipped: the ancestor's copy holds it.
 *
 * @param scene     Holds the selection.
 * @param resources Names the copies' assets.
 * @param state     Its selection is taken; its clipboard replaced.
 */
void copySelection(const Scene& scene, const ResourceManager& resources, EditorState& state);

/**
 * @brief Copy the selection, then delete it as one undo step.
 *
 * @param scene     Holds the selection, and loses it.
 * @param resources See copySelection.
 * @param state     See copySelection; takes the step.
 */
void cutSelection(Scene& scene, const ResourceManager& resources, EditorState& state);

/**
 * @brief Carry what was copied into @p scene, the world @p from came back in every entity's slot.
 *
 * Stop restores the world in place under a new epoch; without this, a copy taken before it would
 * paste as if into another world, its parents forgotten. A clipboard of any other world is left.
 *
 * @param state Holds the clipboard.
 * @param from  The epoch the world had before it came back.
 * @param scene The world as it came back.
 */
void carryClipboard(EditorState& state, uint64_t from, const Scene& scene);

/**
 * @brief Place what was copied as one undo step, selecting it.
 *
 * Copies are made as duplicateEntity says, but for their references outside what was copied,
 * which are dropped, and for where they land, with no nudge: in the world it was taken from, each
 * exactly where it stood when copied, under the same parent while copyParent keeps it; in
 * another, there in the world, at the root.
 *
 * @param scene     Receives the copies.
 * @param resources Resolves the copies' asset names, and takes the assets they carry that it lacks.
 * @param state     Takes the step, the selection, the dirty flag and the toasts.
 */
void paste(Scene& scene, ResourceManager& resources, EditorState& state);

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
 * @param before    A child of @p newParent to stand in front of; null puts it last.
 *                  Roots stand in slot order, so it is ignored there.
 */
void reparentKeepingWorld(
    Scene& scene,
    EditorState& state,
    EntityId child,
    EntityId newParent,
    const char* label,
    EntityId before = {}
);

/**
 * @brief Fork @p material for @p entity, so edits to it stop reaching whatever else draws it, and
 *        open the copy in a window of its own.
 *
 * The copy, under a " copy" name, goes on @p entity's Mesh alone, one step that records a prefab
 * instance's override, while that Mesh draws @p material; else it goes on nothing.
 *
 * @param scene     Scene holding the entity.
 * @param resources Where the copy is registered.
 * @param state     Takes the step and opens the copy's window.
 * @param entity    The entity the copy is for.
 * @param material  The material forked.
 * @return The copy, or a null handle on registration failure.
 */
MaterialHandle forkMaterial(
    Scene& scene,
    ResourceManager& resources,
    EditorState& state,
    EntityId entity,
    MaterialHandle material
);

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
 * @brief Create a flat terrain, painted all with its first layer, kept in a new .terrain file.
 *
 * Named for the file it is saved to, under assets/terrain/, at a path no loaded terrain and no
 * file holds; the file is written at the next save or Play (writeTerrainFiles).
 *
 * @param resources Where the terrain is registered.
 * @return The new handle.
 */
TerrainHandle createNewTerrain(ResourceManager& resources);

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
 * @brief Add an empty ScriptComponent to @p id as one undoable step, as addComponent adds the rest.
 *
 * Its own: a ScriptComponent is move-only, so its step holds it serialized (ScriptEditCommand).
 *
 * @param scene Holds the entity; it must not already carry one.
 * @param state Takes the step and the warning.
 * @param id    Entity to add to.
 * @param title The component's name as the author sees it, for the warning.
 * @param label History entry text; the command keeps the pointer.
 */
void addScript(Scene& scene, EditorState& state, EntityId id, const char* title, const char* label);

/**
 * @brief Draw the "Create" submenu and create and select the chosen kind.
 *
 * "Import Model" only sets EditorState::requestModelImport: its modal must be
 * drawn outside the menu, which closes on click (see ModelImportDialog::draw).
 * Under a parent it is "Create Child", offering only what createEntity makes.
 *
 * @param scene Where new entities are created.
 * @param resources Passed through to createEntity.
 * @param state Takes the new selection or the import request.
 * @param parent Puts what is made under this entity, or {} for none.
 */
void drawCreateEntityMenu(Scene& scene, ResourceManager& resources, EditorState& state, EntityId parent = {});

} // namespace EditorActions

} // namespace Vkm::Engine

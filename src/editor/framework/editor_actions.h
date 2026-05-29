#pragma once

#include "ecs/entity.h"
#include "resource/material_asset.h"   // MaterialHandle

#include "framework/asset_picker.h"

namespace Engine {

class Scene;
class ResourceManager;
class CameraController;
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

/// Built-in entity factory kinds the editor's "Create" menu exposes.
/// Each kind composes a fixed set of components on a fresh entity.
enum class EntityKind {
    Empty,
    Cube,
    Sphere,
    PointLight,
    SpotLight,
    DirectionalLight,
    RectLight,
    DiskLight,
    Camera,
};

EntityId createEntity(Scene& scene, ResourceManager& resources, EditorState& state, EntityKind kind);
void duplicateEntity(Scene& scene, EditorState& state, EntityId source);
void deleteEntity(Scene& scene, EditorState& state, EntityId entity);
void focusOnSelected(FrameContext& ctx, EditorState& state, CameraController& camera);

/**
 * @brief Commit a hierarchy mutation: dirty bits, panel rebuild, scene save flag.
 *
 * Cascades ECS dirty bits, requests a hierarchy panel rebuild, and flags the
 * scene for save. Replaces the HierarchyOperations::markDirty +
 * state.hierarchyDirty + markSceneDirty triplet that used to live at every
 * call site - one missed line dropped the scene-dirty flag, which is the
 * user-trust hazard the editor audit flagged.
 */
void commitHierarchyMutation(Scene& scene, EditorState& state, EntityId entity);

/// Mark a non-hierarchy structural change (add/remove entity, etc.).
/// Used by paths that don't have a specific entity to dirty.
void commitStructureChange(EditorState& state);

/**
 * @brief Fork a material asset for safe per-entity edits.
 *
 * Copies the asset's params + texture refs, suffixes the name with " copy",
 * resets the version, and registers the clone in @p resources. If
 * @p assignTo is non-null, also overwrites its material handle with the clone.
 * Returns the new handle, or a null handle on registration failure.
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

/// Frame the entire visible scene: union the world-space AABBs of every
/// visible mesh entity, then focus the camera so the union fits in view.
/// No-op if there is nothing visible.
void frameAll(FrameContext& ctx, CameraController& camera);
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
        void draw(Scene& scene, ResourceManager& resources, EditorState& state);
    private:
        AssetPicker m_picker;
};

} // namespace EditorActions

} // namespace Engine

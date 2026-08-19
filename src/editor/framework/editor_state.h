#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "ecs/entity.h"
#include "framework/command_stack.h"
#include "input/editor_keybinds.h"
#include "gizmo/transform_gizmo.h"
#include "resource/asset/material_asset.h"

namespace Engine {

/**
 * @brief Shared editor state passed to all panels by reference.
 *
 * This is a plain data struct (no getters/setters) because it is internal
 * editor state shared among tightly-coupled panels. Panels read and write
 * fields directly.
 *
 * Owned by EditorSystem, passed as EditorState& to each panel's draw().
 */
struct EditorState {
    EntityId selectedEntity{};               ///< The ACTIVE entity (last clicked); always in `selection` when set.
    std::vector<EntityId> selection;         ///< Every selected entity (multi-select set), active included.
    bool     worldSelected = false;

    GizmoOperation gizmoOperation = GizmoOperation::Translate;
    GizmoMode      gizmoMode      = GizmoMode::Local;

    bool  snapEnabled    = false;
    float snapTranslate  = 1.0f;
    float snapRotate     = 15.0f;    ///< Degrees
    float snapScale      = 0.1f;

    EditorKeybinds keybinds;

    bool showHierarchy   = true;
    bool showInspector   = true;
    bool showBottom      = true;
    bool showPreferences = false;   ///< Preferences window (Ctrl+,)
    bool showMaterialEditor = false;            ///< Material Editor window
    MaterialHandle materialEditorTarget{};      ///< Which material it edits (else: selected entity's)
    bool showAssetBrowser   = false;            ///< Asset Browser window (material/mesh thumbnail grid)
    bool showRenderSettings = false;            ///< Render Settings window (pass toggles + per-effect tuning)
    bool showColliders      = false;            ///< Draw physics collider wireframes in the viewport (View menu)
    bool showBounds         = false;            ///< Draw per-entity world AABBs in the viewport (View menu)
    int  colliderFitDetail  = 4;                ///< Voxel resolution for the Collider "Fit to Mesh" button

    // Layout dimensions (pixels)
    float leftPanelWidth    = 260.0f;
    float rightPanelWidth   = 340.0f;
    float bottomPanelHeight = 200.0f;

    bool viewportHovered = false;    ///< Whether mouse is over viewport
    bool hierarchyDirty  = true;     ///< Set by entity ops, consumed by HierarchyPanel
    bool editorVisible   = true;     ///< Toggle entire editor UI (F5)
    bool requestModelImport = false;  ///< Set by the Import Model menu item, consumed by the menu-bar dialog
    bool requestPlacePrefab = false;  ///< Set by the Create > Prefab item, consumed by the menu-bar dialog
    int  lodGenLevels = 2;            ///< Levels the LOD card's Generate button builds below the source.
    bool requestScriptReload = false; ///< Set by the Reload Scripts menu item, consumed by EditorSystem (hot-reload)

    bool sceneDirty = false;    ///< Unsaved edits since last save/load. Title shows '*'.

    /**
     * @brief The destructive scene actions that pass through the shared
     * unsaved-changes guard. confirmAction is what the modal is currently
     * confirming; afterSaveAction is deferred until the next clean save
     * (the "Save" choice); pendingScenePath is the target of a guarded Open.
     */
    enum class PendingSceneAction : uint8_t { None, Quit, New, Open, OpenProject };
    PendingSceneAction confirmAction   = PendingSceneAction::None;
    PendingSceneAction afterSaveAction = PendingSceneAction::None;
    std::string        pendingScenePath;
    std::vector<std::string> recentScenes;    ///< MRU list (absolute paths), most-recent first.
    std::vector<std::string> recentProjects;  ///< MRU project roots, most-recent first.

    bool        showOpenProject = false;  ///< File > Open Project dialog is up.
    std::string pendingProjectOpen;       ///< Project chosen from a menu; opened after the draw.
    std::string projectName;              ///< What the open project calls itself; titles the window.
    static constexpr size_t MAX_RECENT_ENTRIES = 8;

    // Undo/redo history for every editor mutation on the command path.
    // Cleared on scene load (entity IDs across scenes aren't comparable).
    CommandStack commands;

    // Floating-toast notification, ticked down each frame by EditorSystem.
    // Keeps save/load failures (and other transient feedback) on-screen
    // instead of burying them in the console log.
    enum class ToastKind { Info, Warning, Error };
    std::string toastMessage;
    float       toastTimeRemaining = 0.0f;
    ToastKind   toastKind          = ToastKind::Info;

    /**
     * @brief Mark the scene as having unsaved changes. Call from every code path
     * that mutates the live Scene (entity ops, gizmo drags, inspector edits).
     * Cheap, idempotent.
     */
    void markSceneDirty() { sceneDirty = true; }

    /**
     * @brief Selection helpers - route ALL selection changes through these.
     *
     * Invariants they maintain: selectedEntity (the active entity) is always a
     * member of `selection` when non-null; selection and worldSelected are
     * mutually exclusive. Plain click = selectEntity (replace); Ctrl+click =
     * toggleSelection; Shift+click = addToSelection / a range in the
     * Hierarchy.
     */
    void selectEntity(EntityId id) {
        selectedEntity = id;
        selection.assign(1, id);
        worldSelected = false;
    }

    void addToSelection(EntityId id) {
        if (!isSelected(id)) selection.push_back(id);
        selectedEntity = id;
        worldSelected  = false;
    }

    void toggleSelection(EntityId id) {
        auto it = std::find(selection.begin(), selection.end(), id);
        if (it != selection.end()) {
            selection.erase(it);
            if (selectedEntity == id)
                selectedEntity = selection.empty() ? EntityId{} : selection.back();
        } else {
            selection.push_back(id);
            selectedEntity = id;
        }
        worldSelected = false;
    }

    bool isSelected(EntityId id) const {
        return std::find(selection.begin(), selection.end(), id) != selection.end();
    }

    void selectWorld() { selectedEntity = {}; selection.clear(); worldSelected = true; }
    void deselect()    { selectedEntity = {}; selection.clear(); worldSelected = false; }

    /**
     * @brief Show a transient toast at the corner of the editor. `seconds` <= 0
     * uses a kind-appropriate default. Replaces any prior toast.
     */
    void pushToast(ToastKind kind, std::string msg, float seconds = 0.0f) {
        if (seconds <= 0.0f) {
            seconds = (kind == ToastKind::Error) ? 6.0f
                    : (kind == ToastKind::Warning) ? 4.0f : 2.5f;
        }
        toastKind          = kind;
        toastMessage       = std::move(msg);
        toastTimeRemaining = seconds;
    }
};

/**
 * @brief Push @p value to the front of an MRU path list, de-duplicated and capped.
 *
 * The recent-scenes and recent-projects lists are the same list under two
 * names, so "most recent first, no duplicates, at most MAX_RECENT_ENTRIES"
 * is defined once here rather than once per controller.
 *
 * @param mru   The list to promote into, most-recent first.
 * @param value The path to move to the front.
 */
inline void pushRecentPath(std::vector<std::string>& mru, const std::string& value) {
    mru.erase(std::remove(mru.begin(), mru.end(), value), mru.end());
    mru.insert(mru.begin(), value);
    if (mru.size() > EditorState::MAX_RECENT_ENTRIES) mru.resize(EditorState::MAX_RECENT_ENTRIES);
}

} // namespace Engine

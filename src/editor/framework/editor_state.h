#pragma once

#include <imgui.h>

#include <algorithm>
#include <string>
#include <vector>

#include "ecs/entity.h"
#include "framework/command_stack.h"
#include "input/editor_keybinds.h"
#include "gizmo/transform_gizmo.h"
#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

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
    MaterialHandle materialEditorTarget{};      ///< What the Material tab edits (else: the selected entity's)
    bool materialFloating  = false; ///< Material editor is a window rather than the right panel's second tab
    ImVec2 materialDetachAt = {};   ///< Where the drag that detached it let go, so the window opens under the cursor
    ImVec2 rightPanelMin    = {};   ///< Screen rect of the right panel, so a detached window knows when it is over it
    ImVec2 rightPanelMax    = {};
    bool revealMaterialTab = false;             ///< Pending request to open the right panel on Material
    bool showRenderSettings = false;            ///< Render Settings window (pass toggles + per-effect tuning)
    bool showColliders      = false;            ///< Draw physics collider wireframes in the viewport (View menu)
    bool showBounds         = false;            ///< Draw per-entity world AABBs in the viewport (View menu)
    bool showSkeletons      = false;            ///< Draw posed rigs as bone segments in the viewport (View menu)

    // Layout dimensions (pixels)
    float leftPanelWidth    = 260.0f;
    float rightPanelWidth   = 340.0f;
    // Tall enough for one whole row of default-size asset tiles - face, name
    // and detail line - because the panel's first tab is a grid and a row cut
    // across the middle reads as a broken tile rather than as a short panel.
    // Persisted per project, so this is what a project with no saved layout
    // opens at.
    float bottomPanelHeight = 250.0f;

    bool viewportHovered = false;    ///< Whether mouse is over viewport
    bool hierarchyDirty  = true;     ///< Set by entity ops, consumed by HierarchyPanel
    bool editorVisible   = true;     ///< Toggle entire editor UI (F5)
    bool requestModelImport = false;  ///< Set by the Import Model menu item, consumed by the menu-bar dialog
    bool requestPlacePrefab = false;  ///< Set by the Create > Prefab item, consumed by the menu-bar dialog
    bool requestScriptReload = false; ///< Set by the Reload Scripts menu item, consumed by EditorSystem (hot-reload)
    bool requestNewProject   = false; ///< Set by the New Project menu item, consumed by the dialog that draws it
    bool requestOpenProject  = false; ///< Set by the Open Project menu item, consumed by the dialog that draws it

    bool sceneDirty = false;    ///< Unsaved edits since last save/load. Title shows '*'.

    /**
     * @brief What action that throws the live scene away has been asked for,
     *        and how far the unsaved-changes guard has got with answering it.
     *
     * One request at a time, in three stages. Ask is a request nobody has
     * answered yet: EditorSystem prompts when the scene is dirty and goes
     * straight to Run when it is not. Saving is the prompt's "Save" answer
     * waiting for the write to land, and drops the request instead if the
     * author backs out of the Save-As it opened. Run is approved, and
     * EditorSystem performs it at one point in the frame with no ImGui window
     * on the stack, because all four rebuild the world.
     */
    enum class SceneAction : uint8_t { None, Quit, New, Open, OpenProject };
    enum class ActionStage : uint8_t { Ask, Saving, Run };
    SceneAction pendingAction = SceneAction::None;
    ActionStage actionStage   = ActionStage::Ask;
    std::string actionPayload;    ///< The target: a scene file, a project root, or nothing.

    std::vector<std::string> recentScenes;    ///< MRU list (absolute paths), most-recent first.
    std::vector<std::string> recentProjects;  ///< MRU project roots, most-recent first.

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
     * @brief Ask for the right panel's Material tab, showing @p material.
     *
     * A request rather than a tab switch because every caller is drawn before
     * the tab bar that would answer it, and because the panel may be hidden
     * when the ask is made.
     *
     * @param material The material to edit; the tab follows the selection again
     *        once a different entity carrying one is picked.
     */
    void openMaterial(MaterialHandle material) {
        materialEditorTarget = material;
        showInspector        = true;
        revealMaterialTab    = true;
    }

    /**
     * @brief Ask for an action that throws the live scene away.
     *
     * The one way in for Quit, New Scene, Open Scene and Open Project. The
     * request is parked rather than performed, so a caller says what it wants
     * instead of remembering to ask about unsaved changes - which is what stops
     * the next destructive action from being the one that forgets - and so the
     * scene is never rebuilt inside the ImGui frame that asked for it.
     * EditorSystem prompts, waits out a save, and performs.
     *
     * Ignored while an earlier request is still unresolved, so a held key or a
     * second click cannot stack prompts.
     *
     * @param action What to do once the guard clears.
     * @param payload The action's target - the scene file for Open, the project
     *        root for OpenProject, empty for the two that need none.
     */
    void requestSceneAction(SceneAction action, std::string payload = {}) {
        if (pendingAction != SceneAction::None) return;
        pendingAction = action;
        actionStage   = ActionStage::Ask;
        actionPayload = std::move(payload);
    }

    /**
     * @brief Drop the pending request, whatever stage it had reached.
     */
    void clearSceneAction() {
        pendingAction = SceneAction::None;
        actionStage   = ActionStage::Ask;
        actionPayload.clear();
    }

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

} // namespace Vkm::Engine

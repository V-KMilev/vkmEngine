#pragma once

#include <algorithm>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "command/command_host.h"
#include "command/command_stack.h"
#include "io/project.h"
#include "input/camera_controller_system.h"
#include "input/editor_keybinds.h"
#include "overlays/transform_gizmo.h"
#include "platform/window/window_manager.h"
#include "resource/asset/material_asset.h"

namespace Vkm::Engine {

/**
 * @brief What a viewport drag does: the four tools on the tool strip.
 *
 * Select draws no gizmo, which is why it is a tool rather than a fourth GizmoOperation.
 */
enum class EditorTool { Select, Translate, Rotate, Scale };

/**
 * @brief The gizmo operation a tool asks the gizmo for.
 *
 * @param tool The active tool; Select answers Translate (GizmoOverlay draws none under it).
 * @return The matching operation.
 */
inline GizmoOperation operationFor(EditorTool tool) {
    switch (tool) {
        case EditorTool::Rotate: return GizmoOperation::Rotate;
        case EditorTool::Scale:  return GizmoOperation::Scale;
        case EditorTool::Select:
        case EditorTool::Translate: break;
    }
    return GizmoOperation::Translate;
}

/**
 * @brief What follows the person rather than the project.
 *
 * Persisted in the user file (EditorSettings::saveUser), not the project, which
 * would carry them onto somebody else's desk. The owner of each value;
 * EditorSystem applies it to the camera controller and the window.
 */
struct Preferences {
    EditorKeybinds keybinds;

    bool  snapEnabled   = false;
    float snapTranslate = 1.0f;
    float snapRotate    = 15.0f;    ///< Degrees
    float snapScale     = 0.1f;

    CameraControllerSystem::Settings camera;

    /**
     * @brief How much bigger the editor's UI is than the display asks for.
     *
     * A multiplier on the window's content scale, not a replacement for it.
     */
    float uiScale = 1.0f;

    bool       vsync      = true;
    /// Frames per second the window is held to; 0 is unlimited.
    int        fpsCap     = 0;
    WindowMode windowMode = WindowMode::Windowed;

    static constexpr float MIN_UI_SCALE = 0.5f;   ///< Smallest uiScale that is still legible.
    static constexpr float MAX_UI_SCALE = 4.0f;   ///< Largest uiScale that still leaves room to work.

    /**
     * @brief @p prefs held to what the Preferences window could have set.
     *
     * The file is hand-editable; nothing downstream was written for a value no control reaches.
     *
     * @param prefs Preferences as read.
     * @return The same, with the UI scale and the pitch limits in range.
     */
    static Preferences bounded(Preferences prefs) {
        prefs.uiScale = std::clamp(prefs.uiScale, MIN_UI_SCALE, MAX_UI_SCALE);
        prefs.camera  = CameraControllerSystem::Settings::bounded(prefs.camera);
        return prefs;
    }
};

/**
 * @brief Shared editor state, owned by EditorSystem.
 */
struct EditorState : CommandHost {
    /// The ACTIVE entity (last clicked); always in `selection` when set.
    EntityId selectedEntity{};
    std::vector<EntityId> selection;         ///< Every selected entity, active included.
    bool     worldSelected = false;

    /**
     * @brief The selected id at each entity slot, null where none is.
     *
     * `selection` indexed by slot, so isSelected is one comparison, not a scan of
     * thousands. The stored id carries the generation, so a reused slot cannot
     * answer for its old occupant. Written only by the selection helpers below.
     */
    std::vector<EntityId> selectedAt;

    EditorTool     tool           = EditorTool::Translate;
    GizmoMode      gizmoMode      = GizmoMode::Local;

    Preferences prefs;

    // Which dockable window groups are shown, each named for its group's first
    // window (see EditorSystem::drawPanels). Where each is docked is ImGui's ini.
    bool showHierarchy   = true;
    bool showInspector   = true;
    bool showAssets      = true;

    bool showPreferences = false;
    /// What the Material window edits (else: the selected entity's)
    MaterialHandle materialEditorTarget{};
    /**
     * @brief The selection the pin above was chosen against.
     *
     * Here rather than on the panel, which only draws while its window is showing.
     */
    EntityId materialPinnedAt{};
    bool revealMaterial    = false;             ///< Bring the Material window to the front
    bool showRenderSettings = false;
    bool showProjectSettings = false;
    bool showColliders      = false;            ///< Collider wireframes in the viewport
    bool showBounds         = false;            ///< Per-entity world AABBs in the viewport
    bool showSkeletons      = false;            ///< Posed rigs as bone segments in the viewport

    // A menu closes the frame its item is clicked, taking any modal opened inside it,
    // so a menu item asks and the dialog, drawn at the root window's scope, answers.
    // Flags rather than calls: each has several askers that cannot see the dialog.
    bool requestModelImport = false;  ///< Consumed by ModelImportDialog
    bool requestPlacePrefab = false;  ///< Consumed by PlacePrefabDialog
    bool requestScriptReload = false; ///< Consumed by EditorSystem (hot-reload)
    bool requestNewProject   = false; ///< Consumed by NewProjectDialog
    std::string newProjectTemplate;   ///< The project it copies; empty for templates/default

    /// A vkm command for BuildController: the words after `vkm`, and a project to open once
    /// it succeeds (after `vkm new`). Empty for none.
    std::vector<std::string> requestVkm;
    std::string              requestVkmOpens;
    bool requestOpenProject  = false; ///< Consumed by OpenProjectDialog
    bool requestResetLayout  = false; ///< Consumed by the workspace before its dockspace

    bool sceneDirty = false;    ///< Unsaved edits since last save/load.

    /**
     * @brief The pending action that throws the live scene away, and how far the
     *        unsaved-changes guard has got with it.
     *
     * Ask: unanswered; EditorSystem prompts if the scene is dirty, else goes to Run.
     * Saving: "Save" was chosen and the write has not landed; backing out of the
     * Save-As it opened drops the request. Run: approved; EditorSystem performs it
     * with no ImGui window on the stack, as New, Open and OpenProject rebuild the world.
     */
    enum class SceneAction : uint8_t { None, Quit, New, Open, OpenProject };
    enum class ActionStage : uint8_t { Ask, Saving, Run };
    SceneAction pendingAction = SceneAction::None;
    ActionStage actionStage   = ActionStage::Ask;
    std::string actionPayload;    ///< The target: a scene file, a project root, or nothing.

    /// MRU list (absolute paths; saved project-relative), most-recent first.
    std::vector<std::string> recentScenes;
    std::vector<std::string> recentProjects;  ///< MRU project roots, most-recent first.

    /**
     * @brief Where the editor last viewed each scene from, keyed by project-relative
     *        path (ProjectPaths::toProjectRelative) so it survives the project moving.
     *
     * The viewpoint is no entity's, so the project's editor_settings.json keeps it,
     * not the scene file (SceneIOController::rememberView / restoreView).
     */
    std::map<std::string, EditorViewpoint> sceneViews;

    Project project;              ///< The open project.json, as read and as edited.
    bool    projectOpen = false;  ///< Set by ProjectController::open; a failed open leaves it.

    static constexpr size_t MAX_RECENT_ENTRIES = 8;

    // Dropped when another scene replaces the world: entity IDs across scenes aren't comparable.
    CommandStack commands;

    // Toast, ticked down by EditorSystem. ToastKind is the command layer's, so a
    // command can raise one without knowing what an editor is.
    std::string toastMessage;
    float       toastTimeRemaining = 0.0f;
    ToastKind   toastKind          = ToastKind::Info;

    /**
     * @brief Mark the scene as having unsaved changes.
     *
     * pushStep calls it for a finished edit; a drag that pushes once at the end calls it as it goes.
     */
    void markSceneDirty() override { sceneDirty = true; }

    /// @copydoc CommandHost::commandStack()
    CommandStack& commandStack() override { return commands; }

    /**
     * @brief Ask for the Material window, showing @p material.
     *
     * A request, not a focus: the caller may draw before the window, which may be hidden.
     *
     * @param material The material to edit; the window follows the selection again
     *        once another entity carrying one is picked.
     */
    void openMaterial(MaterialHandle material) {
        materialEditorTarget = material;
        materialPinnedAt     = selectedEntity;
        showInspector        = true;
        revealMaterial       = true;
    }

    /**
     * @brief Ask for an action that throws the live scene away.
     *
     * The one way in for Quit, New Scene, Open Scene and Open Project. Parked, not
     * performed, so no caller has to ask about unsaved changes and the scene is never
     * rebuilt inside the ImGui frame that asked. Ignored while an earlier request is
     * unresolved, so a held key or a second click cannot stack prompts.
     *
     * @param action What to do once the guard clears.
     * @param payload Scene file for Open, project root for OpenProject, else empty.
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
     * @brief Replace the whole selection with @p id.
     *
     * Selection changes go through this and the helpers below, which keep a non-null
     * selectedEntity in `selection`, `selectedAt` matching `selection`, and selection
     * and worldSelected exclusive. clickSelect decides what a click does.
     *
     * @param id The entity to select.
     */
    void selectEntity(EntityId id) override {
        clearSelection();
        selectedEntity = id;
        selection.assign(1, id);
        markSelected(id);
        worldSelected = false;
    }

    void addToSelection(EntityId id) {
        if (!isSelected(id)) {
            selection.push_back(id);
            markSelected(id);
        }
        selectedEntity = id;
        worldSelected  = false;
    }

    void toggleSelection(EntityId id) {
        if (isSelected(id)) {
            selection.erase(std::find(selection.begin(), selection.end(), id));
            selectedAt[id.slot()] = {};
            if (selectedEntity == id)
                selectedEntity = selection.empty() ? EntityId{} : selection.back();
        } else {
            selection.push_back(id);
            markSelected(id);
            selectedEntity = id;
        }
        worldSelected = false;
    }

    /**
     * @brief Drop every selected entity the scene no longer holds.
     *
     * Deletes and scene swaps leave dead ids behind; the active entity falls
     * back to the last one still selected.
     *
     * @param scene The scene the selection names entities of.
     */
    void pruneSelection(const Scene& scene) {
        const auto dead = [&](EntityId id) {
            if (scene.isAlive(id)) return false;
            if (id.slot() < selectedAt.size() && selectedAt[id.slot()] == id) selectedAt[id.slot()] = {};
            return true;
        };
        selection.erase(std::remove_if(selection.begin(), selection.end(), dead), selection.end());
        if (selectedEntity && !scene.isAlive(selectedEntity)) {
            selectedEntity = selection.empty() ? EntityId{} : selection.back();
        }
    }

    /**
     * @brief Apply a click on @p id with the editor's one click policy.
     *
     * Plain replaces the selection, Ctrl toggles, Shift adds; there is no range.
     * A null @p id clears the selection unless a modifier is held.
     *
     * @param id The entity clicked, or null for a click on empty space.
     */
    void clickSelect(EntityId id) {
        const ImGuiIO& io = ImGui::GetIO();
        if (!id) {
            if (!io.KeyCtrl && !io.KeyShift) deselect();
        } else if (io.KeyCtrl) {
            toggleSelection(id);
        } else if (io.KeyShift) {
            addToSelection(id);
        } else {
            selectEntity(id);
        }
    }

    bool isSelected(EntityId id) const {
        return id && id.slot() < selectedAt.size() && selectedAt[id.slot()] == id;
    }

    void selectWorld() {
        clearSelection();
        worldSelected = true;
    }
    void deselect() {
        clearSelection();
        worldSelected = false;
    }

    /// Empty the selection, and the slot index with it.
    void clearSelection() {
        for (EntityId id : selection) selectedAt[id.slot()] = {};
        selection.clear();
        selectedEntity = {};
    }

    /**
     * @brief Make @p id, already selected, the active entity.
     *
     * @param id A member of the selection; ignored when it is not one.
     */
    void makeActive(EntityId id) {
        if (isSelected(id)) selectedEntity = id;
    }

    /// Record @p id in the slot index; `selection` is the caller's to append.
    void markSelected(EntityId id) {
        if (id.slot() >= selectedAt.size()) selectedAt.resize(id.slot() + 1);
        selectedAt[id.slot()] = id;
    }

    /**
     * @brief Show a transient toast, replacing any shown before it.
     *
     * @param kind How loudly.
     * @param msg What to say.
     * @param seconds How long; 0 or less takes the length from @p kind.
     */
    void pushToast(ToastKind kind, std::string msg, float seconds = 0.0f) override {
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
 * @param mru   The list to promote into, most-recent first.
 * @param value The path to move to the front.
 */
inline void pushRecentPath(std::vector<std::string>& mru, const std::string& value) {
    mru.erase(std::remove(mru.begin(), mru.end(), value), mru.end());
    mru.insert(mru.begin(), value);
    if (mru.size() > EditorState::MAX_RECENT_ENTRIES) mru.resize(EditorState::MAX_RECENT_ENTRIES);
}

} // namespace Vkm::Engine

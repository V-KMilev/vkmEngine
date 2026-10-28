#pragma once

#include <algorithm>
#include <any>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <imgui.h>

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "command/command_host.h"
#include "command/command_stack.h"
#include "core/reflect.h"
#include "editor_settings.h"
#include "io/project.h"
#include "input/camera_controller_system.h"
#include "input/editor_keybinds.h"
#include "input/view_placement.h"
#include "overlays/terrain_tool.h"
#include "overlays/transform_gizmo.h"
#include "panels/console_view.h"
#include "platform/window/window_manager.h"
#include "resource/asset/material_asset.h"
#include "resource/asset_type.h"

namespace Vkm::Engine {

struct EntityClipboard;

/**
 * @brief What a viewport drag does: the four tools on the tool strip.
 *
 * Select draws no gizmo, which is why it is a tool rather than a fourth GizmoOperation.
 */
enum class EditorTool {
    Select,
    Translate,
    Rotate,
    Scale,
    Count  ///< Sentinel; keep last. Drives the VKM_ENUM_NAMES check.
};

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
        case EditorTool::Translate:
        case EditorTool::Count: break;
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

    // The range of snap steps the Preferences window offers, and so all a hand-edited file is held to.
    static constexpr float MIN_SNAP_TRANSLATE = 0.01f;
    static constexpr float MAX_SNAP_TRANSLATE = 100.0f;
    static constexpr float MIN_SNAP_ROTATE    = 1.0f;     ///< Degrees
    static constexpr float MAX_SNAP_ROTATE    = 180.0f;   ///< Degrees
    static constexpr float MIN_SNAP_SCALE     = 0.01f;
    static constexpr float MAX_SNAP_SCALE     = 10.0f;

    /**
     * @brief @p prefs held to what the Preferences window could have set.
     *
     * The file is hand-editable; nothing downstream was written for a value no control reaches.
     *
     * @param prefs Preferences as read.
     * @return The same, every bounded field within its MIN_ and MAX_.
     */
    static Preferences bounded(Preferences prefs) {
        prefs.uiScale       = std::clamp(prefs.uiScale, MIN_UI_SCALE, MAX_UI_SCALE);
        prefs.snapTranslate = std::clamp(prefs.snapTranslate, MIN_SNAP_TRANSLATE, MAX_SNAP_TRANSLATE);
        prefs.snapRotate    = std::clamp(prefs.snapRotate, MIN_SNAP_ROTATE, MAX_SNAP_ROTATE);
        prefs.snapScale     = std::clamp(prefs.snapScale, MIN_SNAP_SCALE, MAX_SNAP_SCALE);
        prefs.camera        = CameraControllerSystem::Settings::bounded(prefs.camera);
        return prefs;
    }
};

/**
 * @brief Which kinds of gizmo the viewport draws, by View > Show.
 *
 * A hidden kind draws no marker either, so a click cannot pick what is not shown.
 */
struct GizmoVisibility {
    bool lights    = true;
    bool cameras   = true;
    bool audio     = true;   ///< Sources and listeners.
    bool acoustics = false;  ///< The ear's rays off the colliders, and those to each sound.
    bool probes    = true;   ///< Reflection probes, planar reflectors and irradiance volumes.
    bool effects   = true;   ///< Decals and particle emitters.
    bool colliders = false;  ///< Colliders and joints.
    bool contacts  = false;  ///< Where bodies touch, and how hard the solver pushes each.
    bool bounds    = false;  ///< Each drawn object's world box.
    bool skeletons = false;  ///< Posed rigs as bone segments.
};

/**
 * @brief The effects the viewport plays while the world stands still (HostChrome::EffectPreview).
 */
struct EffectPreviewState {
    bool     playing  = false;
    bool     paused   = false;
    bool     all      = false;  ///< Every emitter, not only the selection's.
    uint64_t restarts = 0;
};

/// An asset asked to be opened in its editor window: which kind, and which one of it.
struct AssetToEdit {
    AssetType    type;
    StorageIndex key;
};

/**
 * @brief Shared editor state, owned by EditorSystem.
 */
struct EditorState : CommandHost {
    /// What the viewport previews of the world's effects outside a play session.
    EffectPreviewState effects;

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
    /// The terrain brush; while the Terrain window is open with a tool picked, a drag brushes.
    TerrainToolSettings terrainTool;

    Preferences prefs;

    // Which dockable window groups are shown, each named for its group's first
    // window (see EditorSystem::drawPanels). Where each is docked is ImGui's ini.
    bool showHierarchy   = true;
    bool showInspector   = true;
    bool showAssets      = true;
    bool showTerrain     = false;  ///< The Terrain window, docked with the Inspector.

    bool showPreferences = false;

    /// A preview window was under the pointer last frame: its F frames it, not the scene's view.
    bool previewHovered = false;
    /// The Console's list had the keyboard and a selected line last frame: its Ctrl+C copies the line.
    bool consoleHoldsKeys = false;
    /// An asset whose editor window was asked for: consumed by EditorSystem into its AssetEditors.
    std::optional<AssetToEdit> assetToEdit;
    bool revealTerrain     = false;             ///< Bring the Terrain window to the front
    bool revealConsole     = false;             ///< Bring the bottom row forward on its Console
    bool showRenderSettings = false;
    bool showProjectSettings = false;
    GizmoVisibility gizmos;                     ///< What the viewport draws over the scene

    // A menu closes the frame its item is clicked, taking any modal opened inside it,
    // so a menu item asks and the dialog, drawn at the root window's scope, answers.
    // Flags rather than calls: each has several askers that cannot see the dialog.
    bool requestModelImport = false;  ///< Consumed by ModelImportDialog
    bool requestPlacePrefab = false;  ///< Consumed by PlacePrefabDialog
    bool requestScriptReload = false; ///< Consumed by EditorSystem (hot-reload)
    bool requestRestart      = false; ///< Consumed by EditorSystem: a game asked to start play over
    bool requestNewProject   = false; ///< Consumed by NewProjectDialog
    std::string newProjectTemplate;   ///< The project it copies; empty for templates/default

    /// A vkm command for BuildController: the words after `vkm`, and a project to open once
    /// it succeeds (after `vkm new`). Empty for none.
    std::vector<std::string> requestVkm;
    std::string              requestVkmOpens;
    bool requestOpenProject  = false; ///< Consumed by OpenProjectDialog
    bool requestResetLayout  = false; ///< Consumed by the workspace before its dockspace
    bool requestHideEditor   = false; ///< Window > Hide Editor; EditorSystem hides its UI, as the key does

    /// The project's render settings while Render Settings shows a quality tier over them; Play puts
    /// them back before it takes its copy, so a preview never becomes the project's.
    std::optional<RenderSettings> renderPreviewHeld;

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

    /// Where the editor's view looks, for Create to put a new entity there; restated every frame.
    ViewAim viewAim;

    /// The component a card header's Copy took, by value; Paste Values offers it to a card of its type.
    std::any componentClipboard;

    /// What Copy and Cut took (EditorActions::copySelection), for Paste.
    std::shared_ptr<const EntityClipboard> entityClipboard;

    Project project;              ///< The open project.json, as read and as edited.
    bool    projectOpen = false;  ///< Set by ProjectController::open; a failed open leaves it.

    static constexpr size_t MAX_RECENT_ENTRIES = 8;

    // Dropped when another scene replaces the world: entity IDs across scenes aren't comparable.
    CommandStack commands;

    /// One transient message. ToastKind is the command layer's, so a command can raise one without
    /// knowing what an editor is.
    struct Toast {
        ToastKind                  kind = ToastKind::Info;
        std::string                message;
        float                      remaining = 0.0f;  ///< Seconds it still shows.
        std::optional<ConsoleView> view;              ///< The Console view its message points at, if any.

        /**
         * @brief The Console view a click on it opens: the one its message points at, else the Log
         *        for an error or a warning, whose cause the log holds.
         *
         * @return None for an info toast that points at no view: a click only puts it away.
         */
        std::optional<ConsoleView> opens() const {
            if (view) return view;
            if (kind == ToastKind::Error || kind == ToastKind::Warning) return ConsoleView::Log;
            return std::nullopt;
        }
    };

    /// The most toasts shown at once; the oldest goes for a newer one.
    static constexpr size_t MAX_TOASTS = 3;

    /// What the editor said lately, oldest first, ticked down by EditorSystem.
    std::vector<Toast> toasts;

    /**
     * @brief Mark the scene as having unsaved changes.
     *
     * pushStep calls it for a finished edit; a drag that pushes once at the end calls it as it goes.
     */
    void markSceneDirty() override { sceneDirty = true; }

    /// @copydoc CommandHost::commandStack()
    CommandStack& commandStack() override { return commands; }

    /**
     * @brief Forget the history and the copied component, which name the world's entities and assets.
     *
     * For a world replaced or an asset removed: kept, an undo or a paste would reach another
     * scene's entities or a freed handle.
     */
    void forgetWorldState() {
        commands.clear();
        componentClipboard.reset();
    }

    /**
     * @brief End a quality-tier preview: @p render takes the project's settings back, keeping the
     *        editor's view fields - the debug view, the grid - as they are now. Nothing without one.
     *
     * @param render The settings the frame renders with.
     */
    void endRenderPreview(RenderSettings& render) {
        if (!renderPreviewHeld) return;
        EditorSettings::copyViewFields(render, *renderPreviewHeld);
        render = *renderPreviewHeld;
        renderPreviewHeld.reset();
    }

    /**
     * @brief Ask for @p material's own window, as openAssetEditor does.
     *
     * @param material The material to edit.
     */
    void openMaterial(MaterialHandle material) { openAssetEditor(AssetType::Material, material.key); }

    /**
     * @brief Ask for the editor window of the asset @p key (AssetEditors).
     *
     * @param type Its kind.
     * @param key  Its handle's slot and generation.
     */
    void openAssetEditor(AssetType type, StorageIndex key) { assetToEdit = AssetToEdit{type, key}; }

    /**
     * @brief Ask for the Terrain window, editing @p terrain.
     *
     * @param terrain An entity with a Terrain, or null to keep the one it edits.
     */
    void openTerrain(EntityId terrain) {
        if (terrain) terrainTool.target = terrain;
        showTerrain   = true;
        revealTerrain = true;
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
     * @brief Show a transient toast over the oldest past MAX_TOASTS; the newest said again is only
     *        shown for longer.
     *
     * @param kind How loudly.
     * @param msg What to say.
     * @param seconds How long; 0 or less takes the length from @p kind.
     */
    void pushToast(ToastKind kind, std::string msg, float seconds = 0.0f) override {
        pushToast(kind, std::move(msg), std::optional<ConsoleView>{}, seconds);
    }

    /**
     * @brief Show a toast pointing at a Console view, which a click on it then opens.
     *
     * @param kind How loudly.
     * @param msg What to say; " - see the Console's" and @p view's name are appended to it.
     * @param view The view a click opens (Toast::opens); none appends nothing and takes it from @p kind.
     * @param seconds How long; 0 or less takes the length from @p kind.
     */
    void pushToast(ToastKind kind, std::string msg, std::optional<ConsoleView> view, float seconds = 0.0f) {
        if (view) msg += std::string(" - see the Console's ") + consoleViewName(*view);
        if (seconds <= 0.0f) {
            seconds = (kind == ToastKind::Error) ? 6.0f
                    : (kind == ToastKind::Warning) ? 4.0f : 2.5f;
        }
        if (!toasts.empty() && toasts.back().kind == kind && toasts.back().message == msg) {
            toasts.back().remaining = seconds;
            toasts.back().view      = view;
            return;
        }
        toasts.push_back({kind, std::move(msg), seconds, view});
        if (toasts.size() > MAX_TOASTS) toasts.erase(toasts.begin());
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

VKM_ENUM_NAMES(::Vkm::Engine::EditorTool, "Select", "Translate", "Rotate", "Scale")

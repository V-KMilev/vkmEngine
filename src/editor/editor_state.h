#pragma once

#include <algorithm>
#include <any>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "ecs/entity.h"
#include "ecs/scene.h"
#include "undo/command_host.h"
#include "undo/command_stack.h"
#include "chrome/toasts.h"
#include "core/graphics.h"
#include "base/reflect/reflect.h"
#include "base/reflect/enum_names.h"
#include "editor_settings.h"
#include "preferences.h"
#include "workspace.h"
#include "io/project/project.h"
#include "view/editor_viewpoint.h"
#include "view/view_placement.h"
#include "overlays/terrain_tool.h"
#include "panels/console/console_view.h"
#include "resource/asset/material_asset.h"
#include "requests.h"
#include "resource/asset_type.h"
#include "selection.h"
#include "session/scene_actions.h"

namespace Vkm::Engine {

/**
 * @brief What Copy and Cut took: a scene document that names its assets, so it pastes into any scene.
 */
struct EntityClipboard {
    std::string                                scene;      ///< The plain subtrees in selection order, then
                                                           ///< the instances, each root at its world pose.
    std::vector<std::pair<uint32_t, EntityId>> parents;    ///< Each root's parent when taken, by its slot
                                                           ///< in `scene`; null for a root.
    uint64_t                                   world = 0;  ///< Scene::epoch() when taken: where those
                                                           ///< parents mean anything.
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

/**
 * @brief Shared editor state, owned by EditorSystem.
 */
struct EditorState : CommandHost {
    EffectPreviewState effects;  ///< The viewport's preview of the world's effects outside a play session.

    Selection selection;  ///< The entities selected and the active one, or the World.

    Workspace workspace;

    TerrainToolSettings terrainTool;  ///< The brush: a drag brushes while the Terrain window has a tool.

    Preferences prefs;

    bool showPreferences = false;

    bool previewHovered      = false;  ///< A preview window had the pointer last frame: F frames it.
    bool consoleHoldsKeys    = false;  ///< The Console's list had the keyboard and a selected line
                                       ///< last frame: its Ctrl+C copies the line.
    bool showRenderSettings  = false;
    bool showProjectSettings = false;

    Requests requests;

    std::optional<GraphicsPreset> previewTier;  ///< The tier Render Settings shows over the project's
                                                ///< settings, none while theirs show; Play ends it.

    bool sceneDirty = false;  ///< Unsaved edits since last save/load.

    SceneActions sceneActions;

    std::vector<std::string> recentScenes;    ///< Absolute paths, most-recent first; saved project-relative.
    std::vector<std::string> recentProjects;  ///< MRU project roots, most-recent first.

    std::map<std::string, EditorViewpoint> sceneViews;  ///< Each scene's last viewpoint, keyed by its
                                                        ///< project-relative path; kept in the project's
                                                        ///< editor_settings.json, not the scene file.

    std::vector<EntityId> hiddenInView;  ///< The Hierarchy's eye: subtrees the editor's view leaves out.
                                         ///< This session's alone: never saved, dropped by an open.
    std::vector<EntityId> lockedInView;  ///< The Hierarchy's lock: subtrees a view click passes through.
                                         ///< This session's alone, as hiddenInView is.

    ViewAim viewAim;  ///< Where the editor's view looks, for Create to place at; restated every frame.

    std::any componentClipboard;  ///< What a card header's Copy took, by value, for Paste Values.

    std::shared_ptr<const EntityClipboard> entityClipboard;  ///< What Copy and Cut took, for Paste
                                                             ///< (EditorActions::copySelection).

    Project project;              ///< The open project.json, as read and as edited.
    Project projectSaved;         ///< The open project.json as last read or written.
    bool    projectOpen = false;  ///< Set by ProjectController::open; a failed open leaves it.

    static constexpr size_t MAX_RECENT_ENTRIES = 8;

    CommandStack commands;  ///< Dropped when another scene replaces the world, whose ids do not carry over.

    Toasts toasts;  ///< What the editor said lately, oldest first, ticked down as they are drawn.

    /**
     * @brief Mark the scene as having unsaved changes.
     *
     * pushStep calls it for a finished edit; a drag that pushes once at the end calls it as it goes.
     */
    void markSceneDirty() override { sceneDirty = true; }

    /// @copydoc CommandHost::commandStack()
    CommandStack& commandStack() override { return commands; }

    /**
     * @brief Carry a clipboard taken in the world of epoch @p from over to @p scene, as Stop restored it.
     *
     * Stop gives the world a new epoch, and a copy left on the old one pastes with its parents forgotten.
     * The world comes back under its ids, so a parent it holds is kept as it is; one it does not is
     * forgotten, as a slot free at Play hands out again the ids play took in it. A clipboard of any
     * other world is left.
     *
     * @param from  The epoch of the world as Play found it (PlaySnapshot::epochAtCapture).
     * @param scene The world as it came back.
     */
    void carryClipboard(uint64_t from, const Scene& scene) {
        const std::shared_ptr<const EntityClipboard>& held = entityClipboard;
        if (!held || held->world != from) return;
        auto carried = std::make_shared<EntityClipboard>(*held);
        carried->world = scene.epoch();
        for (auto& [slot, parent] : carried->parents) {
            if (!scene.isAlive(parent)) parent = {};
        }
        entityClipboard = std::move(carried);
    }

    /**
     * @brief Forget the history and the copied component, which name the world's entities and assets.
     *
     * For a world replaced or an asset removed, after which an undo or a paste would reach the wrong
     * entities or a freed handle.
     */
    void forgetWorldState() {
        commands.clear();
        componentClipboard.reset();
    }

    /**
     * @brief End a quality-tier preview, drawing with the project's settings again; nothing without one.
     *
     * @param graphics The frame's render settings.
     */
    void endRenderPreview(Graphics& graphics) {
        if (!previewTier) return;
        previewTier.reset();
        graphics.reset();
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
    void openAssetEditor(AssetType type, StorageIndex key) { requests.ask(AssetToEdit{type, key}); }

    /**
     * @brief Ask for the Terrain window, editing @p terrain.
     *
     * @param terrain An entity with a Terrain, or null to keep the one it edits.
     */
    void openTerrain(EntityId terrain) {
        if (terrain) terrainTool.target = terrain;
        workspace.showTerrain = true;
        requests.ask(Ask::RevealTerrain);
    }

    void selectEntity(EntityId id) override { selection.select(id); }

    void pushToast(ToastKind kind, std::string msg, float seconds = 0.0f) override {
        toasts.push(kind, std::move(msg), std::nullopt, seconds);
    }
};

/**
 * @brief Whether @p set holds @p id itself (not an ancestor of it).
 *
 * @param set One of the eye's or the lock's sets.
 * @param id  The entity asked about.
 * @return True when it was put there.
 */
inline bool isIn(const std::vector<EntityId>& set, EntityId id) {
    return std::find(set.begin(), set.end(), id) != set.end();
}

/**
 * @brief Put @p id in @p set, or take it out if it is there.
 *
 * @param set One of the eye's or the lock's sets.
 * @param id  The entity a row toggles.
 */
inline void toggleIn(std::vector<EntityId>& set, EntityId id) {
    const auto at = std::find(set.begin(), set.end(), id);
    if (at == set.end()) set.push_back(id);
    else                 set.erase(at);
}

/**
 * @brief Move @p value to the front of @p mru, de-duplicated and capped at EditorState::MAX_RECENT_ENTRIES.
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

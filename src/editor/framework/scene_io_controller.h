#pragma once

#include <cstdint>
#include <string>

#include "framework/asset_picker.h"

namespace Engine {

struct FrameContext;
struct EditorState;
class CameraControllerSystem;
class MaterialPreviewSession;

/**
 * @brief Owns editor scene replacement: file I/O and the play-mode snapshot.
 *
 * Holds the current scene path, performs Save / Save-As / Load, renders the
 * Save-As and Load-picker modals, and runs post-load editor housekeeping
 * (camera rebind, temporal-history invalidate) directly inside load().
 *
 * It also owns the in-memory play-mode snapshot: captureSnapshot() on Play
 * serializes the authored scene to memory, and restoreSnapshot() on Stop
 * swaps it back. Both go through the same post-swap housekeeping as load(),
 * since a restore is just an in-memory reload - that shared path is why the
 * snapshot lives here rather than in the playbar.
 *
 * Extracted from EditorSystem (god-file decomposition). EditorSystem owns one
 * of these and forwards menu/keybind intents to it; drawDialogs() must be
 * called once per frame from the same scope the modals were drawn before
 * (inside the menu-bar window) to keep popup behavior identical.
 */
class SceneIOController {
    public:
        SceneIOController(
            CameraControllerSystem& cameraController,
            MaterialPreviewSession& materialPreviews
        );
        ~SceneIOController();

        SceneIOController(const SceneIOController& other) = delete;
        SceneIOController& operator=(const SceneIOController& other) = delete;

        SceneIOController(SceneIOController && other) = delete;
        SceneIOController& operator=(SceneIOController && other) = delete;

        /**
         * @brief Save to the current path, or pop the Save-As prompt if none yet.
         * Clears EditorState::sceneDirty on success.
         */
        void save(FrameContext& ctx, EditorState& state);

        /**
         * @brief Replace the scene with a fresh minimal one (a camera + a sun).
         *
         * Clears entities, resets the Environment and the current path, and
         * runs the same post-swap housekeeping as a load (undo stack, preview
         * cache, selection). Callers guard unsaved changes first.
         */
        void newScene(FrameContext& ctx, EditorState& state);

        /**
         * @brief Empty the scene so something else can take its place.
         *
         * The housekeeping a swap needs that Scene::clear() does not do: live
         * behaviors get onDestroy while their code is still loaded, the undo
         * stack is dropped because entity ids do not carry across scenes, the
         * material previews keyed by the outgoing assets are released, and the
         * saved-scene path is forgotten so a later Save cannot write into
         * whatever was open before.
         *
         * Leaves an empty scene; the caller decides what fills it. Exposed
         * because opening a project is also a scene swap, and doing this by
         * hand there is how the two drift apart.
         *
         * @param ctx Frame context owning the scene being replaced.
         * @param state Editor state whose scene-scoped parts are reset.
         */
        void beginSceneReplace(FrameContext& ctx, EditorState& state);
        /**
         * @brief Queue the Save-As prompt to open on the next drawDialogs().
         *
         * Deferred so the modal is opened from the menu-bar scope, keeping its
         * popup behavior identical to where it was historically drawn.
         */
        void requestSaveAs();
        /**
         * @brief Queue the Load-Scene picker to open on the next drawDialogs().
         *
         * Configures the cached load picker (scenes root, .json filter) and
         * flags it to open; the actual popup is issued from drawDialogs().
         */
        void requestLoad();
        /**
         * @brief Load a scene path directly (used by the recent-scenes menu). Goes
         * through the same housekeeping as a Load-modal pick.
         */
        void loadPath(FrameContext& ctx, EditorState& state, const std::string& path);

        /**
         * @brief Open @p path through the unsaved-changes guard.
         *
         * Prompts (Save / Don't Save / Cancel) when the current scene is
         * dirty, otherwise loads immediately. Every open flow - the picker
         * and Open Recent - routes through this; loading a scene used to
         * silently discard unsaved work.
         */
        void requestOpenPath(FrameContext& ctx, EditorState& state, const std::string& path);

        /**
         * @brief Render any pending Save-As / Load modals.
         *
         * Must be called once per frame from the menu-bar scope so the modals
         * survive the menu closing and behave as before the controller split.
         *
         * @param ctx Frame context supplying the scene and resources to save/load.
         * @param state Editor state read for paths/flags and updated on a completed pick.
         */
        void drawDialogs(FrameContext& ctx, EditorState& state);

        /**
         * @brief Serialize the live scene + assets to an in-memory snapshot for play
         * mode. Call when entering play so Stop can restore the authored state.
         */
        void captureSnapshot(FrameContext& ctx, EditorState& state);
        /**
         * @brief Swap the captured snapshot back in (same housekeeping as load()) and
         * clear it. No-op if no snapshot was captured.
         */
        void restoreSnapshot(FrameContext& ctx, EditorState& state);
        /**
         * @brief Whether a play-mode snapshot is currently held.
         *
         * @return true once captureSnapshot() has stored a snapshot (i.e. while
         *         in play mode), false after restoreSnapshot() clears it.
         */
        bool hasSnapshot() const { return !m_playSnapshot.empty(); }

        bool hasPath() const { return !m_currentScenePath.empty(); }
        const std::string& path() const { return m_currentScenePath; }

        /**
         * @brief True while a Save-As prompt is either queued for opening or currently visible.
         *
         * Used by the save-on-quit flow to detect whether the user cancelled
         * mid-Save (EditorState::afterSaveAction is cleared on cancel; left
         * set on success).
         */
        bool isSaveDialogActive() const;

    private:
        /**
         * @brief Load m_currentScenePath: stashes/restores selection, then runs
         * afterSceneReplace() housekeeping (camera rebind done inline).
         */
        void load(FrameContext& ctx, EditorState& state);
        /**
         * @brief Name of the current selection (empty if none), captured BEFORE a
         * scene swap so afterSceneReplace can re-select it by name afterwards.
         */
        static std::string cacheSelectionName(FrameContext& ctx, EditorState& state);
        /**
         * @brief Editor housekeeping shared by load() and restoreSnapshot() after the
         * scene + resources have been swapped: clear undo, drop stale GPU
         * previews, re-select @p priorSelectionName, and rebind the active camera.
         */
        void afterSceneReplace(
            FrameContext& ctx,
            EditorState& state,
            const std::string& priorSelectionName,
            const std::string& eventPath
        );

        CameraControllerSystem& m_cameraController;
        MaterialPreviewSession& m_materialPreviews;

        std::string m_currentScenePath;  ///< Empty until the user saves/loads once.

        /**
         * @brief In-memory play-mode snapshot (serialized scene + assets). Non-empty
         * only between captureSnapshot() (Play) and restoreSnapshot() (Stop).
         */
        std::string m_playSnapshot;
        /**
         * @brief EditorState::sceneDirty at capture time, restored on Stop so a play
         * session leaves the dirty flag exactly as the user left it.
         */
        bool        m_playSnapshotDirty = false;
        bool        m_openSaveAsPopup = false;
        char        m_saveAsBuffer[256] = "scene.json";

        /**
         * @brief Shared cached file picker for the Load-Scene flow: rooted at the
         * scenes dir, filtered to .json. requestLoad() configures + opens it;
         * drawDialogs() drives it and feeds a pick into loadPath().
         */
        AssetPicker m_loadPicker;
};

} // namespace Engine

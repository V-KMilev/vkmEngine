#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace Engine {

struct FrameContext;
struct EditorState;
class EventSystem;
class CameraController;
class RenderSystem;

/**
 * @brief Owns editor scene replacement: file I/O and the play-mode snapshot.
 *
 * Holds the current scene path, performs Save / Save-As / Load, renders the
 * Save-As and Load-picker modals, and runs post-load editor housekeeping
 * (camera rebind, temporal-history invalidate) directly inside load().
 * Emits SceneSerializer::SceneLoadedEvent for external subscribers.
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
            EventSystem& events,
            CameraController& cameraController,
            RenderSystem& renderSystem
        );
        ~SceneIOController();

        SceneIOController(const SceneIOController& other) = delete;
        SceneIOController& operator=(const SceneIOController& other) = delete;

        SceneIOController(SceneIOController && other) = delete;
        SceneIOController& operator=(SceneIOController && other) = delete;

        /// Save to the current path, or pop the Save-As prompt if none yet.
        /// Clears EditorState::sceneDirty on success.
        void save(FrameContext& ctx, EditorState& state);
        /// Queue the Save-As prompt (opens on the next drawDialogs()).
        void requestSaveAs();
        /// Queue the Load-Scene picker (opens on the next drawDialogs()).
        void requestLoad();
        /// Load a scene path directly (used by the recent-scenes menu). Goes
        /// through the same housekeeping as a Load-modal pick.
        void loadPath(FrameContext& ctx, EditorState& state, const std::string& path);

        /// Render any pending Save-As / Load modals. Call once per frame.
        void drawDialogs(FrameContext& ctx, EditorState& state);

        /// Serialize the live scene + assets to an in-memory snapshot for play
        /// mode. Call when entering play so Stop can restore the authored state.
        void captureSnapshot(FrameContext& ctx, EditorState& state);
        /// Swap the captured snapshot back in (same housekeeping as load()) and
        /// clear it. No-op if no snapshot was captured.
        void restoreSnapshot(FrameContext& ctx, EditorState& state);
        /// True once captureSnapshot() has stored a snapshot (i.e. in play mode).
        bool hasSnapshot() const { return !m_playSnapshot.empty(); }

        bool hasPath() const { return !m_currentScenePath.empty(); }
        const std::string& path() const { return m_currentScenePath; }

        /**
         * @brief True while a Save-As prompt is either queued for opening or currently visible.
         *
         * Used by the save-on-quit flow to detect whether the user cancelled
         * mid-Save (state.closeAfterSave is cleared on cancel; left set on
         * success).
         */
        bool isSaveDialogActive() const;

    private:
        /// Load m_currentScenePath: stashes/restores selection, then emits
        /// SceneLoadedEvent (camera rebind handled by our own subscriber).
        void load(FrameContext& ctx, EditorState& state);
        /// Name of the current selection (empty if none), captured BEFORE a
        /// scene swap so afterSceneReplace can re-select it by name afterwards.
        static std::string cacheSelectionName(FrameContext& ctx, EditorState& state);
        /// Editor housekeeping shared by load() and restoreSnapshot() after the
        /// scene + resources have been swapped: clear undo, drop stale GPU
        /// previews, re-select @p priorSelectionName, rebind the active camera,
        /// and emit SceneLoadedEvent{eventPath}.
        void afterSceneReplace(
            FrameContext& ctx,
            EditorState& state,
            const std::string& priorSelectionName,
            const std::string& eventPath
        );
        /// Push a saved/loaded scene path to the top of the recent-scenes
        /// MRU list, deduplicating and trimming to MAX_RECENT_SCENES.
        static void pushRecent(EditorState& state, const std::string& path);

        EventSystem&      m_events;
        CameraController& m_cameraController;
        RenderSystem&     m_renderSystem;

        std::string m_currentScenePath;  ///< Empty until the user saves/loads once.

        /// In-memory play-mode snapshot (serialized scene + assets). Non-empty
        /// only between captureSnapshot() (Play) and restoreSnapshot() (Stop).
        std::string m_playSnapshot;
        /// EditorState::sceneDirty at capture time, restored on Stop so a play
        /// session leaves the dirty flag exactly as the user left it.
        bool        m_playSnapshotDirty = false;
        bool        m_openSaveAsPopup = false;
        bool        m_openLoadPopup   = false;
        char        m_saveAsBuffer[256] = "scene.json";

        /// Cached scenes/*.json listing. Refreshed when the Load picker opens;
        /// stays stable while it's open instead of re-listing the directory
        /// every frame the modal is up.
        std::vector<std::string> m_loadCandidates;
};

} // namespace Engine

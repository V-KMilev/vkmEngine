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
 * @brief Owns editor scene file I/O.
 *
 * Holds the current scene path, performs Save / Save-As / Load, renders the
 * Save-As and Load-picker modals, and runs post-load editor housekeeping
 * (camera rebind, temporal-history invalidate) directly inside load().
 * Emits SceneSerializer::SceneLoadedEvent for external subscribers.
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
        /// Push a saved/loaded scene path to the top of the recent-scenes
        /// MRU list, deduplicating and trimming to MaxRecentScenes.
        static void pushRecent(EditorState& state, const std::string& path);

        EventSystem&      m_events;
        CameraController& m_cameraController;
        RenderSystem&     m_renderSystem;

        std::string m_currentScenePath;  ///< Empty until the user saves/loads once.
        bool        m_openSaveAsPopup = false;
        bool        m_openLoadPopup   = false;
        char        m_saveAsBuffer[256] = "scene.json";

        /// Cached scenes/*.json listing. Refreshed when the Load picker opens;
        /// stays stable while it's open instead of re-listing the directory
        /// every frame the modal is up.
        std::vector<std::string> m_loadCandidates;
};

} // namespace Engine

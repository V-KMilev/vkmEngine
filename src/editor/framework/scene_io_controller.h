#pragma once

#include <cstdint>
#include <string>

namespace Engine {

struct FrameContext;
struct EditorState;
class EventSystem;
class CameraController;

/**
 * @brief Owns editor scene file I/O.
 *
 * Holds the current scene path, performs Save / Save-As / Load, renders the
 * Save-As and Load-picker modals, and owns the post-load camera rebind
 * (subscribes to SceneSerializer::SceneLoadedEvent itself).
 *
 * Extracted from EditorSystem (god-file decomposition). EditorSystem owns one
 * of these and forwards menu/keybind intents to it; drawDialogs() must be
 * called once per frame from the same scope the modals were drawn before
 * (inside the menu-bar window) to keep popup behavior identical.
 */
class SceneIOController {
    public:
        SceneIOController(EventSystem* events, CameraController* cameraController);
        ~SceneIOController();

        SceneIOController(const SceneIOController& other) = delete;
        SceneIOController& operator=(const SceneIOController& other) = delete;

        SceneIOController(SceneIOController && other) = delete;
        SceneIOController& operator=(SceneIOController && other) = delete;

        /// Save to the current path, or pop the Save-As prompt if none yet.
        void save(FrameContext& ctx);
        /// Queue the Save-As prompt (opens on the next drawDialogs()).
        void requestSaveAs();
        /// Queue the Load-Scene picker (opens on the next drawDialogs()).
        void requestLoad();

        /// Render any pending Save-As / Load modals. Call once per frame.
        void drawDialogs(FrameContext& ctx, EditorState& state);

        bool hasPath() const { return !m_currentScenePath.empty(); }
        const std::string& path() const { return m_currentScenePath; }

    private:
        /// Load m_currentScenePath: stashes/restores selection, then emits
        /// SceneLoadedEvent (camera rebind handled by our own subscriber).
        void load(FrameContext& ctx, EditorState& state);

        EventSystem*      m_events           = nullptr;
        CameraController* m_cameraController = nullptr;
        uint64_t          m_sceneLoadedListenerId = 0;

        std::string m_currentScenePath;  ///< Empty until the user saves/loads once.
        bool        m_openSaveAsPopup = false;
        bool        m_openLoadPopup   = false;
        char        m_saveAsBuffer[256] = "scene.json";
};

} // namespace Engine

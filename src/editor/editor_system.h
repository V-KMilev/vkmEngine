#pragma once

#include <cstdint>
#include <string>

#include <imgui.h>

#include "core/system.h"
#include "editor_state.h"
#include "panels/viewport_overlay.h"
#include "panels/inspector_panel.h"
#include "panels/bottom_panel.h"
#include "panels/hierarchy_panel.h"
#include "panels/gizmo_overlay.h"
#include "panels/viewport_toolbar.h"
#include "panels/playback_bar.h"
#include "panels/preferences_panel.h"

struct GLFWwindow;

namespace Engine {

class CameraController;
class EventSystem;
class VisibilitySystem;
class RenderSystem;

class EditorSystem : public System {
    public:
        EditorSystem(
            GLFWwindow* window,
            CameraController* cameraController,
            VisibilitySystem* visibilitySystem,
            RenderSystem* renderSystem,
            EventSystem* events
        );
        ~EditorSystem() override;

        EditorSystem(const EditorSystem& other) = delete;
        EditorSystem& operator=(const EditorSystem& other) = delete;

        EditorSystem(EditorSystem && other) = delete;
        EditorSystem& operator=(EditorSystem && other) = delete;

        void update(FrameContext& ctx) override;

    private:
        void drawMenuBar(FrameContext& ctx);
        void drawStatusBar(const FrameContext& ctx);

        /// Save to m_currentScenePath (or pop the Save-As prompt if empty).
        void saveScene(FrameContext& ctx);
        /// Pop the Save-As prompt; commits on Enter / OK.
        void openSaveAsPrompt();
        /// Pop the Load-Scene picker (lists scenes/*.json).
        void openLoadScenePrompt();
        /// Load m_currentScenePath. Editor-side post-load work (camera
        /// rebind, etc.) is handled by the SceneLoadedEvent subscriber.
        void loadScene(FrameContext& ctx);


    private:
        GLFWwindow*       m_window           = nullptr;
        CameraController* m_cameraController = nullptr;
        RenderSystem*     m_renderSystem     = nullptr;
        EventSystem*      m_events           = nullptr;
        uint64_t          m_sceneLoadedListenerId = 0;

        EditorState      m_state;
        HierarchyPanel   m_hierarchy;
        InspectorPanel   m_inspector;
        BottomPanel      m_bottom;
        ViewportOverlay  m_viewportOverlay;
        GizmoOverlay     m_gizmoOverlay;
        ViewportToolbar  m_viewportToolbar;
        ViewportPlaybar  m_playbar;
        PreferencesPanel m_preferences;

        // Input state for F5 toggle
        bool m_f5WasDown = false;

        // Panel resize drag state
        bool m_resizingLeft   = false;
        bool m_resizingRight  = false;
        bool m_resizingBottom = false;

        // Scene file state.
        std::string m_currentScenePath;  ///< Empty until the user saves/loads once.
        bool        m_openSaveAsPopup = false;
        bool        m_openLoadPopup   = false;
        char        m_saveAsBuffer[256] = "scene.json";
};

} // namespace Engine
